# SimCopter Reverse Engineering Notes

## Current Scope

The first milestone is loading SimCity 2000 `.sc2` city files and rendering them in Unreal as navigable city geometry. The runtime must read assets from a user-provided SimCopter installation instead of committing original game data into this repository.

Companion documentation added for deeper code and evidence review:

- `Docs/DocumentationCoverage.md` tracks what this documentation pass covered and what remains blocked on more reverse engineering.
- `Docs/OriginalGameFileCodeWalkthrough.md` explains the original file parsers, mesh library, procedural mesh builder, tests, and probe scripts function by function.
- `Docs/CityRenderingCodeWalkthrough.md` explains the city actor pipeline and where decompiled terrain/mesh behavior enters the renderer.
- `Docs/GameplayCodeWalkthrough.md` explains the helicopter, ground population, traffic, on-foot pawn, game mode, and mesh debug actors.
- `Docs/ToolingCodeWalkthrough.md` explains the Unreal bake/material scripts, read-only probes, Ghidra helpers, and build files.
- `Docs/DecompilationWorkflow.md` explains the Ghidra scripts, scratch outputs, address ledger, and documentation loop for new reverse-engineering work.
- `Docs/MissionsAndTweakSystem.md` decodes every `tweak/*.twk` file, the master tweak tree, the 30-city career, and the nine mission types with their full money/points scoring.
- `Docs/OriginalGameFileFormats.md` decodes the binary `X/people.df` (global behavior file: state tables, behavior VM, person record, spawn modes, PRNG) and `X/privanim.df` (articulated figure pack) resource containers.
- `Docs/OriginalRuntimeBehavior.md` decodes the original `TRAN` traffic steering and road graph, the `ALTM` slope bits, and the helicopter flight model (tuning binding, master tick, attitude integrator).

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

- Grid data is 128x128 in file order, row by row. The SC2 `MISC` rotation value is saved view/camera state and is not applied as a global mesh transform. The original game applies **no per-tile rotation** to road/building/bridge meshes; orientation is baked into the selected mesh (see "City Geometry Builder" below). The renderer reproduces the game's grid-to-world mapping instead: file Y (row) maps to world Y, and file X (column) maps to a **negated** world X (the game stores `worldY = (127.5 - col) * 0x40`). With that single sign correction, the existing Maxis-to-Unreal vertex rotation lines every mesh up without any per-tile turning. The earlier blanket 180-degree road correction was a workaround for the missing column-axis negation (a reflection that no rotation can fix) and has been removed.
- `ALTM` values are big-endian 16-bit integers. A Ghidra pass over `SimCopter.exe` confirmed the original height helper at `0x4abc20`: bits `0..4` are the base altitude, bits `5..9` are a secondary raised/water level, and bits `10..14` are slope/edge data. The helper returns the secondary level only when it is higher than the base and the matching `XTER` terrain code is greater than `0x0f`.
- The original terrain height-map builder near `0x4abce0` seeds a 256x256 `tmap` from SC2 tile-center heights, stores `(height + 1) * 0x20`, then fills in-between grid points by copying or averaging neighbors. City tile X/Y positions use `0x40` units. The Unreal actor therefore uses `TileSize * 0.5` while `bUseOriginalTerrainHeightScale` is enabled, which is `200` for the current `400` cm tile size, and terrain corners are interpolated from adjacent corrected tile-center heights.
- Water should not be derived from `ALTM` bit `7`; that bit is part of the secondary altitude field. The renderer treats `XTER > 0x0f` as water/shore for city rendering and height selection.
- `CNAM` often starts with `0x1f` even when the actual string is shorter, so the loader reads printable ASCII bytes until null/control/non-ASCII data instead of trusting the first byte as a length.
- `XZON` high-nibble bits are useful footprint markers for suppressing duplicate multi-tile buildings: `0xf0` means a single/full tile, `0x80` marks the top-left owner tile, `0x40` marks top-right, `0x10` marks bottom-left, and `0x20` marks bottom-right. Rendering only owner/single tiles reduced the Cape Wells original mesh placements from `6083` to `4586`.

Sources used:

- OpenCity2k SC2 file specification: https://github.com/OpenCity2k/SC2k-docs/blob/master/sc2%20file%20spec.md
- David Moews' SimCity 2000 MS-DOS format notes: https://djm.cc/simcity-2000-info.txt

## City Geometry Builder (decompiled)

The original 3D city is assembled by `FUN_0047c0c0` in `SimCopter.exe`, confirmed by a Ghidra pass on 2026-06-23. Key facts that drive the renderer:

- It zeroes a 256x256 scene-graph array at `DAT_005d9200` (indexed `[row*0x100 + col]`), then loops `row` `0..127` (outer) x `col` `0..127` (inner). For each tile it reads the XBLD building byte from grid `DAT_005910b0`, derives the footprint size with `FUN_004e4f80`, and runs a large `switch` on the tile id that selects one or more **mesh objects** via `FUN_00470571(globalIndex)`.
- `FUN_00470571(id)` is **not** a positional index. It linearly searches the loaded object pointer table (`DAT_004fe97c`..`PTR_DAT_004fe978`) for the object whose header Id field (file offset `+120`, runtime offset `+0x44`) equals `id` and whose attribute-flag bit 3 (duplicate/alias) is clear. Validated against the reference packs: the three packs hold 143/144/113 = **400** objects whose header Ids form a **bijection onto `0..399`** (no duplicates), so the Id behaves as a globally-unique object index. It happens to equal the pack-concatenation position (`sim3d1.max` `0..142`, `SIM3D2.MAX` `143..286`, `SIM3D3.MAX` `287..399`) only because the packs are stored in Id order. The remake reproduces this with `FMaxisMeshLibrary::FindObjectByObjectId` (matches `FMaxisMeshObject::Header.Id`, skips attribute-flag bit 3).
- There is **no rotation anywhere**. The per-tile render struct (24 bytes) stores only: a flag, `worldX`, `height`, `worldY`, footprint size, and a linked list of object handles. Orientation is fully encoded by *which* object id the switch chooses. Roads pick a flat-vs-sloped variant by comparing the four tmap corner heights, e.g. XBLD `0x1d` -> object `0x3b` when flat or `0x1d` when sloped; `0x1e` -> `0x3c`/`0x1e`. The secondary object some road/zone tiles add (`local_28`, from `FUN_00482890` or a `rand()` pick of `0x186..0x18d`) is a decorative prop (traffic/trees), not an orientation.
- World position (verified by `FUN_004a64d0`, which builds a position vector as `(struct+2, struct+4, struct+6)`): `worldX = (row - 127.5) * 0x40`, `up = tmap height`, `worldY = (127.5 - col) * 0x40`. The engine places each mesh with mesh-X -> worldX, mesh-Z -> worldY, mesh-Y -> up. The **column axis is negated**; the row axis is not.
- The remake's `ConvertMaxisVertexToUnreal` is a cyclic axis permutation (determinant `+1`) that already equals the engine's mesh-to-world rotation. The only discrepancy was that the remake placed the column axis with the wrong sign, which is a reflection (determinant `-1`) that no amount of yaw can undo. `ASimCity2000CityActor` now negates world X for every pass (mesh, terrain, placeholders) and applies no per-tile rotation. Terrain quad winding/normals are flipped to compensate for the mirrored X.

The grid globals used by these functions: XBLD `DAT_005910b0`, ALTM `DAT_00590d70`, XTER `DAT_00591a80`, tmap height grid `DAT_005cde80` (256x256, built by `FUN_004abce0`), terrain texture-type grid `DAT_005bde80`. Mesh/texture handles: SIM3D1 `DAT_005039b4`, SIM3D2 `DAT_005039b8`, SIM3D3 `DAT_005039bc`, SIM3D.BMP `DAT_005039ac`, all set up by the asset loader `FUN_00479bb0`.

Bridges are handled the same way as roads: the original builder dispatches each tile to specific object ids, and some bridge/elevated-road ranges use per-object flags and the `XBIT` variant bit rather than an explicit per-tile yaw. `FUN_0047c0c0` cases `0x3f..0x48` dispatch bridge/causeway tiles to these object Ids:

| XBLD | object Id (flat / sloped) | notes |
| --- | --- | --- |
| `0x3f`..`0x42` | `0x178` / `0x179` / `0x17a` / `0x17b` | raised spans, no slope variant |
| `0x43` | `0x128` flat / `0x17f` sloped | suspension variant by tmap corners |
| `0x44` | `0x129` flat / `0x180` sloped | suspension variant by tmap corners |
| `0x45` | `0x3b` flat / `0x1d` sloped | road-on-bridge, reuses road mesh + prop `0x2d` |
| `0x46` | `0x3c` flat / `0x1e` sloped | road-on-bridge, reuses road mesh + prop `0x2c` |
| `0x47`, `0x48` | `0x17d`, `0x17e` | causeway ends |

"Flat" means the tile's four tmap corner heights are equal. The remake implements this in `ASimCity2000CityActor`: `GetOriginalBridgeDispatch` (the table above) plus `IsOriginalTerrainTileFlat` (four `GetTerrainGridHeightMapSample` corners), resolved through `FMaxisMeshLibrary::FindObjectByObjectId`. This bypasses the heuristic `KnownXbldMappings` table for bridges; selecting the correct object Id fixes both the wrong-mesh and the wrong-facing symptoms (orientation is encoded by object choice). XBLD `0x45`/`0x46` also place the original secondary side objects (`0x2d`/`0x2c`) on the same tile origin. XBLD `0x49..0x6b` now also route through the decompiled object-id switch, including the original `XBIT & 2` choice between paired objects such as `BR81`/`BR81F`, `RD93`/`RD93F`, and `RD106`/`RD106F`; these are the long elevated/water bridge pieces that looked like every span faced the same way when they came from the heuristic tile-id map. The regular road `rand()` props `0x186..0x18d` are not placed yet. The same flat/sloped corner test also governs the regular roads `0x1d..0x23` (flat variants `0x3b..0x3e`); the remake still maps those through the heuristic table, so a flat-road follow-up remains. Note the `KnownXbldMappings` CSV is keyed by geometry-table index, which is **not** the runtime object Id, so the two numbering schemes must not be mixed without conversion.

Terrain ground texture (decoded and implemented for the SC2 city area). The ground texture is **not** chosen by a flat `XTER` lookup. SimCopter keeps a 256x256 terrain **type grid** at `DAT_005bde80`; the first 128x128 region is seeded from the SC2 city tiles, while later terrain-map passes also operate over the generated outside area. The renderer (`FUN_004814c0`) uses the type code through `DAT_005cde90`. `FUN_004abc90` maps type codes `0x00..0x3f` to page `0x14` (the loaded `TILED1.BMP` atlas) and type codes `0x40..0x7f` to page `0x0d` (the 256x256 atlas at `SIM3D.BMP` image `13`, using `type - 0x40` as the cell). The remake currently reconstructs and renders the 128x128 SC2 city region; the generated outer `DAT_005bde80` area is a follow-up. `DAT_005bde80` is also reused for the minimap colours (`FUN_004a28e0`) and vehicle traversability (`FUN_004b10a0`).

`FUN_004abce0` builds the type grid in passes that we now reproduce in `BuildTerrainTextureTypeGrid`:
1. Classify each tile: default `0x30` (inland grass); `XTER > 0x0F` water -> `5`; XBLD `0xF8` -> `0x10`; XBLD `6..0x0D`/`0xD5`/`0xDA` -> `0x20` (wooded ground); XBLD `1..4` -> `10`/`11` (water-feature tiles, originally a `rand` coin-flip).
2. Land (`0x30`) or near-shore (`0x20`) next to water (`5`) -> shore `0x10`.
3. Inland grass (`0x30`) next to shore (`0x10`) -> near-shore `0x20`.
4. Open water (`5`) next to any land (type `> 9`) -> coastal water `0`.
5. Compare averaged terrain-map height against `min/max` thresholds to promote natural land through elevation bands: low `0x10`, mid `0x20`, normal `0x30`, high `0x40`, higher `0x50`, and peak `0x60`.
6. For base natural bands only, add a 4-bit orthogonal-neighbor mask when a neighbor is in the next lower 16-cell band. Important: the original `DAT_005bde80` address is effectively `x * 0x100 + y`, while the remake stores `Grid[y * N + x]`. In original atlas order the bits are `1` = north/file-Y-1, `2` = east/file-X+1, `4` = south/file-Y+1, and `8` = west/file-X-1. This selects directional transition cells such as `0x31..0x3f`. Man-made/feature cells keep hard edges because they are not one of these base natural bands.
7. Finally, unmasked base natural cells get a random detail pass. Candidates are only exact base cells after transition masking: `0x10 -> 0x70..0x72`, `0x20 -> 0x73..0x75`, `0x30 -> 0x76..0x78`, `0x40 -> 0x79..0x7b`, and `0x60 -> 0x7c..0x7e`; `0x50` and any transition-masked cells are skipped. The game gates each candidate with one `rand() & 1` draw, then uses a second `rand() % 3` draw for the variant. `FUN_004d5490` is just the MSVCRT seed wrapper that writes `_holdrand`; SimCopter calls it with `clock()` before the terrain perturbation/detail sequence. The remake preserves the same candidates, probability, and page-`0x0d` type ranges, but uses stable per-tile pseudo-random draws so editor rebuilds are deterministic.

This yields the original behavior visible in captures: natural terrain blends across water/shore/grass/dirt/rock bands, while roads, buildings, and other man-made surfaces remain crisp. The original's water/coastal-water **texture** animation cycle is not yet reproduced (static frame 0 is used). The water surface's vertical undulation is reproduced separately: when `bAnimateWaterSurface` is set (and a textured terrain surface is in use), water terrain quads are committed to their own `TerrainMeshComponent` section that is drawn with the `M_SimCopterWater` material. That material displaces the vertices in the vertex shader (World Position Offset) with a moving sum-of-sines wave and computes analytic per-pixel wave normals, so the animation is GPU-only (no per-frame CPU work, no vertex-buffer re-upload) and runs identically in the editor viewport and in game. A per-vertex weight baked into vertex-color R pins shoreline verts (0 = touches a land/road/building tile, 1 = open water) so the sea stays welded to the static land surface with no gaps. `M_SimCopterWater` is authored by `Tools/Unreal/CreateSimCopterMaterials.py`; the actor feeds it the same `T_TerrainLow` (TILED1) texture the terrain samples plus the `WaveAmplitude`/`WaveLength`/`WaveSpeed` parameters exposed on the actor.

`FUN_004abce0` also **conditions the tmap height grid itself** before anything reads it (decoded 2026-07-02, full decompile at `Docs/scratchpad/ghidra/out_tmap_build.txt`; remake port `BuildConditionedTerrainCornerSamples` in `SimCity2000CityActor.cpp`):
1. Seeding: tile centers store `(height step + 1) * 0x20` (ALTM bits 0-4, replaced by water alt bits 5-9 when higher and `XTER >= 0x10`; tunnels `XTER 0x0d/0x0e` add one step); in-between grid points average their neighbors, and the final 128x128 grid keeps the averaged points (one corner per tile edge crossing). Global min/max height (`DAT_005bde70`/`DAT_005ce090`, padded by `-0x32`/`+100`) are captured here for the type-grid bands.
2. Water dip: plain-ground water tiles (`XTER > 0x0f`, XBLD not trees/parks/`0xf8`) at even/even tile coordinates lower their origin corner by 8 (a quarter step) - subtle water waviness.
3. **Auto-flatten**: every building tile (XBLD `>= 0x70`) and every flat network tile (XBLD `0x1d`, `0x1e`, `0x23..0x2d`, `0x32..0x3a` - roads, rails, intersections, crossings) forces **all four of its tmap corners** to the tile's own ALTM sample. This is why terrain never pokes through buildings or flat roads in the original.
4. **Raised-span ramps**: XBLD `0x3f..0x42` (raised span pieces, object ids `0x178..0x17b`) pull one corner pair a full step (`+0x20`) above the opposite edge: `0x3f` raises the two low-X corners to `corner(x+1, y) + 0x20`, `0x41` mirrors (+X side), `0x40` raises the low-Y pair to `corner(x+1, y+1) + 0x20`, `0x42` mirrors (+Y side). The terrain under the span becomes the exact wedge the model expects - no gaps under up/down sloped road pieces. The pass is one raster sweep (Y outer, X inner) whose ramp reads see earlier writes, so the port keeps the same in-order sweep.
5. Not conditioned: bridges `0x49..0x59`, highways/onramps `0x5d..0x6b`, tunnels, and natural tiles keep the seeded terrain (bridge decks float above it by design).
The remake threads the conditioned corner grid through `BuildTerrainTextureTypeGrid` (band averages), `BuildProceduralExtendedTerrain`, `AppendTerrainTile` (terrain quad corners), and `IsOriginalTerrainTileFlat` (bridge dispatch), all built once per `RebuildCity`.

The reusable headless exploration script is tracked at `Tools/Ghidra/ReverseExplore.java`.

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

## Player Helicopter Meshes And Rotor Animation

The flyable helicopters are ordinary Maxis mesh objects in the `GEO` packs - there is no
skeletal/keyframe animation in the mesh format (the `OBJX` header bytes at `+112/+116` that
an early reader guessed were an animation count/pointer read into the vertex block and hold
garbage for nearly every object). Each helicopter is split into a **fuselage object plus a
separate main-rotor object**; the original engine reproduces the rotor "animation" purely by
spinning the rotor object about its mast each frame. A single shared `ROTORTL` tail-rotor
object is reused by every type.

Every helicopter body and rotor is fully palette-coloured (no face type 13/18 textured
faces), so the city texture-atlas pipeline is not needed to render them - vertex colours from
the pack `CMAP` are sufficient.

Rotor objects mix two face types: the thin opaque blades are face type `15`/`19`, while the
spinning **disc** is **face type `11`** (the wide `verts=14` polygon, palette teal on the main
rotors and red on `ROTORTL`). The original game draws face type 11 as an alpha-blended disc -
the near-translucent grey blur of the spinning blades - over the opaque blade geometry. The
remake reproduces this: `FMaxisProceduralMeshBuilder::IsTranslucentFaceType` classifies face
type 11, `BuildPaletteColoredSections` splits a rotor into an opaque blade section and a
translucent disc section, and the pawn draws the disc section with `M_SimCopterRotorDisc`
(an Unlit + Translucent grey material with `DiscColor`/`DiscOpacity` parameters, authored by
`Tools/Unreal/CreateSimCopterMaterials.py`). The solid disc geometry can still serve as a
rotor hitbox later without looking solid.

Face type 11 is reused on **car headlights**: every `AUTO*`/`CARPOLIC`/`CARAMBUL` object has a
short row of `ftype==11` quads at the nose (all `Y=0`, low `Z`, marching forward in `+X` well
past the body - e.g. `AUTO`'s body ends at Unreal X~183 but its four beam cards reach X375),
i.e. the alpha-blended headlight beams. The ground-agent vehicle build drops these (routing
them into a discarded translucent section) and substitutes real spotlights, since rendering
them opaque made the cars look like they had grey/blue blocks bolted to the front. By contrast
SimCopter's **people are not in the GEO packs at all** (no person/cop/pedestrian object across
the 400 entries - only `CARPOLIC`, a police car); the crowds come from `privanim.df` plus the
flat `BMP/PEOPLE1.BMP` sprite, so the remake stands in a procedural low-poly box body until
the `privanim.df` figures are decoded.

`heli.twk` type name -> GEO table names (validated against the three reference packs):

| Type (`heli.twk`) | Body | Main rotor |
| --- | --- | --- |
| `Jet Ranger` | `JETRANG` | `JETRROTR` |
| `Hughes 500` | `HUGH500` | `H500ROTR` |
| `Bell 212` | `BELL212` | `BELLROTR` |
| `Schwiezer 300` | `SCWZR300` | `SCWZROTR` |
| `Apache` | `APACHE` | `APACROTR` |
| `Agusta` | `AGUSTA` | `AGUSROTR` |
| `Dauphin` | `DAUPHIN` | `DAUPROTR` |
| `MDEXPLORER` | `MDEXPLRR` | `MDEXROTR` |
| `MD520` | `MD520` | `MD52ROTR` |

Coordinate facts used by the remake:

- Bodies face +Z in Maxis space, which maps to Unreal +X (the pawn's forward axis) through
  `ConvertMaxisVertexToUnreal`, so no extra yaw is applied (unlike the 180-degree city yaw).
- Main rotor objects are authored centred on the mast (local X = Y ~= 0, elevated in Z), so the
  pawn spins the rotor component about its own vertical axis at the body origin.
- `ROTORTL` is authored centred on its own hub and lies in the Unreal X-Z plane (thin in Y), so
  it is placed near the rear of the fuselage and spun about the lateral (Y) axis.
- The meshes are decoded at the same `OriginalMeshUnitsPerCentimeter` (`2621.44`) as the city and
  rendered at a `0.25` display scale so the helicopter matches the city's shrunk original-mesh
  scale (`TileSize 400 / source tile 1600`).

`ASimCopterHelicopterPawn` loads these through `FMaxisMeshLibrary::FindObjectByTableName` and
builds them with `FMaxisProceduralMeshBuilder` (a reusable, untextured/vertex-coloured section
builder with centroid-based outward normals). Shadow objects (`SHADJETR`, `SHADRJET`, ...),
the rope/bucket props (`BUCKET`, `HARNESS`, `BRACKET`, `CANNON`), and per-type tail-rotor mount
offsets remain follow-ups.

## Pedestrians, Traffic, And Ground Start

Status on 2026-06-24: first gameplay pass in progress, with the first decompiled traffic
route table wired into runtime movement.

The original game splits ground population data across two asset families:

- Vehicle meshes are regular `GEO/*.MAX` objects and can use the same `FMaxisMeshLibrary` plus
  `FMaxisProceduralMeshBuilder` path as helicopters. Candidate moving-car objects found in
  `SIM3D2.MAX` are `AUTO`, `AUTO2`, `AUTO3`, `AUTO4`, `AUTO5`, `AUTO6`, `CARFIRET`,
  `CARPOLIC`, `CARROBBR`, and `CARAMBUL`.
- Traffic vehicles are saved under a `TRAN` chunk (`0x5452414e`). The runtime object owns three
  render subobjects and stores current tile at offsets `+0x35/+0x39`, target tile at
  `+0x55/+0x59`, and direction bits at `+0x11`. `FUN_004b5290` is the route-step table: it maps
  road/bridge XBLD ids to the next tile and direction bits, then rejects the step if the target is
  not another TRAN-traversable road tile. The remake now ports this table for ids `0x2c..0x3e`,
  `0x45..0x48`, `0x4d..0x4e`, and `0x5a..0x5b`.
- Pedestrian behavior lives in `X/people.df`, which contains readable behavior strings such as
  `Walk-30`, `Idle-40`, `Run a base`, `Back to car`, `tend to run away from the heli`, `Wave`,
  `Pani`, `Dead`, and `NoMo`. The animation/resource payload is in `X/privanim.df`; local probes
  found repeated `ARPP` records, `DgRn` records, and animation labels including `NoMo`, `DgRn`,
  `DgSt`, `Wave`, `Pani`, and `Dead`.
- The first visible pedestrian/player art path uses the original `BMP/PEOPLE1.BMP`. This file is a
  normal 8-bit Windows BMP, not a Maxis composite bitmap: `324x99`, arranged as `12` character
  columns by `3` animation rows of `27x33` pixels. Palette index `254` is the cyan transparency key.
  `FMaxisWindowsBitmapReader` decodes it into RGBA, and runtime people render cropped original
  frames through `M_SimCopterSpriteTexture`. Column `0` has a different non-cyan backdrop and is
  reserved for now until the original draw flags are decoded.

Relevant executable behavior found so far:

- `FUN_004b10a0` is an original ground-agent placement helper. It searches outward in a spiral
  from a requested tile until it finds a valid terrain/cell target, links the agent into the
  `DAT_005d9200` scene cell list, and computes world position as
  `x * 0x400000 - 0x1fe00000`, `(height + 1) * 0x200000`, and
  `y * -0x400000 + 0x1fe00000`.
- The helper rejects open water using the terrain type grid `DAT_005bde80` (`5..9` are treated as
  water-like in the decompiled branch) except for special water-capable objects. The remake can
  mirror that at gameplay scale by deriving traversability from the SC2 city tiles plus the
  reconstructed terrain type grid.
- `people.df` behavior strings show the original simulation already treated people as independent
  agents that can idle, walk, panic, return to cars, run from the helicopter/spotlight, be picked
  up, and be decommissioned when out of range.
- `FUN_004c2f30` initializes the people runtime by loading `People.df`, seeding the person table,
  and setting `DAT_00506444` to a sentinel/default person object. `FUN_004c4190` is the main spawn
  configurator: it finds a free slot among 500 person objects, handles tile/object/world spawn
  modes, marks the person active, and defaults missing animation state to `NoMo`.
- `FUN_004ceab0` loads `PrivAnim.df`, registers animation/resource record handlers, and builds the
  private animation tables. **The container is now truly decoded** (2026-07-01 pass; see
  `Docs/OriginalGameFileFormats.md` "Exact Container Spec" + the rewritten
  `Tools/privanim_extract.py`; validated 437/437 chunks): 21 named figures (`pilot`, `Child`,
  `Kopp`, `Elvis`, `Nessie`, ... `Woman`), per figure an `ARCP` skeleton tree (29-88 named parts,
  parent links, f32 dimensions) and an `ARLU` map from the 18 behavior anim mnemonics (`1Wal`,
  `Dead`, `Wave`, ...) to clip names, and per clip `ARPP` = frames x parts 8-byte records - each
  **one 3D line segment (two s8 xyz endpoints)** of the body wireframe for that frame. The earlier
  2026-06-26 claims (4-byte coord streams, z-bit7 split, 75-clip inheritance tree, 12-segment
  figure) were artifacts of a wrong directory decode and are superseded. Still unread: how the
  scene engine fleshes segments into filled flat-shaded polygons (ARCP type byte + dims + palette).

Implemented first playable remake pass:

1. Add reusable ground-agent actors that render palette-coloured original meshes through
   `UProceduralMeshComponent`, expose a lightweight "janky animation" tick, and move by following
   grid waypoints rather than full navmesh pathing.
2. Add a city-aware population spawner that loads the active `.sc2` file, derives road and
   pedestrian candidate tiles, and spawns/despawns agents around the player. The limits are
   intentionally expanded beyond the 1996 game: higher active counts and a larger radius are
   editor-tunable defaults instead of hard-coded tiny pools.
3. Use original `GEO` vehicle meshes for traffic and original `PEOPLE1.BMP` sprite frames for
   pedestrians and the on-foot player. Active population actors fail closed if the expected original
   asset cannot load rather than silently showing cube stand-ins.
4. Change the default game flow so the player starts as an on-foot pawn near a parked helicopter.
   `F` enters the helicopter when nearby; holding `Space` starts the engine; holding `Ctrl` while
   grounded shuts it down; once shut down and grounded, `F` exits back to the ground.
5. Keep all old helicopter flight and mesh-loading code intact, but gate flight authority on engine
   state so a parked helicopter can exist as an in-world vehicle rather than the default possessed
   pawn.

Remaining hard pass:

1. **NEXT: clean decode of the people logic (behavior VM + spawn rules) so pedestrians spawn
   correctly and carry all the original behaviors.** This is now the top priority. The entry points
   are known: spawn config `FUN_004c4190` (free-slot search among 500 person objects, tile/object/world
   spawn modes, per-state animation defaulting); per-frame driver `FUN_004c6450` (runs behavior +
   advances the animation frame, LOD-gated by `DAT_0058dc26`); and the **88-handler behavior bytecode
   VM** dispatched by `(&DAT_0058ef78)[op]` via `FUN_004ccf20`, executed by walking each agent's
   `BHAV` ("VAHB", `0x42484156`) resource with the IFF walker `FUN_004ce7b0` (the same walker proven
   this pass to be a *behavior interpreter*, not a geometry drawer; `BHAV` accessor `FUN_004d0100`).
   Decode needed: the 88 opcode handlers + bytecode grammar, the per-state table (`DAT_0058de80`
   anim ids, loop flags), state-transition triggers (idle/walk/panic/return-to-car/flee-spotlight/
   pickup/decommission), and the spawn weighting per city (`career.twk`). Goal: replace the remake's
   placeholder waypoint wandering with faithful state machines + spawn rules.
2. `PrivAnim.df` decode is **done and file-validated** (2026-07-01; `Docs/OriginalGameFileFormats.md`
   "Exact Container Spec"; rewritten `Tools/privanim_extract.py` + `Tools/privanim_to_gltf.py`):
   21 named figures, per-figure skeleton trees, the 18-anim clip map, and per-frame per-part 3D
   line segments (walk cycles verified visually). Remaining refinement: the scene-engine flesh/
   fill of segments (ARCP type byte + dimension floats + palette color source) and the
   `DAT_0058de80` anim-id -> clip-name binding hop.
3. Finish porting vehicle spawn validation from `FUN_004b74a0`/`FUN_004b7890`, including the original
   multi-subobject follower update routines (`FUN_004b6a80`, `FUN_004b6c40`, `FUN_004b6f80`,
   `FUN_004b7020`) for car spacing/lane offsets.

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
- The common city atlas pages observed so far are `2`, `13`, `20`, `39`, and `40`. Page `20` is the `SKY.BMP` image `4` exception, not `SIM3D.BMP` image `20`; page `13` is also reused as the high-terrain page for type codes `0x40..0x7f`.
- Raw UV values are fixed-point values scaled by `65536`. Values often extend outside `0..1`, so the renderer keeps those repeat coordinates. In baked page-atlas mode, UV0 stores the repeating in-cell coordinates while UV1 stores the 8x8 cell column/row for `M_SimCopterCityAtlas`; in fallback mode the same UV0 values sample transient 32x32 cell textures with wrap addressing.
- Maxis raw V coordinates use a bottom-left origin. `FMaxisMeshReader::ConvertMaxisUVToUnreal` flips V for Unreal's top-left texture sampling while preserving out-of-range repeat values.
- The preferred city texture path is a local bake generated by `Tools/Unreal/BakeCityAtlas.py`. It decodes the user-provided original art into gitignored assets under `/Game/Generated/CityAtlas`: `MI_CityPage_<id>` instances parented to `M_SimCopterCityAtlas`, `MI_CityImage_<id>` instances for rare face type 13 direct images, and `MI_TerrainLow`/`MI_TerrainHigh` terrain instances parented to `M_SimCopterLitTexture`. Original game art remains outside source control, matching `/Reference`, while committed code/materials know how to load the generated assets when present.
- The fallback city texture path still decodes `SIM3D.BMP`, `SKY.BMP`, and `TILED1.BMP` at rebuild time into transient `UTexture2D` objects. This keeps the renderer usable before a local bake exists, but the baked path avoids per-load slicing and fixes PIE material loss from transient texture/material bindings.
- City terrain and decoded original mesh sections are built into transient `UStaticMesh` objects so Unreal renders them through the normal static-mesh path and builds Nanite data. This is now the only mesh path: the earlier `UProceduralMeshComponent` fallback (`TerrainMeshComponent`/`OriginalMeshComponent`), the `bUseNaniteCompatibleStaticMeshes` gate, and the never-populated `TerrainInstances` placeholder HISM were removed because the static-mesh path is the committed renderer and the dead fallbacks only added confusion to a version-controlled project. `bEnableNaniteForGeneratedStaticMeshes` still toggles Nanite on the generated static meshes. The `WaterInstances`/`RoadInstances`/`BuildingInstances` HISMs remain as colored-cube placeholders for tiles whose original mesh is missing.
- Generated mesh face normals are oriented outward from each object's centroid. The raw Maxis winding (after `ConvertMaxisVertexToUnreal` plus the 180-degree city yaw) yields inward-facing normals for exterior faces - the same reason the terrain quad uses a reversed cross product - which left buildings and roads unlit by both the directional light and Lumen's surface cache (dark everywhere except where Lumen's screen trace faded out near the screen edges). Orienting each face normal away from the object centroid is winding-agnostic and fixes the lighting for boxy buildings, flat road plates, and cars alike.
- Unreal 5.8's Nanite builder limits one mesh to 64 sections/material slots (`Nanite::MaxSectionArraySize`). The original city mesh can exceed that because each source texture page/cell becomes a material section, so the actor splits it into multiple sibling generated static mesh components before enabling Nanite.
- `TILED1.BMP` image `0` is an 8x8 atlas. Exported cell diagnostics show bottom-origin cells `0..9` are water and cells `32+` are land/grass/sand. Direct `XTER & 0x3f` made flat land (`XTER=0`) sample water; terrain now uses the decompiled `DAT_005bde80` type grid instead. Type codes `0x40..0x7f` render through a second terrain mesh section using `SIM3D.BMP` image `13` as page `0x0d`; this includes the random detail codes `0x70..0x7e`, which select cells `0x30..0x3e` within that atlas page.
- SimCopter loads `TILED1.BMP` through `FUN_0046ce50`, ignores the row table after validating the image, and passes the raw 256x256 byte buffer to `FUN_0046cd50`. The tile pointer table uses `row = index >> 3`, `col = index & 7`, and `offset = (row * 0x100 + col) * 0x20`; terrain face setup (`FUN_00478960`) maps pixel-space UVs from `(0.5,0.5)` to `(31.5,31.5)` with the raw texture's first row on the top edge of the ground quad. The remake's terrain UV helper compensates for `FMaxisTextureReader`'s decoded top-down image layout so transition cells are not vertically flipped.
- The current terrain pass creates one procedural quad per SC2 tile with interpolated corner heights. Unreal-facing terrain winding is `0,2,1` and `0,3,2`, while supplied normals remain upward for lighting. Mixing one reversed and one unreversed triangle made one half of each tile render differently and caused the visible triangular texture artifact.
- Exact use of `ALTM` bits `10..14` outside the height-map and terrain-type passes is still a follow-up.

Validated on `CityRender.umap` construction and PIE startup on 2026-06-24 after the local atlas bake:

```text
terrain=102400
extensionTerrain=86016
water=0
extensionWater=0
roads=17
buildings=0
originalMeshTiles=4592
missingOriginalMeshTiles=17
originalTriangles=424522
texturedTriangles=147348
originalTextures=75
```

Next asset tasks:

1. Verify current building UV orientation and terrain tile selection visually against the original game.
2. Decode the remaining terrain-map perturbation passes around `FUN_004ad7c0` and `FUN_004ae530`, then decide whether runtime gameplay should switch from deterministic editor detail draws to the original clock-seeded MSVCRT stream.
3. Replace or refine the old placeholder water/road fallback surfaces after terrain/road atlas behavior is confirmed.
4. Add a diagnostics view listing missing mesh mappings, footprint decisions, and texture references.
5. Split procedural city geometry into deterministic streaming/cache chunks for larger cities.

Mesh format sources used:

- CahootsMalone Maxis mesh format documentation: https://github.com/CahootsMalone/maxis-mesh-stuff/blob/master/Info/Maxis-Mesh-Format.md
- CahootsMalone mesh-to-SimCity building mapping notes: https://github.com/CahootsMalone/maxis-mesh-stuff/blob/master/Info/Mapping%20from%20SimCopter%20meshes%20to%20SimCity%202000%20buildings.csv
- Original repository overview for mesh tools/viewer: https://github.com/CahootsMalone/maxis-mesh-stuff

## Original Executable Quick Facts

`Reference/SimCopterOriginalGame/SimCopter.exe` has been inspected with local disassembly and a Ghidra headless decompilation pass. The reusable Ghidra post-script is tracked at `Tools/Ghidra/DecompileAddresses.java`.

Observed facts:

- PE32 Windows GUI executable.
- x86/i386, little-endian.
- Image base `0x00400000`.
- Reported compile timestamp: `Sun Dec 8 16:16:10 1996 UTC-5`.
- Imports include `DDRAW.dll`, `DSOUND.dll`, `WINMM.dll`, `MSACM32.dll`, `smackw32.DLL`, `GDI32.dll`, `USER32.dll`, `KERNEL32.dll`, `ADVAPI32.dll`, `comdlg32.dll`, and `VERSION.dll`.
- Strings include `FORM`, `SCDH`, `ALTM`, `XTER`, `XBLD`, `SIM3D1.MAX`, `SIM3D2.MAX`, `SIM3D3.MAX`, and `sim3d.twk`, supporting the city/asset pipeline being implemented first.
- Additional local string/analysis probes found `TILED1.BMP`, `TileCnt`, `AltMap`, `RoadTiles`, and `SIM3D.BMP` references in the executable. These support the current focus on terrain atlas, altitude map, and road/building tile behavior before relying on broader online notes.

## Decoded Runtime Systems And Mission Data (2026-06-26 pass)

This pass decoded the previously-blocked areas. Full detail lives in the three new docs linked above; the headline discoveries:

- **Missions and career** (`Docs/MissionsAndTweakSystem.md`). `sim3d.twk` is the master tweak tree (`Class` -> `Redirect`/inline). The career is 30 cities (`career.twk`, `City0..City29`), each with seven mission-type spawn weights (Fire/Crime/Rescue/Riot/Traffic/MedEvac/Transport), difficulty, day/night, a `Points Needed` win target (400 -> 3000), and `$ Earned` (500 -> 100). The nine mission types are defined inline in `sim3d.twk` `[Missions]` with full money/points scoring (Fire Miss alone has 20 controls). `fire.twk`, `figure.twk`, `camera.twk`, and `automssn.twk` are also decoded. The `fxpt` tweak data type is fixed-point (e.g. `MaxBank 426.7` = 42.67 deg).
- **`people.df` = the "global behavior file"** (`Docs/OriginalGameFileFormats.md`). A type-`0xc` Maxis DF container (length-prefixed name + 2-entry `RSRC` directory + opaque payload). Runtime: 500 person slots, ~20 states with hardcoded animation/sequence tables, a 256-entry `XBLD`->behavior-class map (`DAT_0058e800`), an 88-handler computed-goto **behavior VM** (`DAT_0058ef78` -> `LAB_004c84e0`+`0x20`n), an 8-direction sin/cos facing table, and a dedicated 16-bit LFSR PRNG (`FUN_004ce9d0`, tap `0x1bf5`) separate from MSVCRT `rand()`. The person record and seven spawn modes are mapped.
- **`privanim.df`** (`Docs/OriginalGameFileFormats.md`) is the articulated-figure pack: a big-endian IFF "Doug" container with sections `BODC` (body geometry), `ANIP` (a 76-clip animation inheritance tree of `ARPP` pose records), `ARCP`/`ARLU` (12-segment articulation skeleton + lookup), and `SPR#` (sprites). Pedestrian render chain decoded: person state `+0x148` -> `DAT_0058de80[state]` figure id -> `FUN_004ce630` builds a 12-segment figure -> per-frame pose draw, with `PEOPLE1.BMP` at distance. Walkable via `Tools/privanim_probe.py`. Remaining: exact `BODC`/`ARCP`/`ARPP` field semantics + skinning math.
- **Original `TRAN` traffic** (`Docs/OriginalRuntimeBehavior.md`). Per-tile coin-flip steering (`FUN_004b5290`) over a road graph of `0x38`-byte intersections + 3-byte road tiles (`FUN_00495700`), with hospital/police/fire service registries that feed missions. Cars wander (no pathfinding); the remake's graph walk is a faithful feel, not a byte match.
- **`ALTM` slope bits 10..14** (mask `0x7c00`) are a per-tile slope code; `== 0` means flat. The base/secondary altitude decode (`FUN_004abc20`, bits 0-4 / 5-9) is confirmed.
- **Helicopter flight model** (`Docs/OriginalRuntimeBehavior.md`). `heli.twk`-tuned, per type a `0x5c`-byte tuning block bound by `FUN_00489e20`. Master tick `FUN_00484d20` runs the state machine and scene-graph relink; attitude integrator `FUN_00486a30` is input -> clamp to tuning max -> first-order lag (rate from PitchRate) -> integrate heading (full circle = `0xe100000`).

## Flight Tuning And Interaction

Milestone 4 uses the original plain-text tweak files as behavioral data, not copied executable code. The first pass reads `tweak/heli.twk` directly at runtime through `FSimCopterTweakReader`.

Important `heli.twk` facts:

- The file contains sections for the flyable helicopter types listed by `sim3d.twk`: `Jet Ranger`, `Hughes 500`, `Apache`, `Bell 212`, `Schwiezer 300`, `Agusta`, `Dauphin`, `MDEXPLORER`, and `MD520`.
- Each helicopter section is a group of numbered controls with separate label/value/data-type keys. The implementation resolves controls by label prefix rather than trusting `NumCtrl`, because the shipped helicopter sections declare `NumCtrl=14` while containing controls `0..15`.
- Labels that explicitly say `(10 = 1 deg)` are treated as tenths of a degree. This covers max bank, slide, pitch, and landing attitude limits. Rate values are interpreted as gameplay tuning rates and converted into modern frame-rate-independent interpolation/acceleration.
- `Heli Landing`, `Heli Ropestuff`, and `Heli Damage` provide the baseline limits for landing attitude/speed, bucket fill/dump behavior, rope load/tension, fire altitude, collision damage, repair, and fuel distance values.

`camera.twk` only exposes three old chase-camera scalars. The remake intentionally does not recreate that camera behavior. `ASimCopterHelicopterPawn` uses a new Unreal spring-arm camera with camera collision/lag and three practical modes: chase, orbit, and rescue/down-look.

Current implementation notes:

- `ASimCopterHelicopterPawn` is a custom `APawn` with a capsule root and swept movement rather than a physics-simulated rigid body. This keeps the original arcade helicopter feel and avoids frame-rate-dependent physics drift.
- Movement is sub-stepped to a maximum of 1/60 second per simulation step, so low and high frame rates feed the same tuning path.
- Landing uses downward probes, attitude limits, horizontal/vertical speed limits, and generated city collision. Collision damage uses blocking hit speed against the contacted surface.
- Rope/bucket behavior is implemented as runtime state with visible rope/bucket components. Bucket water can fill from named water collision or a configurable waterline fallback until full mission water/fire semantics exist.
- `ASimCity2000CityActor` now exposes collision toggles for terrain, original mesh geometry, and placeholder geometry. The default is collision-enabled so flight probes and capsule sweeps interact with the generated city immediately.

## Implementation Notes

Current code has two layers:

- `FSimCity2000Reader`: pure parser/decoder for `.sc2` files.
- `FMaxisMeshReader`: pure parser/decoder for Maxis `.MAX` mesh packs.
- `FMaxisMeshLibrary`: original-game asset index that maps SC2 tile ids to decoded Maxis mesh objects.
- `FMaxisTextureReader`: pure parser/decoder for Maxis composite bitmap texture files such as `SIM3D.BMP`.
- `FMaxisWindowsBitmapReader`: pure parser/decoder for normal 8-bit Windows BMP assets such as `PEOPLE1.BMP`.
- `FSimCopterTweakReader`: pure parser for SimCopter `.twk` tuning files.
- `FSimCopterPopulationSprite`: shared helper for slicing the original PEOPLE1 sheet into runtime procedural sprite quads.
- `ASimCity2000CityActor`: editor/runtime actor that renders a decoded city with procedural terrain, optional fallback water plates, and original SimCopter road/building mesh geometry. Textured mesh faces prefer locally baked material instances from `/Game/Generated/CityAtlas` (`SIM3D.BMP` page atlases, the `SKY.BMP` page-20 exception, direct face type 13 images, and `TILED1.BMP`/page-13 terrain); when those generated assets are absent, the actor falls back to decoding transient textures from the user-provided originals at rebuild time. Placeholder road/building instances remain as a fallback for missing mappings.
- `AMaxisMeshDebugActor`: editor/runtime actor that renders one decoded `.MAX` mesh for inspection.
- `ASimCopterHelicopterPawn`: runtime pawn for helicopter flight, modern camera modes, landing/collision probes, fuel/damage state, rope/bucket behavior, and searchlight controls.
- `ASimCopterGroundAgent` and `ASimCopterTrafficSystemActor`: runtime population actors for original vehicle meshes, PEOPLE1 pedestrian sprites, and the first ported TRAN road-step table.
- `ASimCopterOnFootPawn`: ground-start pawn that uses the original PEOPLE1 sprite path and handles `F` helicopter entry.

The placeholder renderer is now fallback scaffolding. It still proves city parsing, orientation, tile height, and high-level tile classification, while original terrain textures and road/building geometry can be rendered directly from user-provided SimCopter assets.

## Fire and water effect rendering (2026-07-05)

Decoded from `SimCopter.exe` (outputs: `Docs/scratchpad/ghidra/out_fire_water_effects.txt`,
`out_effect_pool_init.txt`, `out_effect_sprite_creator.txt`, alongside `out_m5_firecreate.txt`
and `out_heli_landing.txt`). The mission/fire simulation (`FSimCopterMissionSystem`) was already
ported; this pass added the rendering and the helicopter wiring.

- **Fire is 3D GEO geometry, not sprites.** Building/car flame = `FIREPTS` (`Object.Header.Id`
  `0x120`) — importantly a cloud of **22 single-vertex point sprites** (palette-coloured "fire
  points"), so it must be billboarded, not triangulated. Car fire = `CARFIRET` (`0x11c`),
  smoke/debris = `DEBRIS1..3` (`0x149..0x14b`) + `SMOKE` (`0x148`), burnt tiles = `BURNTREE`
  (`0x14e`) / `GRUBBLE1..4` (`0x14f..0x152`). Resolve via `FMaxisMeshLibrary::FindObjectByObjectId`.
- **Effect particles (bucket drips, douse splash, rotor wash) are flat palette-coloured,
  camera-facing cards** (Maxis face type `0x17`) built procedurally by `FUN_0046edb0(shape,0x17)`
  and moved by a velocity + rise over a short life (creator `FUN_0048e0b0`, tile-splat
  `FUN_004af220`, splash column `FUN_004af100`, updater `FUN_004af3b0`/`FUN_0048ed00`). There is
  **no sprite atlas** — the colour is a SIM3D palette index. The remake gives each card a
  procedural radial soft-alpha so the flat quad reads as a soft puff (`M_SimCopterParticleFX`).
- **Wind-kickback over water = the rotor wash `FUN_004881b0`:** low over a surface + above a
  minimum altitude, scatter class-8 wash cards with a random-yaw matrix (spray over water, dust
  over land). Ported to `ASimCopterHelicopterPawn::UpdateRotorWash`.
- Renderers: `USimCopterFireRenderComponent` (flames, on the mission actor, polling
  `MissionSystem.GetFlames()`) and `USimCopterParticleFXComponent` (water/dust cards, on the heli).
  `FSimCopterMissionSystem::DouseAt`/`DouseAtTile` and the bucket dump wiring were added here.
