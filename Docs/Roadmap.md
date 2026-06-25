# SimCopter Remake Roadmap

## Guiding Goal

Recreate SimCopter faithfully in Unreal 5.8 C++ using user-provided original game assets, while making targeted compatibility, stability, usability, and rendering fixes where needed.

Local Unreal Engine path is documented in `Docs/DevelopmentEnvironment.md`.

## Milestone 1: City Loading And Debug Rendering

Status: in progress.

- Decode SimCity 2000 `.sc2` files from the original SimCopter install.
- Validate all bundled reference cities.
- Render a 128x128 terrain grid from `ALTM`.
- Render water overlays from `ALTM` water bits.
- Render road/infrastructure plates and building massing from `XBLD`.
- Expose an Unreal actor that can be dropped into `CityRender.umap`.

Done in this pass:

- Added a reusable `.sc2` reader.
- Added an editor-callable city actor.
- Added automation test scaffolding for RLE and optional reference city loading.

## Milestone 2: Original Asset Index

Status: in progress.

- Scan `GEO/*.MAX` files and document container layout.
- Extract object names and offsets.
- Map `XBLD` tile ids to SimCopter object names.
- Determine coordinate system, units, face winding, palette/material references, and UV storage.
- Create a debug view that can render one decoded `.MAX` object.

Done in this pass:

- Added a reusable Maxis `.MAX` mesh reader.
- Added validation for all three bundled `GEO` packs.
- Added an editor-callable `MaxisMeshDebugActor` using `ProceduralMeshComponent`.
- Added `FMaxisMeshLibrary` to index the three original mesh packs by table name and SC2 `XBLD` tile id.
- Enabled the `ProceduralMeshComponent` plugin for the project.

## Milestone 3: City Mesh Replacement

Status: started.

- Replace placeholder building cubes with decoded original mesh assets.
- Generate terrain surfaces that match the original SimCopter slope/road/water conventions.
- Apply original BMP atlas textures.
- Add deterministic import/runtime cache so user-provided assets are processed once and reused.

Done so far:

- `ASimCity2000CityActor` can render original SimCopter road/building meshes through a `UProceduralMeshComponent`.
- Original mesh rendering is project-root configurable through `OriginalGameRoot`, defaulting to `../Reference/SimCopterOriginalGame`.
- Added `FMaxisTextureReader` for Maxis composite bitmap files such as `BMP/SIM3D.BMP`.
- Added a local atlas bake path for original city art: `Tools/Unreal/BakeCityAtlas.py` creates gitignored `/Game/Generated/CityAtlas` texture/material-instance assets from the user's `Reference` copy, and the city actor now prefers those saved assets for mesh and terrain textures while retaining the old transient decode as a fallback.
- Added `M_SimCopterCityAtlas`, a page-atlas material that samples one full 8x8 source page using UV0 for in-cell repeat coordinates and UV1 for the face cell index. This eliminates hundreds of per-cell dynamic texture/material bindings and keeps textures intact in PIE.
- Placeholder road/building blocks remain available as a fallback for missing original mesh mappings.
- Format automation now covers the tile-id mapping layer and the `SIM3D.BMP` composite texture parser.

## Milestone 4: Flight, Camera, And Interaction

Status: started.

- Parse `tweak/*.twk` into runtime tuning data.
- Implement helicopter flight using original tuning values as the baseline.
- Recreate camera modes, landing detection, rope/bucket behavior, and collision probes.
- Add targeted fixes for modern frame rates and input devices.

Done so far:

- Added a reusable SimCopter `.twk` parser for original tuning tables.
- Added an initial playable helicopter pawn that loads `tweak/heli.twk`, applies the selected helicopter type, and implements frame-rate-stable flight integration.
- Added landing checks, rope/bucket state, fuel/damage bookkeeping, forward/ground probes, searchlight controls, and gamepad/keyboard/mouse mappings.
- Replaced the old camera recreation plan with a new spring-arm camera approach: chase, orbit, and rescue camera modes with camera collision and lag.
- Enabled generated city collision for terrain, original mesh geometry, and placeholder fallback geometry, with per-actor toggles.
- Added a `SimCopterGameMode` and moved `CityRender`'s `PlayerStart` above the generated city for immediate flight testing.
- Pulled the original helicopter fuselage + rotor meshes out of the `GEO` packs onto the pawn: `ASimCopterHelicopterPawn` now loads the per-type body and main-rotor objects (and the shared `ROTORTL` tail rotor) through `FMaxisMeshLibrary`, builds them with the reusable `FMaxisProceduralMeshBuilder`, and spins the rotor objects in place to reproduce the original animation. The placeholder cube/cylinder geometry remains as an automatic fallback when the original assets are unavailable.

## Milestone 5: Simulation And Missions

- Decode relevant `.df` behavior/animation data or reimplement behavior with equivalent gameplay semantics.
- Rebuild dispatch, traffic jams, fires, medevac, rescue, crime, transport, and career progression.
- Integrate original WAV/SMK media through user-provided files.

Ground population first pass:

- Document and probe `X/people.df` plus `X/privanim.df`; behavior strings and resource markers are
  identified, and the People.df spawn/runtime entry points have first-pass addresses. The behavior
  VM/state table and PrivAnim record payload still need the full port.
- Add a city-aware traffic/pedestrian spawner that reads the active SC2 city, derives road/ground
  spawn candidates, and runs expanded active counts/radii compared with the original game's small
  pools.
- Render traffic cars from original `GEO` meshes (`AUTO*`, emergency/criminal vehicles). Active
  population actors require original asset loads. The original cars carry translucent "headlight
  beam" cards (Maxis face type `11`) projecting off the nose; the remake now strips those (they
  were rendering as opaque grey/blue blocks) and drives real `USpotLightComponent` headlights at
  the front of each car instead (`SimCopterGroundAgent` `bEnableVehicleHeadlights`).
- Replace the flat `BMP/PEOPLE1.BMP` pedestrian/player sprite with a procedural low-poly 3D body
  (`FSimCopterPopulationBody`): stacked colored boxes for legs/torso/arms/head with police and
  civilian outfit variants, drawn with `M_SimCopterLitVertexColor`. SimCopter's people are not in
  the `GEO` packs at all - they come from `privanim.df`; until those articulated records are
  decoded the box body reproduces the original flat-shaded "charm". The `PEOPLE1.BMP` sprite path
  remains in `FSimCopterPopulationSprite` as a fallback.
- Spawn/route cars on the actual surface-road tiles `0x1D..0x2B` (`RD29..RD43`) plus the elevated
  road ids `0x45..0x48`/`0x4D..0x4E`/`0x5A..0x5B`. The earlier code matched `0x2C..0x3E`, which are
  the `RL44..RL58` **rails** - so every car drove on the train tracks.
- Road routing is a clean graph walk: every road tile is a node linked to its 4-neighbour road
  tiles, and `ChooseNextRouteNode` picks a connected neighbour each hop (no U-turns except at
  dead-ends, ~70% continue straight / ~30% turn at junctions). Agents only ever target an adjacent
  graph node, so they cannot leave the road network. This replaced the decompiled `FUN_004b5290`
  step table (and a buggy 16% "drive to a random far node" fallback that beelined cars off-road).
  Agent movement is kinematic so cars never jam/stall; `UpdateGroundSnap` keeps them on the surface.
- Curve/diagonal road tiles `0x23..0x26` now use shape-aware route-point offsets toward the corner
  shared by the tile's two openings, so cars and pedestrians follow angled roads without zig-zagging
  back and forth across the road width. Cars also apply a right-hand lane offset at target time.
- Pedestrians walk the sidewalks that are part of the road tiles (surface-road tiles with a lateral
  offset toward the road edge), not the empty land beside roads.
- Population is rendered at the city's `0.25` world scale: pedestrian/player capsules and bodies
  (and the on-foot camera) are multiplied by `PopulationWorldScale` so they no longer read ~4x too
  tall next to the shrunk city and cars. Travel speeds/route nodes are unscaled.
- Ground agents (cars and pedestrians) snap to the city collision each tick and on spawn via a
  generous vertical probe on the **Camera** channel (the city terrain/mesh block it, agent/player
  capsules ignore it, so agents never stack on each other). This replaced a shallow `+120cm`
  Visibility probe that missed whenever the spawner's estimated terrain height differed from the
  city's rendered surface - the cause of cars and pedestrians hovering in the air instead of
  driving/walking on the roads.
- Start the player on foot near a parked helicopter, then support `F` enter/exit, hold `Space` to
  start the helicopter, and hold `Ctrl` on the ground to shut the engine down.

## Milestone 6: Fidelity Pass

- Compare screenshots and behavior against the original game.
- Keep fixes explicit and documented.
- Add developer diagnostics for city chunks, tile ids, asset lookup, mission state, and physics tuning.
