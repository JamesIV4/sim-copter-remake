# Overhead geometry is not ground (and not a wall)

*Found and fixed 2026-08-11, from a report that police kept getting stuck mid-chase and that
"there's a box over the whole imported asset".*

## The fault

Two downward probes started **above the world** and took their **first blocking hit**:

| probe | old start | what it feeds |
| --- | --- | --- |
| `ASimCopterGroundAgent::TryGetWalkSurfaceZAt` (pedestrian arm) | agent Z + 120 m | `MoveStep`'s max-climb gate |
| `ASimCopterHelicopterPawn::BuildFlightEnvironment` | aircraft Z + 200 m | `heli[0x59]`, the flight model's surface height |

In the original that is harmless: `FUN_004c82c0` answers with the tallest object **on that cell**,
and a 2.5D city has nothing over a cell that does not belong to it. The remake renders real GEO
meshes, where a **bridge arch, a power span, an elevated rail deck or a first-floor overhang passes
over cells whose ground is the street**. Taking the topmost hit therefore made every mesh behave as
a solid box extruded from its highest point down to the terrain — which is exactly what the report
described.

Consequences:

- **Pedestrians were walled in.** The rise to the span is metres, `MoveStep` refuses it, and
  `AutoTurn` is 1 for every spawned person, so all eight facings fail at once. A cop who ran under
  a bridge or a power crossing stopped dead; BHAV 1150 never closed to its 2-tile arrest test, so
  the pursuit looked like "the police gave up". `TraceGround` (the ground snap) already clamped its
  own start height, so the agent *stood* on the road correctly while being unable to leave it —
  the two functions disagreed, and that asymmetry is the tell.
- **The helicopter was shoved in mid-air.** `SurfaceHeight` became the deck *above* the aircraft,
  so the flight model read ground contact every frame while flying under a span.

## The fix

1. **Probe from the querying body, never from above it.** The pedestrian probe now starts at
   `GetPedestrianWalkProbeCeilingZ(feetZ, maxClimbCm, margin)` — feet plus everything they could
   step onto — so its first hit is by construction the highest reachable surface, and a kerb still
   registers while a one-storey roof does not. The flight probe starts at the top of the collision
   capsule. When it misses (an airframe sunk into geometry), the city terrain grid is still the
   fallback and the movement sweep pushes it out sideways.

1b. **The helicopter needed a second fix: a collider the size of the aircraft.** The root collider
   shipped as `InitCapsuleSize(95, 82)`, and that clamps the half height **up** to the radius — so
   it was a 190 cm sphere, reaching 95 cm above the actor origin when the fuselage stops 15 cm
   below it. 110 cm of phantom above the aircraft is what caught bridge soffits and power spans.
   `SyncCollisionCapsuleToAirframe` now fits the capsule to the rendered fuselage
   (`ComputeAirframeCollisionFit`: half height = the body's Z extent, radius = its beam, clamped so
   it can never re-inflate the height) and moves `ModelPivot` so the **fuselage floor lands exactly
   on the capsule floor**. That last part is mandatory: the capsule bottom is the altitude datum
   (`Altitude = (originZ - halfHeight) / unit`), and the ground probe, the clearance readout, the
   camera anchor and the boarding box are all expressed against it or against `ModelPivot`, so
   pinning both to the skids means **nothing moves on screen and no landing rule changes**. Re-run
   on every model switch, from `ShowOriginalMesh`.

   It is **event-driven** — BeginPlay and `ShowOriginalMesh` only, on the order of once per
   helicopter model, never on the tick — and it cannot be replaced by parenting components in the
   editor, for two reasons worth knowing before someone tries: the fuselage is a
   `UProceduralMeshComponent` built at runtime from GEO data with different bounds per helicopter
   type, so the editor never sees an asset to parent to; and the collider has to BE the root,
   because `MoveComponent` sweeps the root's shape and nothing else and the actor transform is what
   the flight model drives. Both writes come out of one computation and are applied together,
   nothing is cached, and a non-shipping `ensureMsgf` re-reads the applied half height (past the
   clamp) and asserts the capsule floor is still the fuselage floor.

   **A failed attempt worth not repeating:** keeping the oversized sphere and re-sweeping the
   fuselage box as a "narrow phase" to decide whether its hit counted. It suppressed *every* impact
   in the game — nothing took damage or was jolted by anything — and it did not fix flying under
   meshes either, because the sphere was still what `MoveComponent` stopped against. The collider
   is the collision test; if it is the wrong shape, fix the shape, do not add arithmetic on top.

2. **Ask the mesh about walls, because the column no longer can.** Fixing (1) alone would walk
   people *through* building walls: the highest surface below your feet inside a wall is the floor
   the building stands on. `IsPedestrianStepBlockedByGeometry` sweeps a capsule from the walker to
   the step target on `ECC_Camera` (blocked by city geometry, ignored by agent/player capsules).
   The swept body starts one climb allowance above the higher of the two surfaces, so kerbs and the
   step itself pass under it, and it is `PedestrianStepSweepRadiusScale` (0.8) of the capsule radius
   so real gaps stay passable. `bStartPenetrating` is deliberately **not** blocking — an agent
   already clipped into geometry has no free facing and would be stranded forever.

This is a deliberate divergence: the original decides obstruction from per-cell object heights, and
the remake decides it from the actual triangles. The city already cooks complex collision once per
model (`SimCopterRuntimeStaticMesh::Build` sets `CTF_UseComplexAsSimple`) and these are 1996 GEO
models, so a sweep per attempted facing is cheap. `bUseGeometryStepSweep` turns it off for
diagnosing a movement regression.

## Traps

- **`MoveThroughWalls` (person+0x190) skips the sweep**, as BHAV 308's escape must.
- The sweep must run **after** the climb gate, not instead of it: the gate is what keeps people off
  roofs, and it is measured against the surface the new probe returns.
- Do not "simplify" the walk probe back to a high start to make someone stand on a roof. An agent
  who is legitimately up there probes from their own feet and finds the roof; that already works.
- `SimCopter.Formats.SimCity2000.ReferenceCity` fails on this machine for unrelated, data-dependent
  reasons (it hunts for specific raw ALTM samples in whichever `city0.sc2` is installed). Verified
  failing on a clean tree before and after this change.

- **Resizing the helicopter capsule is fine — moving its bottom is not.** The capsule bottom is the
  altitude datum, so any change to the half height must be paired with the `ModelPivot` offset that
  keeps the fuselage floor on it. `ComputeAirframeCollisionFit` returns both together for exactly
  that reason; do not set one without the other.
- `InitCapsuleSize`/`SetCapsuleSize` **clamp the half height up to the radius**. That is how
  `(95, 82)` became a sphere, and it will silently undo any fit whose radius exceeds its height.
  **The ground agents have the same trap**: `ApplyAgentShape` gives a vehicle
  `(135, 82) * PopulationWorldScale` = radius 33.75, half height clamped from 20.5 back up to
  33.75 — a 67.5 cm sphere for a car about 26 cm tall. It has no movement symptom, because vehicle
  motion is graph-driven and never sweeps world geometry, and the contact tests already measure the
  rendered body instead (`GetDistanceToBodyCm`). Latent, not active — but it is the same mistake.
- **`PopulationWorldScale` (0.25) applies to bodies but not to the decoded step allowance.** A
  person is 44 cm tall in this world while `MaxStepClimbOriginalUnits` is still 5 units = 31 cm, so
  a naive "sweep the body above the climb band" leaves a 13 cm sliver and walkers pass through
  nearly everything. `ComputePedestrianStepSweepShape` caps the band at 35% of body height.
- **Do not inset the sweep radius for "passability" at this scale.** `PedestrianStepSweepRadiusScale`
  was 0.8 for that reason and it was wrong: the capsule is 8 cm in radius, so 0.8 sweeps 6.4 cm,
  while the privanim figure calibrated to a 44 cm body is wider than that with its limbs mid-stride
  - the visible person clipped walls the sweep called clear. It is 0.95 now, and the inset exists
  only so a sweep ending exactly at contact does not read as start-penetrating on the next frame
  (penetrating is containment's escape hatch, so that would switch it off).

## People settling inside walls

Two independent causes, both fixed 2026-08-11:

1. The radius inset above.
2. **`MoveStep` is only one of the things that moves a person.** Crowd separation, traffic
   impulses, avoidance offsets, mission guidance and the alighting placer all write the transform
   directly and none of them consult geometry, so a walker standing against a building gets nudged
   half inside it - and nothing ever pushed them back out, because the step sweep's
   `bStartPenetrating` escape then allows every subsequent step.

`ContainOutsideBuildingGeometry` closes that by sweeping the same body along the **net** horizontal
displacement since the last frame the walker was clear, so every mover is caught in one place
instead of each having to remember. It runs beside `ContainToHospitalRoofPost` - after all the
movers, before the ground snap - owns the deck only (the snap owns Z), and zeroes the horizontal
velocity that was pushing them in so they do not grind along the wall. A displacement larger than
`WallContainmentMaxStepCm` is a teleport (boarding, alighting, placement, a restored save) and
re-anchors instead of sweeping; `ClassifyWallContainmentStep` is the tested rule.

Covered by `SimCopter.Collision.WalkProbeIgnoresOverhead`, `SimCopter.Collision.StepSweepShape` and
`SimCopter.Collision.AirframeColliderFit`.

Related: [[simcopter-helicopter-collision]], [[simcopter-people-logic-next]],
[[simcopter-crime-rooftop-rescue]], [[simcopter-instanced-buildings]].
