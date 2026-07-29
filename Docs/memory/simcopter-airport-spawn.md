# SimCopter airport spawn

*"SimCopter's airport is STAMPED over the SC2 city (XBLD 0xf6 terminal + 0xde pads), not just located - finding the block is only half the port."*

*Recorded 2026-07-26; ported into the repo 2026-07-29.*

The session start point (decoded 2026-07-25). `FUN_0047c0c0` sweeps x-outer/y-inner for the
first XZON-zone-8 tile and hands it to `FUN_004829f0`, which does **three** things, and porting
only the first leaves the helicopter spawning inside a building:

1. validates the 4x4 block,
2. **levels the tmap corner grid**: reads the conditioned corner at (origin+1, origin+1) and
   copies it over the inclusive 5x5 patch - this runs *after* `FUN_004abce0` conditioning
   (`FUN_0047bb20` order: condition, then build cells), so the sample it reads was conditioned
   from the **pre-stamp** XBLD ids,
3. **overwrites XBLD**: 0xf6 on the middle 2x2 (terminal) and 0xde on the twelve perimeter pads,
   demolishing whatever SimCity 2000 zoned there.

Traps:
- **XZON's high nibble must be stamped too.** SC2 marks a footprint by flagging its four corner
  tiles - 0x80 top-left, 0x40 top-right, 0x10 bottom-left, 0x20 bottom-right, all four (0xf0) on
  a 1x1, 0x00 on an interior tile - and `ResolveOriginalMeshFootprint` measures footprints from
  those marks, not from matching XBLD ids. Stamping XBLD alone leaves the demolished building's
  corner marks behind, so a 1x1 pad measures itself as a 2x2 and its ground slab z-fights the
  neighbours'. (Bit meanings derived empirically from Demo.sc2, not guessed.)
  `FUN_004829f0` never reads XZON - it builds its cells by hand - so this is a remake-only step.
- Demo.sc2, pengland.sc2 and 6 career cities have a building on **all 12** pads before the stamp.
  Most other shipped cities were saved with 0xf6 already on the terminal tile, which is why the
  bug looks map-specific.
- `case 0xF6` + airport zone + footprint 2 dispatches to {0x096, 0x165} - byte-for-byte the two
  objects `FUN_004829f0` builds by hand. `case 0xDE` is {0x08B}, no base. So no new mesh work.
- There is no separate placement function for the first/new helicopter: city entry is
  `FUN_0047a240` and purchase is `FUN_0048b1a0`, and both use the same free-pad walk
  (`FUN_0048b000`, inlined in the latter) then `FUN_00484790`. The only special case is
  helicopter type 2 when `DAT_00504080` is set, which parks at `DAT_005d91d8` - a pad recorded
  from an XBLD 0xe7 tile, not from the airport table.
- `FUN_00484790` takes the position from the **cell record** the airport built (+2 worldX,
  +4 altitude, +6 worldZ), not from a tile-centre formula.

The hangar is meant to be walk-into-able (shop/upgrade menu) - that entry trigger is still
unported. See [[simcopter-mission-system]] for the placement table and
[[simcopter-emergency-dispatch]] for the vehicle side.
