# The collective IS the engine control, and the takeoff prompt

*Decided 2026-08-25. Both halves are remake-only — there is no `FUN_004xxxxx` behind either — and
both exist because "how do I get this thing off the ground" was the one question the port could not
answer on screen.*

## 1. The engine start/shutdown bindings are retired

`DefaultInput.ini` used to carry `SimCopterEngineStart` (SpaceBar) and `SimCopterEngineShutdown`
(LeftControl, RightControl) **on the very keys the `SimCopterCollective` axis already used**. Four
rows in the Settings > Controls list for two keys, and — the part that actually bites — a rebind
could split one control in half: move "Engine Start" to a different key and holding it spools
nothing, because `BuildFlightInputs` reads the *collective*, not the action.

`ASimCopterHelicopterPawn::MoveCollective` now derives both from the one axis, through the pure
`ResolveCollectiveEngineHolds`:

- collective **above** `KeyAxisThreshold` (0.25) → `bEngineStartHeld`
- collective **below** `-KeyAxisThreshold` → `bEngineShutdownHeld`

That threshold is deliberately the same one `BuildFlightInputs` uses to turn an axis back into the
original's digital keys, hoisted to a file-scope constant so the two cannot drift. If the starter
engaged below it, the engine could be running on a stick reading the flight model still calls
neutral.

This is what the **gamepad has always done** — `UpdateControllerInput` takes
`bControllerEngineStartHeld` / `bControllerEngineShutdownHeld` straight off
`Routing.CollectiveCommand`'s sign. The keyboard is now the same shape. `UpdateEngineState`,
`ResolveEngineHoldAction` and both hold timers are untouched.

Traps and knock-ons:

- **The defaults are Space up and LeftControl down, one key per direction.** The retired shutdown
  action had a second key (RightControl) that was never a collective mapping; it is deliberately not
  carried over, because a second key for one direction is the duplication this change exists to
  remove.
- **A returning player's saved `Input.ini` still carries the retired rows.**
  `SSimCopterControlSettings::ReadBindings` filters them (`IsRetiredMapping`), and because
  `WriteBindings` clears and refills wholesale, pressing OK on the Controls page purges them from
  the saved ini for good.
- The collective's two rows are relabelled **"Collective Up / Start Engine"** and **"Collective Down
  / Stop Engine"** — `MakeDisplayLabel`'s only special case. "Collective (+)" told the player
  neither what the key does nor that it must be held, which was the whole point of the change.
- `bEngineShutdownHeld` also forces `Environment.bTerrainFlat` in `BuildFlightEnvironment`. That
  behaviour is unchanged; it is just reached from the axis now.

## 2. The takeoff prompt

Two things about taking off are not discoverable from the screen: which key lifts the aircraft, and
that it has to be **held** — the rotor climbs at 100/s to the gate at 300, so a tap produces three
seconds of visible nothing (see [[simcopter-heli-flight-model]]).

A player who gets in and is still on the ground after `TakeoffPromptDelaySeconds` (5 s) gets one
centred line above the instrument panel:

> **Hold the {collective up} to take off, or press {Interact} to get out**

Both key names are read from the **live** `UInputSettings`, not `DefaultInput.ini`, so a rebind on
the Controls page is reflected; each falls back to its shipped key (Space, F) if unbound, and pad
bindings are skipped because the controller has its own overlay and its own routing.

The **way out is named for the same reason as the way up**: sitting on the ground is also what
somebody who boarded the wrong machine, or who meant to walk, is doing, and `SimCopterInteract` is
the only way back onto your feet with nothing on screen saying so. It is safe to offer
unconditionally *here* because the prompt is only ever up while landed, which is exactly when
`CanExitHelicopter` is satisfied.

The rule is the pure `ShouldShowTakeoffPrompt(bLanded, bTakenOffSinceBoarding, SecondsOnGround,
Delay)`, covered by `SimCopter.Flight.TakeoffPrompt`. **One prompt per boarding**: the first airborne
frame latches it off, so a helicopter that lands and sits on a pad is parked on purpose, not nagged.

Three things that are easy to get wrong here:

- **`PossessedBy` must not seed the latch from `bIsLanded`.** That flag is written by
  `SimulateFlightStep`, so on the possession frame it can still hold whatever the previous tick left
  — and a stale `false` latches "already taken off" on a parked helicopter, killing the prompt
  forever. `bTakeoffPromptNeedsSeeding` defers the seed to the first update, which runs *after* the
  substeps in `Tick` and therefore sees this frame's answer. (Boarding mid-air is real: a save
  restored in flight possesses the pawn through `EnterHelicopter`, and that player needs no prompt.)
- **Its clearance is measured off the dashboard**, via `SSimCopterDashboard::GetPanelScreenHeight`,
  not hard-coded — the panel is 125 page pixels times the cockpit scale, and HUD Scale moves it.
  `RebuildCockpitOverlays` tears the prompt down for that reason and deliberately does not rebuild
  it; the next update does, at the new scale.
- It is **destroyed rather than collapsed** for the replay panel's Hide HUD, unlike the dashboard and
  map, which keep live state worth preserving ([[simcopter-replay-clips]]). One line of text has
  none.

Related: [[simcopter-settings-menu]], [[simcopter-heli-flight-model]], [[simcopter-replay-clips]].
