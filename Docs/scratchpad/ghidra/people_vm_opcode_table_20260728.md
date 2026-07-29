# People behaviour VM - complete opcode table (2026-07-28)

The handler-table mapping is authoritative; gameplay ownership and call-site conclusions are not.
`FUN_004c3010` fills the 88-slot table at `DAT_0058ef78` with 0x20-byte thunks starting at
`LAB_004c84e0`; each thunk forwards to its handler with a `call` at thunk+0x0e. Opcode =
`(slot address - 0x58ef78) / 4`, which is why the numbering is sparse - 81 of the 88 slots are
filled and the gaps are real. Rebuild with `scratchpad/opcode_map.py`. The VM requests world
actions; stable engine services remain authoritative for real people, carrier attachments,
helicopter seats, and idempotent mission outcomes.

**Coverage: 81 opcodes in the table, 67 used by the shipped `people.df` programs, 70 ported.
Only 50, 78 and 80 remain among shipped record sites (5 record sites).**

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
| 73 | `FUN_004cb9c0` -> `FUN_004ca4f0(5, 0)` | 1 | Is there a hidden state-5 paramedic on the map (`person+0x152 == 0`). This is a live query: `FUN_004c25b0` spawns state 5 at XBLD D1, and a riding paramedic is hidden. |
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

The complete set of `FUN_004c3eb0` person-spawn call sites in the executable, by state:

    (-1, 1)   (-1, 2)   (-1, 0x13)   rescue victims  -> BHAV 700
    (-1, 3)   riot      (-1, 4)      transport       (-1, 6)  medevac victim
    (9, 10)   (9, 0xb)  (9, 0xc)     the criminals   (0x15, 0xf) scallop
    (0xe, 8)  (0xe, 0xe)             police officers (0xf, 0xd)  arrested speeder driver

That list is not the complete person-spawn graph. `FUN_004c25b0` case XBLD D1 explicitly calls
`FUN_004c2260(0x0c, 5, TileX, TileY, -1, 0)`. State 5 and BHAV 801 "Medevac paramedic new
initbhav" are therefore live hospital behavior; helpers 262, 263, 269 and 272 are not dead-content
evidence. BHAV 282 remains the victim-side completion behavior once a patient is set down on the
hospital tile (opcode 25 against XBLD 209 plus opcode 56).

`FUN_004c4190` also supplies the medevac-health seed that was previously treated as unknown: its
successful non-mode-4 spawn path writes `100` to person `+0x184`, the attr34 drained by BHAV 281.
The unrelated constant `58` from BHAV 800 is not a health seed.

### When the hospital paramedic boards

The shipped graph agrees with the observed intent, but not with restarting the program after an
unload:

1. BHAV 801's XBLD D1/serviceable arm calls BHAV 263 "Pull all med victims off heli".
2. BHAV 263 record 0 uses opcode 84 to select a medevac victim aboard the player. Its victim-found
   arm performs the unload/tote sequence.
3. Only its **no victim aboard** arm reaches BHAV 269 "medic get on starting object".
4. BHAV 269 uses opcode 62 to select the emergency vehicle the medic belongs to, falling back to
   the player's helicopter, then uses opcodes 12/48 to board it. If no starting object can be
   selected, record 9 executes opcode 40 (`Disappear`), which explains the visible vanish after
   this branch became detached from its original population lifecycle.

So a paramedic must not try to board while a patient is aboard. The graph by itself cannot
distinguish "we arrived empty and need help retrieving a patient" from "the last patient was just
removed": both have no patient aboard. The remake supplies that missing temporal guard at the
stable boarding service. A state-5 hospital paramedic may take the fallback helicopter ride only
when an active medevac still has a patient waiting for pickup; completing a handoff resumes the
paused VM stack rather than restarting BHAV 801. State-5 hospital staff are also protected from a
visible `Disappear` immediately after service.

The population scan is not allowed to own whether this service exists. While a medevac is active,
the mission layer guarantees a real state-5 worker on that mission's D1 hospital roof, even when the
hospital is outside the ambient population radius or the random-pedestrian pool is full. A worker
posted this way is distance-persistent. If one leaves as a retrieval helper, the still-active
mission posts a replacement to preserve roof coverage.

### Deceased patients still leave through the hospital

Opcode 66 (`FUN_004cbbc0`) is a terminal fall/death action: it posts `EVT_PersonDied`, binds
`Dead`, and the VM requests population cleanup. Applying that cleanup literally to a medevac
patient who dies inside the cabin relinquishes their seat and destroys the only actor the hospital
paramedic could remove.

The stable carrier service therefore owns one deliberate physical invariant: a deceased medevac
patient already in the cabin remains attached to their real seat until the hospital handoff alights
and carries that same actor inside. The casualty outcome is still posted immediately, so the
mission receives no delivery credit or delivery reward and may retire its scoring record. The
mission layer caches the hospital tile across that retirement and keeps the roof medic posted until
the body has actually left the cabin.

### Transport action order

BHAV 291 "Transport go to avatar/get on heli" probes object class 2 (the player's helicopter) at
one tile and reaches opcode 12, the real walk-and-board action. BHAV 292 "Transport wait to get
off" probes its mission coordinates, runs opcode 17 to perform the real alight, posts delivered
outcome 1, and only then disappears. The class-0 probe was previously declared but absent from the
world adapter. `FUN_004a88e0` returns the live mission record's pointer at `+0x30` when that field
is not `-1`; `+0x30/+0x34` are `SecondaryX/Y`, so class 0 now resolves the actual destination.
Accordingly:

- pickup outcome 0 is only an acknowledgement after this real person owns a matching helicopter
  seat; it can no longer create mission progress by itself;
- the recovery approach targets the cabin door and calls the same `BoardCarrier` service;
- `DROP` is derived from matching live helicopter seats, never from historical
  `VictimsPickedUp`; and
- delivery releases that exact actor and seat before posting the transport outcome. A VM timeout
  cannot despawn an unresolved mission person.

## Stability ownership correction

- **The VM requests actions; it does not replace them.** `BoardCarrier` / `AlightFromCarrier`
  preserve the real person actor, own the attachment and passenger seat together, and route pickup,
  delivery, and death through one idempotent mission-action service.
- **`ProcessRescueTransfers` is a recovery path again.** BHAV 305/303 still drive normal approach,
  harness, and alight behavior. The recovery loop calls the same carrier services when a swimmer,
  roof victim, or moving-train victim would otherwise strand the mission; it does not create a
  second seat or post a second outcome.
- **Boat and train survivors run the VM.** They were inert scripted movers, which only worked while
  an engine-side pickup existed to teleport them into a seat. They keep their remake-side ground
  snap suppression - there is no walkable surface under a swimmer or a roof rider - while stable
  carrier actions remain available to the recovery loop.
- **`SetMissionInjuredPose` no longer freezes the walker.** An injured person is a medevac victim
  and BHAV 800 binds "Dead" itself. Its decoded attr34 seed is 100.
- **`DropPassengerAtSlot` releases the real passenger** rather than spawning a stand-in beside a
  still-attached invisible one.
- **Hospital handoffs are active and bounded.** They reuse the decoded state-5 roof paramedic when
  available, own automatic medevac cabin alighting so the victim VM cannot race the EMT to an empty
  seat, visibly carry the actual patient out of the helicopter, and fall back to direct delivery if
  staging or movement fails. A legacy abstract seat gets a stand-in only when no real attached
  person exists.
- **A casualty is not an empty cabin.** A dead medevac actor keeps its seat through the flight and
  hospital removal. Its already-reported death makes `NotifyMissionPersonDelivered` reject reward,
  while the cached hospital service remains available after the mission record retires.
