# Tooling Code Walkthrough

This document covers project tooling that supports the original-file pipeline. These scripts do not decompile SimCopter, but they make the decompiled format knowledge usable in Unreal.

## Unreal Asset Tools

Folder:

- `Tools/Unreal/`

These scripts run inside Unreal Editor Python. They use local original game art under `Reference/SimCopterOriginalGame`, then write generated Unreal assets under gitignored project folders.

### BakeCityAtlas.py

`Tools/Unreal/BakeCityAtlas.py` bakes original Maxis composite bitmap pages into Unreal textures and material instances.

It produces:

- `T_CityPage_<id>`: full 256x256 atlas page textures.
- `MI_CityPage_<id>`: instances of `M_SimCopterCityAtlas`.
- `T_CityImage_<id>` and `MI_CityImage_<id>`: direct image textures/materials for face type `13`.
- `T_TerrainLow` and `MI_TerrainLow`: `TILED1.BMP` image `0`.
- `MI_TerrainHigh`: material for `SIM3D.BMP` image `13`.

Important constants:

- `OUTPUT_DIR = /Game/Generated/CityAtlas`.
- `ATLAS_MATERIAL = /Game/Materials/M_SimCopterCityAtlas`.
- `TERRAIN_MATERIAL = /Game/Materials/M_SimCopterLitTexture`.
- `SKY_PAGE_ID = 20`, because SimCopter face texture file `20` resolves to `SKY.BMP` image `4`, not `SIM3D.BMP` image `20`.
- `TERRAIN_HIGH_PAGE_ID = 13`, because `SIM3D.BMP` image `13` doubles as high terrain page `0x0d`.

Function walkthrough:

- `reference_root` resolves the local ignored original game folder relative to the Unreal project.
- `read_palette` reads the shared 256-color `CMAP` from a `GEO/*.MAX` pack. It mirrors `FMaxisMeshReader` color-map assumptions.
- `decode_composite` decodes a Maxis composite bitmap. It reads image count and resolution table size, walks image records, honors per-row offsets, maps palette indexes to RGB, and flips rows top-down like `FMaxisTextureReader`.
- `write_png` writes a tiny RGB PNG without external dependencies. It builds PNG chunks manually and compresses scanlines with zlib.
- `import_texture` imports a PNG into Unreal, then sets exact sampling properties: sRGB, uncompressed/editor icon compression, nearest filter, wrap addressing, no mips, and never stream.
- `create_material_instance` creates or reloads a `MaterialInstanceConstant`, sets its parent, assigns the `Texture` parameter, and saves it.
- `main` loads palette plus `SIM3D.BMP`, `SKY.BMP`, and `TILED1.BMP`, writes/imports all atlas pages, applies the page-20 sky exception, imports direct images, and creates low/high terrain material instances.

Why this script exists:

- The runtime can decode textures transiently, but baked material instances survive editor/PIE lifecycle better.
- Full-page materials avoid creating hundreds of per-cell textures.
- Generated assets are local because they contain original game art.

### CreateSimCopterMaterials.py

`Tools/Unreal/CreateSimCopterMaterials.py` creates project-authored parent materials.

Shared helpers:

- `ensure_directory` creates `/Game/Materials` if needed.
- `save` saves an asset path.
- `create_or_load_material` reuses an existing material or creates one.
- `material_exists` checks asset existence.
- `create_if_missing` avoids clobbering hand-tuned material assets.
- `clear_expressions` deletes existing graph expressions when a material is being generated.

Shared shading:

- `SELF_ILLUM_DEFAULT`, `ROUGHNESS_DEFAULT`, and `SPECULAR_DEFAULT` keep city art readable under dynamic lighting without making it glossy.
- `add_scalar_parameter` adds a grouped scalar parameter node.
- `add_shading_nodes` wires base color into emissive through `SelfIllum`, plus roughness and specular.

Material creators:

- `create_lit_texture_material` creates `M_SimCopterLitTexture`, a Default Lit texture material for generated terrain/direct images.
- `create_city_atlas_material` creates `M_SimCopterCityAtlas`, which samples one full 8x8 page using `TexCoord0` for repeated in-cell UVs and `TexCoord1` for atlas cell column/row.
- `create_rotor_disc_material` creates `M_SimCopterRotorDisc`, an Unlit Translucent two-sided material for Maxis face type `11` rotor blur discs.
- `create_sprite_texture_material` creates `M_SimCopterSpriteTexture`, a Masked Unlit two-sided material for `PEOPLE1.BMP` sprites with alpha from palette index `254`.
- `create_lit_vertex_color_material` creates `M_SimCopterLitVertexColor`, a Default Lit vertex-color material for palette-colored meshes and procedural people.
- `create_water_material` creates `M_SimCopterWater`, a Default Lit material that reuses the terrain-low texturing but displaces the sea vertically in the vertex shader (World Position Offset) with analytic wave normals. Shared HLSL Custom nodes (`WaterWPO`, `WaterNormal`) read world position, a `Time` node, the vertex-color-R shoreline weight, and `WaveAmplitude`/`WaveLength`/`WaveSpeed` scalar parameters. `add_custom_node` is the helper that builds a `MaterialExpressionCustom` with named inputs.
- `create_terrain_material` creates `M_SimCopterTerrain`, the ground material: same terrain texturing/shading as `MI_TerrainLow`, plus a `TerrainNormalNoise` Custom node that perturbs the shading normal with three octaves of procedural value noise (fine/medium/large, each with a `NoiseAmp*`/`NoiseScale*` scalar parameter). It rides on `VertexNormalWS` (the smoothed rest normal) and is scaled by a vertex-color-R detail weight the renderer bakes to fade the noise out near the shoreline and on building/road pads. The noise gradient is analytic (one 4-corner hash per octave) and cell coords are wrapped to keep the sin-hash stable far from the origin.

`M_SimCopterWater` and `M_SimCopterTerrain` are deleted and rebuilt every run (their shaders are still being tuned); the other materials are only created if missing. The final script body ensures the directory exists and creates all seven materials.

## Probe Tools

Folder:

- `Tools/`

These are read-only Python probes, useful outside Unreal.

`sc2_probe.py` mirrors the SC2 parser. It validates `FORM/SCDH`, chunk sizes, uncompressed chunks, SC2 RLE, and printable `CNAM` city names.

`maxis_mesh_probe.py` mirrors the mesh parser. It validates `DIRC/CMAP/GEOM`, object offsets, `OBJX`, `FACE`, vertex counts, and face sizes.

`maxis_texture_probe.py` mirrors the composite bitmap parser. It rejects normal Windows BMPs, validates composite headers, row tables, image dimensions, and exact parser end position.

Use these probes when a local reference asset seems to contradict the C++ parser. They are fast, isolated, and do not require Unreal Editor.

## Ghidra Tools

Folder:

- `Tools/Ghidra/`

`ReverseExplore.java` is the preferred reusable script for evidence files. It supports string searches, references, callers, byte dumps, decompile-by-address, and decompile-by-function-name, writing clean UTF-8 output.

`DecompileAddresses.java` is a smaller quick decompile helper for one or more addresses.

See `Docs/DecompilationWorkflow.md` for command examples and the current output index.

## Unreal Build Files

`SimCopterRemake/Source/SimCopterRemake/SimCopterRemake.Build.cs` declares runtime dependencies:

- `Core`
- `CoreUObject`
- `Engine`
- `InputCore`
- `EnhancedInput`
- `ProceduralMeshComponent`

`ProceduralMeshComponent` is required by the original mesh renderer, terrain sections, debug actor, helicopter mesh components, people bodies, and sprite quads.

`SimCopterRemake/SimCopterRemake.uproject` targets Unreal Engine `5.8` and enables the `ProceduralMeshComponent` plugin plus editor/tooling plugins currently used by the project.

`RebuildUnrealCpp.bat` is the local Windows build helper:

1. Sets repo root from the batch file location.
2. Assumes Unreal lives at `C:\GameDev\UE_5.8`.
3. Points to `SimCopterRemake.uproject`.
4. Verifies `Build.bat` and the project file exist.
5. Builds `SimCopterRemakeEditor Win64 Development` with `-WaitMutex -NoLiveCoding`.
6. Reports success or failure and exits with the Unreal build exit code.

The path is local-machine-specific. If Unreal is installed elsewhere, update `UE_ROOT` before using the batch file.
