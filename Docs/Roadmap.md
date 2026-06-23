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
- The city actor can decode original `SIM3D.BMP` textures at rebuild time and bind textured mesh faces to transient `UTexture2D` sections.
- Placeholder road/building blocks remain available as a fallback for missing original mesh mappings.
- Format automation now covers the tile-id mapping layer and the `SIM3D.BMP` composite texture parser.

## Milestone 4: Flight, Camera, And Interaction

- Parse `tweak/*.twk` into runtime tuning data.
- Implement helicopter flight using original tuning values as the baseline.
- Recreate camera modes, landing detection, rope/bucket behavior, and collision probes.
- Add targeted fixes for modern frame rates and input devices.

## Milestone 5: Simulation And Missions

- Decode relevant `.df` behavior/animation data or reimplement behavior with equivalent gameplay semantics.
- Rebuild dispatch, traffic jams, fires, medevac, rescue, crime, transport, and career progression.
- Integrate original WAV/SMK media through user-provided files.

## Milestone 6: Fidelity Pass

- Compare screenshots and behavior against the original game.
- Keep fixes explicit and documented.
- Add developer diagnostics for city chunks, tile ids, asset lookup, mission state, and physics tuning.
