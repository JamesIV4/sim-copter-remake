# SimCopter paramedic handoffs

*Ambulance victim collection and hospital medevac unloading, decoded and ported 2026-07-29*

## Ground truth

The ambulance vtable begins at `0x004f4d20`; its on-scene update slot is
`FUN_004b8f60`. That function calls `FUN_004bd980(0x0c, 5, ...)`, which
ultimately deploys a behavior-class-12, person-state-5 `Medik`. Do not reuse
`FUN_004b8b60`'s `(0x0f, 0x0d)` pair: that function and pair belong to the
stopped criminal car.

The person spawn stores the deploying vehicle through
`FUN_004c4e10(param_7)` at `person+0x170`. People opcode 62 selects that exact
starting object, and opcode 61 messages it when the crew member is finished.

## Street ambulance path

The shipped `people.df` is the interaction specification:

- state 5 starts BHAV 801;
- outside XBLD D1, BHAV 801 calls BHAV 262;
- BHAV 262 searches eight tiles for object class 5 / person state 6, approaches
  the victim, and opcode 44 makes the medic tote that same person;
- BHAV 272 selects original object class 10;
- BHAV 275 reaches the selected vehicle, opcode 51 sets the carried patient
  down, and opcode 39 pushes BHAV 285 onto that patient;
- BHAV 285 posts mission outcomes 0 then 1, then disappears;
- BHAV 269 selects `person+0x170`, boards the starting ambulance, and opcode 61
  tells it to return.

`FUN_004cac70` maps behavior object classes 10, 11, and 12 to
`FUN_0049b060` kinds 0, 1, and 2. Those pools are ambulance
`DAT_00582b20`, police `DAT_00582b50`, and fire `DAT_00582b38`. Reversing
classes 10 and 12 makes the medic seek a fire truck and never complete the
ambulance interaction.

## Hospital path

At XBLD D1 on a serviceable tile, BHAV 801 calls BHAV 263. It:

1. uses opcode 84 to select a real state-6 patient aboard the player helicopter;
2. walks to that selection and opcode 47 alights the patient through the
   carrier;
3. opcode 44 totes the same actor;
4. runs laterally and sets the patient down;
5. pushes BHAV 802 for the patient's slump.

The patient's ordinary BHAV 282 recognizes XBLD 209 plus the serviceable-tile
test, posts medevac-delivered outcome 1, and leaves the map. There is no popup
building, temporary doorway, replacement patient, or paramedic-owned delivery
counter in this graph.

## Port boundary

Keep `BoardCarrier`, `AlightFromCarrier`, and `MissionPassengerSlots` as the
single actor/seat ownership layer. The behavior VM decides when to perform the
interaction; the interaction methods validate the concrete action and route
its outcomes through the idempotent mission service.

For an ambulance delivery, only accept BHAV 285's outcome pair after opcode 51
has set a state-6 patient down at the exact class-10 vehicle selected by the
state-5 medic. For hospital delivery, let BHAV 263 remove the real cabin actor;
a bounded recovery timer may resolve malformed legacy seats, but must not
animate a competing handoff or spawn visual geometry.

The retail game can choose a medevac coordinate inside a building. The remake's
intentional safety guard must validate the whole pedestrian capsule against the
rendered building, not just reject the SC2 origin tile: faithful instanced GEO
models can overhang their nominal footprint, and complex runtime collision may
not answer the old top-down trace. `IsMissionGroundSpawnValid` therefore uses
both building collision overlap and `IsInsideStandingBuildingBounds` before a
patient is spawned; if no nearby point passes, the spawn is refused.

## The roof post is rate-limited (2026-07-30)

`EnsureHospitalParamedicAtTile` is called by the mission tick **every frame** a medevac is
pending, and its `FindPostedMedic` scan rejects any agent with a behaviour carrier or with
`Visible == 0`. A medic boarding the player's helicopter acquires both, so it stops matching on the
very next tick and the post looks empty — which used to spawn a **second paramedic on the roof the
instant the first one climbed aboard**.

Fixed with a per-tile respawn delay: `HospitalParamedicLastSeenSeconds` records when a medic was
last actually seen standing on that roof, and no replacement is posted until
`HospitalParamedicRespawnDelaySeconds` (default **40 s**) after that. A roof that has never been
staffed has no entry and staffs immediately; a failed spawn records nothing, so the retry-every-tick
behaviour for a not-yet-ready city surface is unchanged.

The timing gate is `ASimCopterTrafficSystemActor::CanPostHospitalParamedic`, a pure static so it
can be tested without a world — `SimCopter.Dispatch.HospitalParamedicRespawn`. It also treats
*backwards* world time as "allow", so a level reload that keeps the map cannot lock a roof out for
the rest of the session.

## The roof was unreachable, and the medic on it froze (2026-07-31)

Two separate faults, both reported as "paramedics don't work on hospital roofs".

**1. Most hospitals had no spawn site at all.** `TrySpawnOriginalPersonAtTile` resolves a tile through
`PedestrianNodeIndexByTile`, and the pedestrian-node builder still owned footprints the retired way -
`XZON & 0x80` gates the owner, then scan right/down for the same XBLD. That is the exact rule
[[simcopter-building-footprints]] retired for mesh placement, and it fails the same way here: those
0x80 cells sit at the FAR corner of multi-tile squares, so the scan runs off the building. **In 22 of
the 30 career cities not one hospital had an owner**, Islandtown included (both of its 3x3 hospitals,
at `(91,62)` and `(16,76)`), so `EnsureHospitalParamedicAtTile` could never place anybody and retried
forever. It now uses `FSimCopterCityGeometryRules::ClaimOriginalBuildingFootprint` over a
`SceneCellState` carried across the whole row-major sweep, exactly as `RebuildCity` does - claim on
**every** tile, not just the ones that get a node, or the suppression state grows holes. This also
restores ambient people, cop roof crews (0xD2), the ball park and the plaza on those cities.

**2. A refused despawn parks the walker on the stop opcode.** `FUN_004ce7b0` returns on handler
result 3 *without moving the record cursor*, so a frame that reaches opcode 16/37/40 stays sitting on
it. `ASimCopterGroundAgent::UpdateOriginalBehavior` cancelled `bRequestDespawn` for persistent
hospital crew and unresolved mission people but left the stack alone, so every following tick
re-executed the same stop opcode: alive, visible, inert. That is the frozen paramedic.

It is not a rare path. BHAV 801's loop calls **272 'nearest emerg veh on stack'**, which runs
**265 'Medevac disappear'** (op 51 then op 40) whenever neither the player's helicopter nor an
ambulance is within ten tiles - true within a couple of seconds of the post being staffed, since the
medevac that needs it starts elsewhere. **263 -> 269** ends at op 40 the same way when no patient is
aboard. Relanding sometimes cleared it because an interaction pushes a reaction frame, and unwinding
that frame finally consumes the dead record's edge.

The fix is `BehaviorContext.ResetToState(GetStateIndex())` alongside the refusal: restart the state
program the way `FUN_004c7090` does for a fresh spawn. The post then loops 801 - Walk-10, Idle-10,
probe - so it visibly patrols its roof and picks the player up on the next pass.
`ResetBehaviorProgramOverride` alone was never enough; it early-outs when
`InitialBehaviorProgramId == INDEX_NONE`, which is every hospital medic.

Covered by `SimCopter.City.IslandHospitalFootprints` and
`SimCopter.Behavior.VM.HospitalMedicRetire`. Evidence:
`Docs/scratchpad/agent-sessions/2026-07-31-hospital-paramedics/`.

## Taking the patient is the delivery, and the post is pinned to its roof (2026-07-31)

Restarting the program (above) let the roof medic walk again, and it walked off the building. Two
consequences, both reported from play.

**The delivery must not depend on where the handoff happens.** BHAV 263 rec[3] (op 47) is where a
worker takes the casualty out of the player's cabin, but the shipped graph leaves the *credit* to the
patient's own BHAV 282, which posts it only on XBLD 209. A medic standing anywhere else therefore
completed the whole interaction - took the patient, revived her - and never credited the mission,
with the seat now empty so nothing could hand her over again. **Soft-locked.**
`DropSelectedPerson` now calls `NotifyMissionPersonDelivered` whenever a **state-5** worker takes a
mission passenger off the player's helicopter, wherever the two are standing. That service is
idempotent and resolves the passenger kind from the record itself, so the ordinary hospital route
still plays out in full and BHAV 282's later request is simply refused as already reported, while a
record that carries no such passenger returns 0 and nothing is invented.

**The drop gate had a hole the original does not have.** `FUN_004c9470` reads

```c
if (maxClimb < rise)                { if (person+0x190 == 0) result = 1; else keep the OLD Z; }
else if (rise < -0x8000 - maxClimb) { result = 2; }
```

Only the **climb** arm has BHAV 308's "move through walls" escape (+0x190, decimal 400 in the
decompile), and even that does not lift the walker onto what they walked into - it restores their
previous Z. The **drop** arm is unconditional: nothing in the original has ever let a person step
down off a ledge. The remake wrapped *both* arms in the flag, so a medic crossing the roof to the
helicopter with +0x190 left set - by BHAV 269 rec[10], or by 308 after four failed moves - walked
straight off the edge. Same pass: a walk-surface probe that fails now blocks the step instead of
allowing it, because `FUN_004c82c0` always answers (max of object tops and terrain) and "no surface"
is a remake-only state.

Still divergent and deliberate: the original also lets +0x190 push through **people and objects**
(both collision arms only return 4/5 when it is clear); the remake's bump check is not gated by it.

**Containment beats chasing movers.** `MoveStep` gates climbs and drops, but `MoveByTrafficSeparation`
and `AddTrafficVelocityImpulse` displace agents with no walked-surface check at all, and once the
body is past the edge `UpdateGroundSnap`'s gravity does the rest. So the fix is on the transform, not
on any one mover: `SetHospitalRoofPost` gives the posted worker its building's square and roof Z, and
`ContainToHospitalRoofPost` (called each tick **before** the ground snap - after it is too late) keeps
them over it. `MoveStep` also refuses a step target outside the post with result 3, so they turn at
the edge rather than leaning on the clamp.

Two traps in that containment:

- **Recovery has to restore Z, not just XY.** The pedestrian ground probe starts at the walker's own
  feet, so a medic put back over the building from street level would find the ground *inside* it and
  stand in the lobby. The posted roof Z is restored whenever the feet are more than
  `HospitalRoofPostFallToleranceCm` (100 cm, well under the ~150 cm shortest storey) below it.
- **The roof probe is cached per tile** (`HospitalRoofPostByTile`) and the post is dropped in
  `BoardCarrier`. Re-running a downward trace while the player is parked on the helipad would answer
  the *helicopter's hull* and walk the post up onto the aircraft; and a medic flown away and set down
  elsewhere must not be teleported back, which is also what the "abandoned past twice the half
  extent" arm of `ClampToHospitalRoofPost` covers.

The clamp geometry is the pure static `ASimCopterGroundAgent::ClampToHospitalRoofPost`, tested by
`SimCopter.Dispatch.HospitalRoofPost`.

## Letting go of a person is a drop, not a placement (2026-07-31)

`UpdateGroundSnap` already gives an airborne pedestrian gravity - it only places one that is at or
below the surface. What removed every fall was `SnapToGroundImmediate` being called the instant a
carry ended: `AlightFromCarrier`, `SetDroppedInjuredOnGround`, and the mission recovery drop all
teleported the person onto the ground, so a patient lifted out of the cabin appeared standing on the
deck with no drop at all. Those three now leave `bSnapToGround` on and let gravity run. The spawn-time
snaps in the traffic actor stay - those exist so an agent's first frame is grounded rather than
hovering, which is not a drop.

Carried-person offsets are tunables now, and much tighter: `CarriedPersonRelativeOffsetCm` on the
agent (person carrying person, was 40 cm out in front) and `CarriedMissionPersonOffsetCm` on the
on-foot pawn (the player carrying a casualty, was 48 cm). Both hold the body against the chest
instead of floating it along ahead of the carrier.

## "Arrived at the object" is CONTACT, not the same tile (2026-08-05)

Reported as "paramedics don't walk up to the helicopter before they pull the patient out", and that
is exactly what it was. BHAV 263 does send them: rec[2] selects object class **2** (the player's
helicopter) within ten tiles, rec[17]/rec[7] set the move speed and 50 tries, rec[5] **op 38 walks
to it**, and only then does rec[31] op 84 select the casualty aboard and rec[3] op 47 take her out.
The walk was completing instantly.

`ASimCopterGroundAgent::StepTowardSelectedObject` was reporting arrival when the walker and the
target **shared a tile**. A tile is 400 cm, so a medic entering the far corner of the helicopter's
tile — up to ~283 cm away on the diagonal — was declared to have arrived and reached into the cabin
from across the helipad.

The chain says otherwise. `FUN_004ca940` arrives on move **result 10**; `FUN_004c9470` returns 10
only when `FUN_004c9000`, asked at the **step target**, answers with the frame's own selected
object; and `FUN_004c9000` decides that with `FUN_004c8f70`, a box overlap of the two bodies' own
extents (the walker's `person+0x1c4` radius against the object's `+0x10`). It is a **touch test**.
Two details in it matter:

1. **It returns before the position is written**, so the walker stops *against* what it walked up
   to rather than standing inside it.
2. **The selected object is tested first** — ahead of the tile-class rules, the climb gate and the
   bump. Walking into what you were sent to is arrival, never the result-5 bump, which is what lets
   a medic stand at a casualty instead of circling one forever refusing to share its space. The
   remake's port had the same threshold for both (`FindBumpedPedestrian` uses 2x the body radius,
   and two pedestrians touch at the sum of theirs), so without this ordering the tightened test
   would have deadlocked every approach to a person.

`MoveStep` now raises `bBehaviorStepTouchedSelection` in that position and returns without
displacing, and `IsTouchingSelection` / `GetSelectionContactGapCm` / `ComputeContactGapCm` are the
port of `FUN_004c8f70`. The vertical half is unchanged: the original's 5-unit (`0x50000`)
feet-to-doorsill gate still applies separately.

Three deliberate divergences, all documented at the call site:

- **The helicopter is measured against its airframe mesh**, not `GetSimpleCollisionRadius()`. That
  would answer 95 cm — the flight-sweep sphere, see [[simcopter-helicopter-collision]] — and put
  contact a metre out from the fuselage, which is the bug in a different costume.
- **Only a walk that is seeking the selection stops on it** (`bBehaviorStepSeekingSelection`). The
  original applies the rule to every move because each walk frame owns its own selection slot; the
  remake keeps one slot per person (ops 67/68 are no-ops for that reason), so a stale selection
  could otherwise stop an unrelated walk dead.
- **The hospital roof post still wins.** Containment is checked before contact, so an aircraft
  parked off the building cannot pull a posted medic over the edge.
- **Selections with no body keep the old whole-tile acceptance**: the rope end is a point in the
  air and the spotlight's ground spot has no object at all — the original never walks to one
  (`FUN_004ca940` refuses a frame whose selection slot is not an object), so neither gets an
  invented radius.

Covered by `SimCopter.Behavior.VM.SelectionContact` and `SimCopter.Interaction.AirframeGap`.

## The snatch was never in the behaviour graph — it was the watchdog (2026-08-05)

**Read this before touching the medevac unload.** Tightening BHAV 263's walk (above) did not stop
"the paramedic took my casualty from several tiles away". It made it *worse*, and the reason is the
whole lesson: **the visible handoff and the code that actually empties the cabin were two different
things.**

`ASimCopterMissionSystemActor::AdvanceMedevacHandoff` ran a `MedevacBehaviorRecoverySeconds` (45 s)
no-progress timer and then called `DeliverMedevacDirectly`, which **teleports the passenger out of
the cabin, credits the delivery and destroys the actor**. Nothing about it involved the medic. So:

- a medic that could not reach the aircraft — parked off its roof, or now held to real contact —
  simply meant the timer ran out and the patient left on its own;
- making the walk stricter made the timer fire *more often*, which is exactly the report.

That path is now **abstract seats only**: `DeliverMedevacDirectly` refuses outright when
`FindPersonAboardForEvent` finds a real casualty, and the timer does not accumulate while one is
aboard. Its remaining purpose — a legacy save or a test seat with no person behind it, which nothing
can ever animate — is intact. A real patient waits for a real handoff.

Two gates now decide the visible one, both on `ASimCopterGroundAgent`:

- **`IsAtHelicopterForHandoff`**, checked inside `DropSelectedPerson` (op 47) *and*
  `PutSelectedPersonOnMe` (op 44). Horizontally it is airframe contact plus
  `HelicopterHandoffReachCm` (25 cm); vertically it is a **window** (`150 cm`, 24 original units)
  feet-to-doorsill, not contact, because unloading a helicopter still hovering just off the pad is
  wanted — what is not wanted is doing it from the far side of the roof. Riding the aircraft always
  counts (BHAV 263 rec[1] -> 29 -> 31 -> 3 is the in-flight unload and is meant to work).
  **Op 47 now propagates its result** where `FUN_004cc8d0` always returned 1: rec[3]'s false edge is
  -3, so a refusal unwinds to 801's idle and the medic probes again. A retry, not a dead end.
- **`IsHelicopterWithinRoofPostAggro`**, checked in `SelectObjectOfClass`'s PlayerHelicopter arm. A
  posted roof crew does not *notice* the aircraft until it is over their own building (the post
  square plus `HospitalRoofPostAggroMarginCm`). BHAV 263 rec[2] probes ten tiles, which in the
  remake meant the crew set off after a helicopter three or four tiles away and then walked into the
  containment clamp at the edge of their roof, because that is as far as they may go. Altitude is
  deliberately not tested — they should be walking out while you descend.

`MedevacHospitalHandoffRadiusCm` also came down from **1500 to 900** (nearly four tiles to a bit
over two, which covers a 3x3 hospital from its recorded tile whether that tile is the centre or a
corner).

Covered by `SimCopter.Dispatch.MedevacHandoffGates`.

## The street ambulance loop: getting IN is not being CARRIED (2026-08-05)

Reported as "the paramedic is stuck walking into the side of the ambulance and never gets in with
the patient, and this never takes them to the hospital either". It had already got in. Three
separate faults, all on the last leg of the shipped graph rather than on the walk.

**1. Boarding a vehicle went down the toted-body path.** `BoardCarrier` keyed its presentation off
"is the carrier a ground agent", which is true of an ambulance as well as of a person. So BHAV 269
rec[6] (op 12, *walk to selection AND board it*) laid the medic across the ambulance's flank at
`CarriedPersonRelativeOffsetCm`, rotated 88 degrees, in the **`Dead` clip**, and left it there in
plain sight. That is the whole of the visual report. `BoardCarrier` now takes `bAsCarriedBody`;
only op 44's `PutSelectedPersonOnMe` passes true, and a crew member that gets into a vehicle is
hidden inside it - which is all the original ever had to do, because its op 40 simply stopped the
person existing.

**2. The despawn refusal caught the crew.** This is the trap the 2026-07-31 fix above set without
meaning to. `bUnresolvedMissionPerson` is `MissionEventId != NONE && !bMissionResolutionReported &&
record still active` - and a **state-5 worker never sets `bMissionResolutionReported`**, because
that flag belongs to passengers (`NotifyMissionPersonDelivered` writes it). An ambulance medic
carries the medevac's event id purely so its own opcode 13 can post against it. So the medic
reached BHAV 269 rec[9]'s op 40, had its despawn refused, was `ResetToState(5)`ed back into BHAV
801 - and, already attached to the ambulance with `bBehaviorMoveSuspended` set by `BoardCarrier`,
could neither walk nor ever finish again. **That is the loop.** The guard now excludes
`IsEmergencyCrewMember()` (states 5/7/8/0xe). The persistent hospital roof crew is a separate arm
and still gets the refusal, which is what it is for.

**3. A car is a box, not a circle.** `GetSelectionContactGapCm` measured a vehicle with
`GetSimpleCollisionRadius()` - the **33.75 cm** capsule sized for traffic separation - while the
body it stands for is around 90 cm long. Approaching the nose or tail, a walker can be standing in
the bodywork with the circle still reporting a positive gap. `FUN_004c8f70` overlaps the object's
own extent and that extent is a box, so vehicles now get the same treatment the helicopter's
airframe already had (`GetDistanceToBodyCm` / `ComputeBodyGapCm`, measured across the deck with the
caller keeping the vertical gate). Same bug as the 95 cm flight-sweep sphere, one costume along.

Also added, mirroring the police arm directly above it: an ambulance whose deployed medic has
boarded or gone is recalled instead of sitting out the rest of the 180 s stay. Keyed on a medic the
slot **can still see** - a null weak pointer cannot tell "climbed in" from "was never recorded", so
that case keeps the timer.

Covered by `SimCopter.Dispatch.EmergencyCrewStates` and the vehicle half of
`SimCopter.Behavior.VM.SelectionContact`.

## A medic in your cabin halves the patient's deterioration (BHAV 281)

Worth knowing before anyone "adds" this: it is shipped, and it is ported. **BHAV 281 'Medevac
adjust health'** picks the denominator of the drain roll from where the patient is and who is with
them:

| situation | roll | 
|---|---|
| aboard the player's helicopter **and opcode 73 finds a hidden state-5 medic** | 1 in **20** |
| aboard the player's helicopter with no medic, **or** on the ground on a serviceable tile | 1 in **10** |
| on the ground on a non-serviceable tile | 1 in **3** |

When it hits, `medhealth -= 1 + difficultyTier` (rec[6] then rec[8]/rec[7]). BHAV 280 rec[11] kills
the patient once `medhealth` falls below 1.

Opcode 73 is `FUN_004cb9c0 -> FUN_004ca4f0(state 5, hidden)`, ported as
`HasHiddenBehaviorPersonInState`. "Hidden" is the **`Visible` attribute**, which `BoardCarrier`
zeroes for anyone riding anything - so a paramedic who has climbed into your cabin satisfies it.
Note the search is **whole-map** in the original too: a medic sitting inside an ambulance on the
far side of the city also counts. Faithful, and deliberately not narrowed.

## Passengers boarded instantly because guidance outran the BHAV (2026-08-05)

Reported as "passengers get in before I can even set down; the original was janky and did not
always do it right away". Correct on every count. The shipped approach, from `people.df`:

**BHAV 750 'Transport initbhav'** opens `l0 := 40` / `op0 wait l0--` — **40 ticks = 2.67 s** doing
nothing (the VM runs at 15 Hz, `BehaviorTickRate`, which does match the original). Then **BHAV 291**:

- `movespeed := 16` → 16/12 units per tick × 6.25 cm × 15 Hz = **125 cm/s**, in eight octant
  directions, re-faced each tick, with autoturn retries;
- `l0 := rand(40) + 30` — a step budget **re-rolled every loop**;
- rec[2] commits to boarding only with the helicopter within **ONE TILE**;
- otherwise rec[3] walks toward the *player* within four tiles, waves (`WvNo`), `Idle-5`, and
  accrues boredom (BHAV 290). Those two ranges are different on purpose.

The remake's `ProcessPassengerTransfers` ran `GuideMissionPeopleToLocation` **every mission tick**
at `PassengerPickupRadiusCm` = **780 cm**. And in `UpdateMovement`:

```cpp
if (bBehaviorActive && Pedestrian && !IsAvoidanceMoveActive() && !bUsingGuidanceTargetAtStart)
{ ...the BHAV per-tick walk...; return; }
```

**A live guidance target skips the VM's movement branch entirely**, so the agent fell through to the
generic seek mover at `PedestrianSpeedCmPerSec` = **230 cm/s**. Net: trigger range 1.95x, approach
speed **1.84x**, a straight-line beeline instead of the octant shuffle, and the wave/idle/boredom
arms never ran at all because guidance never let the program get that far.

Fixed both ways, at the user's direction:

- **Guidance is now a backstop.** `PassengerBoardStallSeconds` per event only accumulates while a
  helicopter with a free seat is within reach of an uncollected passenger, and guidance engages
  only past `PassengerBoardRecoverySeconds` (**20 s**) — comfortably longer than 2.67 s of waiting
  plus a tile of walking. Same demotion the medevac watchdog got; the reset on a successful pickup
  stops a queue inheriting the countdown.
- **Retuned when it does fire.** `PassengerPickupRadiusCm` 780 → **400** (BHAV 291's own one-tile
  probe), and `SetGuidanceMoveTarget` takes a speed so guidance runs at
  `ShippedPassengerWalkSpeedCmPerSec` (**125 cm/s**) rather than the generic pedestrian speed.

`BoardMissionPeopleTouching` (130 cm) deliberately still runs every tick — that is the arrival
action, not the approach, and op 12 boards at about the same place anyway.

Separately and by request, `CanTransferMissionPassengers` was relaxed the same day from "Parked" to
a 60 cm ground-clearance band, which is a different axis but also part of "before I set down".

Analysis and dumps: `Docs/scratchpad/agent-sessions/2026-08-05-ambulance-and-dash/`.

## Getting OUT is six units, and it is never tested every frame (2026-08-06)

Reported as "people get out of the helicopter too quickly, giving no time to land first". Both
halves of the guess in that report were right.

**The height.** Opcodes 17 and 21 both end in `FUN_004c9bc0`, whose last line is
`(person.Y - FUN_004c82c0(person.pos)) >> 16 < 6` — **six original units, 37.5 cm**. For a rider
that is the *aircraft's* height, because `FUN_004c6450` copies the carrier's `+0x18/+0x1c/+0x20`
onto the person every tick. And `GroundClearanceCm` is exactly that quantity with no conversion:
`ApplyFlightModelToActor` writes `ActorZ = Altitude * 6.25 + CapsuleHalfHeight` and
`UpdateGroundProbe` takes the same half height back off, so the sphere bottom sits at the flight
model's `Altitude`. A **landed** helicopter is `TerrainHeight + 0x13333` — 1.2 units, 7.5 cm — so the
shipped allowance over a parked one is 4.8 units.

The remake was on `GroundContactTolerance (28) + PassengerTransferClearanceCm (60)` = **88 cm**,
nearly a metre of hover, and the same constant gated boarding. **The original's two gates are
different numbers and the alight is the tighter one**: opcode 12's `FUN_004ca940` boards while
`(objectY - personY) & 0xffff0000 < 0x50000`, five units above a walker who is themselves three units
(`+0x30000`, `FUN_004cb190`) off the ground, so **eight units / 50 cm** of aircraft-above-ground. You
may drop to a low hover and have a fare climb in; you may not have them step out from there. Now
`PassengerAlightClearanceCm` (37.5) and `PassengerBoardClearanceCm` (50), and
`CanBoardMissionPassengers()` alongside `CanTransferMissionPassengers()`.

**The cadence, which is the other half.** BHAV 292 'Transport wait to get off' is
`local0 := 10` / `op0 wait local0--` / BHAV 264 (whose own 'idle a bit' is three more ticks) before
each probe, and a failed probe goes back to the ten — so the shipped game asks about every
**thirteenth tick, 0.87 s**. BHAV 700 does the same for rescues, looping 305 -> 303 -> 267 'random
motion'. The VM path inherits that for free; **the mission tick had none**, so
`ProcessPassengerTransfers` / `ProcessRescueTransfers` emptied the cabin on the first frame the gate
opened, mid-descent. `ASimCopterHelicopterPawn::SecondsWithinAlightClearance` accumulates while the
aircraft is inside the band and resets when it rises out, and
`ASimCopterMissionSystemActor::IsHelicopterSettledForAlight` holds the mission-side release until it
reaches `PassengerAlightSettleSeconds` (13/15 s). Same demotion the boarding guidance and the medevac
watchdog got: the shipped program acts first, the backstop waits its turn.

Covered by `SimCopter.Flight.PassengerTransferClearance`. Decompiles and BHAV dumps:
`Docs/scratchpad/agent-sessions/2026-08-06-alight-and-roof-post/`.

## BHAV 801 walks ONCE; refusing the despawn replayed it (2026-08-06)

Reported as "when paramedics are not picking up a patient they go to the edge of the roof every
single time; they should be near the middle and walk around". The spawn and the post centre were
never the problem — both are the footprint's scene centre (`Node.Location`, `OriginX + (Size-1)/2`).
**The march is.**

Read BHAV 801's edges rather than its record list:

    [0] bind 'NoMo' ->5  [5] attr32 := 916 ->2  [2] attr14 := 0 ->4
    [4] autoturn := 0 ->6
    [6] CALL 1015 'Walk-10' ->7          <-- ENTRY PATH ONLY
    [7] Idle-10 ->11  [11] 'idle a bit' ->10  [10] CALL 272 ->1  [1] ... ->11

The steady-state loop is 11 -> 10 -> 1 -> 11 and **`Walk-10` is not in it**. It is ten ticks at
`movespeed := 10` — `10/12 * 6.25 cm` a tick, about **52 cm, once, with autoturn cleared**. In the
original that is the worker's whole life: 272 reaches 265 'Medevac disappear' within seconds and it
is gone. The remake keeps the post staffed by refusing that despawn and `ResetToState`-ing (the
2026-07-31 fix above), which **re-enters at rec[0] and re-runs the entry walk** — roughly once a
second, always along the same facing, because nothing re-rolls it and autoturn is off. Eleven
restarts is 5.7 m. That is a straight line to the containment limit, and then standing on it.

Two changes, both on the repetition rather than on the walk:

- **The restart re-rolls the facing** (`UpdateOriginalBehavior`, the `bHospitalParamedic` arm), so a
  replayed one-shot reads as a step somewhere rather than as a heading.
- **An aimless walk is held to `HospitalRoofPostIdleWanderFraction` (0.45) of the post**, so the
  drift stays around the middle; a walk that is seeking a selection — BHAV 263 rec[5] heading for
  the aircraft — still gets the whole roof. `IsWithinRoofPostSquare` is the pure static form, and it
  always allows a step that *shortens* the distance to the post centre, or a worker left out at the
  parapet by a handoff, a traffic shove or a reload would refuse every direction and be pinned there.

Covered by the step-target half of `SimCopter.Dispatch.HospitalRoofPost`.

## "Wave" is panic; "WvNo" is the greeting

Both clips ship in **every** figure, and the remake was binding the wrong one for a waiting victim -
the "weird jig". The shipped bind sites settle it (`find_wave_binds.py` lists all 88):

- **`Wave`** — 287 'Rioter flee tree', 289 'Rioter run', 902 'Rxn: Ouch', 1062 'Riot Follower',
  805 'Fireman', 260, 666, 888, 1498.
- **`WvNo`** — **291 rec[4] (the transport passenger waving at the player)**, 1020 the mechanic,
  1051/1053/1055 cops at the station, 1201 rooftop worker, 1202, 1203 park, 1206 baseball fielder.

The comment that justified `Wave` claimed op 22 binds it "when a person notices the player". Op 22
binds nothing — it reads the player's speed and facing into two locals. Both remake sites
(`UpdateOriginalBehavior`'s idle substitution and `UpdateJankyAnimation`'s off-program victims) now
bind `WvNo`.

## The marching band is BHAV 444, not a script (2026-08-05)

The level-complete band was invented rather than ported. Everything it does is in the shipped data:
**person state 17 -> BHAV 443 'Tuba leader initbhav'** (which just calls 444) and **state 18 ->
BHAV 444 'Tuba initbhav (SID 246)'**, figure **TubaExpert** (behaviour class 18).

    rec[2]  bind 'Play'                    the instrument animation - NOT a wave
    rec[24] movespeed := 8                 62.5 cm/s at 15 Hz: a march
    rec[3]  select class 9 within 4 tiles  the player, cockpit included
    rec[4]  op 38 walk, autoturn           via MoveStep, so tile/climb rules apply
    rec[25]/[27] attr29 == 444 ?
        member -> 1014 'Random Turn' + 294 'Move rand speed rand time idle rand',
                  then a tick-gated 1-in-3 **sound 37** - one of NINE instrument
                  samples (trbna/trbnc_/trbng/trptb/trpte/trptf_/tubab/tubae/tubaf_)
        leader -> **sound 38 = march.wav**, then keep following

**`attr29` (person+0x17a) is the state's own program id**, and BHAV 444 rec[25]/rec[27] is its only
shipped reader: it is how a member tells itself from the leader. `ResetToState` now writes it.

Four faults, one cause - the old code drove them with `SetMissionScriptedMover` + `SetMoveTarget`:

- **through walls**: the scripted mover is the generic seek path, which neither sweeps nor consults
  tile class or the climb gate. `MoveStep` is what enforces those, and `SetMissionScriptedMover`
  sets `bBehaviorActive = false`, so none of it ran;
- **no music**: the mission actor played march.wav itself from one looping voice slot. The VM never
  played anything, and sound 37 - the individual instruments - was never played at all;
- **the wave**: `SetMissionAwaitingRescue(true)` instead of rec[2]'s `'Play'`;
- **no figure**: `ConfigureAgent(..., TEXT(""), FString(), ...)` passed an **empty original-game
  root**, so neither people.df nor the TubaExpert figure could load.

They are now spawned through `TrySpawnMissionPerson` as ordinary VM people and nothing steers them.

**Trap worth knowing: the airport is tile class 1 and nobody may walk on it.**
`GetTileClassForBuildingId` falls through to its catch-all `return 1` for both stamped ids - 0xde
(pad) is past the `0xD2..0xDC` range and 0xf6 (terminal) is past `0xE8..0xF5` - and class 1 appears
in no row of either `GetAllowedTileClasses` or `GetAmbientStateTileClasses`. A band spawned on the
apron would refuse every direction and spin. The old scripted mover never hit this because it
bypassed the rules that produce it.

The fix is an **exemption, not a relocation**: `ASimCopterGroundAgent::SetIgnoresTileClassRules(true)`
on each band member, and `MoveStep` skips the tile-class gate for such a walker. The band spawns on
the pads, where a mission-success band belongs. A first pass instead searched rings outward for
tiles a class-18 walker would accept and spawned there — the user's call was that there is no reason
to keep them off the apron at all, and this is the smaller change: it waives only "may a person of
this class stand on this kind of tile". **The climb gate and the walk-surface probe still apply, so
an exempt walker still cannot walk into the terminal.** BHAV 444's four-tile probe does the rest.

## Passenger gravity starts before a landing surface is known (2026-08-08)

`DropPassengerAtSlot` already releases the real cabin occupant at
`GetPassengerAirDropWorldLocation` and calls `BeginPassengerFall`; the regression was later in
`UpdateGroundSnap`. That function returned immediately whenever `TraceGround` could not yet find a
surface. A passenger dropped above `GroundProbeDistanceCm` therefore hung at cabin height instead
of descending until the ground entered probe range.

The same order corrupted the impact calculation: `PassengerFallStartZ` was not latched until after
the first successful trace. If gravity ran before that trace, a tall fall would be measured only
from the already-lowered position. `UpdateGroundSnap` now latches the release Z before tracing and
integrates pedestrian gravity even on a trace miss. Once a surface is found, the existing landing
clamp and `FinishPassengerFall(PassengerFallStartZ - GroundedLocation.Z)` decide the injury from the
full fall. `SimCopter.Passengers.FallGravity` covers cumulative acceleration while no surface is in
probe range.

## Evidence and verification

- Fresh executable output:
  `Docs/scratchpad/agent-sessions/2026-07-29-paramedic-evidence/`
- Complete decoded BHAVs:
  `Docs/scratchpad/agent-sessions/2026-07-27-ambient-vehicles/transport_medevac.txt`
- Repeat a focused dump:
  `Tools/re-agent/.venv/Scripts/python.exe Tools/people_bhav_dump.py
  Reference/SimCopterOriginalGame/people.df 801 262 272 275 285 269 263 282`
- Automation contracts:
  `SimCopter.Dispatch.TileRules` and `SimCopter.Behavior.VM.Reference`

Verification on 2026-07-29:

- `RebuildUnrealCpp.bat` — `Result: Succeeded`;
- `Automation RunTests SimCopter.Dispatch` — 6/6 passed;
- `Automation RunTests SimCopter.Behavior.VM` — 4/4 passed, including the
  shipped-`people.df` reference graph;
- `Automation RunTests SimCopter.Missions` — 15/15 passed.

Not verified in-game; project policy reserves foreground interactive runs for
cases the build, decoded data, and automation cannot settle.
