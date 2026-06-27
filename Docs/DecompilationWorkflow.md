# Decompilation Workflow

This project depends on user-provided original SimCopter assets and local reverse engineering. This document explains how to reproduce and extend the decompilation evidence without committing original game data.

## Repository Policy

The remake should not commit copyrighted original game files. The expected local layout is:

```text
Reference/SimCopterOriginalGame/
  SimCopter.exe
  BMP/
  GEO/
  X/
  cities/
  sound/
  tweak/
```

`Reference/` is intentionally ignored by git. Code and docs should describe file formats and behavior, not include original binary payloads.

## Main Evidence Paths

Current human-readable notes:

- `Docs/ReverseEngineering.md`: main discovery log and behavior notes.
- `Docs/DocumentationCoverage.md`: documentation coverage map and known gaps.
- `Docs/OriginalGameFileCodeWalkthrough.md`: parser and format-reader code explanation.
- `Docs/CityRenderingCodeWalkthrough.md`: city actor and decompiled render behavior explanation.
- `Docs/GameplayCodeWalkthrough.md`: helicopter, ground population, traffic, and on-foot code explanation.
- `Docs/ToolingCodeWalkthrough.md`: bake/material scripts, probes, Ghidra helpers, and build files.
- `Docs/TrafficSystemNotes.md`: handoff notes for traffic/population.

Current scratch evidence:

- `Docs/scratchpad/ghidra/out_*.txt`: Ghidra decompile/xref/string outputs.
- `Docs/scratchpad/*.png`: visual probes and atlas/terrain diagnostics.
- `Docs/scratchpad/*.py`: one-off local experiments.

Reusable tools:

- `Tools/Ghidra/ReverseExplore.java`: flexible Ghidra post-script for strings, xrefs, callers, bytes, and decompile output.
- `Tools/Ghidra/DecompileAddresses.java`: simple address decompiler.
- `Tools/sc2_probe.py`: read-only SC2 parser probe.
- `Tools/maxis_mesh_probe.py`: read-only MAX mesh parser probe.
- `Tools/maxis_texture_probe.py`: read-only composite bitmap probe.
- `Tools/privanim_probe.py`: read-only `privanim.df` container walk (section/clip/record overview).
- `Tools/privanim_extract.py`: deterministic `privanim.df` extractor (sections, node dir, 75-76-clip tree, 21 figures, per-part node-defs, ARCP coord streams) -> structured JSON model. Derived from the decompiled reader; no guessed offsets.

## Ghidra Project State

The scratchpad currently contains a local Ghidra project:

```text
Docs/scratchpad/ghidra/SimCopter.gpr
Docs/scratchpad/ghidra/SimCopter.rep/
```

This is useful for continuity, but future work should treat generated `out_*.txt` files as the readable evidence. When a claim from the executable matters, add or refresh a focused output file with the script command used.

## ReverseExplore.java

`ReverseExplore.java` writes UTF-8 output to a file, avoiding console encoding problems.

General shape:

```text
analyzeHeadless <project_dir> <project_name> -process SimCopter.exe -noanalysis ^
  -postScript ReverseExplore.java <outFile> <command> [args...]
```

Supported commands:

- `strings <substr>`: list defined strings containing the substring and referencing functions.
- `xrefsto <hexaddr>`: list references to an address.
- `decompile <hexaddr>...`: decompile the function containing each address.
- `func <name>...`: decompile functions by Ghidra function name.
- `callers <hexaddr>`: list callers of the function containing an address.
- `bytes <hexaddr> <count>`: dump bytes and little-endian dwords (count accepts decimal or `0x` hex).
- `disasm <hexaddr> <count>`: force-disassemble raw blocks (for vtable-only code); shows call/jump targets.
- `decompileforce <hexaddr>...`: create a function at the address first (for vtable-only targets Ghidra never made into functions), then decompile. Essential for figure/anim-node methods that are referenced only through vtables.
- `scan <hexbytes>`: search all program memory for a byte sequence and report addresses + containing function. Used to locate code by an immediate constant, e.g. `scan 6856414842` finds `push 0x42484156` ("VAHB"/BHAV).

Typical examples:

```text
analyzeHeadless Docs/scratchpad/ghidra SimCopter -process SimCopter.exe -noanalysis ^
  -postScript Tools/Ghidra/ReverseExplore.java Docs/scratchpad/ghidra/out_xbld_refs.txt strings XBLD
```

```text
analyzeHeadless Docs/scratchpad/ghidra SimCopter -process SimCopter.exe -noanalysis ^
  -postScript Tools/Ghidra/ReverseExplore.java Docs/scratchpad/ghidra/out_builder.txt decompile 0x0047c0c0
```

Use focused output names. A file named `out_terrtexsel.txt` is more useful months later than `out_tmp.txt`.

## DecompileAddresses.java

`DecompileAddresses.java` is the small script for quick address dumps:

```text
analyzeHeadless <project_dir> <project_name> -import SimCopter.exe ^
  -postScript Tools/Ghidra/DecompileAddresses.java 0x4abc20 0x4abce0
```

Prefer `ReverseExplore.java` when the output should be retained, because it writes directly to a named UTF-8 file.

## Current Ghidra Output Index

The scratchpad has these notable outputs:

- `out_builder.txt`: decompile of the original city builder around `FUN_0047c0c0`; source for mesh object dispatch, no per-tile rotation, grid array setup, and world-coordinate mapping.
- `out_getobj_callers.txt`: callers of the original object lookup path.
- `out_meshref.txt`: mesh-related string references.
- `out_loader.txt`: original asset-loader evidence for `SIM3D1.MAX`, `SIM3D2.MAX`, `SIM3D3.MAX`, `SIM3D.BMP`, and related globals.
- `out_ground.txt`: ground-agent placement and terrain-cell checks.
- `out_gridcells.txt`: grid-cell data references.
- `out_tmap.txt`: height grid/tmap related output.
- `out_terrsetup.txt`: terrain grid setup and face allocation output.
- `out_terrtex.txt`, `out_terrtex2.txt`, `out_terrtexsel.txt`: terrain texture/type selection evidence.
- `out_tilecnt.txt`, `out_tile.txt`, `out_xter.txt`, `out_xbld_refs.txt`: tile-layer references.
- `out_roadgraph.txt`, `out_road.txt`, `out_tran_strings.txt`: traffic/road/TRAN research.
- `out_people_strings.txt`, `out_people_loader.txt`, `out_people_behavior_runtime.txt`, `out_population_ai_pass1.txt`: people runtime, spawn, behavior, and loader research.
- `out_privanim_strings.txt`: `PrivAnim.df` string reference.
- `out_texslice.txt`: texture slicing/page evidence.
- `out_rngseed.txt`: randomness/seed behavior.
- `out_scene_refs.txt`, `out_render.txt`: scene/render references.
- `out_people_parser.txt`: `people.df` parse delegation (`FUN_004cd550` -> `FUN_004ce2d0`), behavior-file open check, the people LFSR PRNG (`FUN_004ce9d0`), the generic resource opener (`FUN_00433b20`), and Pascal-string helpers. Source for `OriginalGameFileFormats.md`.
- `out_df_reader.txt`: DF resource read path and resource-type-`0xc` path resolver internals.
- `out_traffic_terrain.txt`: original `TRAN` per-tile steering (`FUN_004b5290`), the road-graph dump/structures (`FUN_00495700`), and the `ALTM` altitude/slope helper (`FUN_004abc20`). Source for `OriginalRuntimeBehavior.md` traffic and slope sections.
- `out_heli_physics.txt`: helicopter master tick (`FUN_00484d20`) and attitude integrator (`FUN_00486a30`).
- `out_heli_tuning.txt`, `out_heli_callers.txt`, `out_heli_xref.txt`, `out_heli_xref2.txt`, `out_heli_find.txt`, `out_heli_find2.txt`, `out_heli_strings.txt`: heli.twk tuning binding (`FUN_00489e20`), its caller (`FUN_00479bb0`), and the tuning-global readers that locate the flight model.
- `out_ped_render_xrefs.txt`, `out_ped_anim.txt`, `out_figure_instantiate.txt`, `out_privanim_bind.txt`, `out_figure_vtable.txt`, `out_rendernode_vtables.txt`, `out_rendernode_vtables2.txt`, `out_figure_rendervtable.txt`: the pedestrian render chain (state -> `DAT_0058de80` anim id -> `FUN_004c7090`/`FUN_004c7c00` -> 12-segment figure `FUN_004ce630`/`FUN_004ce6c0`). Source for the `OriginalGameFileFormats.md` render-pipeline section.
- `out_privanim_parser.txt`, `out_privanim_read.txt`, `out_privanim_chunks.txt`, `out_iff_api.txt`, `out_figure_draw.txt`, `out_figure_recordread.txt`: the `privanim.df` IFF "Doug" container - reader (`FUN_004ce320`), chunk-type register + endian fixup (`FUN_004d1ed0`, handlers `FUN_004d0090`/`FUN_004d00e0`), and the IFF node API. Source for the `privanim.df` on-disk format section.
- 2026-06-26 deep pass (privanim full decode + draw-path trace): `out_leaf_handlers.txt`, `out_leaf_force.txt`, `out_geom_parse.txt`, `out_recordarray.txt`, `out_linkresolve.txt` (load path: loader `FUN_004ceab0`, node parse `FUN_004cfed0`/`FUN_004d18e0`, record-array `FUN_004d1a00`/`FUN_004d1df0`/`FUN_004d1b60`, link resolver `FUN_004cf8b0`); `out_figdraw_consumer.txt`, `out_figure_loop.txt`, `out_figure_vtables_full.txt`, `out_rendernode_methods.txt`, `out_fig_attach.txt`, `out_scenenode_iface.txt`, `out_disasm_4d4800.txt` (draw path: `FUN_004c6450`, `FUN_004c7c00`, the figure/render-node vtables, and the `0x4d4800` stub); `out_scan_vahb.txt`, `out_fig0100.txt`, `out_vtable_4fa190.txt`, `out_text_bytes.txt` (behavior-VM "VAHB"/`BHAV` anchor + the SimCopterX `.text` relocation caveat). Source for `Docs/OriginalGameFileFormats.md` "Faithful Extraction Method" and the `Tools/privanim_extract.py` extractor.

When adding new outputs, update this index or add a short "used by" note near the behavior documented in `ReverseEngineering.md`.

## Address and Behavior Ledger

These are the main executable functions currently tied to remake code:

| Address | Ghidra name | Meaning in docs/code |
| --- | --- | --- |
| `0x00470571` | `FUN_00470571` | Object lookup by globally unique object id; mirrored by `FMaxisMeshLibrary::FindObjectByObjectId`. |
| `0x00478960` | `FUN_00478960` | Terrain/grid render setup; evidence for terrain face allocation and atlas-style UV assumptions. |
| `0x00479bb0` | `FUN_00479bb0` | Asset loader setup for Sim3D packs and texture handles. |
| `0x0047c0c0` | `FUN_0047c0c0` | City builder; loops 128x128 tiles, dispatches mesh object ids, stores render structs, no per-tile rotation. |
| `0x004814c0` | `FUN_004814c0` | Terrain renderer consuming terrain type/page mapping. |
| `0x004abc20` | `FUN_004abc20` | Original altitude helper; base/secondary/slope bit interpretation. |
| `0x004abce0` | `FUN_004abce0` | Terrain height/type grid builder; source for `BuildTerrainTextureTypeGrid`. |
| `0x004b10a0` | `FUN_004b10a0` | Ground-agent placement helper; searches valid terrain/cell target and computes fixed-point world position. |
| `0x004b5290` | `FUN_004b5290` | Original TRAN route-step table; partly superseded by the current graph-walk traffic approach. |
| `0x004c2f30` | `FUN_004c2f30` | People runtime initialization and `People.df` loading. |
| `0x004c3010` | `FUN_004c3010` | People behavior/runtime initialization block seen in `out_people_behavior_runtime.txt`. |
| `0x004c4190` | `FUN_004c4190` | Main person spawn configurator, free-slot search, spawn modes, animation defaulting. |
| `0x004ceab0` | `FUN_004ceab0` | `PrivAnim.df` loader; registers IFF chunk types (`ARCP`/`ARLU`/`ARPP`), builds the 25x25 figure LOD table. |
| `0x004ce320` | `FUN_004ce320` | `privanim.df` reader (`fopen` of the IFF "Doug" container; lazy by-4CC reads). |
| `0x004d1ed0` | `FUN_004d1ed0` | Register an IFF chunk type (tag, record size, endian-fixup handler) and process its records. |
| `0x004ce630` | `FUN_004ce630` | Figure instance ctor: 12 segments x 20-byte part records. |
| `0x004ce6c0` | `FUN_004ce6c0` | Bind a figure's animation cursor (`animId`, `frame`, `timer`, owner transform). |
| `0x004c7090` | `FUN_004c7090` | Pedestrian state setup: writes state `+0x148` and figure anim id `DAT_0058de80[state]` to `+0x17a`. |
| `0x004c7c00` | `FUN_004c7c00` | Attach a pedestrian render node at world coords and instantiate its figure. |
| `0x004cd550` | `FUN_004cd550` | `people.df` parse delegator; stores behavior buffer at manager `+0x114`. |
| `0x004ce2d0` | `FUN_004ce2d0` | DF resource reader (virtual read of the resolved path). |
| `0x004ce4f0` | `FUN_004ce4f0` | "behavior file open" check (manager `+0x108`). |
| `0x004ce9d0` | `FUN_004ce9d0` | People behavior 16-bit LFSR PRNG (tap `0x1bf5`); `FUN_004cea00` = `rng % n`. |
| `0x00433b20` | `FUN_00433b20` | Generic Maxis resource path resolver by type (`0xc` = `.df` in `X/`). |
| `0x004b5290` | `FUN_004b5290` | Original `TRAN` per-tile car steering (coin-flip turning, dead-end reverse). |
| `0x00495700` | `FUN_00495700` | Road-graph debug dump; reveals `0x38`-byte intersections, 3-byte road tiles, service registries. |
| `0x00489e20` | `FUN_00489e20` | heli.twk tuning binding (14 controls x 9 types into `0x5c`-byte blocks). |
| `0x00484d20` | `FUN_00484d20` | Helicopter per-frame master tick (state machine, scene-graph relink, sub-steps). |
| `0x00486a30` | `FUN_00486a30` | Helicopter attitude integrator (clamp to tuning max, first-order lag, integrate heading). |
| `0x004d5490` | `FUN_004d5490` | MSVCRT random seed wrapper writing `_holdrand`; relevant to terrain detail and random behavior. |
| `0x004cfed0` | `FUN_004cfed0` | `privanim` BODC figure-node init: builds the figure's ARCP record-array at `node+0x28` (key `name+"c"`) and ARLU at `node+0x2c` (key `name+"L"`). |
| `0x004d18e0` | `FUN_004d18e0` | `privanim` ANIP clip-node init: builds the ARPP record-array at `node+0x28` (key `name+"i"`). |
| `0x004d1a00` | `FUN_004d1a00` | Record-array loader: reads a chunk's `(stride,rows,cols)` header (3 BE u16) and iterates `rows*cols` records via a callback. |
| `0x004d1df0` | `FUN_004d1df0` | Builds the in-place row-pointer table for a record-array (data starts at chunk+8). |
| `0x004d1b60` | `FUN_004d1b60` | Record-array factory (lazy: dims start `-1`, stride at `+0x18`, bank/tag at `+0x24/+0x28`, key name at `+0x3c`). |
| `0x004cf8b0` | `FUN_004cf8b0` | ARCP skeleton link resolver: matches `record+8` (part id) to resolve `record+0xc` (parent id) into a pointer = bone hierarchy. |
| `0x004d1d70` | `FUN_004d1d70` | Chunk locator + lazy allocator (`(stride*cols+4)*rows+8`, `LocalAlloc` zeroed; records stream from file on demand). |
| `0x00470650` | `FUN_00470650` | 16.16 fixed-point 4x4 affine matrix multiply (`out = local x parent`, bottom row forced to `[0,0,0,1.0]`). |
| `0x004704d1` | `FUN_004704d1` | Set a scene node's current transform (copies the 16-dword matrix, calls `FUN_00470650` into `node+0x50`). |
| `0x004c6450` | `FUN_004c6450` | Per-person, per-frame figure driver: runs behavior (`FUN_004ce7b0`) LOD-gated by `DAT_0058dc26`, advances `frame@+0x14c` (wraps at clip ARPP row count). |
| `0x004ce7b0` | `FUN_004ce7b0` | Generic IFF chunk-tree walker; used as the **behavior bytecode interpreter** (dispatches leaf chunks to the object's `vtable[0]`). |
| `0x004ccf20` | `FUN_004ccf20` | People render-node `vtable[0]`: behavior VM dispatch `(&DAT_0058ef78)[op]` (the 88-handler table). |
| `0x004c7c00` | `FUN_004c7c00` | Pedestrian render-node ctor: embeds the 12-segment figure at `node+0x4c` (`FUN_004ce630`), sets vtables `PTR_FUN_004f5018` (+0) and `PTR_LAB_004f5000` (+0x100). |
| `0x004d0100` | `FUN_004d0100` | `BHAV`/"VAHB" (`0x42484156`) behavior-resource accessor ctor (vtable `PTR_LAB_004f5130`). |
| `0x004d4800` | (stub) | Pure-virtual placeholder (`push 0x19; call __amsg_exit`); the figure base class's three draw slots are unimplemented (real rasterization is in the 3D scene engine). |

Add confidence notes when a function is only partially understood. Do not rename a decompiled function in docs as though the name were certain unless the behavior has been cross-checked.

## File Format Validation Loop

For binary formats, keep the loop tight:

1. Find strings/xrefs in `SimCopter.exe`.
2. Decompile the smallest function that touches the data.
3. Run a read-only probe over local original files.
4. Port the minimal parser behavior into C++.
5. Add an automation test that skips cleanly when `Reference/` is absent.
6. Document the invariant in `ReverseEngineering.md` and, if code-specific, in the relevant walkthrough doc.

This prevents a common reverse-engineering failure mode: one local sample appears to work, but the parser silently bakes in an assumption that fails across all 48 original cities or all three mesh packs.

## Decompilation Documentation Rules

Use this convention for future notes:

- `Confirmed`: validated against decompiled code and local original files.
- `Implemented`: code now depends on this behavior.
- `Hypothesis`: plausible but not yet validated.
- `Follow-up`: known missing or partially ported behavior.

When documenting a decompiled switch or table, include:

- Original address or output file.
- Runtime data read by the switch.
- Output values or side effects.
- Which remake function consumes the finding.
- Any known mismatch between original and remake behavior.

## Current Original Game File Coverage

Implemented parsers:

- `.sc2` EA IFF city files.
- Maxis Sim3D `GEO/*.MAX` mesh packs.
- Maxis composite bitmap `.BMP` files such as `SIM3D.BMP`, `SKY.BMP`, and `TILED1.BMP`.
- Normal 8-bit Windows BMP files such as `PEOPLE1.BMP`.
- Plain-text `tweak/*.twk` tuning files.

Partially documented or probed:

- `X/people.df`: behavior strings, people initialization, spawn configuration, runtime behavior entry points. **The behavior VM remains the top open item** (see follow-ups).
- `TRAN` traffic resources/runtime data: route-step table identified, but current gameplay uses a cleaner graph walk.

Fully decoded:

- `X/privanim.df`: container, directories, 75-76-clip animation tree, 21 figures, per-part node-defs, and `ARCP` coord streams (`Docs/OriginalGameFileFormats.md` "Faithful Extraction Method"; `Tools/privanim_extract.py`). Only the figure rasterization primitive (in the 3D scene engine) is unread.

Known major follow-ups (priority order):

1. **Clean decode of the people logic (behavior VM + spawn rules)** so pedestrians spawn correctly and carry all original behaviors. Entry points: spawn config `FUN_004c4190`; per-frame driver `FUN_004c6450`; the **88-handler behavior bytecode VM** `(&DAT_0058ef78)[op]` (`FUN_004ccf20`) executed by walking each agent's `BHAV`/"VAHB" (`0x42484156`) resource with `FUN_004ce7b0` (`BHAV` accessor `FUN_004d0100`). Decode the 88 opcode handlers + grammar, the per-state table (`DAT_0058de80`), and state-transition triggers (idle/walk/panic/return-to-car/flee-spotlight/pickup/decommission).
- Optionally emit glTF from the `privanim` model, and (lower value) reverse the 3D scene rasterizer for the exact figure draw primitive.
- Preserve or deliberately replace original traffic subobject follower routines.
- Identify remaining `ALTM` slope-bit uses outside terrain height/type selection.
- Build diagnostics that show original object ids, table names, texture references, and footprint suppression per tile.

## Adding New Evidence

When you discover new behavior:

1. Save the focused Ghidra output under `Docs/scratchpad/ghidra/out_<topic>.txt`.
2. Add a short note to this workflow index if the file is meant to stick around.
3. Update `Docs/ReverseEngineering.md` with the gameplay/file-format meaning.
4. Update a code walkthrough if implementation already exists.
5. Add or update a test when the behavior can be checked without shipping original files.

The goal is that a future reader can answer three questions quickly: where did this claim come from, which code depends on it, and what original-file samples validated it?
