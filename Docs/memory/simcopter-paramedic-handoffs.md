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
