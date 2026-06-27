# Documentation Coverage

This file tracks the current documentation pass so future reverse-engineering work can continue from a clear map instead of rediscovering what is already covered.

## Added In This Pass

| Doc | Covers |
| --- | --- |
| `Docs/OriginalGameFileCodeWalkthrough.md` | SC2 reader, Maxis mesh reader, composite bitmap reader, Windows BMP reader, tweak reader, mesh library, procedural mesh builder, format tests, read-only probes. |
| `Docs/CityRenderingCodeWalkthrough.md` | `ASimCity2000CityActor`, terrain type grid, original mesh dispatch, bridge/elevated-road object ids, texture/material routing, generated terrain extension, placeholder fallbacks. |
| `Docs/GameplayCodeWalkthrough.md` | Helicopter pawn, ground agents, population sprite/body helpers, traffic system, on-foot pawn, game mode, mesh debug actor. |
| `Docs/ToolingCodeWalkthrough.md` | Unreal atlas bake script, material creation script, probe tools, Ghidra helpers, build files. |
| `Docs/DecompilationWorkflow.md` | Ghidra workflow, scratch output index, address ledger, validation loop, documentation rules. |
| `Docs/MissionsAndTweakSystem.md` | `.twk` format + `fxpt`, the `sim3d.twk` master tree, all per-file tuning (heli/fire/camera/figure/automssn), the 30-city career, and the nine mission types with full money/points scoring. |
| `Docs/OriginalGameFileFormats.md` | Maxis DF container, `people.df` global behavior file (state tables, 88-handler behavior VM, person record, spawn modes, LFSR PRNG, 500-slot pool, building->class map), and `privanim.df` figure pack load path. |
| `Docs/OriginalRuntimeBehavior.md` | Original `TRAN` per-tile steering + road graph + service registries, `ALTM` slope bits 10..14, and the helicopter flight model (tuning binding, master tick, attitude integrator). |

`Docs/ReverseEngineering.md` now links to all of the above and records the 2026-06-26 decode pass.

## Source Coverage

| Area | Files | Coverage |
| --- | --- | --- |
| SC2 city parser | `SimCity2000Reader.h/.cpp` | Covered function by function in `OriginalGameFileCodeWalkthrough.md`. |
| Maxis mesh parser | `MaxisMeshReader.h/.cpp` | Covered function by function in `OriginalGameFileCodeWalkthrough.md`. |
| Maxis texture parser | `MaxisTextureReader.h/.cpp` | Covered function by function in `OriginalGameFileCodeWalkthrough.md`. |
| Windows BMP parser | `MaxisWindowsBitmapReader.h/.cpp` | Covered function by function in `OriginalGameFileCodeWalkthrough.md`. |
| Tweak parser | `SimCopterTweakReader.h/.cpp` | Covered function by function in `OriginalGameFileCodeWalkthrough.md`. |
| Mesh lookup/library | `MaxisMeshLibrary.h/.cpp` | Covered function by function in `OriginalGameFileCodeWalkthrough.md`. |
| Procedural mesh builder | `MaxisProceduralMeshBuilder.h/.cpp` | Covered function by function in `OriginalGameFileCodeWalkthrough.md`. |
| City actor | `SimCity2000CityActor.h/.cpp` | Covered by pipeline, helper groups, and major function walkthrough in `CityRenderingCodeWalkthrough.md`. |
| Flight | `SimCopterHelicopterPawn.h/.cpp` | Covered by data, original tuning, mesh loading, interaction, and runtime function groups in `GameplayCodeWalkthrough.md`. |
| Ground agents | `SimCopterGroundAgent.h/.cpp` | Covered by shape, asset loading, movement, headlights, route state, and animation in `GameplayCodeWalkthrough.md`. |
| Traffic | `SimCopterTrafficSystemActor.h/.cpp` | Covered by route node data, road classification, graph build, spawning, traffic interactions, and route target geometry in `GameplayCodeWalkthrough.md`. |
| People sprite/body | `SimCopterPopulationSprite.h/.cpp`, `SimCopterPopulationBody.h/.cpp` | Covered in `GameplayCodeWalkthrough.md`. |
| On-foot player | `SimCopterOnFootPawn.h/.cpp` | Covered in `GameplayCodeWalkthrough.md`. |
| Game mode | `SimCopterGameMode.h/.cpp` | Covered in `GameplayCodeWalkthrough.md`. |
| Mesh debug actor | `MaxisMeshDebugActor.h/.cpp` | Covered in `GameplayCodeWalkthrough.md`. |
| Unreal scripts | `Tools/Unreal/*.py` | Covered in `ToolingCodeWalkthrough.md`. |
| Probe scripts | `Tools/*.py` | Covered in `OriginalGameFileCodeWalkthrough.md` and `ToolingCodeWalkthrough.md`. |
| Ghidra scripts | `Tools/Ghidra/*.java` | Covered in `DecompilationWorkflow.md` and `ToolingCodeWalkthrough.md`. |
| Build/project files | `.uproject`, `.Build.cs`, `RebuildUnrealCpp.bat` | Covered in `ToolingCodeWalkthrough.md`. |

## Original Game Data And Logic Coverage

| Area | Source | Coverage |
| --- | --- | --- |
| Tweak format + missions/career | `tweak/*.twk`, `sim3d.twk` | Fully decoded in `MissionsAndTweakSystem.md`. |
| `people.df` container + runtime | `X/people.df`, `FUN_004c2f30/004c3010/004c4190/004cd550/004ce2d0` | Container, state tables, behavior VM structure, person record, spawn modes, PRNG in `OriginalGameFileFormats.md`. |
| `privanim.df` full decode | `X/privanim.df`, `FUN_004ceab0`/`FUN_004cfed0`/`FUN_004d18e0`/`FUN_004d1a00` | Container, directories, 75-76-clip tree, 21 figures, per-part node-defs, `ARCP` coord streams in `OriginalGameFileFormats.md`; extractor `Tools/privanim_extract.py`. Only the figure raster primitive (3D scene engine) is open. |
| `TRAN` traffic + road graph | `FUN_004b5290`, `FUN_00495700` | Steering, graph structs, service registries in `OriginalRuntimeBehavior.md`. |
| `ALTM` altitude + slope bits | `FUN_004abc20`, `FUN_00495700` | Base/secondary + slope bits 10..14 in `OriginalRuntimeBehavior.md`. |
| Helicopter flight model | `FUN_00489e20/00484d20/00486a30` | Tuning binding, master tick, attitude integrator in `OriginalRuntimeBehavior.md`. |

## Known Documentation Gaps

The 2026-06-26 passes closed the previous gap list. Remaining (deeper) follow-ups:

- **TOP PRIORITY - clean decode of the people logic (behavior VM + spawn rules)** so pedestrians spawn correctly and carry all original behaviors. The **88-handler behavior bytecode VM** (`DAT_0058ef78` dispatch via `FUN_004ccf20`, executed by walking each agent's `BHAV`/"VAHB" `0x42484156` resource with `FUN_004ce7b0`; `BHAV` accessor `FUN_004d0100`); the opcode grammar; the per-state table `DAT_0058de80` + transition triggers; spawn config `FUN_004c4190` + per-city `career.twk` weighting. See memory `simcopter-people-logic-next` and `ReverseEngineering.md` "Remaining hard pass" #1.
- `X/privanim.df` is **fully decoded** (deep pass 2026-06-26): container, section + 12-byte node directories, 21 figures, 75-76-clip animation inheritance tree, per-part node-defs (`(x,y,0.5)` extents + parent links), and `ARCP` 4-byte coord streams. Deterministic extractor `Tools/privanim_extract.py`; full method in `OriginalGameFileFormats.md` "Faithful Extraction Method". Confirmed: `0x4d4800` is a pure-virtual stub; the IFF walker `FUN_004ce7b0` is the behavior interpreter (not the geometry drawer); `ARPP` records are 8 bytes (the 40-byte blocks are clip-table entries). Only open: the figure rasterization primitive (line vs polygon), which lives in the 3D scene software engine, and emitting glTF from the extracted model.
- The road-graph *builder* (constructs `DAT_0051ac80` from `XBLD`); `FUN_00495700` only dumps it.
- The exact 5-bit `ALTM` slope-shape enumeration (which bit = which raised corner/edge).
- Helicopter horizontal-velocity-from-attitude and position-integration sub-steps (`FUN_00488060`, `FUN_00487bb0`, ...) beyond the attitude integrator.
- The small fixed-header `u32` fields in both DF containers, and the exact weight->probability / `Interval Adj` formulas the mission scheduler uses.

## Verification Performed

- First pass: confirmed walkthrough docs are ASCII-clean and linked from `Docs/ReverseEngineering.md`.
- 2026-06-26 pass: every fact in the three new docs is grounded in either the raw bytes of the shipped `tweak/*.twk` / `X/*.df` files or a fresh Ghidra decompile retained under `Docs/scratchpad/ghidra/out_*.txt` (see the workflow output index). Confidence is marked per-claim (`Confirmed`/`Hypothesis`/`Follow-up`) in each doc.
- Tweak/career/mission values are transcribed directly from the text files; DF headers and the `RSRC` directory were verified with a `python` byte probe.

No C++ build or automation tests were run for this pass because the changes are documentation-only (there is no remake mission/people/career code yet to test against).
