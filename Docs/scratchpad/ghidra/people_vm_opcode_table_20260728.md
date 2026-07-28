# People behaviour VM - complete opcode table (2026-07-28)

The authoritative map, not a reconstruction. `FUN_004c3010` fills the 88-slot table at
`DAT_0058ef78` with 0x20-byte thunks starting at `LAB_004c84e0`; each thunk forwards to its handler
with a `call` at thunk+0x0e. Opcode = `(slot address - 0x58ef78) / 4`, which is why the numbering is
sparse - 81 of the 88 slots are filled and the gaps are real. Rebuild with
`scratchpad/opcode_map.py`.

**Coverage: 81 opcodes in the table, 67 used by the shipped `people.df` programs, 70 ported.
Of the 67 the programs actually run, 65 are live; only 50, 78 and 80 remain (5 record sites).**

## Ported this pass

| op | handler | sites | meaning |
|----|---------|-------|---------|
| 25 | `FUN_004cb550` | 5 | XBLD building id at my tile == arg0. BHAV 801 tests 209 = `XbldHospital`. |
| 37 | `FUN_004cc530` | 4 | `FUN_004ca4b0()` then result 3 - leave the map. |
| 46 | `FUN_004cc7d0` | 1 | Select whatever I am riding. |
| 53 | `FUN_004cca60` | 3 | Select the player's helicopter if within 24 units (3D Manhattan). |
| 56 | `FUN_004ccc40` | 5 | Is my tile one a crew member can work on. |
| 62 | `FUN_004ca700` | 2 | Select the emergency vehicle I belong to (person+0x170), else the player's helicopter. |
| 63 | `FUN_004ca6f0` | 6 | person+0x1a0 != 0 - am I riding something. |
| 67 | `FUN_004cbb80` | 6 | Copy my selection to the caller's frame. |
| 68 | `FUN_004cb730` | 1 | Copy the caller's selection to mine. |
| 69 | `FUN_004cbb60` | 1 | Is my selection the player's helicopter. |
| 71 | `FUN_004cbaa0` | 2 | Do I have a carrier. |
| 72 | `FUN_004cb770` | 1 | Clear the selection. |
| 74 | `FUN_004cba70` | 3 | local := `DAT_004f9740`, the difficulty tier. |
| 75 | `FUN_004cba10` | 2 | local := my tile X (person+0x12a). |
| 76 | `FUN_004cba40` | 2 | local := my tile Y (person+0x12c). |
| 77 | `FUN_004cb9e0` | 4 | local := abs(local). |
| 82 | `FUN_004ccad0` | 5 | Is my selection within 25 units (3D Manhattan). |

Ported earlier in the session: 13, 14, 15, 18, 38, 39, 40 (see
`criminal_ai_decode_20260728.md`). Already present before: 0, 1, 2, 4, 6, 7, 16, 17, 19, 20, 21,
22, 23, 24, 27, 28, 29, 31, 34, 36, 57, 59, 70, 85, 86.

Opcodes 67/68 are implemented as successful no-ops: they exist to copy the selected object between
walk frames, and the remake keeps one selection slot per person rather than one per frame, so the
copy is already implicit.

## The carry / board family - now ported

These are how every passenger in the game gets into and out of everything. `ASimCopterGroundAgent`
gained person+0x1a0 (`BehaviorCarrier`), and boarding a helicopter cabin claims a real passenger
seat so the seat window and the mission counters agree with what the VM did.

| op | handler | sites | meaning |
|----|---------|-------|---------|
| 12 | `FUN_004ca940` | 7 | Walk to the selection **and get on it** - the same handler as op 38, taking its `*param_3 == 0xc` branch. |
| 44 | `FUN_004cc6a0` | 2 | Teleport the selected *person* onto me and become their carrier. |
| 46 | `FUN_004cc7d0` | 1 | `FUN_004ca650`: select the person whose carrier is me - whoever I am toting. |
| 47 | `FUN_004cc8d0` | 1 | Clear the selected person's carrier - put them down. |
| 48 | `FUN_004cc900` | 3 | Set the selection as my carrier and snap to it - board without walking. |
| 51 | `FUN_004cca00` | 4 | `FUN_004ca570`: set down whoever I am toting, re-seat them on the ground, and select them. |
| 58 | `FUN_004cccd0` | 2 | `GetOnHeliIfHarnessRaised` - the assert strings name the function and its file, `C:\Copter\Source\X\Y\Yobjsim.cpp:0x94f`. True unless my carrier is the harness (`DAT_005040d0+0xbc`) with the bucket raised, in which case transfer me into the cabin (`+0xa4`). |
| 59 | `FUN_004cce30` | 13 | Is my carrier the player's helicopter. |
| 61 | `FUN_004ccef0` | 12 | Message the emergency vehicle I belong to - how a boarded crew member releases its car. |
| 71 | `FUN_004cbaa0` | 2 | Am I carrying anyone (not "do I have a carrier" - that is op 63). |
| 84 | `FUN_004cc830` | 2 | Select the first medevac victim riding the player. |
| 86 | `FUN_004cceb0` | 3 | Is my carrier the harness. |
| 87 | `FUN_004cce50` | 1 | Am I back on the tile I was placed on (person+0x188/+0x18a). |

### Two opcodes that were ported wrong

**Ops 17 and 21 were not threat probes.** Both go through `FUN_004c9bc0`, which is: the tile I am
on is one people may occupy, **and** I am within 6 original units of the ground, **and** if I am
riding something I am still within `DAT_0058dc32` of it. Op 21 is that test; op 17
(`FUN_004cb190`) runs it and then *performs the alight* - clears the carrier and re-seats the
person on the terrain. It is the drop-off gate for every passenger type in the game. They had been
mapped to "is the player's helicopter close and low", which is why nothing could ever be set down.

### The rest

| op | handler | sites | meaning |
|----|---------|-------|---------|
Ported with a stated compromise:

| op | handler | sites | meaning and what the remake does |
|----|---------|-------|----------------------------------|
| 30/60/83 | `FUN_004cbfd0` / `FUN_004cc130` | 5 | Bind "Thro" and launch a projectile. The remake has no rioter projectile object, so only the animation half runs - which is the visible half. |
| 32/33 | `FUN_004cc290` -> `FUN_004cc2b0` | 3 | Face away from / toward person+0x1a4, the last interaction source. The remake keeps no handle to it, which lands on the handler's own no-bearing arm: random facing, result 0. |
| 35 | `FUN_004cc410` | 2 | Conditional despawn against `FUN_004abb00(0x20)`, a tuning read the remake does not have. Stays put rather than vanishing people on a threshold it cannot read. |
| 54 | `FUN_004ccb40` | 4 | Which of three faces the person shows in the seat window (BHAV 264 sets it from speed and health). Stored on the agent; the seat window does not draw moods yet. |
| 66 | `FUN_004cbbc0` | 5 | Fall and die: detach, land, `FUN_004ccf50(10)` = `EVT_PersonDied`, hold "Dead". The projectile-as-body half is not reproduced. |
| 73 | `FUN_004cb9c0` -> `FUN_004ca4f0(5, 0)` | 1 | Is there a hidden paramedic on the map. Always false in the shipped game - nothing spawns person state 5 - so BHAV 281's fast health-decay arm is the only reachable one. |
| 79 | `FUN_004cb7d0` | 3 | local := `DAT_00506448`, a frame counter with no remake equivalent; writes 0. |

Still not ported (5 record sites):

| op | handler | sites | meaning |
|----|---------|-------|---------|
| 26 | `FUN_004cb5e0` | 0 | Unused by shipped programs. |
| 50 | `FUN_004cc980` | 3 | Write an identifier for the selected object into a local (the player's helicopter resolves through `FUN_0048c1e0`; anything with obj flag 0x10 writes 0x1721). The id space has no remake counterpart. |
| 78 | `FUN_004cb830` | 1 | Move toward person+0x1a8 at movespeed; result 2 once within 0x15 tiles of `DAT_0061a618`/`0x61a61c`. |
| 80 | `FUN_004cb790` | 1 | Re-run the post-move clip selector against person+0x1a4. |

## Mission-layer findings from the same pass

`FUN_004a7a10` is the record creator. Its spawn counts:

- **Transport (0x40):** tile chosen by up to 10 tries for an XBLD in `(0x6f, 0xdc)`, then
  `rand() % (DAT_004f9740 + 1) + 1` passengers spawned at person state 4. The remake had a fixed
  ten every time; fixed.
- **Medevac (0x20):** `rand() % DAT_004f9740 + 1` victims at person state 6. The remake already
  matched.

`DAT_004f9740` is the difficulty tier - `FUN_004a92f0` switches on it being 1/2/3, `FUN_004a6d20`
loads it from the career city, and its static initialiser is 2.

`FUN_004c3eb0(param_1, param_2, tileX, tileY, eventId, ...)`'s **param_2 is the person state**, not
a spawn mode: the transport creator passes 4, the medevac creator 6, and `FUN_0049bd00` passes the
officer's 8 or 0xe (confirmed twice over by `FUN_004bd980(0xe, 8, ...)` in `FUN_004b9e40`).

The complete set of person-spawn call sites in the executable, by state:

    (-1, 1)   (-1, 2)   (-1, 0x13)   rescue victims  -> BHAV 700
    (-1, 3)   riot      (-1, 4)      transport       (-1, 6)  medevac victim
    (9, 10)   (9, 0xb)  (9, 0xc)     the criminals   (0x15, 0xf) scallop
    (0xe, 8)  (0xe, 0xe)             police officers (0xf, 0xd)  arrested speeder driver

**Nothing spawns person state 5.** BHAV 801 "Medevac paramedic new initbhav" and its helpers (262
"Put all nearby medevac victims on emerg vehs", 263, 269, 272) are unreferenced content, as are
1498 "old Rescue initbhav" and 1499 "old Medevac paramedic initbhav". The remake's hospital EMT
hand-off was therefore an invention rather than a port, and is disabled: a medevac victim is
delivered by BHAV 282 "Medevac test for finished" the moment they are standing on a hospital tile
(opcode 25 against XBLD 209, plus opcode 56).

## Divergences removed this pass

- **`ProcessRescueTransfers` deleted.** It was a second, engine-side rescue passenger loop. BHAV
  305 boards survivors onto the harness and BHAV 303 sets them down, both posting their own mission
  events; running both double-counted. Survivors now walk away from the drop point themselves
  instead of being swapped for stand-in pedestrians.
- **Boat and train survivors run the VM.** They were inert scripted movers, which only worked while
  an engine-side pickup existed to teleport them into a seat. They keep their remake-side ground
  snap suppression - there is no walkable surface under a swimmer or a roof rider - but the program
  is the original's.
- **`SetMissionInjuredPose` no longer freezes the walker.** An injured person is a medevac victim
  and BHAV 800 binds "Dead" itself; stopping the VM discarded the health decay, the death and the
  delivery test with it.
- **`DropPassengerAtSlot` releases the real passenger** rather than spawning a stand-in beside a
  still-attached invisible one.
- **`ProcessMedevacHospitalHandoffs` disabled** - see above.
