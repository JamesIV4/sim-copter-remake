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
