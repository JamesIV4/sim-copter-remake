# Cars hit people (a whole-cloth divergence)

*Added 2026-08-11. There is nothing to port here and no `FUN_004xxxxx` to cite — this mechanic does
not exist in the original at all. Recorded so nobody goes looking for the decompile it came from,
and so the rules it does have are written down somewhere.*

## What the original does: nothing

A car and a pedestrian pass straight through one another in retail SimCopter.

- `FUN_0049ee30` / `FUN_0049be50` — the vehicle blocking probe — only ever consider **other
  vehicles** (see [[simcopter-traffic-jam-queue]]: only same-direction traffic even blocks, and a
  blocked car does not move at all).
- `FUN_004c9000` — the person move core's overlap search — only ever considers **other people**
  (move result 5, the bump/chat).
- The one vehicle-vs-person collision that does exist is the **helicopter's**, and it is restricted
  to state 10..13 criminals: `FUN_0049a4f0(0xc)` → BHAV 912 → 903 "Rxn: Die"
  ([[simcopter-helicopter-collision]]).

So a person standing in a lane is invisible to traffic and vice versa. Nothing below reproduces
anything; it is an invented mechanic, asked for deliberately.

## The rule

**The car takes nothing and the pedestrian takes everything.** No brake, no swerve, no speed loss,
no damage, no mission, no penalty — `UpdatePedestrianVehicleImpacts` never touches the vehicle. The
person is launched proportionally to the car's speed and then exaggerated well past what momentum
would give, tumbles as one rigid body, bounces off the ground / buildings / into the sea, lies there
briefly and gets up unhurt.

Everything is in `ASimCopterGroundAgent` (`ESimCopterKnockdownPhase`,
`ApplyVehicleKnockdown`, `UpdateKnockdown`) plus the one scan in
`ASimCopterTrafficSystemActor::UpdatePedestrianVehicleImpacts`, called from `Tick` outside both
traffic AI modes — a moving car hits whoever is in front of it whatever the flow model says.

### Proportional, and only to the car's ground speed

`ComputeVehicleKnockdownLaunchVelocity` is forward + lateral + up, and **every term is the car's 2D
speed times a gain**, so halving the speed halves the whole launch in every axis. The gains
(2.4 / 0.55 / 1.35) are the exaggeration and nothing else is. The car's *vertical* motion — a ramp,
a bridge approach — is deliberately not part of it. Which side they are flung to comes from which
side of the car's centre line they were standing on.

At the shipped cruising speed (`VehicleSpeedCmPerSec` 259, at `PopulationWorldScale`) and the
pedestrian gravity of 980 cm/s², that is roughly 1.5 tiles of flight. `SimCopter.Knockdown.Launch`
pins the range between one and six tiles so a re-tune cannot quietly turn it into a shove or into
orbit.

**Four live knobs, on the debug panel's Calibration tab as the `KNOCKDOWN` and `airframe` rows**, and
on the console as `SimCopter.Knockdown.*`:

| knob | CVar | default | what it does |
|---|---|---|---|
| `POWER x` | `LaunchScale` | **0.7** | scales the whole launch; the arc keeps its shape, only its size changes. **Shared by cars and the airframe.** |
| `UP x` | `UpwardScale` | 1.0 | the CARS' launch angle: the vertical alone. 0 is a flat shove with no air in it. |
| airframe `UP x` | `HelicopterUpwardScale` | 0.4 | the same for the helicopter, separate because it flies at several times a car's road speed and one number cannot suit both. |
| airframe `MIN` | `HelicopterMinSpeed` | 400 cm/s | ground speed below which the airframe knocks nobody down, however close it gets. |

All four are multipliers/gates over the per-agent gains, and all four are **CVars rather than
UPROPERTYs because the population is hundreds of actors** — a per-instance property would only move
whichever agent you happened to have selected. Folded into the gains at the call site, so the pure
launch function stays "gains × the striker's speed".

## The airframe does it too

`KnockDownPedestriansUnderHelicopter`, called from the pawn immediately after
`RunOverCriminalsUnderHelicopter` so the decoded criminal death (BHAV 912, `FUN_0049a4f0(0xc)`) keeps
first claim on anyone it kills — `WasRunOverByHelicopter` is what the knockdown pass checks, because
suspending a body's VM part-way through BHAV 903 would leave it owing a death. Everyone else the
aircraft flies through tumbles exactly as a car's victim does; contact is
`GetDistanceToAirframeCm` in full 3D (unlike a car, the aircraft's height above a walker is the whole
question), never the 190 cm flight-sweep sphere.

**The minimum ground speed is the important one.** It is horizontal-only on purpose, so a vertical
descent onto somebody sets down on them rather than launching them — the people underneath a
descending helicopter are usually the ones it came to collect, and landing, hovering and taxiing
among a crowd all have to stay safe.

### The ragdoll is one rigid rotation, because that is all there is

A privanim figure is line segments driven by whole-pose frames with **no skeleton**
([[simcopter-privanim-decoded]]), so there is nothing to articulate. The "ragdoll" is the whole
`VisualRoot` turning about one horizontal-ish axis at a rate proportional to the launch speed, with
the `"Whoa"` clip playing — which is the recoil clip the move core itself binds when something
shoves a walker (`FUN_004c6970` result 2), so it is the game's own art for this.

**`VisualRoot` pivots about the capsule centre**, so a body turned on its side hangs half a body
height in the air. `ApplyKnockdownVisual` drops it by (half height − radius) as it goes flat. Getting
this wrong is the difference between a body lying on the road and one hovering over it.

### Two collision answers, not one

- **Walls, piers and the sides of buildings**: a sphere sweep on **`ECC_Camera`**, the same channel
  every other pedestrian query uses — the city mesh blocks it and no agent or player capsule does,
  so a tumbling body cannot catch on the crowd it is flying over.
- **The ground**: `TraceGround`, *not* that sweep. The walk probe is what knows where a pedestrian
  may stand (the road under a bridge, a roof, the sea's own plane — see
  [[simcopter-overhead-geometry-collision]]). Its answer is a *standing* height, which is above where
  the sphere would have contacted, so it always resolves first and the two cannot fight.

Water does not bounce anybody: contact ends the tumble. The terrain water section carries collision,
and `TryGetWaterSurfaceZAt` qualifies the tile test with the height, exactly like `IsStandingInWater`.

### Recovery: 1 s sprawled, 3 s in the lying pose, then up

`AdvanceKnockdownRecoveryPhase`. The sprawl holds the tumble where it stopped — eased to the
**nearest** quarter turn that is flat (`ComputeKnockdownRestSpinDegrees`; odd multiples of 90, since
0 and 180 are standing up and standing on their head). The lying pose is the authored **`"Inju"`**
clip — the one a passenger holds after falling out of the helicopter, before they pick themselves up
(`BeginPassengerFall`). Deliberately not `"Dead"`, which is the corpse the medevac casualties hold:
this person is getting back up. Note `"Inju"` is also the deepest-drawn pose in the shipped art, so
it depends on the ground lift below.

**The tumble axis must be horizontal, and this is not cosmetic.** The rest pose is a quarter turn
about that same axis, so any Z in it is *yaw* — which spins the body on the spot instead of laying it
down and leaves the sprawl standing at an angle. It shipped wrong once: the axis jitter drew up to
0.35 of Z against a unit vector, so the sprawl came out visibly tilted and more so the more Z it
happened to draw. `ComputeKnockdownTumbleAxis` drops Z outright and the test asserts it.

**And the authored poses compose with whatever the tumble left behind**, because they are already
drawn lying in the figure's own upright frame — a lying pose turned a further quarter turn is a body
standing on its head. `ResetKnockdownVisualTransform` takes all of it off (VisualRoot rotation and
offset, plus any actor pitch/roll, keeping the facing) before `"Dead"` is bound, and again whenever
the knockdown ends for any reason.

### Nobody is exempt, and everybody continues from where they end up

`CanBeKnockedDownByVehicle` is deliberately **not** a list of people too important to be run over. A
mission's casualty, a paramedic, a criminal, an arsonist, a rooftop survivor — they are all just
people standing in a street and they all get launched.

What makes that safe is the other half: **the tumble suspends a program, it never replaces one.**
`ApplyVehicleKnockdown` captures `bBehaviorActive`, `bMissionStationary` and the forced clip, and
`FinishKnockdown` hands all three straight back at the new position — the same stack, the same
frame, the same pose. So a casualty is still a casualty lying in a different gutter, a medic still
has its patient to reach, an arsonist is still on his clock. Nothing is restarted and
`BehaviorHomeTile` is not re-homed; the only thing that changed about them is which street they are
in. (`bMissionStationary` has to be cleared *for the duration*, or a casualty knocked into the sea
could not wade out of it.)

What `CanBeKnockedDownByVehicle` still refuses is physics, not policy: someone riding a carrier, slung
over a medic's shoulder, halfway up a UFO's beam or already falling out of a helicopter is **not
standing in the street to be hit** — their position is somebody else's to write.

**The boarding handshake is refused by the AIRFRAME ONLY** (`IsBoardingPlayerHelicopter`, read from
`ApplyHelicopterKnockdown`): a fare walking to the door, a rescue victim waving to be collected, a
harness pickup, a hospital roof medic crossing to take a patient, or anyone carrying a casualty to the
cabin. Being launched by the machine you are walking towards is the pickup failing rather than
slapstick — and with the airframe's minimum speed at 0, a hover that drifts a few cm a second is
enough to do it to the fare standing at the door. Detected from the walker's own selection being the
aircraft or its rope end (BHAV 291 / 305 / 263), the mission layer's cabin guidance being live, the
awaiting-rescue wave, a roof post, or a body over their shoulder.

**The cars deliberately do not honour it**: a fare crossing a road to reach you is fair game for the
traffic on it.

The same ownership question can arrive mid-tumble — a winch pickup, a medic, a boarding — so `Tick`
cancels the knockdown **above both carried early-returns** when a carrier appears. Below them, the
state would sit latched until the body was next put down and then resume in mid-air. That cancel
passes `bRestoreAppearance = false`: the new owner has already set the pose.

**Landed in the sea** → skip the lying pose and wade to `TryFindNearestTransportLandTile` as an
ordinary `SetMoveTarget`, so the walk clip, the ground snap and the wall containment all apply
unchanged. This is the one phase where `UpdateKnockdown` returns **false** and hands movement back to
`UpdateMovement`. They already sink to the waist and ride the swell for free
([[simcopter-ambient-vehicles]]'s water submersion).

## Traps worth keeping

- **The person is immune for the whole knockdown**, so a lane full of traffic cannot pinball a body
  down the street. They are eligible again the moment they stand up, which is intended.
- **Measure the car's rendered body, not its capsule.** The traffic capsule is a 33.75 cm separation
  radius on a body nearly three times that long, so a person standing in the bonnet measures a
  positive gap against it — the same trap `GetSelectionContactGapCm` documents for a medic walking up
  to its ambulance. `TryGetBodyLocalBoundsCm` is resolved once per car per frame, not per candidate.
- **`ComputeBodyGapCm` is horizontal only**, so the caller owns the vertical gate. Without it a car
  on a bridge deck mows down the people on the quay below.
- **The voice must be pumped from inside `UpdateKnockdown`.** The ordinary path only calls
  `UpdatePersonVoice` from inside the behaviour VM, which is stopped — and the wade can last 25 s.
  The bank is fourteen slots for the whole city ([[simcopter-sound]]).
- **Pedestrians do not dodge**, and that is not new: `UpdatePedestrianAvoidance` calls
  `SetAvoidancePathOffset`, which `UpdateMovement`'s behaviour-VM branch ignores outright.
- **Who is in the road to be hit is its own rule** — see below. An earlier note here said ambient
  people are admitted to the road by `DAT_0058d750`'s default row; that is the wrong table. The row
  that governs an ambient walker is `DAT_0058ec00`, and it contains no road class at all.
- Agent save blob is **v5**; a save taken mid-tumble used to restore a person with no VM and a forced
  clip, i.e. frozen in the recoil pose forever.

## The lying poses were drawn INTO the ground (and always had been)

Not a knockdown bug — it hit every casualty the game has ever placed, and the knockdown just made it
constant enough to notice. `Calibrate` pins local Z=0 to the feet of the **standing** clip, because
scale has to come from one pose or every clip would be a different size, and **nothing then holds any
other pose above that plane**. `ToLocal` negates the model's screen-space Y-down vertical, so a pose
drawn low on the 1996 screen — which is what a body on the ground is — maps straight down through the
floor. Measured off the shipped art at the 44 cm population body height:

| figure | `Dead` | `Inju` |
|---|---:|---:|
| Child | 15.9 cm | **19.6 cm** |
| 5man | 7.2 cm | 8.0 cm |
| fatman | 7.9 cm | 5.9 cm |
| most adults | 1-4 cm | 1-3 cm |
| Coww | 26.4 cm | 28.4 cm |

An injured child was buried to nearly half their height with only the head showing, which is exactly
the reported screenshot. `ComputeClipGroundLiftCm` raises the built figure by the amount its pose
reaches past the **walk cycle's own lowest point** — not past zero, because `Calibrate` reads frame 0
only and every shipped walk swings 2-3 cm below it mid-stride. That swing is where a pedestrian
visibly meets the pavement today, so measuring from zero instead would have lifted the whole
population off the ground to fix the casualties. Binding a walk or an idle now lifts by exactly
nothing.

The lift lives on `OriginalMeshComponent` *inside* `VisualRoot`, so it is vertical for the unrotated
poses that need it (`Dead`, `Inju`, `Slum`) and would be sideways under the knockdown's tumble
rotation — which is fine, because the tumble binds `Whoa`, a standing clip whose lift is zero.

## Who is in the road in the first place: the jaywalk window

**There IS a rule keeping people out of the road and it is the original's.** `FUN_004c9470` splits on
`person[0x168]`:

- **`!= 0` (ambient)** — the target tile class must appear in this person's `DAT_0058ec00`
  behaviour-class row, plus a per-tile crowd cap. **No row in that table contains class 7**, so a
  shipped ambient walker can never step off the kerb. That is why retail crowds mill about on the
  blocks.
- **`== 0` (everyone else)** — no tile-class test at all, only `if (person[0x166] && current == 7 &&
  target != 7) return 0xc`, i.e. a refusal to *leave* a road once on one.

The remake had this effectively inverted. Its ambient branch is gated on `EBhavAttr::AmbientFlag`
(+0x168) and **nothing in the C++ ever writes that attribute**, so every walker fell into the
non-ambient branch — whose "safety net" is the *union* of the state row (`DAT_0058d750`, which does
contain 7) and the ambient row. Net effect: the whole population was free to wander down the middle
of the road. (The VM's expression engine can write any attribute from data, so a shipped BHAV could
still set +0x168; whether one does was not determined.)

`IsPedestrianRoadStepAllowed` now applies the road half of the rule to the branch that actually runs,
in both directions: a road step is refused whatever the rows said, unless this walker's **jaywalk
window** is open, in which case it is allowed whatever the rows said. Everything that is not a road
is left entirely to the rows.

**The window (DIVERGENCE):** every `RoadJaywalkWindowSeconds` (10 s) each walker rolls
`RoadJaywalkChance` (20%) and may cross for that window — so about a fifth of the street population
is jaywalking at any moment, on individually staggered clocks (the first window is seeded to a random
fraction, or the whole city would re-roll on one frame). Chance 1 is the old free-for-all, 0
reproduces the executable.

**THE TRAP, and it is not hypothetical — it shipped for an afternoon.** A walk step is
`MoveSpeed/12` original units, i.e. **about 8 cm against a 400 cm tile**, so every one of the eight
facings from the middle of a road tile lands on that same road tile. Refuse them all and the walker
does not stay out of the road — they are *already in it*, thrown there by a car — they stand
perfectly still until a window happens to open, tens of seconds later. That is exactly the reported
"they get up and then stand there for a very long time, intermittently". `IsPedestrianRoadStepAllowed`
takes `bAlreadyOnRoad` for this: the rule is about **stepping out** into the road, never about being
in it. The executable never has to answer the second case because no ambient walker of its own is
ever on a road tile.

Three more things to keep in mind:

- **Roads are an ambient walker's only route between blocks.** Their row is otherwise all building
  classes — empty land (class 2) was never in it either — so this is also how often they change block
  at all. If the streets look dead, that is the knob.
- **It applies to ambient population only** (`IsSubjectToRoadRule`), which is the same line the
  executable draws. A paramedic, a fare, a cop mid-chase or anyone BHAV 308 has given up on is sent
  somewhere by their program; a kerb that refused them four times in five would strand the mission.
- **The window is wall-clock**, so it is rolled at the top of `UpdateOriginalBehavior` above the
  behaviour-tick gate — the same trap as the arsonist clock, and below the gate a 10 s window would
  last 40.

Tuning lives on the agent (`SimCopter|Knockdown`) and the traffic actor
(`bVehiclesKnockDownPedestrians`, `VehicleKnockdownMinSpeedCmPerSec` — below it a car is parking or
crawling out of a jam and launches nobody), with the two live multipliers above on the debug panel.
Tests: `SimCopter.Knockdown.*`, five of them.

Related: [[simcopter-traffic-jam-queue]], [[simcopter-people-logic-next]],
[[simcopter-helicopter-collision]], [[simcopter-overhead-geometry-collision]],
[[simcopter-pacing-divergences]].
