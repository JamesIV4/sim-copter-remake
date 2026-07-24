# SimCopter fire / water / rotor-wash effect layer decode (2026-07-05)

The functions that spawn and update the fire/water/wash/dust/debris effects were decompiled.
The original software-renderer consumers were not included in this 2026-07-05 pass. A live
follow-up pass on 2026-07-24 traced face type `0x17` to `FUN_00491520`, traced face type `0x1a`
to `FUN_00496da0`, and captured the latter's complete optimized instruction range, jump tables,
palette producers, and selector bytes. See `effect_renderer_decompile_run_20260724.md` and
`effect_renderer_gap_20260724.md`. A subsequent resolution-global read established that the
active `DAT_004f9750 == 0x10` path projects into a 560x400 gameplay viewport within a
640x480 framebuffer; the alternate mode uses 280x200 within 320x240. See
`effect_resolution_run_20260724.md`. Raw effect-layer decompiles live beside this file:
`out_fire_water_effects.txt`, `out_heli_landing.txt`, `out_effect_pool_init.txt`
(pool alloc `FUN_0048da50` + init `FUN_0048db20` + **master updater `FUN_0048ed00`**),
`out_effect_sprite_creator.txt` (card geometry `FUN_0046edb0` + face get/set),
`out_fx_palette_init.txt` (`FUN_004aeba0` splash/wash pool + palette-frame init and the
previously misidentified `FUN_00483c20`), `out_fx_colorlookup.txt` (`FUN_0046cd20`),
`out_fx_ramp_builder.txt` (`FUN_00479bb0` global load), `out_fx_spritesize.txt`.

## 0. The single most important correction

The effects are **NOT** big camera-facing dithered quads with invented colors (what the
former approximation did). Every effect particle is one of exactly two authentic primitives:

1. **A small GEO billboard** — a pre-loaded Maxis mesh (SMOKE `0x148`, a smoke variant
   `0xae`, debris `0x149/0x14a/0x14b`, …) drawn at its own small size with its own
   palette-shaded faces.
2. **A short "trajectory" card** — a string of **1–4 single-vertex point sprites** built
   procedurally by `FUN_0046edb0(N, 0x17)`, each point colored by a **SIM3D palette index**
   and moved along a path.

There is **no texture atlas** in the effect layer. The effect layer sets a class/palette input
and world-space size; the 8-bit software rasterizer determines the final framebuffer operation.
Face type `0x17` is now confirmed as a direct projected point write. Face type `0x1a` reads and
remaps existing framebuffer pixels through class- and depth-dependent kernels, so its final
colors cannot be inferred from the raw class value alone.

## 1. Global load order (`FUN_00479bb0`)

- `DAT_005039ac = FUN_0046a530("SIM3D.BMP", 3, …)` — the **shared 256-color palette / shade
  ramp**. This is the same palette baked into every `.MAX` `CMAP` section, i.e. the remake's
  `FMaxisMeshLibrary::GetSharedColorMap()`.
- `DAT_005039b4/b8/bc` = SIM3D1/2/3.MAX mesh packs, each bound to the palette via
  `FUN_0046e710(mesh, DAT_005039ac)`.
- Then `FUN_00483c20(&fireType)` ×9, which initializes helicopter bodies, rotors, and shadows;
  it is not fire-material setup. `FUN_004aeba0()` initializes splash/wash pools and palette
  frame tables, and `FUN_0048db20()` initializes the moving-particle pools.

`FUN_0046cd20(palette, i)` returns palette entry `i` (`*(palette + 0x20 + i*0xc)`). That is
valid for direct palette paths. Values passed to face type `0x1a` are kernel classes whose final
pixels come from the renderer's palette-remap tables; they must not be interpreted as literal
RGB entries.

## 2. Exact palette colors (extracted from `sim3d1.max` CMAP)

| index | RGB | used for |
|------:|-----|----------|
| 0x08 | `#C0DFC0` pale green-white | wash / fire-glow frame 0 |
| 0x09 | `#A5CAF0` pale blue        | wash / fire-glow frame 1 |
| 0x0A,0x0B | `#000000` | wash/glow frames 2,3 (dark) |
| 0x0F | `#000000` | splash/fire frame 0 (darkest) |
| 0x10..0x1F | `#8F1005 #901A05 #9A2005 #9F2F05 #A53505 #AA3F0A #B0450A #B5500A #BF5A0A #C0600A #CA6F0A #CF750A #D07F0A #DA850A #DF900A #E59A0A` | **fire gradient** dark-red→orange→amber (splash-column frame table = {0x0F,0x10,0x11,0x12,0x19,0x1A,0x1B,0x1C,0x1D}; ember color-cycle = 0x10..0x1F) |
| 0x73 | `#FFF01F` bright yellow | fire-trajectory tip frame |
| 0x7B | `#FFDA6F` pale yellow   | fire-trajectory tip frame |

Small consts 0..5 (`#000000 #800000 #008000 #808000 #000080 #800080`) are used with **face
type `0x1a`** (drip/sub-spray). Under type-`0x1a` the rasterizer treats these as a separate
shaded run, so the raw palette RGB is not the on-screen color for those few particles — this
is the *only* color path not fully pinned to an RGB here; everything else is exact.

## 3. Card geometry (`FUN_0046edb0(N, 0x17)`)

Allocates `N*0x40 + 0x98` bytes; builds **N faces, each 1 vertex** (`face[+4]=1`), face type
`0x17`. Points are spaced along +Z at init:
- **1-pt** "trajectory" (wash) — pool `DAT_005d62e0`.
- **3-pt** "bullet" (splash/embers) — pool `DAT_005d4f30`; points at Z = 0, 0xA0000, 0x140000
  (0, 10, 20 sim-units).
- **4-pt** "fire" — pool `DAT_005d41f0` slots 1+; points at Z = 0,0xA0000,0x140000,0x1E0000.

Per-point size is set with `FUN_0046e590(geom, s, s)` (splash column uses `s = 4<<scale`), and
`renderNode+0x10` holds the point size in 16.16 (e.g. `0x140000` = 20, `0x60000` = 6,
`0x30000` = 3 sim-units). Face color (palette index) is written to `face+0x24` via
`FUN_0046f5e1`; face type to `face+0x10`.

## 4. Particle pools (`FUN_0048da50` alloc, `FUN_0048db20` init)

Entry stride = 0x12 dwords. `w0` bit0 = alive; `w1` = life countdown (16.16); `w2` = spawn
sub-timer; `w3` = age/turbulence; `w4..w6` = velocity xyz; `w10` = render node
(`+0x18..0x20` pos, `+0x24` 4×4 matrix, `+0x10` size, `+0xe` slot index); `w11,w12` = tile
cell; `w13` = frame cursor; `w14` = state; `w17` = scale exponent.

| pool | slots | visual | notes |
|------|------:|--------|-------|
| `DAT_005d4900` | 10 | GEO `0xae` (smoke) | rising smoke, size 0x60000 |
| `DAT_005d4bd0` | 10 | GEO `0x147` | size 0x30000 |
| `DAT_005d4ea0` | 2  | GEO `0x7c`  | size 0x60000 |
| `DAT_005d6880` | 30 | DEBRIS `0x149/14a/14b` cycled | size 0x30000; settles → splat puff |
| `DAT_005d62e0` | 20 | proc **1-pt** card `0x17` | rotor-wash "trajectory", size 0x10000 |
| `DAT_005d4f30` | 70 | proc **3-pt** card `0x17` | splash/embers, size 0x140000 |
| `DAT_005d41f0` | 25 | slot0 smoke `0xae`; slots1+ proc **4-pt** card | building-fire column, size 0x50000 |

`DAT_00581788` (20) = splash **columns** (`FUN_004af100`), visual GEO `0x148` SMOKE sized
`0x140000`. `DAT_00581c68` (100) = tile **splat/puff** pool (`FUN_004af220`), visual GEO
`0x148` SMOKE sized `0x140000`.

## 5. Spawners

### `FUN_0048e0b0(type, cell, pos, vel, p5, p6, p7=size, p8)` — moving-particle creator
Finds a free slot in the type's pool, sets face type + color, life, velocity. Colors/types:

| type | pool | face type | palette color | life (w1) |
|-----:|------|-----------|---------------|-----------|
| 1 | 4900 | (GEO) | — | 0x50000 |
| 2 / 0xE | 4f30 | **0x17** | cycles 0x10..0x1F (`DAT_00504558`, wraps 0x10..0x1F) | 0x50000 |
| 5 | 4f30 | 0x1a | 3 | 0x50000 |
| 6 (bucket drip) | 4f30 | 0x1a | 0 | 0x50000 |
| 7 | 4f30 | 0x1a | 3 | 0x1CCCC |
| 9 (splash sub) | 4f30 | 0x1a | 5 | 0xE666 |
| 3 | 4bd0 | (GEO) | — | 0x50000 |
| 4 (smoke) | 6880 | (DEBRIS GEO) | — | 0x1E0000 |
| 10 | 6880 | (DEBRIS GEO) | — | 0x40000 |
| 8 | 62e0 | (1-pt card) | — | 0x60000 |
| 0xC | 41f0 | (GEO smoke) | — | 0x50000 |
| 0xD (fire) | 41f0 | (4-pt card) | `DAT_00504578[cursor&3]` = {0x73,0x7B,…} | 0x30000 |

On spawn (non-type-7) it also emits a tile splat via `FUN_004af220(cell, pos+10·vel, class)`.

### `FUN_004af220(cell, pos, class)` — tile splat / puff (SMOKE `0x148`, 100-slot pool)
Color field = `class`. The fixed lifetime/countdown is `w2 = 0x20000` for every class.
The class switch writes **vertical rise velocity `w5`**, not lifetime:
0→0xA0000, 1→0x190000, 2→0x110000 (+ can trigger sound 0xB), 3→0xD0000,
4→0x1E0000, 5/10→0x190000/0x140000 (sets bit 0x10), and
default (including **8 = rotor wash**)→0xF0000. `FUN_004af3b0` confirms the roles by
subtracting frame time from `w2` and integrating `w5 * frameTime` into vertical position.

### `FUN_004af100(cell,x,y,z,scale,color)` → `FUN_004af3b0` — big splash column (20-slot)
Column visual = SMOKE `0x148` sized `4<<scale`, pushed up by −0x200000. Over its first 9
frames it advances the color through the **splash frame table** `DAT_00581c40`
(= palette {0x0F,0x10,0x11,0x12,0x19,0x1A,0x1B,0x1C,0x1D}), and on frame 1 it bursts a **ring
of type-9 sub-particles** (`FUN_0048e0b0(9,…)` around the offsets in `DAT_00581b98..c40`) —
these are the "smaller versions of the water effect."

### `FUN_00488060(heli)` — bucket water drip
When bucket timer active, spawns `FUN_0048e0b0(6, bucketPos, downVel, size 0xF0000)` — a
single **type-6 drip** (3-pt card, palette 0, face 0x1a) with small random lateral spread.

### `FUN_004881b0(heli, groundZ)` — **rotor wash / wind kickback** (called 1×/frame from `FUN_00484d20`)
If `heliBottom − groundZ < 0x140000` (≈20u) **and** altitude `heli+0x158 > 0x1180000`:
compute count `iVar3` (grows as it gets lower / with altitude), and — for the once-per-call
spawn — build a **random-yaw** rotation (`FUN_0046cad1` + `FUN_0046cafc(rand·0x10000)`),
rotate the fixed offset `(−heliRight)`, scale by **×0x20**, add to the ground point, then
`FUN_004af220(cellUnderHeli, offsetPoint, 8)` — one **class-8 SMOKE puff**
(fixed life 0x20000, rise velocity 0xF0000).
Over water vs land is **not** a color change in this function — it is always the class-8 puff;
the SMOKE sprite reads as spray/dust against whatever it is over.

### `FUN_00489250(heli)` — rotor **downwash disc** (the dust ring directly under the heli)
Ray-marches 16 steps down, picks a dust-density tier 0..3 from `DAT_00504430`, and when the
tier changes recolors its disc sprite through the **wash/glow frame table** `DAT_005d91e0`
(= palette {0x08,0x09,0x0A,0x0B}); places a scaled disc via `FUN_0048ae70`.

### `FUN_0048a8b0(heli)` — hard-landing / crash splash + dust (state machine)
State 1: `FUN_004af220(cell, heliPos, 1)` + 5× `FUN_0048e0b0(4, …)` debris + one big
`FUN_004af100(cell, …, 4)` splash column. States 2/3 fade it with class-4/1 puffs.

## 6. Master updater `FUN_0048ed00` (once per frame)

Walks every pool. Per live particle: `life -= DAT_005039a8` (frame-time in 16.16); integrate
velocity by `FUN_0046c49d(v, dt)`; apply gravity **−0x280000·dt** to Z-velocity each frame;
re-hash the owning tile cell from the new position; periodically (`w2<0`) drop a
`FUN_004af220` splat of a table-driven class (`DAT_00504518` = {5,4,0xA,0xB,0xB,0xA,2,1},
z-offset `DAT_00504538`); on death, unlink from the cell list and (for fire) emit
`FUN_004af100`/burst debris. Building fire (`DAT_005d41f0`) at death spawns 0x18 (24) type-0xD
sprites `FUN_0048e0b0(0xd,…)` in a fan. Finally submits the render node with `FUN_004704d1`.

Key rates: gravity `0x280000`/frame·dt; fire spread checks `FUN_004a5f60/004a6860/004a7a10`;
smoke turbulence adds random yaw each frame when age>0x40000.

## 7. Faithful-remake checklist (what to change)

- Replace the big dithered quads with the decoded projected-point handlers: point strings
  (1/3/4 points) for wash/splash/fire embers, and the actual SMOKE/DEBRIS GEO objects for
  puffs/smoke/debris.
- Derive projected coverage from the original renderer and the 16.16 size inputs, not arbitrary
  Unreal-centimeter sprite widths.
- Preserve direct SIM3D palette indices on face type `0x17`. For face type `0x1a`, reproduce the
  decoded class-specific palette-remap kernel; do not look up the small class value as RGB.
- Spawn **cadence/lifetime/gravity** from §5–6 so density accumulates the same way (one wash
  puff per frame, short lives), instead of dozens of quads per frame.
- Wash = class-8 SMOKE puff at a random-yaw ×0x20 offset, only when <20u over ground and above
  the altitude gate. Splash column bursts a type-9 sub-particle ring ("smaller versions").
