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

---

# Second pass (2026-07-29): the last three, and the ambient stubs behind them

**Coverage now: 81 opcodes in the table, 67 used by the shipped programs, 73 ported. Every opcode a
shipped record reaches is ported** - `opcode_map.py`'s "used but NOT ported" list is empty for the
first time. The eight that remain (42, 43, 45, 49, 52, 64, 65, 81) have no record site in
`people.df` at all. Everything below is decoded from the executable, not inferred from play; each
row names the function it came from.

## 50 - "is there room for me?" (3 sites)

`FUN_004cc980` writes into `local[args[0]]`:

- selection == `DAT_005040d0+0xa4` (the player's helicopter) -> `FUN_0048c1e0(DAT_005040d0+0x1d4)`,
  which is `manifest[+4] - manifest[+8]`;
- selection has obj flag `0x10` -> the constant `0x1721`;
- anything else -> **nothing is written** (the local keeps its previous value).

`DAT_005040d0+0x1d4` is the **seat manifest**, and the two fields are decoded outright:
`FUN_0048bff0` (add a passenger) refuses when `[+4] == [+8]`, increments `[+8]`, and fills the first
free of 16 entries at `[+0x1c]` (stride 0x14: `+0x00` head image from person+0x18e, `+0x04` face
index - the slot opcode 54 writes - `+0x08` flags `0x200`, `+0x0c` person id, `+0x10` display slot);
`FUN_0048c120` (remove) decrements `[+8]`; `FUN_004c0ba0` iterates `[+4]` entries. So `[+4]` is the
helicopter's seat count and `[+8]` the number occupied: **opcode 50 on the player's helicopter is
`GetAvailablePassengerSeats()`.** Flag `0x10` is named by `FUN_004c4e10`, which copies `obj+0xe`
into person+0x170 - the "emergency vehicle I belong to" field - so the `0x1721` arm means "an
emergency vehicle, always room".

Every site follows it with `local > 0`:

| program | records | what the gate does |
|---------|---------|--------------------|
| 1051 `cop - wait at station` | `[13]` -> `[14]` | a waiting officer only walks over and boards (op 12) when your cabin has a free seat; otherwise back to Idle-20. |
| 1054 `cop - return to copter` | `[10]` -> `[11]` | same gate before the return trip. |
| 272 `nearest emerg veh on stack` | `[5]` -> `[6]` | a full cabin sets `l0 := 5000`, so the nearest fire truck wins the "closest ride" comparison instead. |

## 78 + state 16 - the UFO abducts people (1 site)

`FUN_004cb830`: no `person+0x1a8` -> result 1. Otherwise normalise the 3D delta to that object
(`FUN_00467a80` normalises in place and returns the length), scale by `person+0x164` (movespeed -
**whole units per tick, not the walker's `/12`**), and *teleport* to the result
(`FUN_004c7910` / `FUN_004c6fa0` / `FUN_004c78c0`: unlink, set position+tile+tile class, relink).
No move check, no climb gate, no terrain. Result 2 (yield) only while the pre-step distance was at
least one step **and** the person is within 0x15 tiles of the camera tile
(`DAT_0061a618`/`0x61a61c`); result 1 otherwise.

Who fills `person+0x1a8`: `FUN_004c0f40(person, target)` = eligibility (`FUN_004c0f80`) then
`FUN_004c0df0(0x10, -1)` - **a state change to 16** - then `+0x1a8 = target`. State 16 is BHAV
**666 'Porkchop'**, and the program is the whole gag:

    [0] Whoa -> [1] movespeed := 20 -> [12] Wave -> [10] 1-in-2 -> sound 51 / Idle-10
    -> [6] idle rand20+1 -> [3] sound 33 -> [4] op78 (fly up, yields until it arrives)
    -> [8] visible := 0 -> [2] sound 34 -> [9] Idle-10 -> [5] op40 despawn

The caller is `FUN_004c0d10`, and `FUN_004b2630`'s `param_1[0x15] != 0x12e` arm is its only
call site - i.e. **the plane that is not PLANE1, which is the UFO** (see
`planes_trains_boats_decode_20260727.md`). Per UFO tick:

    if (peopleRand(DAT_0058dc3a >> 2) != 0) return;             // the "beam now?" roll
    for (i = 0; i <= DAT_0058dc3e; ++i)                          // every person slot
        if (peopleRand(2) == 0) FUN_004c0f40(person[i], ufo);

(asm at `Docs/scratchpad/agent-sessions/2026-07-29-people-vm-opcodes/asm_4c0d10.txt`; `DAT_0058dc3e`
is the person high-water index every people loop uses, and slot 32000 resolves to `DAT_00506444`,
the player - dead code, because the eligibility test rejects them.)

`DAT_0058dc3a` has two writers and **the order settles it**: `FUN_00479bb0` calls the
figure.twk bind `FUN_004c8120` at `0x00479bd3` ("Consider this large" = 4) and then
`FUN_004c06d0` -> `FUN_004c2f30` -> `FUN_004c3010` at `0x0047a212`, which stores `0xfde8`. So it is
**65000 during play**, the roll is 1-in-16250 per UFO tick, and the same threshold feeds the
celebrity re-roll in `FUN_004c7190` (which is how the remake already had it).

`FUN_004c0f80`, the per-person eligibility test:

- `+0x142` alive;
- **state != 0 also needs `peopleRand(3000) == 0`** - the UFO overwhelmingly takes ambient
  pedestrians and only very rarely a mission person;
- `+0x15e == 0` (`FUN_004c0ba0` sets it on everyone aboard when the helicopter is destroyed, so it
  reads "already written off");
- not the player (`+0x12e != 32000`);
- `FUN_0049ad30(pos, +0x1c4)` - a four-plane frustum test, i.e. **on screen**;
- within 9 tiles of the camera tile;
- and `+0x1a0 != DAT_005040d0+0xa4` - not riding your helicopter.

## 80 - the street conversation (1 site)

`FUN_004cb790`: with `person+0x1a4` set, call the post-move selector
`FUN_004c6970(movespeed, (source+0xc & 8) ? 5 : 4, source)`. Result 5 is the decoded
"met another person" arm - face them (`facing = bearing-2 & 7`), bind `2Gab` or `HipH` 50/50, and
play one of nine random voice lines; result 4 is `Whoa` plus sound 0x2a. Its one site is
**914 `Rxn: Person--civil, neutral` rec[2]**, which is `DAT_0058d728[13]` - the reaction the move
core broadcasts through `FUN_004c1050` when a step bumps into somebody
(`FUN_004c9470` result 5). This is the "bump result 5 (street chats)" item that has been open in
`simcopter-people-logic-next.md` since 2026-07-02.

    914: [0] attr14 += 1 -> [5] 1-in-4 ? [6] op32 face away + Scatter + Move 10
                                       : [2] op80 gab at them + Idle-5

`FUN_004c9300`'s retry loop breaks on results 0/7/8/10, so **result 5 blocks the step** and turns
the walker one octant clockwise.

## 26 - rebind my state's program (0 sites)

`FUN_004cb5e0`: if the top walk frame's program id != `person+0x17a` (the per-state program from
`DAT_0058de80`), pop when the stack is nearly full and `FUN_004ce700(0,0,+0x17a)` - push it. The
same "push it unless it is already on top" shape appears inline in `FUN_004c65e0` for BHAV 802.
Nothing shipped uses it.

## The stubs behind them, all on the ambient hot path

`600 'Ambient initbhav'` - the program every pedestrian runs - calls `270` (riot check) at `[8]`,
`274` (gawk at fire) at `[9]` and `273` (gawk at corpse) at `[10]`. Those three are exactly the
programs whose opcodes were still stubs, so the stubs were costing every person in the city three
behaviours.

**Op 31 `FUN_004cc240` (12 sites)** - `facing = bearing(selection) + 2 & 7`: face **away from my
selection**, TRUE when there is nothing selected, FALSE when there is no bearing. Used by every
flee program (`1171/1172/1173` run from cop / cop car / copter, `900/901/907/908` reactions,
`273/286`) right after the thing to run from is selected. Returning a bare TRUE meant they fled in
whatever direction they happened to face.

**Ops 32/33 `FUN_004cc290` -> `FUN_004cc2b0`** - the same thing against `person+0x1a4`: token 0x21
(33) faces toward (`bearing-2`), token 0x20 (32) faces away (`bearing+2`). No source -> **TRUE with
no facing change**; no bearing -> random facing, FALSE. The remake was taking the random-facing arm
unconditionally.

**Op 24 `FUN_004cb480` (1 site) - the riot contagion.** Requires a live 0x1000 record
(`FUN_004a9230`), then `FUN_004c9f10(sceneNode, radius, &avgSpeed, &count, &bearing)` scans the
square of scene cells within `local[args[0]]` tiles, counts objects with flag 8 (people, minus
itself), sums their `+0x150`, and reports the bearing octant to the crowd's mean position, the mean
`+0x150`, and the head count -> `local[args[1]] = bearing`, `local[args[2]] = mean`,
`local[args[3]] = count`. `852 'Refigure riot val and turn to it'` then computes
`riotValue = count * mean / 15`, turns toward the crowd when it exceeds 2, and walks its own
`+0x150` one step toward it per pass. **`+0x150` is an agitation level, not a speed.**

**Op 27 `FUN_004cb630` (1 site)** - `person+0x1c4 = (+0x150 > 5) ? 1.5 : 3.0` original units. That
field is the person's own radius: the frustum test uses it, and so does the object-collision query
`FUN_004c9000` (`obj+0x10 / DAT_00506aec`). So an agitated rioter shrinks to half size and the mob
packs twice as tight.

**Op 28 `FUN_004cb680` (1 site) - join the riot.** `FUN_004c4e60`: find the live 0x1000 record,
post `EVT_RiotPersonAdded (0x0b)` with value 1, then `FUN_004c0df0(3, record)` - become a **state 3
rioter** owned by that record. Returns 3 (Stop) because the state change replaced the program;
returns 0 when no riot is running. `270 'check riot and join if big'` reaches it when the person's
own agitation passes 2, so a riot spreads through the ambient crowd by itself.

**Op 36 `FUN_004cc470` (1 site)** - `FUN_004ca190(radius, &x, &y, &dist)` clears cell bit 2 over the
square, then takes the Manhattan-nearest cell with **cell flag 0x20** set; op 36 faces toward it and
writes the distance to `local[args[1]]`. `274 'Gawk at (or flee) fire'` calls it with radius 12 and
branches: **>= 6 tiles away run toward it, 4-5 stand and dance `HipH`, < 4 turn 180 and flee**, with
a 1-in-12 chance per pass of giving up (it saves/restores the ambient and autoturn attributes around
the whole thing). Flag 0x20 is also what makes `FUN_004c9cc0` refuse an ambient spawn on a cell. Its
setter is not in the Ghidra export set, so "0x20 = this cell is alight" rests on the program name
plus the spawn-gate use, not on a decoded write - the one inference in this pass, and it is called
out again at the port.

**Op 35 `FUN_004cc410` (2 sites)** - the threshold is `FUN_004abb00(0x20)`, which counts **active
mission records whose type mask contains 0x20 (MedEvac)**. `906 'Rxn: Swoon'` sets
`l0 := difficulty + 2`, falls off its carrier, and leaves the map unless that many medevacs are
already running; `293 'Scallop fall'` passes -1, which skips the compare and always leaves.

**Op 79 `FUN_004cb7d0` (3 sites) - a stopwatch, not a dead counter.** `DAT_00506448` is the people
system's tick counter: `FUN_004c5fb0` accumulates the frame delta and, when it passes
`DAT_00506450 = 0x147a` (0.08 s -> **12.5 Hz, the original behaviour tick rate**), increments it and
runs one behaviour tick for every person. `444 'Tuba initbhav (SID 246)'` reads it into `l3`, walks
to the player's avatar (object class 9), reads it again into `l2`, and plays a tuba note (sound
37/38, 1-in-3) every time `l2 - l3 > 20` ticks. Writing 0 made the difference always 0, so the tuba
player never played.

## What the port added

All of it goes through the existing interaction framework and the existing carrier/mission services;
nothing about the framework's shape changed. New `ISimCopterBehaviorWorld` methods carry the world
half of each opcode, so the VM stays a pure interpreter.

| behaviour | how it reaches the player |
|-----------|---------------------------|
| An officer checks for a free seat before boarding (50) | a cop at the station or heading back to your machine now waits when the cabin is full instead of walking over to it; a hospital helper prefers the fire truck. |
| The UFO abducts people (78 + state 16 + `TryBeamPeopleUp`) | 1-in-16250 per UFO tick, then a coin flip per person: everyone eligible waves, flies up to the saucer at 20 units a tick, and vanishes. |
| Street conversations (80 + the move core's bump) | walking into somebody blocks the step, turns you both, and plays `2Gab`/`HipH`; cops and paramedics carry attribute 32 = 916 so nobody chats with them. |
| Fleeing in the right direction (31, 32/33) | criminals, rioters and reaction programs now turn away from what they are running from instead of keeping their old facing. |
| Riot contagion (24, 27, 28) | a bystander measures the mob's size and agitation, drifts toward it, and joins as a state-3 rioter once their own agitation passes 2; agitated people shrink to half radius and pack tighter. |
| Fire watching (36) | people converge on a fire from 6+ tiles, gawk between 4 and 6, and run at under 4. |
| Swooning into a casualty (35) | tear gas can now put a civilian on the ground as a real medevac victim with its own mission, capped at difficulty + 2 live medevacs. |
| The tuba player (79) | its note timer measures real elapsed ticks, so it actually plays. |

Two deliberate remake choices, both noted at the call site:

- **Opcodes 28 and 35 yield rather than stop.** The original returns 3 because the state change
  already rebound the program; the remake's `EOpResult::Stop` is wired to the despawn path, which
  would delete the rioter or casualty that was just created.
- **A collapse (35) routes through the player-caused injury service**, so it pays no delivery reward.
  Almost every route into BHAV 906 is one of the player's own tools, and that matches the existing
  rule for putting a civilian in hospital yourself.

Verified: clean editor build, and the whole automation suite (86 tests) green, including a new
`SimCopter.Behavior.VM.LateOpcodes` covering each opcode's edges and `SimCopter.Behavior.VM.Reference`
pinning the shipped record sites (1051 rec 13/14, 666 rec 4/8, 914 rec 2/6, 274 rec 0, 852 rec 2).
The ambient reference run reports 356 move steps and zero unported opcodes. Nothing here has been
watched on screen yet - the abduction in particular needs a UFO overhead in a live city.

## Still open after this pass

- **Op 15 class 15**, the "corpse" the ambient program gawks at (`273` probes it at radius 3). The
  class dispatch is the jump table at `0x004cb130` inside `FUN_004cac70`, and Ghidra's function
  boundary stops at 82 instructions - the class-15 inline scan at `0x004caee5` needs a raw capstone
  pass like the criminal decode used. Everything else about opcode 15 is decoded.
- **Cell flag 0x20's setter** (see op 36 above).
- **The spotlight reaction's odds.** `FUN_004c1050`'s mode-1 arm rolls `FUN_004cea00(DAT_0058dc3a)`,
  i.e. 1-in-65000 rather than the 1-in-N the remake currently uses. Worth checking
  `SpotlightReactionChance` against that before the next spotlight pass.
- **The original behaviour tick rate is 12.5 Hz**, not the remake's 15: `DAT_00506450 = 0x147a` is
  0.08 s per people tick (`FUN_004c5fb0`). Everything scales off that constant - move distance, idle
  durations, clip playback - so it is a one-line change with a wide blast radius, left alone here.

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
