# SimCopter water gameplay

*"Water/bucket/cannon gameplay — the douse comes from particle impact (strength = remaining life), never from the bucket; plan at Docs/WaterGameplayDecompilePlan.md"*

*Recorded 2026-07-24; ported into the repo 2026-07-29.*

**Scoped 2026-07-24; full plan at repo `Docs/WaterGameplayDecompilePlan.md`.**
The remake's bucket fill/dump is a hand-written approximation, not a port. Traps
worth keeping out of band:

- **The bucket never douses.** `FUN_00488060` emits ONE type-6 particle per
  frame; `FUN_00490690` (the only caller of `FUN_004a50c0`) douses when that
  particle hits a burning cell. Strength = the particle's **remaining life**
  (`p[1]`, starts `0x50000` = 5.0s, decays by the frame delta), quartered for
  bucket water (flag `0x40`) and full for cannon water (flag `0x20`). So water
  dropped from high up extinguishes less. A per-frame douse from the bucket
  position cannot reproduce any of that.
- **There is a water cannon**, entirely absent from the remake: input action
  `0x10`, capability bit `DAT_00504060 & 0x10`, emitter `heli[0x57] = 5`, muzzle
  speed = Cannon Force, drains half the dump rate. Shares the emitter slot with
  the Apache's weapons.
- Water is **pounds** in `heli[0x74]`, capped at heli.twk Ctrl8 `Max Load`, and
  fill/dump rates are applied **per frame with no delta scaling**.
- Fill needs all three of: bucket is the deployed attachment (`heli[0x70]==0`),
  bucket Y under `FUN_004ae7a0` + `0x20000`, and `DAT_005bde80[tile] < 10`
  (that grid writes 0/5 for water+shore, `0x10`+ for land).
- The rope is a **20-node chain** (`FUN_0046ec60(0x14,0x78,...)`) with two end
  models, GEO `0x141`/`0x142`; its swing velocity times Water Throw is what
  scatters dumped water sideways.
- Scooping picks up a **person** (`FUN_004c0c40`), released with behaviour
  `0x125` when the bucket empties.
- Blocked the same way as [[simcopter-mission-system]]'s fire damage: bucket
  strikes route through `FUN_0049a4f0` modes `0x11`/`0x12` into `DAT_0058d728`.
- **The water gauge is solved and ported (2026-07-31).** Its owner is not missing,
  it is in the Ghidra gap after `FUN_00454ee0` - read it out of the bytes, not the
  exports. The flap tick `0x00455300` computes `heli[0x74] * 11 / maxLoad[type]`
  every fourth frame and `0x00455700` repaints **eleven 5x10 cells from page
  (16, 43)** out of `WATERGGE.BMP`, which is exactly 15x10: full water | meniscus |
  empty. The row is `level` full cells, then **one** meniscus (skipped when the tank
  is full), then empties. Unlike flap3's canister lamps, all three states have to be
  drawn - `flap0.bmp` ships the *empty* gauge with the meniscus printed at x 16, so a
  partly full tank has to paint over it. Divide by the **heli.twk** max load, not the
  static table's. See [[simcopter-cockpit-flaps]].
