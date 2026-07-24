# Fire simulation decode — 2026-07-24

Why: the remake spawned fires that quietly vanished after ~32 seconds. The
decompiled originals show a fire is a long-lived, growing, spreading thing that
ends in one of two opposite ways.

Sources: `out_m5_firecreate.txt` (FUN_004a5080 / FUN_004a5340 / FUN_004a4fb0 /
FUN_004a5fd0 / FUN_004a6370), `out_m5_firebinder.txt` (FUN_004a5f10 /
FUN_004a48e0), `out_m5_scheduler.txt` (FUN_004a4ac0), `out_m5_spreadtable.txt`
(0x00505f60), `out_m5_tilequeries.txt` (FUN_004a5f60 / FUN_004a6860),
`.ghidra-exports/004a50c0.json` (douse), `.ghidra-exports/004a47c0.json`
(flame table init), `Reference/SimCopterOriginalGame/tweak/fire.twk`.

## Fire Parms (fire.twk -> FUN_004a5f10)

| Ctrl | Label | Global | Shipped | Use |
| ---: | --- | --- | ---: | --- |
| 0 | Douse Points | `DAT_00505f40` | 37.3 (fxpt) | flame douse health, `tier*0x14 + this` |
| 1 | Douse Mult | `DAT_00505f44` | 21 (int) | douse damage, `(1-tier)*3 + this` |
| 2 | TimeToLive (secs) | `DAT_00505f48` | 190.3 (fxpt) | burn per storey, `(1-tier)*0x140000 + this` |
| 3 | SpreadInterval (secs) | `DAT_00505f4c` | 34.7 (fxpt) | spread clock, `(1-tier)*0x40000 + this` |
| 4 | SpreadProb | `DAT_00505f50` | 224 (int) | spread roll `rand % ((1-tier)*10 + this) == 0` |
| 5 | Fire Radius | `DAT_00505f54` | 43.9 (fxpt) | douse reach, `(1-tier)*0x80000 + this` |

Every one is shifted by the difficulty tier (`DAT_004f9740` = city difficulty +
1), always in the direction that makes tier 4 harder.

## Flame record (DAT_005ce0a0, 0xa0 bytes, 0x8c slots)

| Off | Meaning |
| --- | --- |
| +0x00 | bit 0 active; bits 2/4/8/0x10 pick which wall the flame climbs |
| +0x04 | burn countdown to the next growth step / burn-out |
| +0x08 | douse health |
| +0x0c | ONE GROWTH STEP (a storey). 0x200000 flat on a 1-cell building, otherwise `objectTop / footprint`. **Not** a render size — FUN_004a47c0 gives every flame the same 0x100000 scale at +0x34 |
| +0x10/14/18 | local offset from the cell origin; +0x14 is the climb |
| +0x1c | growth steps remaining = `footprint - 1` |
| +0x88 | 0x8000 countdown between FUN_004a6370 damage sweeps |
| +0x94 | the cell object flagged 4 (the structure). Zero = nothing to climb |
| +0x98 | owning fire object (DAT_005d3820, 3 dwords: cell, flameCount, rescueSpawned) |

## Lifecycle (FUN_004a4ac0, per active flame per frame)

1. Trapped people: `fireObj.rescueSpawned == 0 && tier > 1 && countdown <
   (tier*5 + 15) * 0x40000 && xbldProps & 4` -> raise one 0x80010 rescue.
2. `countdown -= DAT_005039a8` (the smoothed frame delta).
3. Still burning -> FUN_004a6370(flame, 6) damage sweep, then advance the
   **global** spread accumulator `DAT_00505f80`. It is advanced once per active
   flame per frame, so a bigger fire reaches the next spread roll sooner.
4. Countdown elapsed:
   - steps remaining -> climb one storey and re-arm BOTH the full burn and the
     full douse health. This is what makes a fire last: a 4-cell building burns
     4 x 190.3s, not 190.3s.
   - no structure at +0x94 -> zero the steps, expire next pass.
   - no steps left -> the flame dies with event 3 (EVT_FlameExpired).

When the last flame of a fire object goes:
- expiry path -> event 4 (EVT_CellBurnedOut) then FUN_004a5fd0 demolishes the
  building (XBLD tile cleared, density 10, geometry swapped for rubble
  0x14f..0x152 by footprint, debris thrown).
- douse path (FUN_004a50c0) -> event 6 (EVT_ObjectCaughtFire), the "Bldg Saved"
  cash award. The building survives.

Both paths first move the mission marker (event 0) to a surviving flame of the
same event if the record still points at the emptied cell.

## Spread (FUN_004a4fb0)

Picks one of four edge neighbours from the table at `0x00505f60`:
`(-1,0) (1,0) (0,-1) (0,1)`. Refuses cells already flagged burning (0x20) or
with no display list, and refuses a cell that already owns a live flame. The
new cell gets its **own** fire object but keeps the parent's event id, so one
mission can burn several buildings, each on its own clock.

## The flags byte

`FUN_004a5340(..., param_5)` and `FUN_004a48e0(..., param_9)` take the mission
event's *silent* byte. `FUN_004a7a10` passes 1 for the ignition that starts the
mission — its flames are free and it sizes the end award by the footprint —
while `FUN_004a4fb0` passes 0, so every flame a spreading fire creates docks the
"Flame($)" score penalty as it appears.

## Ported to the remake

`FSimCopterMissionSystem::SpawnFlame / UpdateFires / GrowFlame / RemoveFlame /
RetireFlame / SpreadFireFrom / DouseAtLocalOffset / IgniteBuilding`, pinned by
`SimCopter.Missions.FireLifecycle`.

Not ported: FUN_004a5fd0's demolition. The core posts EVT_CellBurnedOut and
calls `ISimCopterMissionWorld::OnBuildingBurnedDown`, but no remake actor
overrides it, so a burned-out building still stands and can be re-ignited.
