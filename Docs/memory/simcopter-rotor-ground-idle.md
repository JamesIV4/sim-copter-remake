# The rotor ground idle, and why touchdown stopped meaning shutdown

*Decided 2026-08-25. A deliberate divergence from `FUN_00487740` and `FUN_00487160`, recorded here
so nobody "fixes" it back by citing the decompile.*

## The mismatch

Retail winds a parked rotor all the way to a dead stop — `FUN_00487740`'s parked branch is
`RotorSpeed -= 50/s`, floored at zero — and the remake reproduced that faithfully. The **engine loop
does not go quiet with it**, and that is not a port bug either:
`USimCopterAudioSubsystem::SetFrequencyHz` clamps the pitch multiplier at `GMinPitchMultiplier`
(0.4), so `FUN_00488fd0`'s pitch law bottoms out around 250 rpm and holds one **steady slow swish**
all the way down to the loop's own cut-off at 30. So the sound said *idling* while the blades said
*off*, and holding the collective down briefly and letting go gave you a five-second `CHOPSTOP`
over a rotor that had already stopped.

## What changed

**1. A parked helicopter keeps idling until it is shut down.**
`UpdateEngineState` used to clear `bEngineRunning` on touchdown outright. It no longer does — which
also means `EngineShutdownHoldSeconds` is **reachable again**; that branch had been dead code
because the touchdown arm always won.

**The shutdown hold is the ONLY thing that stops it.** Not touchdown, and — the mistake made and
corrected on the way in — **not stepping out either**: a pilot who lands, gets out and walks away
leaves the rotor turning at idle, and it is still turning when they come back. Clearing
`bEngineRunning` in `ExitHelicopter` made the blades stop dead on the way out, which is precisely
what this feature exists to prevent. A dry tank or a wreck still stops it where it stands, or the
idle floor would be the one thing turning the blades on a helicopter with nothing to burn.

Holding the collective down *through* the landing therefore never settles on idle: the hold elapses
0.8 s after touchdown with the rotor still around 320, the target becomes zero, and it carries
straight on down past the idle speed to a stop without pausing on it.

**An idling helicopter stays audible after the pilot gets out**, as the airframe's own emitter.
`UpdateHelicopterAudio` is the port and stops at the cockpit door — every 2D helicopter sound is
gated on `heli[8] & 1`, the aircraft the player is flying — so this lives in a separate remake-only
`UpdateUnattendedEngineLoopAudio`, called from `Tick` after it, guarded by a `bWasPlayerFlown` latch
because an unpossessed pawn has no controller left to ask. Three things make it work:

- **`Play3D` re-aims an already-playing slot instead of restarting it** (`FUN_0042a1f0` calls
  `SetPosition` after Play unconditionally), so the 2D cockpit voice simply *becomes* the positional
  one on the first tick after the player steps out. No gap, no retrigger.
- **No `SetVolumeAdjust` on that path.** It writes `VolumeIndex` outright and would wipe the
  distance attenuation `SetPosition` just computed — which is the whole point. Pitch alone carries
  the rotor anyway; the volume law only spans about −80 across the entire range.
- **`EnterHelicopter` must put it back to 2D with the `Restart` flag.** `Play2D` leaves an
  already-playing slot completely alone, positional flag included, unless told to restart — so
  without it the cockpit would go on hearing its own engine spatialised and attenuated from the boom
  camera.

Leaving the loop simply running instead is not an option: a 2D voice nobody updates follows the
player across the city at full volume for ever, and with the idle holding the rotor above the loop's
cut-off it never self-corrects.

**It is heard at a fraction of the normal range** (`UnattendedIdleAudioRangeDivisor`, currently 4 —
a quarter). A parked helicopter ticking over is not a siren, and on the shared 1920-unit law it
carried most of the way across a city block. The scaling is done **on this one emitter** — feed
`DistanceVolumeIndex` a multiplied distance, which reproduces its exact curve over the shorter
distance, and cull at the scaled radius in place of `FUN_0042a1f0`'s own 1920-unit reject. Never by
retuning `AudibleRangeUnits` or
`SpatialAttenuation`: every 3D sound in the game is on those. The volume write has to come **after**
`Play3D`, which sets the slot's index off the full-range law via `SetPosition`. The mission layer's
sirens drive their own volume through the same law (`DriveSiren`), so the shape is not new.

**2. `FSimCopterFlightModel::RotorIdleSpeed` = 75.0 (`0x4b0000`).** Inside the band it picks is a
look-and-feel choice — 75 draws at roughly two turns a second
(`RotorSpeed * 32 * RotorVisualMultiplier` tenth-degrees per second) — but the band itself is fixed
by three existing numbers, all of which have to hold or the picture and the sound come apart again:

| bound | value | why |
|---|---|---|
| COPLOOP cut-off | 30 (`0x1e0000`) | above it, with margin, so the loop cannot flicker off at the boundary |
| lift gate | 300 (`RotorLiftGate`) | far below, so an idling helicopter can never lift off on its own |
| pitch clamp | ~250 | under it, so the note is the steady clamped idle, not a pitch that drifts as the rotor settles |

Retuning it for feel is expected; keep assertions about it loose, which is why
`SimCopter.Flight.GroundIdle` compares against multiples of the constant rather than against
absolute rotor speeds.

The parked branch now **converges on a target** at the original's own 50/s rather than subtracting
and clamping. With the engine off the target is zero and it is arithmetically the retail line. With
the engine on and the rotor somehow *below* idle — a save restored on a parked, running helicopter —
it comes back up gently instead of snapping, which is the only reason the code is shaped this way.

The flag rides in on `FSimCopterFlightInputs::bEngineRunning`, **not** a model field: inputs are
rebuilt every step and never serialised, so no save-format change, and it defaults false so every
hand-built input in the automation tests keeps retail behaviour unless it opts in. `BuildFlightInputs`
sets it *after* the dead-controls early-out, which is exactly the engine-off case.

**3. The cue routing is UNCHANGED, deliberately — do not "fix" it.**
`CHOPSTAR`, `CHOPSTOP` and `SOFTBMP2` keep `FUN_00487160`'s exact routing: `CHOPSTOP` still fires on
touchdown and on the release-the-collective-while-parked edge, both of which now happen over a rotor
that carries on turning at idle rather than one that is stopping. That was tried the other way —
moving `CHOPSTOP` onto the `bEngineRunning` true→false edge — and **reverted on 2026-08-25 at the
project owner's instruction: the audio is to be left exactly as it was.** The ground idle is a
rotor-and-visuals change only. The one exception is the line below, which is a defect guard rather
than a routing change.

## Knock-on worth knowing

An idling helicopter has **live controls** — `BuildFlightInputs` returns dead controls only when the
engine is off — so you can lift straight back off after landing without the 0.85 s restart hold.
That is intended. Idling burns **no** fuel, because `StepFuelAndDamage` only burns while Flying;
retail-faithful, and left alone rather than inventing a ground burn rate.

Covered by `SimCopter.Flight.GroundIdle`, which asserts the settle, that the blades are still moving
at idle, both bounds in the table above, and that shutdown still stops the rotor dead.

Related: [[simcopter-heli-flight-model]], [[simcopter-sound]],
[[simcopter-collective-is-the-engine]].
