# Original Game File Code Walkthrough

This document is the code-level companion to `Docs/ReverseEngineering.md`. The reverse-engineering notes explain what has been discovered about the original game. This file explains how the remake code turns those discoveries into parsers, lookup tables, and renderable data.

The intent is deliberately conservative: keep the original data model visible, validate every byte range before trusting it, and avoid "helpful" normalization that would erase SimCopter quirks we may need for mission and gameplay fidelity later.

## Source Map

Primary format readers:

- `Public/Formats/SimCity2000Reader.h`
- `Private/Formats/SimCity2000Reader.cpp`
- `Public/Formats/MaxisMeshReader.h`
- `Private/Formats/MaxisMeshReader.cpp`
- `Public/Formats/MaxisTextureReader.h`
- `Private/Formats/MaxisTextureReader.cpp`
- `Public/Formats/MaxisWindowsBitmapReader.h`
- `Private/Formats/MaxisWindowsBitmapReader.cpp`
- `Public/Formats/SimCopterTweakReader.h`
- `Private/Formats/SimCopterTweakReader.cpp`
- `Public/Formats/MaxisMeshLibrary.h`
- `Private/Formats/MaxisMeshLibrary.cpp`
- `Public/Formats/MaxisProceduralMeshBuilder.h`
- `Private/Formats/MaxisProceduralMeshBuilder.cpp`

Validation tests:

- `Private/Tests/SimCity2000ReaderTests.cpp`
- `Private/Tests/MaxisMeshReaderTests.cpp`
- `Private/Tests/MaxisProceduralMeshBuilderTests.cpp`
- `Private/Tests/SimCopterTweakReaderTests.cpp`

Read-only probes:

- `Tools/sc2_probe.py`
- `Tools/maxis_mesh_probe.py`
- `Tools/maxis_texture_probe.py`

## SimCity2000Reader

`FSimCity2000Reader` is the pure parser for SimCity 2000 `.sc2` city files. SimCopter consumes those city files directly, so this reader is the first layer of original mission/city compatibility.

### Data Structures

`FSimCity2000Chunk` preserves the raw IFF chunk identity:

- `Id` is the four-character chunk name such as `ALTM`, `XBLD`, or `XTER`.
- `StoredSize` is the byte count as written in the IFF file.
- `DecodedSize` is the byte count after SC2 RLE decoding, or the same as `StoredSize` for chunks stored raw.
- `bStoredUncompressed` records the storage path used for this chunk.
- `Data` is always the usable decoded payload handed to later code.

`FSimCity2000Tile` is the per-tile view after the decoded layers are merged by tile index:

- `RawAltitude` keeps the original big-endian 16-bit `ALTM` word.
- `Altitude` is `RawAltitude` bits `0..4`, the base ground height.
- `SecondaryAltitude` is bits `5..9`, used by the original executable for water/raised height when it exceeds base altitude and `XTER > 0x0f`.
- `Slope` is bits `10..14`, still preserved for later fidelity passes.
- `bWater` comes from `XTER > 0x0f`, not from an `ALTM` bit. This matters because an earlier interpretation mistook an altitude bit for water.
- `Terrain`, `Building`, `Zone`, `Underground`, `Text`, and `BitFlags` mirror `XTER`, `XBLD`, `XZON`, `XUND`, `XTXT`, and `XBIT`.

`FSimCity2000City` owns the decoded city:

- `MapSize` is `128`, matching the shipped `.sc2` tile grid.
- `TileCount` is `128 * 128`.
- `SourceFile` is kept for diagnostics and fallback city naming.
- `CityName`, `Rotation`, and `WaterLevel` are metadata extracted from `CNAM` and `MISC`.
- `Chunks` keeps all decoded chunks for future fields that are not interpreted yet.
- `Tiles` is the merged tile grid used by rendering, traffic, and gameplay.

### Helper Functions

`ReadUInt16BE`, `ReadUInt32BE`, and `ReadInt32BE` read SimCity's big-endian integers. The C++ code expands each byte explicitly instead of casting, so it works regardless of platform alignment and host endian order.

`ReadFourCC` copies four bytes into a five-byte null-terminated buffer, then converts it into an Unreal `FString`. Every chunk and file header comparison goes through this helper.

`ReadPrintableAscii` walks bytes until null, control, or non-ASCII data. `CNAM` often begins with `0x1f`, so the reader starts at offset `1` and does not trust a Pascal-style length byte as authoritative.

`FindChunkData` searches `City.Chunks` for the first matching chunk id and returns the decoded payload. It returns `nullptr` when a chunk is absent so callers can distinguish optional metadata from required tile layers.

`RequireChunkData` wraps `FindChunkData` for required chunks. It rejects missing chunks and rejects decoded-size mismatches before a tile layer is indexed.

`PopulateCityMetadata` fills human-facing fields after chunks are decoded. It reads `CNAM` if present, falls back to the source filename, and reads rotation/water level from `MISC` only when the chunk is large enough to include the known offsets.

`PopulateCityTiles` is the layer merger. It requires `ALTM`, `XTER`, `XBLD`, `XZON`, `XUND`, `XTXT`, and `XBIT`, sets `City.Tiles` to 16,384 entries, then decodes each tile index in row-major file order. This is where `ALTM` bit fields and the `XTER > 0x0f` water rule become runtime fields.

### Public Functions

`FSimCity2000City::FindFirstChunk` performs a case-sensitive linear search. Multiple chunks are not expected for the core city layers, but preserving the "first chunk" behavior leaves the raw chunk list available for later investigation.

`FSimCity2000City::HasChunk` is a convenience wrapper for tests and diagnostics.

`FSimCity2000Reader::LoadCityFromFile` reads bytes with `FFileHelper::LoadFileToArray`, reports a filename-specific error on failure, then delegates to `LoadCityFromBytes`.

`FSimCity2000Reader::LoadCityFromBytes` does the full file parse:

1. Clears `OutCity` and records the source name.
2. Requires at least the 12-byte IFF header.
3. Requires `FORM` at byte `0` and `SCDH` at byte `8`.
4. Checks the big-endian declared size against `FileData.Num() - 8`.
5. Iterates chunk records from offset `12`.
6. Validates each chunk header and payload boundary before copying the stored payload.
7. Marks whether the chunk is stored uncompressed.
8. Either moves raw payload bytes or RLE-decodes them.
9. Verifies known decoded sizes.
10. Adds the decoded chunk to `OutCity.Chunks`.
11. Populates metadata and required tile fields.

`FSimCity2000Reader::DecodeRleChunk` implements the SC2 RLE stream:

- Control `0..127`: copy that many literal bytes from the stream.
- Control `129..255`: repeat the next byte `Control - 127` times.
- Control `128`: reserved and rejected.

The function checks literal runs, repeat runs, and expected decoded size. It appends into a caller-provided `TArray<uint8>` so tests can exercise the decoder without needing a full city file.

`FSimCity2000Reader::GetExpectedDecodedSize` is the central chunk-size table. Unknown chunk ids return `INDEX_NONE`, which means "parse but do not enforce a known decoded length." Known grid layers are fixed at 128x128, while aggregate maps use their original 64x64 or 32x32 sizes.

`FSimCity2000Reader::IsChunkStoredUncompressed` captures the exceptions to SC2 RLE. `ALTM` and `CNAM` are required for the current remake; `TEXT`, `SCEN`, `PICT`, and `TMPL` are retained because they are known uncompressed SC2-era chunks.

### Tests

`FSimCity2000RleTest` proves a mixed literal/repeat stream decodes as expected and that repeat count is `Control - 127`.

`FSimCity2000ReferenceCityTest` is optional because original game files are not committed. When `Reference/SimCopterOriginalGame/cities/Demo.sc2` exists, it validates chunk count, tile count, city name population, and representative altitude/water/slope decoding.

## MaxisMeshReader

`FMaxisMeshReader` parses the `GEO/*.MAX` packs used by SimCopter. These are Maxis Sim3D containers, not Autodesk scene files. The parser intentionally retains table entries, duplicate entries, object headers, raw vertices, face metadata, and raw UVs because the executable uses several numbering schemes.

### Data Structures

`FMaxisMeshGeometryEntry` models each 53-byte GEOM table row:

- `TableIndex` is the row number in the geometry table.
- `Name` is the table name, such as `RD29` or `JETRANG`.
- `ObjectOffset` points to the `OBJX` record.
- `ObjectCount`, `RenderedVertexCount`, `FaceCount`, and `UniqueVertexCount` preserve table metadata.

`FMaxisMeshDuplicateGeometryEntry` models the 36-byte duplicate table. Its `Id` is important because the original city builder resolves meshes by globally unique object id through a function now mirrored by `FMaxisMeshLibrary::FindObjectByObjectId`.

`FMaxisMeshVertex` stores raw Maxis integer coordinates. Maxis uses `X`, `Y`, `Z` with `Y` as up.

`FMaxisMeshFace` stores one `FACE` record:

- `Offset` and `SizeBytes` preserve the original record extent.
- `VertexCount`, `Flags`, `LightType`, `FaceInfo`, `FaceType`, `MaterialIndex`, and `TextureAtlasIndex` mirror the file.
- `VertexIndices` are 16-bit indices into the object vertex table.
- `RawUVs` are fixed-point pairs, left unnormalized until conversion.

`FMaxisMeshObjectHeader` stores the 124-byte `OBJX` header. `DeclaredSizeBytes` is kept separate from `TableSizeBytes` because `SIM3D2.MAX` object `EXPLODE` has a declared size that is shorter than the actual table extent.

`FMaxisMeshObject` combines a header, vertices, and faces.

`FMaxisMeshFile` is one decoded pack and keeps the shared `CMAP` palette, GEOM tables, duplicate table, and objects.

### Helper Functions

`CanRead` is the safety gate used before reading any span. It checks negative offsets, negative sizes, overflow, and end-of-buffer overrun with one expression.

`ReadUInt16LE`, `ReadUInt32LE`, and `ReadInt32LE` read little-endian mesh integers byte by byte.

`ReadFourCC` is the same pattern as the SC2 reader but for little-endian mesh containers.

`ReadFixedAsciiString` reads null-terminated text from a fixed-size field. The stack buffer is capped at 127 characters, and the loop stops at the first null byte.

`RequireFourCC` validates markers like `DIRC`, `CMAP`, `GEOM`, `OBJX`, and `FACE`, returning a precise byte-offset error when a marker is missing or truncated.

`ParseColorMap` requires `CMAP` at byte `28`, reads the CMAP section size and color data offset, validates a 256-color RGB table, then expands each palette color to `FColor(R,G,B,255)`.

`ParseGeometryTables` reads the GEOM header and both geometry tables. It validates the invariant `ObjectCount == GeometryEntryCount - 1`, because entry zero is a summary. It then parses 53-byte main entries and 36-byte duplicate entries into explicit structs.

`FindNextObjectOffset` walks the sorted unique object offsets and returns the first object start after the current object. That next offset is the authoritative boundary used to survive bad `OBJX` declared sizes.

`ParseFace` validates a `FACE` marker, reads the fixed 21-byte face header, calculates the minimum record size as `21 + VertexCount * 2 + VertexCount * 8`, rejects records that exceed the object boundary, then reads vertex indices and raw UVs.

`ParseObjects` builds full objects from GEOM entries `1..N`. For each object it validates `OBJX`, reads the header, establishes the table-derived object boundary, reads `VertexCount * 12` raw coordinate bytes, then parses exactly `FaceCount` face records.

### Public Functions

`FMaxisMeshFile::FindObjectByTableName` case-insensitively searches decoded objects by GEOM table name.

`FMaxisMeshFile::FindObjectById` searches decoded objects by object header id.

`FMaxisMeshReader::LoadMeshFileFromFile` reads the pack into memory and delegates to `LoadMeshFileFromBytes`.

`FMaxisMeshReader::LoadMeshFileFromBytes` is the top-level parser:

1. Clears the output struct and records source path plus file size.
2. Requires `DIRC` at byte `0`, `CMAP` at byte `12`, and `GEOM` at byte `20`.
3. Verifies the DIRC-declared file size.
4. Reads `ColorMapOffset` and `GeometryTableOffset`.
5. Requires the current known CMAP offset of `28`.
6. Parses CMAP, GEOM tables, then objects.

`FMaxisMeshReader::ConvertMaxisVertexToUnreal` maps Maxis `(X,Y,Z)` to Unreal `(Z,X,Y)` and converts mesh units to centimeters. The default scale uses `262,144` mesh units per meter, so one centimeter is `2,621.44` mesh units.

`FMaxisMeshReader::ConvertMaxisUVToUnreal` divides raw fixed-point UVs by `65536` and flips V because Maxis texture coordinates are bottom-origin while Unreal samples top-origin images. It intentionally does not clamp values; repeated coordinates outside `0..1` are real source data.

### Tests

`FMaxisMeshUvConversionTest` locks in bottom-left to top-left V conversion and out-of-range repeat preservation.

`FMaxisMeshReferencePacksTest` optionally validates the three shipped packs, object counts, palette count, and probe objects.

## MaxisTextureReader

`FMaxisTextureReader` parses Maxis composite bitmap files such as `SIM3D.BMP`, `SKY.BMP`, `SKYDARK.BMP`, and `TILED1.BMP`. These are not Windows BMPs even though they use the `.BMP` extension.

### Data Structures

`FMaxisTextureImage` is a decoded image with `Width`, `Height`, and RGBA `Pixels`.

`FMaxisCompositeBitmap` is a decoded composite file:

- `SourceFile` and `FileSize` are diagnostics.
- `ImageCount` and `ResolutionCount` mirror the header.
- `Images` stores decoded `FMaxisTextureImage` records.

### Helper Functions

`CanRead`, `ReadUInt32LE`, and `ReadInt32LE` are the same defensive byte helpers used by the mesh reader.

`ResolvePaletteColor` maps an 8-bit palette index through the mesh pack `CMAP`. If `bFirstPaletteColorTransparent` is true, palette index `0` gets alpha `0`; otherwise all valid palette entries get alpha `255`. Invalid palette indexes decode to transparent.

### Public Functions

`FMaxisCompositeBitmap::FindImage` returns a pointer only when the image index is valid.

`FMaxisTextureReader::LoadCompositeBitmapFromFile` reads the file and delegates to `LoadCompositeBitmapFromBytes`.

`FMaxisTextureReader::LoadCompositeBitmapFromBytes`:

1. Clears the output struct and records source/file size.
2. Requires a 256-color palette from a decoded mesh pack.
3. Rejects files shorter than the 16-byte composite header.
4. Rejects normal Windows BMP files beginning with `BM`.
5. Validates the declared file size.
6. Reads image count and resolution count, rejecting impossible values.
7. Skips the resolution table after validating that it fits.
8. For each image, reads width, height, and an expected-zero field.
9. Validates the per-row offset table and pixel data span.
10. Uses each row offset to read palette indexes.
11. Flips rows into Unreal top-down layout while expanding palette colors to RGBA.
12. Requires the parser cursor to end exactly at the file size.

`FMaxisTextureReader::ExtractAtlasTile` cuts a 32x32 cell out of an 8-column atlas by bottom-origin tile index. Tile `0` is bottom-left, tile `63` is top-right for a 256x256 page. This matches how SimCopter indexes terrain and city texture cells.

### Tests

`FMaxisTextureAtlasTileExtractionTest` locks in bottom-origin atlas addressing.

`FMaxisCompositeBitmapReferenceTextureTest` optionally loads `SIM3D.BMP` and `SKY.BMP`, proving key 256x256 pages exist and share the mesh palette.

## MaxisWindowsBitmapReader

`FMaxisWindowsBitmapReader` handles the normal Windows BMP files in the original install, especially `BMP/PEOPLE1.BMP`.

### Public Functions

`LoadPalettedBitmapFromFile` reads bytes and delegates to `LoadPalettedBitmapFromBytes`.

`LoadPalettedBitmapFromBytes`:

1. Clears `OutImage`.
2. Requires the 54-byte minimum BMP header.
3. Requires the `BM` signature.
4. Reads declared size, pixel offset, and DIB header size.
5. Validates an in-file DIB header.
6. Reads width, signed height, planes, bits per pixel, compression, and colors used.
7. Requires uncompressed 8-bit paletted BMP data.
8. Handles positive heights as bottom-up and negative heights as top-down.
9. Reads up to 256 BGRA palette entries and converts them to Unreal RGBA.
10. Applies `TransparentPaletteIndex` by setting that palette entry alpha to zero.
11. Reads padded rows using the 4-byte BMP stride rule.
12. Writes pixels into top-down `FMaxisTextureImage` order.

### Tests

`FMaxisWindowsBitmapPeopleReferenceTest` optionally validates `PEOPLE1.BMP` as `324x99` and confirms palette index `254` becomes transparent.

## SimCopterTweakReader

`FSimCopterTweakReader` parses original text tuning files from `tweak/*.twk`. These files are closer to INI files than binary resources, but they have their own loose habits.

### Data Structures

`FSimCopterTweakSection` stores the section `Name` and a normalized key/value map. Values remain strings until callers ask for typed conversion.

`FSimCopterTweakFile` stores sections in file order.

### Helper Functions

`NormalizeTweakKey` trims and lowercases keys so lookups are case-insensitive and whitespace-tolerant.

`IsCommentOrBlank` treats blank lines and lines beginning with `#`, `;`, or `%` as comments. The percent marker matters because shipped tweak files use repeated `%` comment lines.

### Public Functions

`FSimCopterTweakSection::TryGetValue` normalizes the requested key and returns the raw stored string.

`GetString` returns the stored string or a default.

`TryGetFloat` and `TryGetInt` get the string then use Unreal's `FDefaultValueHelper` to parse numeric values.

`GetFloat` and `GetInt` return parsed values or defaults.

`FSimCopterTweakFile::FindSection` has const and mutable overloads. Both trim the requested name and compare section names case-insensitively.

`FSimCopterTweakReader::ParseTweakText`:

1. Clears output sections and errors.
2. Splits text into lines.
3. Trims each line and removes a UTF-8 BOM if present.
4. Skips comments and blanks.
5. Starts a new section for `[Section Name]`.
6. Splits key/value lines on the first `=`.
7. Creates an unnamed default section if key/value data appears before any explicit section.
8. Stores normalized keys and trimmed values.

`LoadTweakFileFromFile` reads text, parses it, and appends the filename to parse errors.

### Tests

`FSimCopterTweakParserTest` proves comments, sections, string values, floats, ints, and case-insensitive section lookup.

`FSimCopterTweakReferenceHeliTest` optionally loads `heli.twk` and validates representative Jet Ranger, landing, and rope tuning fields.

## MaxisMeshLibrary

`FMaxisMeshLibrary` is not a file parser. It is the bridge between decoded mesh packs and SimCity/SimCopter semantics.

### Constants and Tables

`ExpectedMeshPacks` lists the three original packs in load order: `sim3d1.max`, `SIM3D2.MAX`, and `SIM3D3.MAX`.

`FKnownXbldMapping` maps a pack name plus GEOM table index to an SC2 `XBLD` tile id. The huge `KnownXbldMappings` array is transcribed from public mapping notes. It is intentionally static data because many city-building mesh choices cannot be inferred safely from names alone.

### Helper Functions

`NormalizePackName` turns a source path into an uppercase base filename like `SIM3D1`.

`TryKnownXbldMapping` linearly searches the transcribed table. A match has the highest confidence.

`TryExtractTableNameTileId` is the fallback. It rejects `BASE*`, finds the final run of digits in a table name, requires a known static-city prefix, then parses the digits as an SC2 tile id. This fills gaps for road/highway pieces omitted from the external mapping table.

`MappingVariantPenalty` gives suffix variants such as `F`, `H`, and `L` a slightly worse score so unsuffixed/default meshes win when multiple objects map to the same tile id.

### Public Functions

`LoadFromOriginalGameRoot` clears all indexes, resolves the configured original game root, requires a `GEO` folder, loads the three expected packs, and registers each pack.

`FindObjectByTileId` resolves the heuristic/remake tile-id mapping and optionally returns that pack's color map.

`FindObjectByTableName` resolves exact table-name lookups, used for helicopters and known named assets.

`FindObjectByObjectId` mirrors original executable function `FUN_00470571`: it searches all loaded objects for a globally unique header id and skips objects with attribute flag bit `3` set. This is the exact path needed for bridge/elevated-road city-builder dispatch.

`GetSharedColorMap` returns the first loaded pack palette. The current evidence says the three `GEO` packs share identical `CMAP` data, so this palette is also used to decode composite bitmaps.

### Private Functions

`RegisterMeshFile` indexes every object by table name, tries high-confidence known mappings, then tries name-derived mappings with a lower priority score.

`RegisterTileMapping` rejects invalid tile ids and invalid object keys. If a tile already has a mapping, the lower score wins.

`GetObject` validates the stored mesh-file index and object index, fills the optional color-map pointer, and returns the decoded object.

### Tests

`FMaxisMeshLibraryTileMappingTest` optionally loads the full original mesh library and validates representative tile ids plus many bridge/elevated-road object ids.

## MaxisProceduralMeshBuilder

`FMaxisProceduralMeshBuilder` turns one decoded `FMaxisMeshObject` into Unreal procedural mesh section data. It is used for moving objects and other palette-colored meshes where the full city texture-atlas system is not required.

### Data Structures

`FMaxisMeshSection` is exactly the data expected by `UProceduralMeshComponent::CreateMeshSection_LinearColor`: vertices, triangles, normals, UVs, vertex colors, tangents, and local bounds.

`IsEmpty` checks both vertex count and triangle count so callers can fail closed.

`Reset` clears all arrays and reinitializes bounds.

### Helper Functions

`ResolveFaceColor` returns a palette color for non-textured face types. Face types `13` and `18` are texture-oriented, so this helper gives them a fallback color in the procedural path.

`AppendFaceToSection`:

1. Rejects faces with fewer than three vertex indices.
2. Converts each valid Maxis vertex into Unreal space.
3. Adds placeholder UVs because this builder is palette-color oriented.
4. Adds per-face vertex color, tangent, and bounds.
5. Rolls back partially-added face vertices if fewer than three valid source vertices were found.
6. Computes a winding-derived normal.
7. Averages a face center and flips the normal outward from the object centroid when needed.
8. Adds one normal per emitted vertex.
9. Triangulates an N-gon as a fan.
10. Optionally emits reversed backface triangles.

### Public Functions

`IsTranslucentFaceType` identifies Maxis face type `11`, used by rotor blur discs and vehicle headlight cards.

`BuildPaletteColoredSection` is the simple one-output wrapper.

`BuildPaletteColoredSections` clears output sections, computes the object centroid in converted/scaled space, routes translucent face type `11` to the optional translucent section, suppresses generated backfaces for translucent discs to avoid double-blending, and appends every face.

### Tests

`FMaxisProceduralMeshBuilderQuadTest` validates vertex/triangle counts, color-map lookup, normal length, coordinate conversion, optional backfaces, and scale.

`FMaxisProceduralMeshBuilderHelicopterTest` optionally proves key helicopter objects resolve and build, and that the rotor object bounds are centered enough for in-place rotor spin.

## Probe Scripts

The Python scripts in `Tools/` are deliberately read-only mirrors of the C++ parsers:

- `sc2_probe.py` validates IFF headers, decoded chunk sizes, and city names across local `.sc2` files.
- `maxis_mesh_probe.py` validates `DIRC`/`CMAP`/`GEOM`, object offsets, `OBJX`, `FACE`, vertex counts, and face record extents.
- `maxis_texture_probe.py` validates Maxis composite bitmap headers, image records, row offsets, and dimensions while skipping normal Windows BMPs.

These scripts are useful when a future parser change is suspected: run the small probe first to confirm whether the binary assumption changed or the Unreal-side consumer changed.
