# SimCopter helicopter collision and impact

*"The original has ONE collision system and it never blocks — it overlaps, then throws the airframe clear. A blocking collider in the remake pins the helicopter."*

*Decoded and ported 2026-08-01. Evidence:
`Docs/scratchpad/agent-sessions/2026-08-01-dash-and-collision/dash-points-bar.md`.*

## The rule

`FUN_00484d20` writes the simulated position into the helicopter's node **unconditionally**, then
looks for contact two ways, and both responses *push the airframe away*:

| Path | Test | Response |
| --- | --- | --- |
| Height (`LAB_00485605`) | `altitude < heli[0x59]` (the surface top at the helicopter's own position) and `heli[0x44] < 1` | damage, kick pitch/slide **away from the motion vector**, `ClimbSpeed = ClimbRate*4`, `heli[0x44] = 0x3333` (0.2 s control cut) |
| Objects (`FUN_0048ad50`) | AABB overlap against every object on the helicopter's **own tile** | `FUN_0049a4f0(0xc, ...)` on the object (BHAV 912 - the airframe running people/cars over), then damage, **random** pitch/slide kicks, `ClimbSpeed = ClimbRate*4` |

Both then play **sound 1 (EXPLODE, 2D)** and throw `FUN_004af100(tile, x,y,z, 0x80000001, -1)` —
an impact column whose size is `4 << (param5 & 0x1f)`, so **scale 1**, not the scale-4 splash a
ditching uses.

**Nothing blocks.** That is why the original cannot wedge you against anything.

## The trap this cost

The remake trades the tile-object AABBs for a swept capsule against real geometry — fair, since
it has real buildings — but it must keep the "never block" half. Until 2026-08-01 it did not:

- the sweep **blocked**, and the blocked position was written **back into the flight model**, so
  the simulation believed it too and kept steering into the obstacle. Dead stick, no escape.
- the response was gated on `FMath::Abs(hit.Normal.Z) < 0.6`, which dropped every roof, slope and
  overhang **and** counted a ceiling (normal.Z ≈ -1, abs = 1) as a floor — so flying up under
  something was the quietest way to get stuck.
- because `bPadBounce` was what raised the EXPLODE sound, "no crash sound" and "gets stuck" were
  the same bug.

Now: the sweep is a **detector only**. A blocking hit raises `NotifyObjectCollision`; the position
is then handed straight back to the flight model. **If you ever add another collider to the pawn,
it must not block either.**

Three follow-on traps, all found on screen after the first fix (2026-08-01):

- **The threshold is the landing test, not a wall test.** Use `LandingFlatNormalZ` (0.99), the
  same number `StepGroundImpact` uses to decide whether terrain is landable. An earlier 0.6 let
  every roof pitch and hillside through: the skids touched a slope at speed, the height test did
  not fire either because the point directly below the origin was still clear, and *nothing
  happened*. There is deliberately **no speed gate** - the original has none, any contact with a
  surface it cannot land on is an impact - and `BounceTimer` (0.2 s) is its own rate limit.
- **Order the audio after `ApplyFlightModelToActor`.** `Step()` clears the whole event block at
  the top of every frame, and the swept collider raises `bPadBounce` from inside
  `ApplyFlightModelToActor`. With `PlayFlightEventAudio` called before it - which is where it sat
  - the impact sound was wiped before anything played it, so EXPLODE never fired no matter how
  well the collision worked. The visual pass ran later and *did* see the event, which is what made
  this look like "the sound is missing" rather than "the event is missing".
- **The impact burst goes at the contact point, un-submerged.** `FUN_004af100` seats its column
  32 units *below* the point it is given so a water splash rises through the surface; on a
  building face that just buries the burst. Spawning at `GetActorLocation()` compounded it -
  centre of the airframe, then two metres down, then the sub-particle ring falling under gravity
  from there. Pass `Hit.ImpactPoint` and `bSubmergeOrigin = false`.

## Fire damage rate

`ReferenceFrameSeconds` is a **feel** knob the debug panel dials in, and the pawn ships it at
**1/60**. Pointing the fire burn at it made a fire cost three times what the executable charges.
Damage is fidelity, not taste: the burn is now pinned to `OriginalFrameSeconds` (20 Hz). Check any
other rule before you reach for `ReferenceFrameSeconds` - the climb decay and the EMA window
belong there, hit points do not.

## Fire damage

`FSimCopterFlightModel::StepTurbulence` has the whole decoded fire arm and always has - it was
dead code, because `BuildFlightEnvironment` never assigned `FireHeightDelta`. The input is
`FUN_004a5c10`: a **box** gate (not a circle) of `DAT_00505f54` = 24.0 units around each flame on
the tile, returning `heliY - flameTop` (1 when exactly 0, because 0 means "no fire"). It is tight
on purpose - inside it the cost is `61 - delta` hit points per frame, which kills a healthy
airframe in seconds. Ported through `ASimCopterMissionSystemActor::GetFireHeightDelta1616`, which
takes the flame's world position from the same helper the fire renderer uses so the damage band
is the fire the player can see.

Related: [[simcopter-heli-flight-model]], [[simcopter-fire-water-fx]], [[simcopter-mission-system]].
