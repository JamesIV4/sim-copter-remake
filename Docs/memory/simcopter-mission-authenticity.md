# Mission authenticity pass — what the shipped data actually says (2026-08-05)

*Answers, from `people.df` + the decompile, to a batch of "does the remake have X?" questions, plus
the ports that closed the gaps they found. Read alongside [[simcopter-people-logic-next]] (the
behaviour VM), [[simcopter-mission-system]] (records and fires) and
[[simcopter-heli-tools-models]] (the tools that reach people).*

Dumps behind every claim: `Docs/scratchpad/agent-sessions/2026-08-05-mission-authenticity/`.

---

## 1. There is no "get out from under the helicopter" behaviour, and adding one was wrong

`UpdateDescendingHelicopterAvoidance` (removed this pass) pushed BHAV 904 "Rxn: Run away" at anyone
sharing a tile with a low helicopter. **No shipped program contains that branch.** The only altitude
test any person runs is opcode 14 case 1, and only the criminal programs reach it (BHAV 1173
rec[10], which is how setting down on a criminal arrests them). Its cost was concrete: a crowd
scattered every time the player descended, so a transport fare could never be standing there when
you arrived.

**Boarding is a height band, not a flight state.** `FUN_004ca940` (opcode 12, the walk-and-board every
passenger program reaches) accepts the move on `moveResult == 10` — body-vs-body contact — *and*
`(objectZ - personZ) & 0xffff0000 < 0x50000`, five original units, about 31 cm. `FUN_004c9bc0`
(opcodes 17/21, the alight) asks only for a standable tile and six units of ground clearance.
Neither consults anything like `ESimCopterFlightState::Parked`. `CanTransferMissionPassengers()`
required `bIsLanded` on top of the clearance test, which refused a fare who had walked up to a
hovering aircraft with its skids a hand's breadth off the road. It is now the clearance band alone;
contact is still enforced on the walker's side by `StepTowardSelectedObject`.

## 2. The interaction vocabulary is 18 clips and a voice bank — and yes, there is a kiss

Every figure carries the **same 18 ARLU mnemonics, in this exact order** (corrected 2026-08-12 by
reading the file; the order previously written here was wrong, and all 21 figures agree on it):

```
0 1Wal   1 Inju   2 HipH   3 Whoa   4 DgRn   5 NoMo   6 Tote   7 Yumm   8 2Gab
9 Dead  10 Slum  11 Wave  12 1Run  13 DgSt  14 WvNo  15 Thro  16 Play  17 FaCl
```

Clip ids run sequentially from the figure's base (`SUIT` = `227!`..`244!`), so the ordinal and the
clip id are interchangeable — which is exactly why the wrong order was dangerous to leave lying
around. Nothing in the port indexes by ordinal (`FPrivAnimFigure::ClipIndexByMnemonic` is keyed by
the four-character name), so no code was affected.

**What each clip actually depicts, measured rather than inferred** (2026-08-12,
`Docs/scratchpad/classify_arlu_clips.py`, figure SUIT; "height" is 1.0 at the crown, 0.0 at the
feet, and the columns are peak travel in each third of the body). Every earlier description in this
repo was reasoned backwards from where a mnemonic is bound in `people.df`; this is the geometry:

| clip | frames | motion centre | legs | torso | arms/head | what it is |
|---|---:|---:|---:|---:|---:|---|
| `1Wal` | 8 | 0.41 | 29 | 22 | 10 | walk, legs leading |
| `1Run` | 6 | 0.44 | 39 | 18 | 17 | run |
| `Tote` | 6 | 0.33 | 39 | 9 | 4 | carrying, still a leg cycle |
| `Whoa` | 4 | 0.70 | 7 | 9 | 12 | upper-body recoil |
| `Thro` | 4 | 0.81 | 1 | 4 | 36 | one big arm throw |
| `2Gab` | 4 | 0.73 | 1 | 7 | 8 | talking |
| `Yumm` | 3 | 0.64 | 3 | 5 | 7 | hand to mouth |
| `HipH` | 3 | 0.26 | 5 | 5 | 0 | low-body dance |
| `NoMo` | 3 | 0.30 | 2 | 1 | 0 | idle |
| `Inju` | 3 | 0.04 | 2 | 1 | 0 | down on the ground, twitching |
| `FaCl` | 3 | 0.52 | 8 | 1 | 4 | — |
| `Play` | 4 | 0.47 | 11 | 10 | 10 | instrument |
| **`Wave`** | 4 | **0.76** | **25** | 21 | **25** | **whole-body flail: arms up AND legs kicking** |
| **`WvNo`** | 3 | **1.07** | **0** | **0** | **11** | **one arm above the head, nothing else moves** |
| `Dead` / `Slum` | 1 | — | — | — | — | static poses |
| `DgRn` / `DgSt` | 8 / 2 | — | — | — | — | quadruped clips; nonsense on a human skeleton |

So the `Wave` vs `WvNo` split recorded elsewhere in this repo **is correct, and now has evidence
under it**: `WvNo` is the greeting (a raised arm and literally zero leg motion) and `Wave` is the
panic flail. If somebody reports a rioter "kicking a leg in the air", that is `Wave` rendering
faithfully — look for why their program keeps failing its move (BHAV 289 rec[8]'s false edge binds
`Wave`), not for a mis-mapped clip.

**Read ARPP z as DOWN when doing any of this.** Taking the raw z as up puts `WvNo`'s wave at ankle
height and `Thro`'s arm swing at the feet, which is how a greeting gets mistaken for a kick.

What the player can provoke:

| what | how it is reached |
|------|-------------------|
| **wave at you** | BHAV 260 "Ambient possibly look for spolight", a 1-in-12 roll inside BHAV 600: same tile as the player (op 22) → face them → `Wave` → wait 15 → restore facing |
| **talk to you / each other** | move result 5 → `FUN_004c1050(0xd, ...)` → BHAV 914, plus `FUN_004c6970` case 5 on the bumper: face them, 50/50 `2Gab`/`HipH`, and one of nine voice lines |
| **wave you off** | `WvNo` — BHAV 291 rec[4] is a transport fare who cannot reach you |
| **dance** | `HipH`, the other half of the chat roll and BHAV 267's 1-in-4 |

**`Yumm` is bound by no BHAV.** It is in every figure's clip table and reachable from no shipped
program — dead art, like BHAV 268 "unused".

**The kiss is a SOUND, not an animation.** `kissA.WAV` / `kissB.WAV` at `0x0050650c` / `0x00506500`,
referenced by `FUN_004c5210` — the person-voice player. It is **voice event 39**, already decoded in
`SimCopterSoundTable.h` as `VOX_KISS`, and it is in the *looping* group with chewgum and the
footsteps, i.e. it is somebody's assigned idle voice (`person+0x18c`), not a one-shot reaction.

**Ported this pass:** the chat's nine voice lines, which the remake was dropping ("no people voice
bank yet" — a stale comment; the bank has been in since the sound pass). `FUN_004cea00(9)` rolls
0..8 and the switch at `0x004c6b3a` maps it to voice events **10, 1, 4, 11, 2, 18, 6, 7, 3** — not
the identity, and not sorted. Case 4 (shoved by an object) plays `0x2a`.

## 3. Arsonists: the firebomb was missing entirely

BHAV 1301 "Criminal Arsonist" → 1078 "crim - arsonist unspotted" rec[3]/[4]: `rand(1000) < 6` per
loop → **opcode 60**. That was reaching a handler that only played the `Thro` animation, so
arsonists never started anything.

`FUN_004cbfd0` reads its own record's opcode: **`*param_3 == 0x3c` (op 60) asks `FUN_0048e0b0` for
projectile type 4**; ops 30 and 83 ask for type 10. The two are completely different objects:

| | type 4 (op 60) | type 10 (ops 30/83) |
|---|---|---|
| class flag | `0x10` | `0x400` |
| pool | `DAT_005d6880`, 30 slots (shared) | same |
| life | `0x1e0000` (30 s) | `0x40000` (4 s) |
| on landing | `FUN_0048ed00` grounds it, **fresh life `0x3c0000` = 60 s of burning** | life 0, gone |
| on burnout | **can start a building fire** | — |

The ignition, `FUN_0048ed00`'s class-0x10 arm: tile takes a fire (`FUN_004a5f60`) **and** none
already burning in `FUN_004a6860`'s spiral — the remake already has that exact pair as
`CanIgniteCrashSite` — then `rand() % (8 - difficulty) == 0` creates the mission (`FUN_004a7a10`).
Events 7 / 8 (`EVT_DebrisCreated` / `EVT_DebrisExpired`) bracket it.

So the firebomb is a **delayed, probabilistic** ignition with a 60-second window in which the player
can spot the burning debris and douse it. The launch is `(rand % 200) + 0x2ee` tenth-degrees about X
= **75.0 to 94.9 degrees**, i.e. nearly straight up, so it lands on the arsonist's own tile.

Ported as `ASimCopterMissionSystemActor::ThrowArsonistFirebomb` + `UpdateBurningDebris`. The
ballistic flight is skipped (a 75-95° lob lands where it started); the 60-second burn, the smoke
cadence, the tile tests and the difficulty roll are the original's.

One rendered-geometry correction is required: retail people can occupy the same logical building
cell that accepts the eventual fire, while the remake moves their capsules outside imported walls.
The firebomb therefore remembers the nearest `FUN_004a5f60`-eligible cell within six cells, which is
the maximum displacement used by the generic mission-person placer. It still appears and can be
doused at the arsonist's position. The `6/1000` throw roll, 60-second burn, nearby-fire exclusion,
and `1 in (8 - difficulty)` ignition roll remain unchanged.

**This also gives riots their fires.** BHAV 288 "Rioter maybe throw" rec[10]/[11] rolls `rand(9)`
and **one throw in nine is op 60**, not op 30. Wiring the arsonist wired the rioters.

## 4. The water cannon is a riot tool, and the remake had only half the line

`FUN_00490690`'s impact loop maps class flags to interaction modes:

    if ((uVar9 & 0xe0) == 0) { ... } else { local_d0 = 4; }

`0xe0` is the cannon (`0x20`), the bucket (`0x40`) and type 7 (`0x80`) together, and **mode 4 is
`DAT_0058d728[4]` = BHAV 908 "Rxn: Water"**. The remake had the douse half of that function and not
the people half, so hosing a crowd did nothing whatever. Now delivered from
`AdvanceWaterTrajectoryStep`.

What 908 does to a **state-3 rioter**: `rand 1 in 6` → agitation **+8** (a backfire), else **−1**.
Compare 907 "Rxn: Teargas": same 1-in-6 → **+5**, else **−2**. So tear gas is twice as effective per
hit *and* its backfire is smaller. Non-rioters just run away (BHAV 904).

## 5. Riot escalation: what is real, and what is not

| claim | verdict |
|-------|---------|
| riots cause **fires** | **yes** — 1 in 9 rioter throws is the op-60 firebomb (§3) |
| riots cause **medevacs** | **yes** — BHAV 907 → 906 "Rxn: Swoon" → opcode 35, which is a collapse into a fresh state-6 casualty, capped at difficulty + 2 live medevacs |
| riots cause **traffic jams** | **no evidence.** The jam is `FUN_0049fe30`'s flag `0x200` on a *car*; nothing in the riot path touches it, and the original's traffic never tests pedestrians. Not ported, not found. |
| rioters respond to the **water cannon** | yes, BHAV 908 — see §4, and it was the gap |
| **megaphone → water → tear gas** ordered bonus | **not in the shipped data.** Searched the record graph and the scoring events; there is no order state and no bonus term. |

On that last row, what the data *does* say about the progression:

- **megaphone** (BHAV 901): messages 2 and 3 take `logicspeed += -1` and set attr33; message 1 is a
  Whoa + Idle-80; message 4 forwards to the spotlight reaction. So the megaphone calms at the same
  rate water does, and costs nothing.
- **spotlight** (BHAV 900): makes rioters flee (287) and throw (288). It does not calm at all.
- **water** −1 per hit, backfire +8. **tear gas** −2 per hit, backfire +5.
- **BHAV 311 "Rioter maybe leave riot"** retires anyone under agitation 3, and posts outcome **4
  (`EVT_RioterDispersed`, +10 cash +10 score)** when the player's helicopter is within 6 tiles, or
  **5 (`EVT_RioterCalmed`, no payment)** when it is not.

The incentive to escalate in order is therefore **emergent, not scored**: the free tools shed the
weak rioters, the tank and the ten canisters are finite, and the payout depends on being *there*
when they quit. If the project wants an explicit ordered bonus it is a deliberate divergence and
should be commented as one — it is not a port.

## 6. Two audits that came back clean

- **People rescued from burning buildings: supported, with one retail bug.** Intended as the Rescue
  bucket's `TYPE_RooftopRescue` (person state 2, `(rand % tier) + 1` victims), but the retail
  scheduled placer reads occupied XBLD ids as signed and rejects all of them. The remake uses the
  intended unsigned test deliberately. The unaffected *emergent* live-fire path raises one
  `TYPE_RooftopRescue` per fire object once the flame's
  burn countdown drops under `(tier * 5 + 15) * 0x40000`, above tier 1, on a tile whose XBLD
  property flags carry bit 2.
- **Ambient criminals do not exist in the original.** `FUN_004c2450`'s ambient class roll is 0..9
  plus the special 10/17 and the rare 20/11; **behaviour class 15 ("Badguy") is never spawned
  ambiently**, and `DAT_0058ec00`'s ambient rows do not contain it. Criminals exist only as
  `TYPE_Robber` / `TYPE_Arsonist` / `TYPE_Mugger` mission records (plus the car-spawned Burglar).
  The *gameplay* the
  question describes is real and already works, though: a crime-mission criminal walks around
  visibly (BHAV 1174 "crim - walk unspotted") before doing anything, and the pre-emptive arrest is
  the shipped path — spotlight them (object class 16), dispatch police, `1150 copf - chase criminal`
  rec[6] pushes BHAV 1060 onto them. Making them spawn ambiently would be a divergence.

## 7. Difficulty: the number is right, three of its consumers were missing

`_DAT_004f9740` is the difficulty, and `FUN_004cba70` (VM opcode 74) is what hands it to the
behaviour programs. `FUN_004a6d20` sets it from the career city record's field 0 **and then
increments**, so it is `Difficulty + 1` — which is exactly `RebuildCumulativeWeights`'s
`DifficultyTier = CareerCity.Difficulty + 1`. **The remake's `GetDifficultyTier()` IS
`_DAT_004f9740`**, so every `8 - tier` / `5 - tier` / `tier + 2` term is on the right number.
(One harmless difference: the original also stores the *pre*-increment value into the cumulative
weight table's slot 0, which nothing ever reads as a weight.)

Fifteen functions read it. Twelve were already ported — the scheduler cadence and its per-bucket
type rolls (`FUN_004a6e60`), the tile-search range `(fails + 18) * tier + 8` (`FUN_004abb30`), the
riot head count `rand(8) * (tier - 2) + 16` and the two `rand % tier + 1` party sizes
(`FUN_004a7a10`), the fire terms (`GetFlameBurnTime`, `GetFlameDouseHealth`, the spread interval and
probability, the douse radius and multiplier, the fire-rescue gate), and the CAPBOAT1 speed divisor.

**Three were not, and all three are difficulty-scaled *sources of missions* — which is why hard
cities felt no busier than easy ones:**

| original | rule | where it went |
|---|---|---|
| `FUN_004a92f0` param_1 == 1 | which BUILDINGS a scheduled fire may start in — tier 1 is 1x1 only, and each tier up admits bigger, occupied ones | `IsBuildingFireTargetAllowedByDifficulty`, plus the missing `FUN_004a6860` "nothing already burning here" reject in the same loop |
| `FUN_0049fd00` tail | setting a car alight rolls `1 in (0x40 >> tier)` for a **MedEvac** at the same tile — 1-in-32 easy, 1-in-4 hard | `CreateEventAt`'s `TYPE_CarFireEvent` arm |
| `FUN_004a22e0` | a **special** vehicle striking an **ordinary** car rolls `1 in (0x200 >> tier)` to set that car alight (asymmetric: the ordinary one burns) | `ApplyCollisionCarFireRoll`, on a fresh impact only |
| `FUN_0049be50` (5 sites) | a blocked **emergency** vehicle rolls `1 in (0x40 >> tier)` to raise a TYPE_TrafficJam | `ApplyBlockedVehicleJamRoll` |

One trap on the last one: `FUN_0049be50` has **no callers except the emergency/criminal vehicle
state machines** — ordinary ambient cars never run it, so the jam roll is gated on the dispatch
pools, not on "any blocked car".

Covered by `SimCopter.Missions.BuildingFireDifficulty`.

## 8. The XBLD property table — EXTRACTED, not stood in for

`DAT_00504848`, reached through `FUN_0049a4d0(id) = base + id * 0x14`. **Nothing in the code writes
it**, so it is statically initialised data sitting in the image — no live-process rip needed, which
is what [[simcopter-live-memory-rip]] would otherwise have you reach for.

**How to pull any `.data` global out of the exe** (the method generalises; scripts in
`Docs/scratchpad/agent-sessions/2026-08-05-mission-authenticity/`):

1. Parse `SimCopter.exe`'s PE section headers *from the file itself* — image base `0x00400000`,
   `.data` at VA `0x004f8000`, raw `0x0f5a00`. Do not hardcode a delta; read the headers, so a
   differently-patched copy still maps correctly.
2. `offset = raw + (VA - image_base - section_rva)`. For `0x00504848` that is file `0x102248`.
3. **Verify before trusting.** `verify_data_mapping.py` checks six `.data` strings that
   `ghidra-bridge strings` reports at exact addresses (`"Criminal Miss"` at `0x00506100`,
   `"kissA.WAV"` at `0x0050650c`, ...). All six match, which proves both the arithmetic *and* that
   this exe is the one the Ghidra project was built from.

**The record (0x14 bytes x 256), named by its four consumers:**

| field | meaning | who reads it |
|---|---|---|
| `+0x00` bit 0 (`0x01`) | solid — a person may not stand here | `FUN_004c40a0` (`(*flags & 1) == 0`) |
| `+0x00` bit 1 (`0x02`) | is a building — **57** ids | — |
| `+0x00` bit 2 (`0x04`) | **has people in it — 39 ids** | `FUN_004a92f0` fire placer, `FUN_004a4ac0` trapped-occupant spawn |
| `+0x04`, `+0x08` | X/Y anchor, 16.16 | `FUN_004c2260` |
| `+0x0c`, `+0x10` | X/Y spread, `/ 0x30000` and added to the anchor | `FUN_004c2260` |

Only byte 0 is ported (it is all `GetXbldPropertyFlags` returns and all anything currently needs);
the raw 5120-byte blob is kept at `xbld_property_table.bin` so the emission anchors are recoverable.

The occupied ids: `0x81, 0x90-0x93, 0x9a-0x9d, 0xab-0xad, 0xaf, 0xb1-0xb3, 0xb7-0xbb, 0xc4, 0xc5,
0xc9-0xcb, 0xcf, 0xd1-0xd4, 0xd6, 0xd8, 0xd9, 0xfb-0xff`. **The old stand-in was wrong in both
directions** — it gave occupants to ~105 ids instead of 39, and denied them to `0xd1/0xd2/0xd3`
(hospital, police, fire station), which really do carry the bit; that is why the fire-rescue
placer's hand-written exclusion of those three looked redundant. Note `0xfd` is `0x05`: solid and
occupied with the *building* bit clear — occupancy does not quite imply building, and assuming it
did is what the new test caught.

**A shipped bug this exposes.** `FUN_004a92f0`'s `0x80010` arm reads the tile as a **signed** char:

    cVar2 = *(char *)((&DAT_005910b0)[x] + y);
    pbVar7 = FUN_0049a4d0(cVar2);
    if (pbVar7 != NULL && (*pbVar7 & 4) != 0 && cVar2 != -0x2f && -0x2e && -0x2d) create;

`FUN_0049a4d0` rejects anything `< 0`, and **every occupied id is 0x81 or higher**, so all of them
sign-extend negative and the accessor returns null. The test can never pass: in the shipped game the
Rescue bucket's scheduled fire-rescue rolls always burn their five tries and create nothing. Trapped
occupants appear only through `FUN_004a4ac0`'s emergent path, which reads the same byte *unsigned*.
The remake reads it unsigned in both places and says so at the site — reproducing the bug would
delete a whole scheduled mission type.

Covered by `SimCopter.Missions.XbldPropertyTable` (bit counts 117/57/39, the `0xd1-0xd3` occupancy,
the `0xfd` exception, and "nothing below 0x81 has occupants").

## 9. The boats

`FUN_004afb60` only speeds a hull up **along its own heading** when a helicopter is within 70 units
of the ground on its tile — there is no lateral term, so in the original a hover beside a boat cannot
move it. The remake now adds a push directly away from the aircraft on the same altitude ramp
(`BoatRotorWashPushUnits`), applied as displacement rather than steering. Flagged as a divergence at
the call site.

**The waves are the trap.** `M_SimCopterWater` displaces the sea in the *vertex shader*
([[simcopter-vertex-animation-wpo]]), so nothing on the CPU knows the surface has moved:
`TryGetWaterSurfaceZ` answers with the rest plane and the boats sat in it while the swell heaved
through them. `ASimCity2000CityActor::GetWaterWaveOffsetCm` is now a term-for-term CPU copy of
`WATER_WAVE_PRELUDE` + `WATER_WPO_CODE` in `Tools/Unreal/CreateSimCopterMaterials.py`, including
both early-outs (pinned shoreline weight, Low Power Graphics). Two things have to match or the boat
tracks a different crest than the one it is drawn on: **the shader's Time node is world time**, and
**Weight is the shoreline-pinning vertex colour** — which is why `WaterCornerWeightGrid` is baked
next to the vertex weights rather than approximated.

Boats keep `World` on the rest plane (tiles, targets and the mission marker all read it) and draw at
`WaveWorld`; the hull's pitch/roll comes from a central difference over `BoatWaveProbeSpanCm`, about
a boat length — sampling the analytic slope at a point pitches a whole boat on ripples shorter than
it is. The capsized boat's survivors are **spawn-mode-1 swimmers, so they belong in the water beside
the hull, not on its deck**: each holds the offset it spawned at, rides the boat's drift, and takes
the swell sampled at its own position.
