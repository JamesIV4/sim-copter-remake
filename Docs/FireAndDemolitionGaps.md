# Fire, demolition and building gaps

Outstanding differences between the original and the remake in the fire
simulation, the demolition that follows a burn-out, and the instanced building
geometry that demolition operates on. Written 2026-07-24, after the fire
lifecycle port and the move to instanced buildings.

Every entry names the original function or data it comes from, what the remake
does instead, and what closing it needs. Decoding evidence lives in
`Docs/scratchpad/ghidra/fire_simulation_decode_20260724.md`.

Nothing here is guessed. Where a gap is open because the original's behaviour is
not yet decoded, that is stated rather than approximated.

---

## 1. Nothing is damaged by fire or by a collapse

**Original:** `FUN_004a6370(flame, 6)` runs every 0.5 simulated seconds while a
flame burns, and `FUN_004a6370(flame, 0x10)` runs once when the building
collapses. Both walk the cell's object list and call
`FUN_0049a4f0(mode, ...)` per object, which routes by object flag: 4 to
`FUN_0048b370`, 8 (people) to `FUN_004c1050`, 0x10 (cars) to `FUN_0049fc10`.
`FUN_004c1050` reads the person's new behaviour id from
`(&DAT_0058d728)[mode]` and drives their state from it.

**Remake:** the mission core calls `ISimCopterMissionWorld::DamageInFlameBounds`
on the correct schedule, but no actor overrides it, so it is a no-op. The
collapse sweep is not called at all. People and cars stand in fire and under
collapsing buildings unharmed.

**Blocked on:** `DAT_0058d728` is not in `.ghidra-exports`
(`ghidra-bridge global 0058d728` reports it missing), and the remake has no
people-side event dispatch to receive a behaviour id even once it is decoded.

**To close:** decode `DAT_0058d728` (a fresh Ghidra bytes dump, or a live-process
read - see `simcopter-live-memory-rip`), then build the people/car event
dispatch. This belongs with the people behaviour work, not with demolition; the
two sweeps then share it. `EVT_PersonDied` already exists on the mission side to
receive the outcome.

---

## 2. A flame climbs straight up instead of following the wall

**Original:** `FUN_004a4ac0`'s growth branch offers the wall-surface query
(`FUN_0046e2c0` / `FUN_0046e430`, chosen by the flame's growth-axis flags) a
position one storey higher. The query re-projects the horizontal axis named by
those flags onto the wall and fails once the flame passes the top.

**Remake:** `FSimCopterMissionSystem::GrowFlame` raises `PosY` by one storey and
leaves `PosX`/`PosZ` alone, bounding the climb by `GrowthStepsRemaining`.

**Why this is close but not exact:** the climb is `footprint - 1` steps of
`buildingTop / footprint`, which never reaches the top, so the query never
actually fails - the step count alone bounds it, and the vertical motion is
right. What is missing is the horizontal re-projection, which matters on
buildings whose walls are not vertical.

**To close:** port the two surface queries, which needs the cell object's
geometry available to the mission layer.

---

## 3. Fire Parms are compiled in, not loaded from fire.twk

**Original:** `FUN_004a5f10` binds the six `[Fire Parms]` controls from
`fire.twk` to `0x505f40..0x505f54`.

**Remake:** `FSimCopterMissionTuning` hardcodes the six values, and they do match
the shipped `Reference/SimCopterOriginalGame/tweak/fire.twk` (Douse Points 37.3,
Douse Mult 21, TimeToLive 190.3, SpreadInterval 34.7, SpreadProb 224, Fire Radius
43.9). `career.twk` *is* loaded at runtime (`LoadCareerData`); `fire.twk` is not.

**Effect:** editing `fire.twk` does nothing. Behaviour is correct for the shipped
file only.

**To close:** small - reuse the existing tweak reader and bind the six controls,
the way `LoadCareerData` already does for the career table.

---

## 4. Burnable tiles below XBLD 0x70 never clear

**Original:** `FUN_004a5f60` marks a tile burnable when it is *not* in
`0x1d..0x6b`, not below 5, and not one of `0xd1`, `0xd2`, `0xd3`, `0xde`, `0xf6`.
That leaves `0x05..0x1c` (power lines and similar) and `0x6c..0x6f` burnable even
though they are not buildings. `FUN_004a5fd0` clears them like anything else, and
for XBLD `0x06..0x0c` takes a separate branch that swaps flag-8 objects to object
`0x14e` and returns early.

**Remake:** only tiles at XBLD `>= 0x70` are placed as instances. A fire on a
lower burnable tile runs its full lifecycle, but `DemolishBuildingAtTile` finds
no instanced building, returns false, and `OnBuildingBurnedDown` returns before
clearing the XBLD entry. The tile stays burnable and can re-ignite.

**To close:** clear the XBLD footprint even when there is no instance to remove
(cheap, and independently correct), and port the `0x06..0x0c` object-`0x14e`
branch if those tiles should leave their own debris.

---

## 5. The density/altitude grid is not modelled

**Original:** `FUN_004a5fd0` writes 10 into `DAT_005bde80` for every tile the
demolished building covered, alongside zeroing XBLD.

**Remake:** no equivalent grid exists. Only the XBLD entry is cleared.

**Effect:** unknown until something reads that grid. Nothing in the remake does
yet, so this is currently inert - recorded so it is not missed when whatever
consumes it is ported.

---

## 6. The XBLD 0xcb special case is not ported

**Original:** `FUN_004a5fd0` sets the global `DAT_0050458c = 1` when the
demolished tile's XBLD id is `0xcb`. `FUN_00490690` sets the same global from a
different demolition path.

**Remake:** not ported - the global's meaning has not been decoded.

**To close:** find what reads `DAT_0050458c`. Likely a one-off "this specific
building was destroyed" flag with a scoring or narrative consequence.

---

## 7. Water strength is always full, and applied per frame

**Original:** `FUN_004a50c0` scales douse damage by
`(0x50000 - particleSize) / 0x50000`, and is called once per water particle that
lands on the burning cell.

**Remake:** `DumpWaterAt` calls `DouseAtLocalOffset` once per frame at full
strength (`0x10000`) from the bucket position. The health model, radius test and
the 3.0s burn-countdown stall are exact; only the strength term and the call
cadence are not.

**Effect:** dousing is frame-rate dependent and slightly stronger than the
original at high frame rates.

**To close:** drive the douse from the water particles the effect system already
spawns, passing each one's size through as the strength term.

**Update 2026-07-24:** this gap is wider than written above. `FUN_00490690` is
the only caller of `FUN_004a50c0`, and it passes the particle's **remaining
life**, quartered for bucket water and full for cannon water — so travel time,
not just particle size, sets how much a drop extinguishes, and the bucket must
stop dousing directly. There is also a second delivery system (the water cannon)
that the remake has no trace of. The full decode plan is
`Docs/WaterGameplayDecompilePlan.md`; close this gap through step 1 there rather
than by adjusting the strength term.

---

## 8. Roads, bridges and power lines cannot be destroyed, and cast no shadow

Only buildings were moved to instanced models, because only buildings are
demolished. Everything else stays baked in the merged `OriginalMeshComponent`,
which has no per-object identity to remove and casts no shadow (it is one
unculled 509k-triangle proxy, so shadowing it means re-rendering the city per
light).

This is a deliberate boundary, not an oversight, but it is what gap 4 runs into.
Extending instancing to road-like tiles would close both.

---

## 9. Mission types that still stub out

Unrelated to fire, but these `ISimCopterMissionWorld` hooks return false, so the
scheduler creates the event and immediately fails it: `TryActivatePlaneCrash`,
`TryActivateTrainCrash`, `TryActivateBoatRescue`, `TryActivateTrainRescue`,
`TryActivateSpeederCar`. The fire bucket of the scheduler rolls plane crashes at
difficulty tier 2+, so some scheduled fire missions silently never appear.

---

## Verified working

For contrast, these were confirmed by decode and are covered by
`SimCopter.Missions.FireLifecycle`, `SimCopter.Missions.FireDouse` and
`SimCopter.City.BuildingDemolition`:

- Full `TimeToLive` burn per storey, with the climb re-arming both the burn and
  the douse pools, so fires last minutes rather than seconds.
- Difficulty shifts on all six Fire Parms.
- Four-way spread off the `0x00505f60` table, on the shared global accumulator
  that advances once per active flame per frame.
- Burn-out vs douse as opposite endings (`EVT_CellBurnedOut` + demolition, vs
  `EVT_ObjectCaughtFire` "Bldg Saved").
- The silent-byte distinction: the mission's own ignition is free, spread flames
  each dock the Flame($) penalty.
- Demolition removing the building and leaving the footprint-sized rubble model
  (`0x14f..0x152`), clearing the XBLD footprint so it cannot re-ignite.
- Trapped-people rescue raised at tier 2+ once the burn passes its threshold.
