# Burning debris, and the two fires it can start

*Decoded and the missing half ported 2026-08-11. Raw decompile:
`Docs/scratchpad/ghidra/projectile_update_0048ed00.txt`.*

`FUN_0048ed00`'s class-0x10 arm is the only projectile in the game that sets buildings alight. Once
a type-4 slot's speed drops under `0x40001` it grounds, takes a fresh 60 s life (every other class
gets 0), puffs every `0x3333` of sub-timer and plays sound `0xb` (`SND_FIREMIS2`) at the debris
node. When that life runs out it does this:

```c
if (class 0x10 && (cell[0] & 0x20) == 0 && FUN_004a5f60(x, y) == 1) {
    if (FUN_004a99a0(slot[0x10], 1) == 0) {          // no owning record
        if (rand() % (8 - tier) == 0 && FUN_004a6860(x, y) == 0)
            FUN_004a7a10(x, y, 1);                   // open a NEW building-fire mission
    } else {                                          // an owning record
        obj = FUN_004a5080();
        if (obj) { *obj = cell;
            if (rand() % (8 - tier) == 0)
                FUN_004a5340(obj, x, y, (short)slot[0x10], 0);   // ignite INTO that record
        }
    }
}
```

**The branch is the whole point.** `FUN_004a5f60` gates both arms; `FUN_004a6860`'s nearby-fire
spiral gates only the first. So debris that belongs to something already burning may ignite a
building right next to that fire — which is precisely what "the crash site spread" means — while an
arsonist's unowned firebomb may not.

## Who throws what

Every `FUN_0048e0b0` call site, by type and by the event id in its last argument:

| caller | type | event id | what it is |
| --- | ---: | --- | --- |
| `FUN_004cbfd0` (VM op 60) | 4 | `0xffffffff` | the arsonist's firebomb — **no-record arm** |
| `FUN_004b2cd0` x3 | 4 | `param_1+0x3c` | plane crash — **owning-record arm** |
| `FUN_004b49b0` x3 | 4 | `param_1+0x69` | train derailment, one per carriage — owning arm |
| `FUN_0049ff00` | 4 | `param_1+0x113` | a burning car finally going up — owning arm |
| `FUN_00490690` | 4 | `param_1[0x10]` | a projectile breaking up, inheriting its owner |
| `FUN_004a5ca0` / `FUN_004a5dd0` | 6 | -1 | fire embers; not this mechanic |
| `FUN_004cc130`, `FUN_004cbbc0`, `FUN_00484d20`, … | 8/10/… | -1 | rocks, sparks, wash |

So the owning arm is the game's **fire-spread-from-wreckage** mechanic, and the remake had none of
it: `ThrowArsonistFirebomb` was the only producer of `BurningDebris`, and `SpawnCrashDebris` in the
ambient-vehicle actor **accepted an `EventId` parameter and dropped it**, throwing cosmetic particles
only.

## What is ported now

- `FSimCopterBurningDebris::OwnerEventId` is slot[0x10]; `AddBurningDebrisSlot` is the shared tail.
- `FSimCopterMissionSystem::IgniteIntoExistingRecord` is the owning arm: allocate a fire object,
  `IgniteBuilding(..., Flags = 0)` — flags 0 is the **spread** ignition, so each flame docks the
  "Flame($)" penalty instead of being scored as a fresh job — and no nearby-fire test.
- `ASimCopterMissionSystemActor::SpawnCrashBurningDebris` is the owned spawner. It uses the debris's
  own tile; the arsonist's `FindNearestFireSuitableTile` search is a rendered-building adaptation for
  a *person* pushed out of walls, and wreckage lands where it lands.
- Wired at `SpawnCrashDebris` (plane + one per derailed carriage) and at the wreck burn-out beside
  `EVT_CarBurned`, which is `FUN_0049ff00`'s moment.

## Traps

- The site test must stay **ahead** of the `rand()` draw in both arms. An ineligible tile consumes
  no PRNG in the original, and moving the draw forward desynchronises every later mission roll.
- `(cell[0] & 0x20)` is an extra per-cell gate on both arms that the remake does not have; it can
  only *reduce* ignitions, and `IgniteBuilding` already refuses a cell that is burning.
- **`DAT_00504518` / `DAT_00504538`, the 8-entry puff class and offset tables, are runtime
  initialised** — they read as zero in the PE and `DAT_00504538` holds a self-referential list head,
  so Ghidra's array indexing there is aliasing a different structure. `dump-asm` before believing
  any puff class from that decompile. Scratchpad: `dump_debris_effect_tables.py`,
  `find_debris_table_writer.py`.
- The grounded arm never reaches `FUN_00490690`, so **water does not douse burning debris**. It is
  not a save-it-in-time window.

Related: [[simcopter-crime-rooftop-rescue]], [[simcopter-mission-system]],
[[simcopter-ambient-vehicles]], [[simcopter-fire-water-fx]].
