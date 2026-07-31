# SimCopter cockpit map

*"dash5 decoded + ported: the map is a 124x98 palette-index raster, not drawn primitives; six
mapbttn buttons; zoom is a shift and the grid rows are asymmetric"*

*Decoded and ported 2026-07-31.*

The map panel is the widget constructed at `FUN_00454420` (class vtable `PTR_LAB_004f3068`,
0x118-byte object, 58 entries), fed by the map module at `FUN_004a2740`-`FUN_004a4780`. Port:
`SimCopterMapRaster.{h,cpp}` (pure, unit-tested), `SimCopterMapArt.{h,cpp}` (palette + icon
sheets), `SSimCopterMapPanel.{h,cpp}` (Slate). Tests: `SimCopter.Map.*`, six of them.

## The map is a rasterised buffer, not primitives

`FUN_004a28e0` fills a **124x98 8-bit buffer of palette indices** from the city grids and
`FUN_004a3b20` blits it into the panel at (54,13). Everything else - direction lines, icons, the
heading needle, the service blips - is stamped into that same buffer before the blit. **Keep it as
indices.** Two things depend on it: burning tiles animate by *counting up the palette*
(0x10..0x1f, one step per pass) and ground shading is an index add (`ramp + altitude`), so
resolving to colour early means re-deriving both.

Geometry, all from `FUN_004a2740` unless noted:

| | |
| --- | --- |
| `dash5.bmp` | 185x148, at screen (455,290)-(640,438) - `FUN_00412440`'s rect for the call to 0x454420 |
| buffer | 124x98 (`DAT_00505ed8/edc`), blitted to panel (54,13) (`DAT_00505ec8/ecc`) |
| tile view | 104x80 (`DAT_00505eec/ef0`) at buffer (10,9) (`DAT_00505ee0`), so panel (64,22)-(168,102) |
| label | (30,126)-(175,139), the current mission's name, font size 12 |
| buttons | six 15x15 rects at x 9/27, y 54/73/92 |

**The instrument panels are at the bottom LEFT in the original**, not the right: `FUN_00412440`
puts dash4 at (0,355) and dash6 at (0,398), while the original map rect is at (455,290). The
remake intentionally anchors the map itself directly to the bottom-left corner per the current
cockpit layout direction; do not restore the initial right alignment or the 125-pixel spacer that
was placed below the first port.

## Zoom is a shift, and the grid rows are asymmetric

`_DAT_00505f08` is 0..3, one pixel per tile at zoom 0 and eight at zoom 3
(`FUN_004a3d50`/`FUN_004a3d80` clamp it; `input.cfg` binds them to **`=` and `-`**, commands 0x1b
and 0x1c in `FUN_0044ac80`'s dispatch). The view covers `104>>Z` by `80>>Z` tiles centred on the
helicopter, so the drawn band is always 104 px wide and the row advance a constant +20.

The per-zoom block expansion is **not symmetric and the decompile is right about it** (verified
against the assembly at 0x4a320c after it looked wrong):

- zoom 0: one pixel, no grid, no centre marker.
- zoom 1: 2x2, row 1 a copy of row 0. Still no grid.
- zoom 2: 4x4. Grid pixel at sub-column 0, **whole grid row at sub-row 1**, rows 2 and 3 copies
  of row 0.
- zoom 3: 8x8. Grid pixel at sub-column 0, rows 1-6 copies, **grid row last**.

From zoom 2 up the tile under the helicopter is painted 0x70 (yellow) - and the heading needle
always starts on the buffer's centre pixel, which *is* that tile's first pixel, so a test that
probes there measures the needle, not the tile.

## Tile colours (all dash5.bmp palette indices)

Every original UI bitmap ships the same 256-colour palette - dash4, dash5 and mapbttn are
byte-identical - so dash5's own table is the game palette.

Order matters; the first match wins:

1. burning cell (tile record byte 0 & 0x20) -> `0x10 + step`, step from `DAT_00505ef4` (16 steps)
2. XBLD 0xd1/0xd2/0xd3 -> 0xea white / 0x9a cyan / 0x1a orange (hospital / police / fire station)
3. inside the 4x4 block at the airport origin (`_DAT_005d91b0/b4`) -> 0xca
4. XBLD 0x06-0x0d or 0xd5 -> 0x5c (parks)
5. XBLD < 0x1d - nothing built, show the ground: terrain class < 0x0a -> 0x90 (deep blue water),
   0x20-0x2f -> `0x50 + shade`, anything else -> `0x80 + shade`
6. XBLD < 0x70 -> 0xd4 (roads, rails, power, highways)
7. otherwise -> 0x3b (buildings)

`shade` is the tmap corner sample `>> 6` clamped to 15 - a corner holds `(step+1) * 0x20`, so one
shade per two altitude steps. **Off the 128x128 map the original reads zeroed terrain and paints
ocean**; that is the water border round a coastal city, so reproduce it rather than clipping.

The terrain class is compared as a *signed* char, so a class >= 0x80 would miss the water test.
The remake's grid only holds 0x00-0x7e, which makes `< 0x0a` the same test.

## Overlays

- **Two direction lines** to the *selected* mission (`FUN_004a3820`): one to `+0x30` (secondary)
  falling back to `+0x28`, one to `+0x38` (tertiary, the delivery end). Both fade with an
  octagonal distance `2*max + min` in tiles: `0x3f - (len<<4)/0x184` on the grey ramp and
  `0x6a - (len<<3)/0x184` on the red. Drawn whatever the blip toggle says.
- **Other missions** (`FUN_004a4200`) get no line - colour 0 - only an icon at the point the ray
  leaves the buffer, which pins off-screen jobs to the map edge along their bearing.
- **Heading needle**: 20 steps of the facing vector in 16.16 from the centre, colour 0x70. The Z
  component is *subtracted* because the map's Y runs opposite the world's Z.
- **Service vehicles** (`FUN_004a4370`): the 20-slot table at `DAT_005d3eb0`, one row per
  dispatched unit, drawn at `((tile - origin) << zoom) + 9/+8` with a track to its destination
  coloured by service (0 fire 0x1a / 1 police 0x9a / 2 ambulance 0xea).

**Icons are SIM3D.BMP pages, not a BMP strip.** `FUN_0046cd20(lib, 3)` is page 3 (140x14, ten
14x14 cells, the first eight used - the mission blips) and page 12 (30x10, three 10x10 cells - the
service vehicles). `FUN_004a4000` maps a mission's *whole* type mask to two page-3 cells, not its
individual bits: boat rescue 0x90 is its own row, not 0x80 plus 0x10. Read the pages as **raw
palette indices in file row order** - `FMaxisTextureReader` resolves colours and flips the image
bottom-up for texture UVs, and the map blits these rows top-down.

## Buttons

`mapbttn.bmp` is 64x48 cut as **four 16x16 columns**: left-column released, right-column released,
left pressed, right pressed. So a right-hand button's pressed cell is the *fourth* column, and the
two columns of buttons do not share art. Rects and cells: `FUN_00454420` and `FUN_00454880`.

| # | glyph | action |
| --- | --- | --- |
| 0 | `-` | zoom out (`FUN_004a3d80`) |
| 1 | `+` | zoom in (`FUN_004a3d50`) |
| 2 | `M-` | previous live mission (`FUN_004a3ec0`) |
| 3 | `M+` | next live mission (`FUN_004a3ed0`) |
| 4 | diamond | toggle mission blips (`DAT_00505f0c`), starts ON |
| 5 | rectangle | toggle service vehicles (`DAT_00505f10`), starts ON |

Buttons 0-3 are momentary and **fire on release, only if the release lands back inside the rect
the press started in** (`FUN_00454c40`); 4 and 5 flip on the press and latch their pressed face.
The mission cycle scans away from the current slot then wraps from the far end, and skips records
whose `+0x54` is 2 (finished) even while `+0x4c` bit 0 is still set.

## Not ported

Clicking a service blip hit-tests (`FUN_004a3d00`) but opens nothing. The original pops a window
(`FUN_00454da0` -> `FUN_00440ff0`) with the unit's name (string ids 0x5a-0x5f from
`FUN_00454e50`) and one order that calls `FUN_0049b3f0` to release it - that needs the original's
popup window class, which the remake has no equivalent for.

Related: [[simcopter-emergency-dispatch]] (the vehicles the blips show),
[[simcopter-mission-system]] (the record table the map reads),
[[simcopter-terrain-flattening]] (the corner grid the shading samples),
[[simcopter-checkup-menu]] (the same "layout comes from the assembly" lesson).
