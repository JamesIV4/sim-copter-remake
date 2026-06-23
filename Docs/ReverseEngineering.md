# SimCopter Reverse Engineering Notes

## Current Scope

The first milestone is loading SimCity 2000 `.sc2` city files and rendering them in Unreal as navigable city geometry. The runtime must read assets from a user-provided SimCopter installation instead of committing original game data into this repository.

`/Reference` is intentionally ignored by git. The local reference copy currently contains:

| Folder | Files | Notes |
| --- | ---: | --- |
| `cities` | 48 | SimCity 2000 `.sc2` cities, including career cities. |
| `BMP` | 144 | UI art plus likely texture atlases: `GRND*`, `BLDG*`, sky, cockpit/dashboard, catalog screens. |
| `GEO` | 3 | `sim3d1.max`, `SIM3D2.MAX`, `SIM3D3.MAX`; these appear to be Maxis Sim3D geometry containers, not Autodesk scene files. |
| `X` | 2 | `people.df`, `privanim.df`; behavior/animation resources with readable strings. |
| `tweak` | 8 | Plain-text tuning tables for helicopter, camera, fire, figures, missions, career, and global sim3d settings. |
| `sound` | 516 | WAV radio, dispatcher, effects, and voice lines. |
| `SMK` | 77 | Smacker movies. |

## `.sc2` City Format

The provided city files are EA IFF containers:

1. 12-byte header: `FORM`, big-endian file length excluding the first 8 bytes, then `SCDH`.
2. Repeating chunks: 4-byte chunk id, 4-byte big-endian stored size, then chunk payload.
3. Most chunks use SimCity 2000 RLE. `ALTM` and `CNAM` are stored uncompressed.

Validated against all 48 bundled `.sc2` files on 2026-06-23:

```text
cities 48 errors 0
```

Expected decoded chunk sizes used by the loader:

| Chunk | Decoded bytes | Meaning for this project |
| --- | ---: | --- |
| `CNAM` | 32 | City name, optional/order varies. |
| `MISC` | 4800 | City metadata, rotation, water level, budgets, simulation state. |
| `ALTM` | 32768 | 128x128 altitude map, 2 bytes per tile. |
| `XTER` | 16384 | 128x128 terrain surface codes. |
| `XBLD` | 16384 | 128x128 building/infrastructure tile codes. |
| `XZON` | 16384 | 128x128 zoning/footprint flags. |
| `XUND` | 16384 | Underground layer. |
| `XTXT` | 16384 | Sign/microsim references. |
| `XLAB` | 6400 | Labels/sign strings. |
| `XMIC` | 1200 | Microsim records. |
| `XTHG` | 480 | Unknown. |
| `XBIT` | 16384 | Per-tile flags such as power/water/connectivity. |
| `XTRF`, `XPLT`, `XVAL`, `XCRM` | 4096 each | 64x64 traffic, pollution, land value, crime maps. |
| `XPLC`, `XFIR`, `XPOP`, `XROG` | 1024 each | 32x32 police, fire, population, growth maps. |
| `XGRP` | 3328 | Unknown/group data. |

Important interpretation notes:

- Grid data is 128x128 in file order, row by row. The renderer now maps file X/Y directly to Unreal X/Y so Maxis road mesh orientation matches the SC2 tile ids. Earlier mirrored placement made asymmetric road/corner meshes look rotated.
- `ALTM` values are big-endian 16-bit integers. Bits `0..4` hold the altitude step, and bit `7` marks water coverage. Procedural terrain uses `Altitude * TerrainHeightScale` for land tiles and `WaterLevel * TerrainHeightScale` for water tiles; the earlier `Altitude + 1` cube-top convention was only appropriate for placeholder cube terrain.
- `CNAM` often starts with `0x1f` even when the actual string is shorter, so the loader reads printable ASCII bytes until null/control/non-ASCII data instead of trusting the first byte as a length.
- `XZON` high-nibble bits are useful footprint markers for suppressing duplicate multi-tile buildings: `0xf0` means a single/full tile, `0x80` marks the top-left owner tile, `0x40` marks top-right, `0x10` marks bottom-left, and `0x20` marks bottom-right. Rendering only owner/single tiles reduced the Cape Wells original mesh placements from `6083` to `4586`.

Sources used:

- OpenCity2k SC2 file specification: https://github.com/OpenCity2k/SC2k-docs/blob/master/sc2%20file%20spec.md
- David Moews' SimCity 2000 MS-DOS format notes: https://djm.cc/simcity-2000-info.txt

## SimCopter Geometry Assets

The `GEO/*.MAX` files contain readable chunk/object markers such as:

```text
DIRC
CMAP
GEOM
GEOMD
sim3d1
BASE1X1R
BASE2X2R
BASE3X3R
RD29
RD30
MD520
MDEXPLRR
FACE=
OBJX
```

Working hypothesis:

- These are Maxis Sim3D asset packs.
- Object names map to SimCity tile categories and ids. Examples: `RD29` likely corresponds to road tile `0x1D`/decimal `29`; `AB138` likely corresponds to abandoned building id `0x8A`/decimal `138`.
- The files contain palette/color map data (`CMAP`), geometry records (`GEOM`/`GEOMD`), object records (`OBJX`), and face records (`FACE=`).
- All integers in these mesh packs are little-endian.
- Mesh coordinate system is left-handed with `+Y` up; moving objects generally face `+Z`.
- Public reverse-engineering notes estimate `262,144` raw mesh units per metre. A SimCity tile is therefore about `16m` wide in SimCopter coordinates.

Initial marker scan:

| File | Bytes | `OBJX` markers | `FACE=` markers | Object-like names |
| --- | ---: | ---: | ---: | ---: |
| `sim3d1.max` | 497281 | 143 | 3400 | 153 |
| `SIM3D2.MAX` | 605029 | 144 | 3304 | 115 |
| `SIM3D3.MAX` | 474026 | 113 | 3772 | 115 |

Sample names found:

```text
BASE1X1R BASE2X2R BASE3X3R BASE4X4R
RD29 RD30 RD31 RD32 RD33 RD34 RD35 RD36 RD37 RD38 RD39 RD40 RD41 RD42 RD43
WR14 WR15 WR16 WR17 WR18 WR19 WR20 WR21 WR22 WR23 WR24 WR25 WR26 WR27 WR28
RL44 RL45 RL46 RL47 RL48 RL49 RL50 RL51 RL52 RL53 RL54 RL55 RL56 RL57 RL58
BR81 BR82 BR83 BR84 BR85 BR86
TREE6 TREE7 TREE8 TREE9 TREE10 TREE11
AUTO2 AUTO3 MD520 MDEXPLRR
```

Implemented Maxis mesh reader validation:

```text
sim3d1.max: geometry_entries=144 objects=143 vertices=7605 faces=6636
SIM3D2.MAX: geometry_entries=145 objects=144 vertices=8828 faces=8455
SIM3D3.MAX: geometry_entries=114 objects=113 vertices=7612 faces=6197
mesh_files 3 errors 0
```

Implemented city mesh library validation:

```text
SimCopter.Formats.MaxisMesh.TileMapping: passed
```

Implemented Maxis composite bitmap validation:

```text
SIM3D.BMP: images=68, key texture pages 39 and 40 are 256x256
SimCopter.Formats.MaxisTexture.ReferenceCompositeBitmap: passed
```

Important implementation notes:

- `DIRC` at byte 0 declares the file size.
- Byte 16 points to the colour-map section at byte 28.
- Byte 24 points to the geometry table at byte 829.
- Geometry table entries are 53 bytes. Entry 0 is a summary; entries 1..N map object names such as `RD29` to `OBJX` offsets.
- Duplicate geometry table entries are 36 bytes and carry the game-facing object id.
- `OBJX` object headers are 124 bytes, followed by 12-byte vertices and `FACE` records.
- The `FACE` size field is the full face record size starting at `FACE`, not a payload size after the first 8 bytes.
- `SIM3D2.MAX` object `EXPLODE` has an `OBJX` declared size that is 24 bytes shorter than its actual extent. The reader therefore uses the next geometry-table object offset as the authoritative object boundary, while still recording the declared size.
- The debug actor currently triangulates polygon faces and colors them from the `CMAP` palette. Textured faces render with a neutral fallback color until composite BMP atlas decoding is implemented.
- `FMaxisMeshLibrary` now loads `sim3d1.max`, `SIM3D2.MAX`, and `SIM3D3.MAX` from a configured original game root and indexes objects by table name plus SC2 `XBLD` tile id.
- The tile-id map uses the public mesh-number-to-XBLD table where available. Table-name number inference fills in static road/highway pieces that are omitted from that table, while explicit mappings take priority.
- Some visual variants share one SC2 tile id, for example `AP221F`/`AP221`, `BR81`/`BR81F`, and `LP213`/`LP213L`. The library currently prefers the unsuffixed object as the nearest/highest-detail default.

## SimCopter Composite Bitmap Textures

`SIM3D.BMP`, `SKY.BMP`, `SKYDARK.BMP`, and `TILED1.BMP` are not Windows bitmaps. They are Maxis composite bitmap files containing palette-indexed images. `SIM3D.BMP` is the main city mesh texture source, with a known SimCopter exception for texture file/index `20`, which resolves to `SKY.BMP` image `4`. `TILED1.BMP` image `0` is a 256x256 terrain atlas containing water, shore, grass, sand, and related ground cells.

1. 32-bit little-endian file size at byte 0.
2. 32-bit image count at byte 8.
3. 32-bit resolution/table count at byte 12.
4. A 12-byte entry table beginning at byte 16.
5. Image records beginning after that table. Each record stores width, height, an unused zero field, a per-row offset table, then width x height palette indices.

The palette is not stored in the composite bitmap. The three `GEO/*.MAX` files contain identical 256-color `CMAP` data, which is used to expand texture pixels to RGBA.

Face texture notes:

- Composite bitmap row data is stored bottom-up. The loader flips rows while expanding palette indices to Unreal texture pixels; without that flip, city building texture pages can resolve to visibly wrong atlas regions such as repeated head/icon cells.
- Mesh face type `18` is the main textured face type for city geometry. Face type `13` appears rarely and is currently routed through the same texture path when its image index exists.
- For face type `18`, `MaterialIndex` is the index within an 8x8 texture atlas and `TextureAtlasIndex` is the atlas texture file/index. The original viewer extracts `32x32` cells using column `MaterialIndex % 8` and row `MaterialIndex / 8`, with row `0` at the bottom of the atlas image.
- The common city atlas pages observed so far are `2`, `20`, `39`, and `40`. Page `20` is the `SKY.BMP` image `4` exception, not `SIM3D.BMP` image `20`.
- Raw UV values are fixed-point values scaled by `65536`. Values often extend outside `0..1`, so the renderer keeps those repeat coordinates and applies them to transient 32x32 atlas-cell textures with wrap addressing.
- Maxis raw V coordinates use a bottom-left origin. `FMaxisMeshReader::ConvertMaxisUVToUnreal` flips V for Unreal's top-left texture sampling while preserving out-of-range repeat values.
- The city actor creates transient `UTexture2D` objects for direct `SIM3D.BMP` images plus atlas-cell textures, then assigns them to procedural mesh sections using Unreal's built-in `/Engine/EngineMaterials/EmissiveTexturedMaterial`.
- The current terrain pass creates one procedural quad per SC2 tile, samples `TILED1.BMP` image `0`, and uses `XTER & 0x3f` as an 8x8 atlas index. This is a local-data implementation clue, not yet a fully proven copy of the original terrain tile selection logic.
- Terrain vertex heights are averaged from adjacent visible tile surfaces so neighboring quads form sloped surfaces instead of flat cubes. The terrain triangle order is reversed for Unreal front-face culling while keeping supplied normals upward. Exact original slope orientation/type decoding still needs a deeper executable pass.

Validated on `CityRender.umap` construction on 2026-06-23 after the footprint, terrain mesh, and water overlay passes:

```text
terrain=16384
water=0
roads=23
buildings=0
originalMeshTiles=4586
missingOriginalMeshTiles=23
originalTriangles=422338
texturedTriangles=147348
originalTextures=389
```

Next asset tasks:

1. Verify current building UV orientation and terrain tile selection visually against the original game.
2. Decode the exact original terrain slope/type rules instead of the current averaged-height approximation.
3. Replace or refine the old placeholder water/road fallback surfaces after terrain/road atlas behavior is confirmed.
4. Add a diagnostics view listing missing mesh mappings, footprint decisions, and texture references.
5. Split procedural city geometry into deterministic streaming/cache chunks for larger cities.

Mesh format sources used:

- CahootsMalone Maxis mesh format documentation: https://github.com/CahootsMalone/maxis-mesh-stuff/blob/master/Info/Maxis-Mesh-Format.md
- CahootsMalone mesh-to-SimCity building mapping notes: https://github.com/CahootsMalone/maxis-mesh-stuff/blob/master/Info/Mapping%20from%20SimCopter%20meshes%20to%20SimCity%202000%20buildings.csv
- Original repository overview for mesh tools/viewer: https://github.com/CahootsMalone/maxis-mesh-stuff

## Original Executable Quick Facts

`Reference/SimCopterOriginalGame/SimCopter.exe` was inspected with `rizin`/`objdump` only. No decompilation pass has been committed yet.

Observed facts:

- PE32 Windows GUI executable.
- x86/i386, little-endian.
- Image base `0x00400000`.
- Reported compile timestamp: `Sun Dec 8 16:16:10 1996 UTC-5`.
- Imports include `DDRAW.dll`, `DSOUND.dll`, `WINMM.dll`, `MSACM32.dll`, `smackw32.DLL`, `GDI32.dll`, `USER32.dll`, `KERNEL32.dll`, `ADVAPI32.dll`, `comdlg32.dll`, and `VERSION.dll`.
- Strings include `FORM`, `SCDH`, `ALTM`, `XTER`, `XBLD`, `SIM3D1.MAX`, `SIM3D2.MAX`, `SIM3D3.MAX`, and `sim3d.twk`, supporting the city/asset pipeline being implemented first.
- Additional local string/analysis probes found `TILED1.BMP`, `TileCnt`, `AltMap`, `RoadTiles`, and `SIM3D.BMP` references in the executable. These support the current focus on terrain atlas, altitude map, and road/building tile behavior before relying on broader online notes.

## Implementation Notes

Current code has two layers:

- `FSimCity2000Reader`: pure parser/decoder for `.sc2` files.
- `FMaxisMeshReader`: pure parser/decoder for Maxis `.MAX` mesh packs.
- `FMaxisMeshLibrary`: original-game asset index that maps SC2 tile ids to decoded Maxis mesh objects.
- `FMaxisTextureReader`: pure parser/decoder for Maxis composite bitmap texture files such as `SIM3D.BMP`.
- `ASimCity2000CityActor`: editor/runtime actor that renders a decoded city with procedural terrain, optional fallback water plates, and original SimCopter road/building mesh geometry. Textured mesh faces use transient textures decoded from `SIM3D.BMP` plus the `SKY.BMP` page-20 exception; terrain uses `TILED1.BMP` image `0` when available. Placeholder road/building instances remain as a fallback for missing mappings.
- `AMaxisMeshDebugActor`: editor/runtime actor that renders one decoded `.MAX` mesh for inspection.

The placeholder renderer is now fallback scaffolding. It still proves city parsing, orientation, tile height, and high-level tile classification, while original terrain textures and road/building geometry can be rendered directly from user-provided SimCopter assets.
