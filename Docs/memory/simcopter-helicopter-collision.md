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
- **A wreck in its death spiral needs its own response.** The `Dying` branch of `Step` integrates
  **no horizontal motion at all** - it only spins and drops - and `NotifyObjectCollision` returns
  early outside `Flying`/`FlyingAI`, so a wreck that came down against a building had nothing to
  carry it clear and hung there spinning. `NotifyWreckCollision` is the movement half of the wall
  response with the attitude half deliberately left out (the spiral owns pitch and bank): it
  shoves the wreck along the surface normal, lifts it over the lip, and raises `bPadBounce` so the
  explosion and EXPLODE come with it. The pawn re-reads the model's position after the shove
  rather than restoring the pre-impact point, or the push lands a frame late and a wedged wreck
  can out-run it.
- **The impact burst goes at the contact point, un-submerged.** `FUN_004af100` seats its column
  32 units *below* the point it is given so a water splash rises through the surface; on a
  building face that just buries the burst. Spawning at `GetActorLocation()` compounded it -
  centre of the airframe, then two metres down, then the sub-particle ring falling under gravity
  from there. Pass `Hit.ImpactPoint` and `bSubmergeOrigin = false`.

## The capsule is a 190 cm sphere, and it is not the airframe (2026-08-05)

`CollisionComponent->InitCapsuleSize(95.0f, 82.0f)` does **not** make a 95x82 capsule.
`UCapsuleComponent` clamps the half height up to the radius, so the pawn's root collider is a
**sphere of radius 95 cm** — around a fuselage that is a fraction of that across. It is sized for
the swept impact detector above, and it must stay that way; what it must not be is the answer to
"where is the helicopter" for anything on foot.

Two things were measuring interaction against it, or against the actor origin, and both read as a
massive invisible hitbox:

- **Boarding.** `HelicopterAutoEnterRadiusCm = 145` fired every tick on origin distance, and
  `Interact()` reached **620 cm** — a tile and a half in a world where a tile is 400 cm and the
  avatar is 46 cm tall. You boarded from beside the aircraft without ever touching it.
- **Blocking pawns.** The sphere blocked `ECC_Pawn`, so the avatar was stopped a metre out from the
  fuselage flank — it could not have reached the airframe even if the test had asked it to. Pawns
  now **overlap**: people are not obstacles to an aircraft in the original either (`FUN_0048ad50`
  answers a person with damage and a bounce, never with a stop), and ground agents never swept
  against it anyway because they all move with `SetActorLocation(..., bSweep=false)`.

The replacement is `TryGetAirframeLocalBoundsCm` / `GetDistanceToAirframeCm` /
`ComputeAirframeGapCm`: the **rendered fuselage's own box**, taken from whichever body component is
visible via `CalcBounds(GetRelativeTransform())` — the same source
`UpdateCameraAnchorFromVisibleBody` already uses, so it works for the GEO mesh and the placeholder
cube alike, and it banks with `ModelPivot`. Rotors are separate components and stay out of it.

Traps in it:

- **Use the 3D form for boarding.** The horizontal-only form clamps the query point into the box's
  vertical span, which is right for a walker standing on the same deck (their vertical gate is
  applied separately) and completely wrong for entering: it would board an aircraft hovering
  overhead the moment the avatar walked underneath it.
- **Rank by the gap, not by origin distance.** `FindHelicopterWithinReach` exists because a long
  fuselage two metres away can otherwise be "nearer" than the one you are standing against.
- **Do not resize the root capsule to fix this.** Its half height is the flight model's altitude
  datum (`Altitude = actorZ - CapsuleHalfHeight`), the body mesh's vertical offset is derived from
  it so the skids sit at the capsule bottom, `PlaceOnHelipad` rests on it, and the sweep's
  sensitivity to buildings is tuned around its radius. A capsule cannot be both a fair airframe
  proxy and small; measure the mesh instead.

Reaches are now gaps from the avatar's own capsule to that box: `HelicopterInteractionReachCm`
60 cm, `HelicopterAutoEnterReachCm` 4 cm. Covered by `SimCopter.Interaction.AirframeGap`.

The same box is what a paramedic walks up to — see [[simcopter-paramedic-handoffs]].

## People are not obstacles and not ground (2026-08-05)

"We are able to land the helicopter on their head." Three separate mechanisms, all fixed together;
the aircraft now passes through people entirely and only *criminals* answer for it.

1. **The swept collider blocked pawns.** Fixed with the `ECC_Pawn -> ECR_Overlap` above. A sweep
   only blocks when both sides block, so this alone settles heli-vs-person for movement.
2. **A pedestrian's head was a landing surface.** Both of the helicopter's downward queries traced
   plain `ECC_Visibility`, and one of them (`BuildFlightEnvironment`'s `heli[0x59]` equivalent)
   starts 200 m *above* the aircraft — so the topmost blocking hit under it was whoever was standing
   there. That is the whole of "it touched down on their head, reported itself landed, and sat
   there". `TraceFlightSurface` now multi-traces and takes the first blocking hit that is not an
   `ECC_Pawn` object; both `UpdateGroundProbe` and the flight environment go through it. This is what
   the original does anyway: `heli[0x59]` is terrain and objects, and people are answered separately.
3. **Nothing ran anyone over.** `FUN_0048ad50` hands each object overlapping the airframe
   `FUN_0049a4f0(0xc, ...)`, whose person arm is reaction table entry 12 -> BHAV 912 "Rxn: Large fast
   vehicle hit" -> 903 "Rxn: Die" -> 309 "Fall off master": death sounds, outcome 10
   (`EVT_PersonDied`), attr15 "written off", despawn. `RunOverCriminalsUnderHelicopter` restores that
   pass **narrowed to the criminal set** — `DAT_0058de80` states **10..13** (BHAV 1300-1303), not
   `FUN_004ca350`'s loop-flag-0 test, which also matches state 3's *rioters* (a riot is dispersed,
   not run over) — and not caught yet. Ordinary pedestrians are untouched; they already scramble out
   from under a descending aircraft (`UpdateDescendingHelicopterAvoidance`).
   **DIVERGENCE:** the squash also posts outcome 9 (`EVT_CriminalCaught`), because the executable has
   no way to end a crime mission with the helicopter and killing the target would otherwise leave the
   job open forever with nobody left to catch. `bRunOverByHelicopter` latches it — 903 takes several
   ticks to die and the overlap stays true throughout. Nothing in the pass can move the aircraft.

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
