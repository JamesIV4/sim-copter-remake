# Burglar getaway car and police pursuit - historical decode

> **Superseded conclusion (2026-08-09):** the `0x4000` record is the Burglar mission. The person
> argument pair is behavior class `0xf`, state `0xd`, which runs BHAV 1303 and reaches opcode 61
> with message 1. The `veh[8]` return arm is therefore live and repeats burglaries. Preserve the
> raw work below as history, but use `Docs/memory/simcopter-crime-rooftop-rescue.md` for the aligned
> lifecycle.

Decoded 2026-07-25 from SimCopter.exe via ghidra-bridge, completed and **ported 2026-07-26**.
See `SimCopterCriminalCar.h` for the ported rules and section 6 below for what was left out.

Related: `emergency_dispatch_decode_20260725.md` (the dispatch pool, states and on-scene
actions), `heli_tools_models_decode_20260724.md` (spotlight).

---

## 1. The criminal car object

| Thing | Address / value |
| --- | --- |
| Class constructor | `FUN_004b8470` |
| vtable | `PTR_FUN_004f4cd8` (entry 0 = `FUN_004b8510`) |
| Message id (`obj[5]`) | `0x11e` |
| GEO object | `0x11e` = `CARROBBR`, table name `badguy` (in `GEO\SIM3D2.MAX`) |
| Pool array | `DAT_00582b08` |
| Pool count | `DAT_0050637c` |
| Instance size | `0x13f` bytes (`FUN_004d28b0(0x13f)`) |

`FUN_00479bb0` (world init) preallocates the pool **five at a time**, the same shape as the
three emergency-service pools:

```
iVar6 = iVar1 + 5;
while ((iVar1 < iVar6 && (iVar2 = FUN_004b8470(iVar1), iVar2 != 0))) { iVar1 = iVar1 + 1; }
```

so a city can have at most five criminal cars alive at once.

### Spawn - `FUN_004b8540(eventId, param2, tileX, tileY)`

Called from `FUN_004b84f0` (its only caller - the mission hook). Walks the pool for a slot whose
`flags & 2` is clear (not spawned), places it with `FUN_0049cf10`, and then:

| Field | Value |
| --- | --- |
| `+0x113` | event id (`param_1`) |
| `+0x12b` | 0 |
| `+0x12f` | `param_2` |
| `+0x137` | `DAT_00506360 + rand() % DAT_00506364` - a randomised timer |
| `+0x135` | 1 |
| `+0x136`, `+0x133`, `+0x134` | 0 |

Returns 1 on success, 0 when the pool is full or no road tile was found.

### Placement - `FUN_0049cf10(veh, &tile)`

Generic vehicle road placement: `FUN_004beda0(5)` starts a **radius-5** spiral from the given
tile and takes the first tile whose XBLD (with XBIT bit 1 folded in at bit 15, the same
`(xbit & 2) << 0xe | xbld` composite the stop gate uses) is a usable road. Bridge ids
`0x49..0x50` are gated on the vehicle's own message id, and `0x11e` is explicitly **excluded**
from that branch - a criminal car will not be placed on a bridge deck.

---

## 2. The target filter

```
FUN_0049dab0(obj):
    return obj[0x14] == 0x11e || (obj[5] & 8)
```

i.e. a police car hunts the criminal car **or** anything carrying the "fleeing/speeding" flag
`8`. That second clause is why a speeder person and a speeder car both work as targets.

---

## 3. Pursuit - `FUN_004b9e40` case 2 (state 3, the F5 chase)

1. Spotlight tile, straight off the helicopter's spotlight node `DAT_005040d0 + 0xc0`:

   ```
   tileX = (node.x + 0x20000000) >> 0x16
   tileY = (0x20000000 - node.z) >> 0x16
   ```

2. `FUN_004beda0(3)` - **radius-3** spiral around that tile. For each tile, walk the cell's
   object list; for objects with tile-object flag `0x10` (vehicle), take the first that passes
   `FUN_0049dab0`. Store it at `veh[0xc6]`.

3. **No target** (or the police car itself carries flags `0x70`):
   - if the marker flag `0x20` is set: unlink the marker (`FUN_004be820`), then re-run the same
     radius-3 scan around the **police car's own tile**, then run the on-scene action
     `FUN_004bd980(0xe, personState, 0x780000, ...)`;
   - otherwise `FUN_004beda0(4)` - a **radius-4** spiral from the spotlight tile for the nearest
     road tile - and drive there. The road test here is the raw XBLD ranges
     `0x1d..0x2b`, `0x3f..0x46`, `0x51..0x59`.

4. **Target found**: destination = the target's tile, and

   ```
   rec = DAT_0057f768[target[0xe]]                 // the target's owning vehicle record
   if ((rec[1] & 0x800) && (rec[1] & 0x70) == 0
       && FUN_004a0370(targetPos, vehPos, 0x600000)   // within 96.0 units
       && FUN_0049df60(veh))                          // may-I-stop gate
       rec->vtable[1](-1);                            // the "pull over" call
   ```

   Ghidra's decompile renders the argument as `0xffffffff` here, but that is a *different* call
   site (a vehicle halting itself). The real pull-over is at `0x004b9f99`, and the asm shows it
   pushes `[EDI + 0x14]` - the **police car's own message id, 0x11d**. That distinction is the
   whole mechanic; see section 4a.

### vtable[1] - resolved

The bridge's exported globals stop at `PTR_FUN_004f4cd8`, so the tables were read straight out
of `.rdata`:

| Class | vtable | `[1]` |
| --- | --- | --- |
| criminal car | `0x004f4cd8` | `FUN_004b89a0` |
| ambulance | `0x004f4d20` | `FUN_0049e0c0` |
| fire truck | `0x004f4d48` | `FUN_0049e0c0` |
| police car | `0x004f4db0` | `FUN_0049e0c0` |

`FUN_004b89a0` is not in the export set - Ghidra folded it into the tail of `FUN_004b8630` - so
it was disassembled by hand:

```
state = this[0x12b]
if (state == 3 || state == 4) return                   ; arrested / leaving: immune
if (caller == 0x11d && this[0x11b] != 0) goto Halt     ; police + marked: unconditional
if (state != 5 && state != 1) return
if (this[4] & 0x30) return                             ; already stopping or stopped
Halt: FUN_0049e0c0(this, caller)
```

`FUN_0049e0c0` is the shared halt the other three classes use directly: set the stop distance at
`+0xd3` (0x100000 / 0x7d000000 / 0x40000 by flags and message id), clear `+0xab`, raise flag
`0x10` and clear `0x60`.

## 4a. The spotlight is the mechanic - Confirmed

`this[0x11b]` is not a "wanted" bit, it is an **illumination accumulator**, and it is what makes
the whole feature hang together:

- `FUN_0049f680` case **1** (interaction mode 1 = the searchlight) calls `FUN_004a01f0`.
- `FUN_004a01f0(obj, spotlightNode)`: if `DAT_00503aa0 == 3` (light off) it zeroes `+0x11b`
  outright. Otherwise, while `+0x11b < 10`, it measures the **horizontal** distance (Y forced to
  zero) from the beam's ground point to the object and adds **2** when it is inside the band's
  radius - `< 0x300000` / `0x480000` / `0x600000` (48 / 72 / 96 units) for bands 0 / 1 / 2. Band
  3 marks nothing.
- `FUN_0049d980` then sets the vehicle's speed target: a fleeing car (`obj[5] & 8`) runs at
  `0x1c000` (**1.75x**) normally, and while `+0x11b > 0` that collapses to `0x10de1` / `0x150d7`
  / `0x18590` (**1.054 / 1.316 / 1.522x**) by band. A tighter beam slows it more.

So: hold the searchlight on the speeder to slow it *and* to make it stoppable, then send police.
An unmarked burglar getaway car cannot be stopped by police at all - `FUN_004b89a0`'s police branch requires
`+0x11b != 0` and no other branch admits a cruising car.

## 4b. The arrest - `FUN_004b8b60` + `FUN_004b8c90` - Confirmed

**Read the branch carefully.** `FUN_004b8b60` posts `EVT_SetCategory` value 4 only when
`FUN_0049bd00(0xf, 0xd)` returned **0** - i.e. when nobody could be placed. That is the give-up
path: `CAT_ExpireSilently` makes the update loop skip the completion test, so the record just
runs out with no payout. On success it posts nothing and falls through to state 3 with
`veh[0x10] = 0x780000` (120 s).

The mission is closed by `FUN_004b8c90`, which runs when that hold expires and posts
`{0x25, eventId, ., ., 1}` - `EVT_CriminalCaught` - taking `CriminalsCaught` to `TargetCount` and
completing the mission properly. **The payout therefore lands 120 s after the car stops, not at
the moment it stops.**

### `veh[8]`, the hold's early exit - Resolved

`FUN_004b8630` case 3 also exits on `veh[8] != 0`. Chasing that down:

- Ghidra's export lists no writer, and a decompiler-text grep is useless because offset 8 is
  generic. Disassembling every function and filtering for `dword ptr [reg + 8]` writes gives
  exactly one candidate in the vehicle range that stores a constant: `FUN_0049aed0`.
- `FUN_0049aed0(recordIndex, value)` resolves the vehicle through `DAT_0057f768[recordIndex]`
  and, if it is spawned, sets **`veh[8] = 1` and `veh[0xc] = value`**.
- Its only caller is `FUN_004ccef0`, which Ghidra reports as having **no callers** - the export
  missed it. The address appears nowhere as a pointer, so it is not a vtable entry; scanning for
  `call rel32` targets finds the single site at `0x004c8bee`, inside a run of 0x20-byte argument
  thunks. The thunk `0x004c8be0` is installed by
  `mov dword ptr [0x0058f068], 0x004c8be0`, and `0x0058f068` is slot **60** of the sparse
  people-VM dispatch table `DAT_0058ef78`.

So `veh[8]` is set by **people-VM opcode 60**: a person who came out of a vehicle telling it they
are finished. The opcode's first argument becomes `veh[0xc]`, which selects `FUN_004b8c90`'s
branch - `0` posts `EVT_CriminalCaught`, non-zero runs the door sounds and returns the car to
state 0, i.e. it drives away again.

Across all 137 BHAV programs in the shipped `people.df`, opcode 60 appears **twice**:

| BHAV | name | argument |
| ---: | --- | --- |
| 288 | `Rioter maybe throw` | -1 |
| 1078 | `crim - arsonist unspotted` | -1 |

Both pass -1, and neither is the program the speeder arrest's deployed person runs. **The early
exit is therefore unreachable for a speeder car**, `veh[0xc]` stays 0, and the 120 s hold followed
by `EVT_CriminalCaught` is the only path. (The -1 uses are the arsonist/rioter getting back in
and driving off - a different feature.)



Runs once the car is actually at rest (`veh[4] & 0x20`), sequenced by sound completion:

1. clear flag 4, play sound `0x6f` (arrive);
2. on completion: `FUN_0049bd00(0xf, 0xd)` - deploy the criminal-car person (behavior class
   `0x0f`, state `0x0d`). This is **not** the ambulance paramedic; `FUN_004b8f60` deploys that
   medic as behavior class `0x0c`, state `5`. If placement fails, post the mission event anyway.
   Play sound `0x70` (doors);
3. on completion: state 3, arm `veh[0x10] = 0x780000` (120 s), then `FUN_004b8c90` removes it.

The mission record is closed with `FUN_004a89c0({0x1d, eventId, ., ., 4})` - `EVT_SetCategory`
with value 4, which is `CAT_ExpireSilently`. The chase itself pays out through
`EVT_SpeederPursuit` (0x21) while it is running, so the arrest does not score again.

### Officer deployment personState

`FUN_004b9e40` picks the state from the target, in both the chase and responding paths:

```
if (veh[0xc6] == 0 || (DAT_0057f768[target[0xe]][5] & 8) == 0)  personState = 8;
else                                                            personState = 0xe;
FUN_004bd980(0xe, personState, 0x780000, DAT_0051ac50);
```

So the remake's current hardcoded `TrySpawnMissionPerson(0xe, 8, ...)` is the no-target case;
a police car that arrives on a *fleeing* target deploys its officer in state `0xe` instead.

---

## 4. Stop gate - `FUN_0049df60(veh)` - Confirmed

Returns 0 (may not stop) when any of:

1. the current tile's composite id is `0x27..0x2b` (an intersection),
2. `veh[4] & 0x80`,
3. `veh[0xaf] > 0` (a stop timer already running),
4. the tile ahead, derived from the route node at `veh[0x103]`, is also `0x27..0x2b`,
5. another object on the tile has vehicle flag `0x10` and its record's flags `& 0xf0` are set.

Otherwise 1. The remake already has the intersection predicate as
`SimCopterDispatch::IsIntersectionTileId` (SCHOOK 0x004bb900).

---

## 5. What was ported - 2026-07-26

- [x] `PTR_FUN_004f4cd8[1]` resolved out of `.rdata` and disassembled by hand (section 4).
- [x] Criminal car as a ground agent with GEO `0x11e` / `CARROBBR`, pool capped at 5.
- [x] `TYPE_Burglar` wired through `TryActivateBurglarCar` to a radius-5 road placement.
- [x] Radius-3 target scan on the police car, in both the on-scene and chase paths.
- [x] The tile-step gate (`FUN_0049b000 < 3`) plus the intersection/occupancy half of
      `FUN_0049df60`, asked of the **target**, not the police car.
- [x] Officer deploy state 8 vs 0xe off the target's flag 8.
- [x] The spotlight mark, its 1.75x -> 1.05x speed collapse, and the arrest.

Pure rules live in `Public/Ground/SimCopterCriminalCar.h`; tests are `SimCopter.Crime.*`.

## 5a. The record the mission creator builds - Confirmed

`FUN_004a7a10`'s `param_3 == 0x4000` branch, in order:

```
sprintf(name, "%s %d", ..., record[0x24])
record[0x20] = DAT_0057f9a0++                      ; per-type serial
if (FUN_004b84f0(record[0x24], 0, tileX, tileY) == 0) { release; return -1 }
record[0x94] = 1                                   ; TargetCount
FUN_004ab480(record[0x28], record[0x2c], 0x4000)   ; UI announce
```

`+0x94` is `TargetCount`, and the retail creator writes 1. The aligned port preserves the write,
but `FUN_004a73e0` gives this specific type its own test: the burglar remains incomplete exactly
while both `CriminalsCaught == 0` and `Casualties == 0`. It does not use the shared target-count
comparison. Pinned by `SimCopter.Missions.BurglarCarStaysOpen`.

The record is closed by `EVT_CriminalCaught` after `FUN_004b8c90`'s no-return path. The category-4
`CAT_ExpireSilently` event belongs only to `FUN_004b8b60`'s person-placement failure.

## 6. Historical divergences

- **Removed 2026-08-09: the payout happened when the car stopped, not 120 s later.** The original posts
  `EVT_CriminalCaught` from `FUN_004b8c90`, at the end of the hold (section 4b). Waiting two
  minutes with no feedback reads as a bug in play, so the remake posts it the moment the car
  comes to rest and the driver is placed. The aligned port now waits for the actual no-return path.
- **Removed 2026-08-09: the port skipped the sound gates.** `FUN_004b8b60` advances on
  `FUN_0042a3a0(0x6f)` / `(0x70)` reporting their clips finished. The aligned port now plays
  `aDrOpen` / `aDrClose`, waits on the same slots, and persists that subphase in runtime saves.
- **`FindPursuitTarget` scans the live speeder list, not the per-tile object map.** The original
  spirals three rings and walks each cell's object list; the remake has no cell object map, so it
  applies the same `FUN_0049b000 < 3` step test to every live speeder. Same answer, because the
  pool is five deep.
- **The criminal car's own state machine (`FUN_004b8630` cases 0/1/2) is not reproduced.** The
  remake's speeder cruises the road graph continuously instead of cycling
  cruise -> pause -> flee on the `+0x137` / `+0x13b` timers, and does not raise the siren or the
  `FUN_004af220` effect on being marked. What is reproduced is everything the *player* interacts
  with: the mark, the speed response, the stop order and the arrest.
- **`FUN_0049df60`'s route-ahead test is not ported.** The remake checks the target's current
  tile and the stopped-vehicle occupancy, but not "the next tile along its route is also not an
  intersection", because the agent's route cursor does not expose a look-ahead tile.

## 7. Known gap - the pursuit payout

`EVT_SpeederPursuit` (0x21) is **not** posted by the remake, so catching a speeder currently pays
nothing. Located but not ported, because the gating flag is not pinned down:

`FUN_0049be50` (the vehicle move step) contains

```
if (veh[1] & 0x800) {
  if (!(veh[1] & 0x1000) || !FUN_0049e130()) {
    if ((veh[1] & 0x20) && veh[0xd7] <= 0 && FUN_0049bc60(veh)) {
       FUN_004a89c0({0x21, eventId = -1, ., ., value = 1})   ; pays Incmtl Points as cash
       veh[0xd7] = 0xa0000                                    ; 10 s cooldown
    } else if (veh[0xd7] > 0) {
       veh[0xd7] -= dt
    }
  }
}
```

`FUN_0049bc60(veh)` is confirmed: a radius-2 spiral that returns 0 when **another vehicle with
message id 0x11d (a police car) is nearby**, 1 otherwise.

What is *not* confirmed is `veh[1] & 0x20`. `FUN_0049e0c0` clears `0x60` when a halt is ordered,
yet `FUN_004b8630` cases 1/2/5 trigger the arrest on `veh[1] & 0x20` being **set** - which only
reconciles if the mover re-sets `0x20` once the car is actually at rest. That is a guess, and the
payout's meaning flips entirely on it (reward while fleeing vs. reward while pinned). Resolve
`0x20`'s writer in the mover before porting this.
