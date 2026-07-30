# SimCopter camera ground lift

*"Raising the boom pivot walks the helicopter DOWN the screen — and the old ramp never actually
reached full strength."*

*Recorded 2026-07-30. Remake behaviour, not a port: the original has no boom camera.*

## What it is

`ASimCopterHelicopterPawn::ResolveCameraGroundLift` raises the chase/orbit/rescue boom **pivot**
(`CameraTranslationWorld.Z`) as the camera nears a surface. The aim direction is unchanged, so
lifting the pivot moves the look-at point up relative to the aircraft, and the aircraft slides
**down** the frame. Parked, it should sit around the middle instead of up near the top where the
authored chase framing puts it in flight.

It returns `max(hard, cosmetic)`: a hard term that keeps the boom out of the scenery, and the
cosmetic framing term. Both read the same downward probe.

## Whatever is under the camera is its ground — including buildings

**A line trace stops at its first blocking hit.** The probe used to skip building hits hoping to
fall through to the terrain beneath them, and that never worked: there is nothing behind a blocking
hit to find, so over an airport apron or beside a building the probe reported **no ground at all**
and the lift never engaged. `LineTraceMulti*` does not help — it returns overlaps plus *one*
blocker, not a column of surfaces.

So an apron, a helipad and a rooftop now all count as ground, which is right: they are surfaces you
park on. The single rejection left is a **non-terrain surface above the camera** — a roof or bridge
deck overhead is not something we are landing on, and treating it as ground heaves the view up over
it. Terrain above the camera is still accepted, because that means the camera has sunk into the
landscape and the hard lift has to push it back out.

## The spring arm's own collision test must stay OFF

`CameraBoom->bDoCollisionTest = false`, deliberately. `UpdateCamera` already does all three jobs
the engine's version would — least-angle avoidance, pull-in along the roof-to-camera segment, and
ground clearance — and the engine's sweep actively **defeated the ground lift**.

`USpringArmComponent::UpdateDesiredArmLocation` sweeps from
`ArmOrigin = GetComponentLocation() + TargetOffset`. This camera keeps `TargetOffset` at **zero**
and puts the entire framing translation in `SocketOffset`, which the engine applies *after* the
arm. So the sweep ran from the fuselage roof straight to the final offset position and clamped the
camera to the first thing it grazed — and near the ground that clamp won every time, which made
the lift look completely dead however its three numbers were tuned. Symptom to recognise: the
lift is computed correctly, the sliders change nothing on screen.

`ProbeChannel` and `ProbeSize` are still read by the remake's own probes, so they stay set.

## Building-avoidance response

The angle search and fallback pull-in are deliberately damped. The tuned defaults use 40 cm of
anticipatory padding, a 50-degree maximum pitch correction, 2.25 avoidance / 1.5 return
interpolation speeds, and 4.0 pull-in / 2.0 release speeds. This keeps the real camera-radius
sweeps intact while preventing a nearby wall that the camera would miss from provoking a sharp
pitch jump. Recorded 2026-07-30 after the original 90 cm, 65-degree, 7.5 / 3.5 / 8.0 / 5.0
response proved too sensitive and abrupt in play.

## The trap

The ramp used to be `alpha = 1 - dist / ProbeRange`, which only reaches full strength at **zero
clearance** — and the camera never gets there, because it stops `CameraGroundClearanceCm` (24 cm)
above the surface and the chase boom parks it about 124 cm up anyway. With the old 260 cm range a
landed helicopter got `alpha ≈ 0.1`, i.e. **25 cm of a 250 cm lift**, and visibly did not move.

Now it plateaus: full lift at or below `CameraGroundLiftFullDistanceCm` (40), easing to zero at
`CameraGroundLiftProbeRangeCm` (430). If the effect ever feels wrong on landing, that **full
distance is the knob** — it has to sit above the camera's actual parked height, which depends on
`CameraAnchor`'s roof height and the arm length.

All three are live on the helicopter debug panel's `GROUND LIFT` row (LIFT / FULL / START) and
persist to `GameUserSettings.ini` under `[SimCopter.CameraGroundLift]`, like the camera offsets.

## Tuned defaults

The 2026-07-30 live tuning was promoted to the code defaults: lift 155 cm, full distance 40 cm,
and start distance 430 cm. The chase framing it was tuned with is translation `(197, 0, -126)`
and -6 degrees of debug pitch; orbit uses `(124, 0, -234)` and 3.5 degrees.

## Zoom reference

`CameraDefaultZoomAlpha` is both the starting zoom **and** the reference `ReferenceZoomArmLength`
scales framing against — they must be the same number, or the default view is already scaled off
its authored framing. All three views use it; do not put the literal back. Lowering it (0.25 → 0.05
on 2026-07-30, chase arm 1090 → 794 cm) therefore also makes zooming *out* scale the framing
translation harder than before, which is the intended "hold screen position through zoom" contract.

The starting zoom is **not** persisted to `GameUserSettings.ini`, unlike the per-view camera
offsets — a new default takes effect immediately without clearing the ini.
