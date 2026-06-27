# Gameplay Code Walkthrough

This document explains the gameplay-facing C++ modules that consume original SimCopter data. It complements:

- `Docs/OriginalGameFileCodeWalkthrough.md` for file parsers.
- `Docs/CityRenderingCodeWalkthrough.md` for city generation and rendering.
- `Docs/ReverseEngineering.md` for discoveries from the original executable.

The code in this file is a mix of three things:

- Implemented original data consumption, such as `heli.twk`, `GEO` helicopter/car meshes, and `PEOPLE1.BMP`.
- Behavior guided by decompilation, such as road tile ranges, terrain height rules, and spawn/placement ideas.
- Remake scaffolding, such as the current traffic graph walker, procedural people bodies, and modern camera/interaction logic.

Where behavior is not known original behavior, this document says so explicitly.

## Helicopter Pawn

Files:

- `Public/Flight/SimCopterHelicopterPawn.h`
- `Private/Flight/SimCopterHelicopterPawn.cpp`

`ASimCopterHelicopterPawn` is the current flyable helicopter actor. It uses original tuning and original Maxis meshes where available, but its movement integration, camera, and input scheme are modern remake code.

### Public Types

`ESimCopterCameraMode` defines the three camera modes:

- `Chase`: normal follow camera.
- `Orbit`: manually orbitable camera.
- `Rescue`: down-looking rescue/bucket camera.

`FSimCopterHelicopterTypeTuning` stores per-helicopter values parsed from the matching section in `tweak/heli.twk`. Some values are direct raw values, and some are converted into Unreal-friendly units.

`FSimCopterLandingTuning` stores `Heli Landing` limits from `heli.twk`.

`FSimCopterRopeTuning` stores `Heli Ropestuff` values for bucket fill/dump, rope load, and water throw.

`FSimCopterDamageTuning` stores `Heli Damage` values for fire altitude, depreciation, collision damage, repair, and fuel distance.

### Constructor

The constructor builds the component tree:

- A capsule root provides swept pawn collision.
- `ModelPivot` parents the visual model so pitch/roll can tilt the model without tilting the collision capsule.
- Placeholder static meshes provide a body, main rotor, tail rotor, rope, and bucket.
- Procedural mesh components hold original Maxis fuselage, main rotor, and tail rotor objects.
- A spring arm and camera provide the current modern camera.
- A spotlight represents the searchlight.
- Lit vertex-color and rotor-disc materials are loaded for original mesh rendering.
- Default original asset root is `../Reference/SimCopterOriginalGame`.

### Lifecycle and Input

`BeginPlay` optionally loads original tuning and original helicopter meshes, resets fuel and state, and initializes collision probes.

`Tick` substeps flight simulation up to a fixed maximum step, then updates visuals and camera. This keeps the arcade-style movement more stable across frame rates.

`SetupPlayerInputComponent` binds flight, collective, camera, rope, bucket, engine start/shutdown, interaction, searchlight, camera cycle, camera drag, zoom, and reset controls.

### Original Tuning

`LoadTuningFromOriginalGameRoot` reads `tweak/heli.twk` through `FSimCopterTweakReader`.

The function:

1. Resolves the original game root.
2. Loads `tweak/heli.twk`.
3. Finds the section named by `HelicopterTypeName`.
4. Reads helicopter controls by label prefix, not by trusting `NumCtrl`.
5. Converts angle controls with `TweakAngleScale` because shipped labels say `10 = 1 deg`.
6. Converts speed/climb/altitude controls into centimeters.
7. Reads landing, rope, and damage subsections when present.
8. Applies derived tuning clamps.
9. Resets current fuel to the loaded fuel capacity.

This is original-data-driven, but the conversion factors are remake interpretation rather than directly decompiled physics.

`ApplyDerivedTuning` ensures speed/response values stay usable after parsing. It also derives current max forward and slide speeds from pitch/slide limits.

### Original Mesh Loading

`GetHelicopterMeshNames` maps flyable `heli.twk` type names to `GEO` table names:

- `Jet Ranger`: `JETRANG` plus `JETRROTR`
- `Hughes 500`: `HUGH500` plus `H500ROTR`
- `Bell 212`: `BELL212` plus `BELLROTR`
- `Schwiezer 300`: `SCWZR300` plus `SCWZROTR`
- `Apache`: `APACHE` plus `APACROTR`
- `Agusta`: `AGUSTA` plus `AGUSROTR`
- `Dauphin`: `DAUPHIN` plus `DAUPROTR`
- `MDEXPLORER`: `MDEXPLRR` plus `MDEXROTR`
- `MD520`: `MD520` plus `MD52ROTR`

`LoadHelicopterMeshFromOriginalGameRoot` loads the three `GEO` packs through `FMaxisMeshLibrary`, resolves body and rotor table names, and builds them into procedural mesh sections.

Important details:

- Body meshes are palette-colored and use `FMaxisProceduralMeshBuilder::BuildPaletteColoredSection`.
- Rotor meshes use `BuildPaletteColoredSections` so face type `11` becomes a separate translucent rotor-disc section.
- The body is vertically offset so its lowest vertex rests near the capsule bottom.
- Main rotor meshes are authored around the mast and spin around local Z.
- The shared `ROTORTL` tail rotor is placed near the rear of the body bounds and spins around the lateral axis.
- Placeholder geometry remains visible if any required original load fails.

`ShowOriginalMesh` toggles between original procedural mesh and placeholder static mesh visibility.

### Runtime State and Interaction

`ResetAircraft` clears velocity, attitude, yaw rate, engine holds, damage, fuel, bucket water, and landed state.

`GetFuelFraction` and `GetDamageFraction` normalize current fuel/damage for UI or debug use.

`CanBeEnteredBy` checks whether an on-foot pawn is close enough to enter.

`EnterHelicopter` possesses the helicopter with the provided player controller.

`CanExitHelicopter` requires the helicopter to be landed and the engine to be off.

`ExitHelicopter` spawns or uses the configured on-foot pawn near `ExitOffset`, possesses it, and leaves the helicopter in-world.

### Input Handlers

The `Move*`, `Look*`, `MouseLook*`, `AdjustRope`, `Start/StopBucket*`, and `Start/StopEngine*` functions clamp or latch input state. They do not move the aircraft directly.

`ToggleRope`, `CycleCameraMode`, `ToggleSearchLight`, and `Interact` are command handlers. `Interact` currently exits only when `CanExitHelicopter` succeeds.

### Flight Simulation

`UpdateEngineState` handles hold-to-start and hold-to-shutdown behavior. Starting requires fuel and not-max damage. Shutdown requires the engine to be running and the helicopter to be landed.

`SimulateFlightStep` is the main movement step:

1. Update engine state.
2. Disable controls if engine/fuel/damage make the helicopter unflyable.
3. Interpolate pitch, roll, and yaw rate toward input targets.
4. Build forward/right vectors from yaw only.
5. Compute desired horizontal velocity from pitch and roll input.
6. Reduce lift/velocity when the rope bucket is carrying water.
7. Compute target vertical velocity from collective.
8. Apply landing damping when grounded.
9. Move with swept collision.
10. Update ground/forward probes, landing, fuel, rope, and bucket.

`UpdateGroundProbe` line-traces below the capsule and records ground clearance.

`UpdateForwardProbe` sphere-sweeps ahead for collision awareness.

`UpdateLandingState` checks surface normal, attitude, horizontal speed, vertical speed, and descent speed against loaded landing tuning. Safe landed contact snaps Z to the hit surface and damps pitch/roll.

`MoveWithCollision` performs swept movement and tries a damped slide along the hit plane when blocked.

`HandleBlockingHit` computes damage from velocity into the surface and projects velocity along the collision plane.

`UpdateFuel` burns fuel only while the engine runs, with input load increasing consumption.

`UpdateRopeAndBucket` changes rope length, fills when the bucket is in water, dumps on command, and updates rope/bucket visuals.

`ProbeBucketWater` checks named water actors/components or falls back to `WaterFillWorldZ`.

`UpdateVisuals` applies body pitch/roll and spins placeholder/original rotor components.

`UpdateCamera` implements the modern camera modes and mouse-drag/gamepad look offsets.

## Ground Agent

Files:

- `Public/Ground/SimCopterGroundAgent.h`
- `Private/Ground/SimCopterGroundAgent.cpp`

`ASimCopterGroundAgent` represents a vehicle or pedestrian spawned by the traffic system.

### Scale and Shape

The local `PopulationWorldScale` constant is `0.25`, matching the city/original mesh scale. Vehicle and pedestrian capsule sizes, proxy meshes, bob amounts, and body heights are scaled so people/cars do not appear four times too large next to the city.

`ApplyAgentShape` changes capsule size, proxy mesh placement, proxy scale, and movement speed constraints based on `AgentKind`.

### Construction and Lifecycle

The constructor creates:

- A pawn-like capsule root that ignores `ECC_Camera` so ground snap traces do not hit other agents.
- A `VisualRoot` for lean/bob animation.
- A procedural original mesh component.
- A cube proxy fallback.
- Two non-shadow-casting vehicle headlights.
- Vertex-color and sprite materials.

`BeginPlay` applies shape and then builds a pedestrian body or loads a vehicle mesh.

`Tick` decays avoidance/guidance timers, updates movement, snaps to ground, and runs the simple visual animation.

### Configuration and Asset Loading

`ConfigureAgent` sets kind, mesh name, original root, speed, and animation phase, then loads the right visual representation.

`LoadOriginalMeshFromOriginalGameRoot`:

1. Routes pedestrian `PEOPLE1` names to the sprite loader.
2. Loads the original mesh library.
3. Finds the requested table name.
4. Builds a palette-colored mesh.
5. Sends face type `11` into a discarded section for vehicles so original headlight beam cards do not render as opaque geometry.
6. Offsets the mesh so its bottom rests at the capsule bottom.
7. Configures real spotlights for vehicles.

`LoadOriginalPedestrianSpriteFromOriginalGameRoot` is the old sprite path. It loads `PEOPLE1.BMP`, picks a column, builds a frame quad, creates a dynamic sprite material, and shows the original mesh component.

`BuildPedestrianBody` is the current pedestrian path. It builds a low-poly procedural body through `FSimCopterPopulationBody`, disables headlights, and shows the procedural mesh.

`ShowOriginalMesh` toggles procedural original/body/sprite visibility against the cube proxy.

### Headlights

`ConfigureVehicleHeadlights` places two spotlights at the front corners of the vehicle local bounds. This intentionally replaces the original face type `11` headlight cards for nighttime readability.

`DisableVehicleHeadlights` hides both lights.

### Movement and Traffic Control

`SetMoveTarget`, `ClearMoveTarget`, and `IsNearMoveTarget` manage destination state.

`SetTrafficSpeedScale`, `LimitTrafficSpeedScale`, and `ApplyTrafficBrake` are used by the traffic system to slow or stop vehicles.

`AddTrafficVelocityImpulse` and `MoveByTrafficSeparation` handle overlap recovery and small bump responses.

`SetAvoidanceMoveTarget`, `SetAvoidancePathOffset`, and `SetGuidanceMoveTarget` provide temporary steering overrides for pedestrian avoidance, vehicle bypass, lane guidance, and traffic light queueing.

`SetRouteState` stores the graph route state owned by the traffic system: target node, previous node, and planned next node.

`UpdateMovement` moves kinematically without sweeping. This is deliberate: traffic graph logic and separation manage roads/agents, while swept movement caused cars to catch on corners and stall. The function interpolates toward the active target, rotates toward travel direction, applies external impulses, and clears temporary targets when reached.

`TraceGround` performs a high-to-low `ECC_Camera` trace so agents snap to rendered city terrain/mesh but ignore other capsules.

`SnapToGroundImmediate` and `UpdateGroundSnap` use `TraceGround`.

`UpdateJankyAnimation` leans/bobs pedestrians and wobbles cars. For sprite pedestrians, it rebuilds the `PEOPLE1` frame quad when the row changes.

## Population Sprite

Files:

- `Public/Ground/SimCopterPopulationSprite.h`
- `Private/Ground/SimCopterPopulationSprite.cpp`

`FSimCopterPopulationSprite` is the reusable helper for `BMP/PEOPLE1.BMP`.

Constants:

- Frame size: `27x33`.
- Columns: `12`.
- Rows: `3`.
- Transparent palette index: `254`.

`IsPeople1Name` accepts `PEOPLE1`, `PEOPLE1:<column>`, or `PEOPLE1_<column>`.

`ResolvePeople1Column` parses an explicit column suffix or hashes a stable object name. Column `0` is reserved because the shipped sheet has a different non-cyan backdrop there.

`ResolvePeople1BitmapPath` resolves `<OriginalGameRoot>/BMP/PEOPLE1.BMP`.

`LoadPeople1Texture` loads the paletted Windows BMP through `FMaxisWindowsBitmapReader`, validates exact sheet size, then creates a transient nearest-filtered texture.

`BuildPeople1FrameQuad` builds a vertical billboard quad at local X=0 with UVs for one frame. The quad faces +X and uses the requested height while preserving the original frame aspect ratio.

## Population Body

Files:

- `Public/Ground/SimCopterPopulationBody.h`
- `Private/Ground/SimCopterPopulationBody.cpp`

`FSimCopterPopulationBody` is a remake stand-in for not-yet-decoded `PrivAnim.df` articulated people.

`GPersonOutfits` defines six low-color outfits: one police uniform with cap plus five civilian variants.

`AppendBox` emits one flat-shaded box with separate vertices per face and double-sided triangles.

`ResolveOutfitIndex` hashes a stable object name so each pedestrian keeps a stable outfit during its life.

`OutfitHasHat` reports whether the selected outfit includes a cap.

`BuildPerson` emits legs, torso, arms, hands, head, and optional cap. The figure faces +X, has feet at local Z=0, and scales all proportions from total height.

This is not decoded original `PrivAnim.df` behavior; it is a visually compatible placeholder until the original figure records are understood.

## Traffic System

Files:

- `Public/Ground/SimCopterTrafficSystemActor.h`
- `Private/Ground/SimCopterTrafficSystemActor.cpp`

`ASimCopterTrafficSystemActor` builds road and sidewalk graphs from the active city, spawns vehicle/pedestrian agents near the player, and manages traffic interactions.

### Route Node Types

`FSimCopterGroundRouteNode` stores:

- Local city-space location.
- World-space location after the active city transform.
- SC2 file X/Y.
- `XBLD` building id.
- Neighbor node indexes.

`ESimCopterTrafficFlowMode` switches between normal traffic and traffic jam behavior.

`FSimCopterVehicleTrafficState` stores per-vehicle runtime state for blockage detection, recent collisions, traffic lights, recovery bypass, rejoin, and intersection commit.

### Road Classification

The helper functions near the top encode the current road knowledge:

- `IsSurfaceRoadTile`: `XBLD 0x1d..0x2b`, the `RD29..RD43` road meshes.
- `IsRoadCrossingTile`: `0x43` and `0x44`, used as road-continuity crossing pieces.
- `IsOriginalTrafficRoadTile`: surface roads, crossings, and known elevated/bridge road ids `0x45..0x48`, `0x4d..0x4e`, `0x5a..0x5b`.
- `IsPedestrianRoadTile`: surface roads plus crossings.

This supersedes an earlier mistake where cars used rail ids `0x2c..0x3e`.

`GetRoadOpeningMask` maps road tile ids to N/E/S/W openings. Straight, corner, T-junction, four-way, crossing, and bridge pieces all rely on this mask for graph connectivity.

`CanRoadTilesConnect` ensures neighboring road nodes only connect when both tile masks open toward each other.

`GetRoadCenterlineLocalOffset` biases corner road nodes toward the visible diagonal/curve path.

`GetRoadSidewalkLocalOffset` offsets pedestrian nodes onto sidewalks embedded in road tiles.

`ChooseNextRouteNode` is the graph walker. It avoids immediate U-turns unless at a dead-end, prefers continuing straight 70 percent of the time, and only returns adjacent neighbors so agents cannot leave the road network.

### Construction and Data Build

The constructor sets defaults:

- Default city path points at `cape wells.sc2`.
- Default original root is `../Reference/SimCopterOriginalGame`.
- Vehicle mesh candidates are `AUTO`, `AUTO2`, `AUTO3`, `AUTO4`, `AUTO5`, `AUTO6`, `CARPOLIC`, and `CARAMBUL`.
- Pedestrian mesh names currently default to `PEOPLE1`.

`BeginPlay` initializes the random stream and optionally calls `RebuildSpawnData`.

`Tick` updates the agent pool and traffic interactions.

`ResolveSourceCityActor` finds the explicit or first active `ASimCity2000CityActor` when enabled.

`RebuildSpawnData`:

1. Clears nodes, indexes, agents, and traffic state.
2. Uses the active city actor as source of truth when available: city path, original game root, tile size, terrain scale, and transform.
3. Parses the `.sc2` city.
4. Builds road nodes for non-water original traffic road tiles.
5. Builds pedestrian nodes for non-water pedestrian road tiles.
6. Converts local node positions through the city actor transform.
7. Builds road neighbor links using tile openings.
8. Builds pedestrian neighbor links the same way.
9. Records debug counts and logs a summary.

### Agent Pool

`GetPopulationFocusLocation` returns player pawn location or the traffic actor location.

`UpdateAgentPool`:

- Prunes far or invalid agents.
- Assigns next route targets to agents that reached their current target.
- Spawns vehicles and pedestrians up to configured counts on a timer.
- Updates active counts.

`PruneAgentArray` destroys agents beyond `DespawnRadiusCm`.

`ChooseNodeNearFocus` samples random graph nodes inside the spawn radius and outside the minimum spawn distance, with a nearest fallback.

`TrySpawnAgent`:

1. Picks a route node and initial next node.
2. For vehicles, requires a clear spawn location.
3. Computes spawn yaw from the first hop.
4. Spawns `ASimCopterGroundAgent`.
5. Picks an original mesh/sprite name.
6. Configures the agent with active original root and speed.
7. Optionally discards agents whose original asset failed to load.
8. Snaps to ground immediately.
9. Seeds route state and first move target.
10. Adds the agent to the vehicle or pedestrian array.

### Traffic Interactions

`UpdateTrafficInteractions` resets speed scales, applies traffic lights and following, resolves overlaps, performs blockage recovery, applies lane guidance, and handles pedestrian avoidance.

`SyncVehicleTrafficStates` creates/removes state entries for live vehicles, decays timers, tracks actual motion, and accumulates blocked time.

`ApplyTrafficLights` treats qualifying intersection nodes as signalized intersections. It groups vehicles by approach, sorts them by distance, assigns queue slots, marks queue state, and brakes vehicles toward their slots until the approach is green.

`IsTrafficLightIntersectionNode` and `IsTrafficLightGreenForApproach` define the current signal logic. This is remake behavior, not yet confirmed original mission logic.

`ApplyVehicleFollowing` detects the closest same-lane vehicle ahead, adjusts stop/slow distances for normal traffic, traffic-light queues, and recovery rejoin, and brakes or speed-limits the following vehicle.

`ResolveVehicleOverlaps` separates overlapping vehicles over up to two passes and adds bump impulses.

`UpdateVehicleBlockageRecovery`, `TryStartVehicleRecovery`, `FindClosestBlockingVehicle`, and `ChooseVehicleBypassDirection` help a blocked vehicle back up, bypass a blocker, and rejoin the lane.

`ApplyVehicleLaneGuidance`, `TryMakeVehicleLaneGuidanceTarget`, `ClampVehicleLocationToRoadNetwork`, and `MakeVehicleRoadSafePathOffset` keep cars on right-hand lane offsets and constrain guidance targets to the road graph.

`UpdatePedestrianAvoidance`, `TryFindPedestrianEscapeTarget`, and `TryGetPedestrianAwayFromRoadCenterDirection` move pedestrians away from car paths when needed.

### Route Target Geometry

`AssignNextTarget` advances an agent to the next adjacent graph node, stores previous/planned-next nodes, and chooses vehicle or pedestrian target locations.

`MakeRoutePointLocation` returns a node location, plus right-hand lane offset for vehicles. It uses corner road travel direction when possible so corner/diagonal road pieces do not zig-zag through tile centers.

`MakeVehicleRouteTargetLocation` adds extra corner clipping for upcoming turns, especially right-hand turns, to reduce sharp stair-step motion.

## On-Foot Pawn

Files:

- `Public/Ground/SimCopterOnFootPawn.h`
- `Private/Ground/SimCopterOnFootPawn.cpp`

`ASimCopterOnFootPawn` is the ground-start player pawn.

The constructor:

- Auto-possesses player 0.
- Builds a scaled capsule root.
- Adds a proxy body and procedural body component.
- Adds a spring-arm camera.
- Loads cube and vertex-color materials.
- Defaults original root and helicopter class.

`BeginPlay` snaps to ground, builds the procedural body, finds or spawns a parked helicopter, hides mouse cursor, and switches input to game-only.

`Tick` updates movement, ground snap, body animation, and camera.

`SetupPlayerInputComponent` reuses pitch/roll/look bindings for walking and looking.

`Interact` finds the nearest helicopter inside interaction radius, calls `EnterHelicopter`, and destroys the on-foot pawn.

`UpdateMovement` moves with swept collision using the current yaw frame and rotates from look input.

`UpdateCamera` clamps camera pitch.

`LoadOriginalBodySprite` is now named historically: it builds the same procedural low-poly body used by NPC pedestrians and hides the proxy.

`UpdateBodySprite` bobs/leans that body while walking.

`SnapToGround` and `ResolveGroundedLocation` line-trace down to keep the pawn on the rendered city.

`FindOrSpawnParkedHelicopter` finds an existing nearby helicopter or spawns one near the player, grounded against the scene.

`FindNearestHelicopter` searches all helicopter pawns and returns the nearest within radius.

## Game Mode

Files:

- `Public/Game/SimCopterGameMode.h`
- `Private/Game/SimCopterGameMode.cpp`

`ASimCopterGameMode` sets `ASimCopterOnFootPawn` as the default pawn and can spawn one traffic system actor at begin play.

`BeginPlay` skips spawning if disabled, class is missing, world is missing, or an existing traffic system is already present.

## Maxis Mesh Debug Actor

Files:

- `Public/Debug/MaxisMeshDebugActor.h`
- `Private/Debug/MaxisMeshDebugActor.cpp`

`AMaxisMeshDebugActor` is an editor/runtime inspection actor for one object inside one Maxis mesh pack.

`RebuildMesh`:

1. Resolves the configured `.MAX` path.
2. Parses it with `FMaxisMeshReader`.
3. Finds `MeshName` by table name.
4. Triangulates each face as a fan.
5. Converts vertices and UVs into Unreal space.
6. Colors non-textured faces from the pack palette.
7. Colors face types `13` and `18` with a fallback color.
8. Optionally emits backfaces and collision.
9. Logs source/render counts.

`ResolveMeshPath` mirrors other path resolvers.

`ResolveFaceColor` is a local debug-only color helper. The city renderer has the full texture-material path; this actor is intentionally simpler.

## Original-Behavior Gaps

The current gameplay code still has large known gaps:

- Helicopter movement is tuned from original values but not a decompiled physics port.
- Traffic uses a remake graph/avoidance system, not the original `TRAN` object update routines.
- Pedestrian bodies are procedural placeholders until `PrivAnim.df` is decoded.
- People behavior labels and state transitions from `People.df` are not yet driving NPCs.
- Traffic lights/queueing/recovery are remake systems for playability and debugging.

The important part is that original data is already flowing through the runtime, and the boundaries between original data and remake scaffolding are explicit.
