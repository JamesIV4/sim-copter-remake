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
  population actors now require original asset loads; pedestrians and the on-foot player render
  cropped frames from original `BMP/PEOPLE1.BMP` through the masked sprite material while the deeper
  `privanim.df` animation records are decoded.
- Route traffic cars with the decompiled `FUN_004b5290` TRAN road-step table for XBLD ids
  `0x2c..0x3e`, `0x45..0x48`, `0x4d..0x4e`, and `0x5a..0x5b`; remaining work is original spawn
  validation and follower/lane-offset subobject updates.
- Start the player on foot near a parked helicopter, then support `F` enter/exit, hold `Space` to
  start the helicopter, and hold `Ctrl` on the ground to shut the engine down.

## Milestone 6: Fidelity Pass

- Compare screenshots and behavior against the original game.
- Keep fixes explicit and documented.
- Add developer diagnostics for city chunks, tile ids, asset lookup, mission state, and physics tuning.
