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

## Main Decode Path: ghidra-ai-bridge + re-agent

As of 2026-07-07 the primary way to read the executable is the
[ghidra-ai-bridge](https://github.com/Dryxio/ghidra-ai-bridge) query CLI over a one-time export of
the Ghidra project, optionally driven by [auto-re-agent](https://github.com/Dryxio/auto-re-agent)
(`re-agent`) for automated reverse/verify loops. Both are MIT-licensed Python packages installed in
a project venv at `Tools/re-agent/.venv` (gitignored). The old `analyzeHeadless` +
`ReverseExplore.java` flow (below) remains the fallback for capabilities the bridge lacks.

Why this is the main path: every query against the export returns instantly from JSON files instead
of paying a ~30-60s JVM/analyzeHeadless round trip, and `re-agent` adds an objective verifier
(call-count + control-flow checks) and an 11-signal parity engine that can check our ported C++
against the decompiled original.

### Setup (already done on this machine)

```powershell
python -m venv Tools/re-agent/.venv
Tools/re-agent/.venv/Scripts/pip install auto-re-agent "ghidra-ai-bridge[headless]"
```

Config files live at the repo root and are committed:

- `ghidra-bridge.yaml` - points at the Ghidra install (scoop), the project
  (`Docs/scratchpad/ghidra/SimCopter`), the export dir (`.ghidra-exports/`, gitignored), and the
  source-annotation pattern.
- `re-agent.yaml` - LLM provider (`codex`, model `gpt-5.5`), backend CLI path, verifier and
  parity settings. Codex uses the local ChatGPT login cached by the Codex CLI; the current personal
  Codex reasoning default is `model_reasoning_effort = "high"` in `C:\Users\james\.codex\config.toml`.

The LLM can be switched per run with `re-agent reverse --llm <choice>` (optional; defaults to the
configured provider, i.e. `codex`). Presets:

- `--llm codex` - Codex CLI over the ChatGPT login, model `gpt-5.5` (default).
- `--llm fable` (aliases `claude`, `claude-code`) - the **npm Claude Code CLI** (`claude -p`) over the
  local **Claude subscription login**, model `claude-fable-5` at `medium` reasoning effort.

The Claude Code path is a separate `claude-cli` provider (added to the local venv at
`re_agent/llm/claude_cli.py` and registered in `re_agent/llm/registry.py`). It shells out to
`claude -p --model claude-fable-5 --effort medium --output-format text --tools ""` with tools
disabled, so the model is used purely as a text-in/text-out reverser with no repo side effects,
mirroring how the codex provider runs `codex exec -s read-only`. It is distinct from the SDK-based
`claude` provider (which needs an `ANTHROPIC_API_KEY`); `claude-cli` uses the subscription OAuth
login instead. Reasoning effort is `re-agent.yaml`'s `llm.effort` (or `RE_AGENT_LLM_EFFORT`);
`--llm fable` forces `medium`. Confirm the login with `claude --version` before a paid run.

Run all commands from the repo root so the YAML configs are found.

### One-time export (refresh after changing the Ghidra project)

```powershell
Tools/re-agent/.venv/Scripts/ghidra-bridge.exe export all
```

This uses PyGhidra headless to dump structs, enums, vtables, strings (with xrefs), globals, and a
decompile + caller/callee JSON per function into `.ghidra-exports/`. It takes a while (it decompiles
every function once); afterwards all queries are instant. Re-run it only when the Ghidra project
changes (new functions created, renames, retypes). `export decompiled` / `export strings` etc.
refresh a single category.

### Everyday queries

```powershell
$gb = "Tools/re-agent/.venv/Scripts/ghidra-bridge.exe"
& $gb decompile 0x4abce0        # decompiled C for the function at/containing an address
& $gb decompile FUN_004abce0    # ...or by Ghidra name
& $gb xrefs-to 0x4ae7a0         # callers
& $gb xrefs-from 0x484d20       # callees
& $gb strings PrivAnim          # string search with referencing functions
& $gb containing 0x486a35       # which function contains this address
& $gb global 0x5040e4           # named global info + references
& $gb search heli               # function-name search
& $gb info                      # export statistics
& $gb dump-asm 0x4ce7b0 out.txt # raw disassembly (needs pyghidra, slower)
```

Prefer these over `analyzeHeadless` for decompiles, xrefs, strings, and globals. Fall back to
`ReverseExplore.java` only for what the export cannot do: `scan <hexbytes>` (byte-pattern search),
`bytes` (raw memory dumps), `disasm`/`decompileforce` (vtable-only code Ghidra never made into
functions - though after using `decompileforce`, re-running `export decompiled` picks the new
function up).

### Annotating ported code (parity)

When porting a decompiled function into `SimCopterRemake/Source`, tag the C++ with a comment so the
tooling can map source back to the binary:

```cpp
// SCHOOK: FlattenTerrain 0x004abce0
void FSimCopterTerrain::FlattenTerrain(...)
```

`ghidra-bridge build-map` scans the source tree for these tags and builds
`.ghidra-exports/address_map.json`; `re-agent parity --address 0x4abce0` then runs the 11 heuristic
signals (call-count mismatch, missing FP ops, missing NaN handling, suspiciously short body, ...)
comparing our port against the decompile. Treat RED/YELLOW findings as review prompts, not verdicts
- our ports are idiomatic UE C++, not 1:1 re-hooks, so some signals will warn by design.

### Automated reversal loop (re-agent)

`re-agent reverse --address 0x<addr>` runs an LLM reverser/checker loop against the bridge: it
gathers the decompile + xrefs + structs + nearby annotated source, drafts C++, has a checker model
critique it, and gates acceptance on the objective verifier. This repo is configured to use the
Codex CLI provider, so paid LLM calls run through the local Codex ChatGPT login. Check the login
before a run:

```powershell
codex --version
codex login status
```

Use it deliberately, usually one small function or caller cluster at a time:

```powershell
$ra = "Tools/re-agent/.venv/Scripts/re-agent.exe"
& $ra reverse --address 0x4b5290 --dry-run   # show what would run, no LLM calls
& $ra reverse --address 0x4b5290             # single function (default provider: codex)
& $ra reverse --address 0x4b5290 --llm fable # single function via npm Claude Code (Fable 5, medium)
& $ra parity --address 0x4abce0              # heuristic port-vs-binary check, no LLM
& $ra status                                 # progress (Docs/scratchpad/re-agent/re-agent-progress.json)
```

Durable outputs land in `Docs/scratchpad/re-agent/` so they are tracked with the rest of the
reverse-engineering evidence:

- `Docs/scratchpad/re-agent/code/` - generated C++ candidate for each target.
- `Docs/scratchpad/re-agent/re-agent-progress.json` - pass/fail summary by address.
- `Docs/scratchpad/re-agent/README.md` - compact run index and notable failure notes.

New generated code candidates should begin with a compact `RE_AGENT_NOTE` comment covering purpose,
where to use/port the function, evidence, and caveats. Treat those notes as review prompts, not
confirmed docs; promote only reviewed claims into the main documentation.

Raw reverser/checker JSON and live watch transcripts stay local-only under `reports/re-agent/logs/`
because they are large and noisy. Keep or copy a raw log into docs only when a specific run needs a
full audit trail. Drafts are starting points - they still go through the normal evidence rules below
before anything is documented as `Confirmed`.

### Choosing the next section

Pick targets by function address, not by a broad feature label. Broad prompts like "decode people"
give the agent too much surface area and make review hard. Start from the project gap docs, narrow
to one function, then walk outward through callers/callees.

Useful gap sources:

- `Docs/memory/MEMORY.md` - short index of active reverse-engineering goals.
- `Docs/memory/simcopter-people-logic-next.md` - live handoff for pedestrian behavior, spawn rules,
  figures, traffic, and the remaining people/vehicle gaps.
- `Docs/DocumentationCoverage.md` - known documentation gaps and follow-ups.
- `Docs/ReverseEngineering.md` - main discovery log and "Remaining hard pass" list.
- `Docs/OriginalRuntimeBehavior.md` and `Docs/OriginalGameFileFormats.md` - confirmed vs follow-up
  items for runtime systems and file formats.

Target-selection loop:

```powershell
$gb = "Tools/re-agent/.venv/Scripts/ghidra-bridge.exe"
$ra = "Tools/re-agent/.venv/Scripts/re-agent.exe"

# 1. Find a candidate from docs, then inspect it locally.
& $gb decompile 0x004c9cc0
& $gb xrefs-to 0x004c9cc0
& $gb xrefs-from 0x004c9cc0

# 2. Dry-run the agent before spending model calls.
& $ra reverse --address 0x004c9cc0 --dry-run

# 3. Run exactly one bounded target.
& $ra reverse --address 0x004c9cc0

# 4. Review the report and only then decide the next caller/callee.
& $ra status
```

To watch a run in a separate PowerShell window while also saving a log, launch it through a small
wrapper. The important Windows details are: set the backend path to an absolute executable path and
force Python/Codex subprocess text to UTF-8.

```powershell
Tools/re-agent/watch-re-agent.ps1 -Address 0x004c9cc0
```

The local `re-agent` venv has also been patched so the Codex provider streams `codex exec` stdout
line-by-line instead of buffering it until the end, feeds long prompts through stdin (`codex exec -`)
instead of a single Windows command-line argument, and the fix loop prints phase markers such as
`[re-agent] round 1/4: starting reverse`, `starting checker`, and `objective verdict=PASS`. This
does not expose hidden model reasoning, but it does make the live CLI transcript, phase boundaries,
generated code, checker output, retries, and final status visible in the watch window and log.

The wrapper is equivalent to:

```powershell
$repo = "S:\Repos\sim-copter-remake"
$addr = "0x004c9cc0"
$bridge = "$repo\Tools\re-agent\.venv\Scripts\ghidra-bridge.exe"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = "$repo\reports\re-agent\logs\live-$addr-$stamp.log"
New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null

$script = @"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
Set-Location '$repo'
`$env:RE_AGENT_BACKEND_CLI_PATH = '$bridge'
`$env:PYTHONUTF8 = '1'
`$env:PYTHONIOENCODING = 'utf-8'
`$gb = '$bridge'
`$ra = 'Tools/re-agent/.venv/Scripts/re-agent.exe'
`$log = '$log'
"=== SimCopter re-agent live run: $addr ===" | Tee-Object -FilePath `$log -Append
codex --version 2>&1 | Tee-Object -FilePath `$log -Append
codex login status 2>&1 | Tee-Object -FilePath `$log -Append
& `$gb decompile $addr 2>&1 | Tee-Object -FilePath `$log -Append
& `$gb xrefs-to $addr 2>&1 | Tee-Object -FilePath `$log -Append
& `$gb xrefs-from $addr 2>&1 | Tee-Object -FilePath `$log -Append
& `$ra reverse --address $addr --dry-run 2>&1 | Tee-Object -FilePath `$log -Append
& `$ra reverse --address $addr 2>&1 | Tee-Object -FilePath `$log -Append
`$exit = `$LASTEXITCODE
"Exit code: `$exit" | Tee-Object -FilePath `$log -Append
& `$ra status 2>&1 | Tee-Object -FilePath `$log -Append
Write-Host "Log saved to: $log"
"@

Start-Process powershell.exe -ArgumentList @("-NoExit", "-ExecutionPolicy", "Bypass", "-Command", $script) -WorkingDirectory $repo
```

If a run crashes during the final session save with
`Docs/scratchpad/re-agent/re-agent-progress.tmp -> Docs/scratchpad/re-agent/re-agent-progress.json`,
delete the stale temp file and rerun `re-agent status`. The completed reports may already be valid
even when that final save step exits non-zero.

Good first-pass targets are:

- Small functions with a clear open note in the docs.
- Functions with a handful of callers/callees, not giant dispatchers.
- Callers of a known core function when the core is already understood.
- Functions that correspond to one missing game behavior, such as a density gate, spawn selector, or
  movement check.

Avoid starting with:

- Whole subsystems ("mission system", "people VM", "traffic") without an address.
- Large dispatch tables before their leaf handlers are mapped.
- Functions already marked `Confirmed` unless the goal is a parity check against the port.
- Broad batches before the first single-function report has been reviewed.

Current recommended queue for Codex-backed runs:

```powershell
& $ra reverse --address 0x004c9cc0   # ambient pedestrian density/tile gate; noted as not ported
& $ra reverse --address 0x004c2450   # ambient spawn behavior-class selection
& $ra reverse --address 0x004c9470   # per-step pedestrian move check
& $ra reverse --address 0x004c9bc0   # related tile/person gate caller
& $ra reverse --address 0x004bfed0   # another FUN_004c9cc0 caller, likely object/traffic interaction
```

When asking Codex to launch a run, use a compact request like:

```text
Run a dry-run for re-agent on 0x004c9cc0, inspect decompile/xrefs first, then tell me if it is safe
to launch the paid reverse loop.
```

or:

```text
Launch re-agent for 0x004c9cc0 now, then summarize the report and suggest the next caller/callee.
```

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
- 2026-07-02 pass (full flight model decode, ported to `FSimCopterFlightModel`): `out_heli_flightcore.txt`
  (control reader `FUN_00485f50`, turbulence/fire-damage generator `FUN_00489800`, velocity
  integrator `FUN_00486e90`, vertical/ground/landing logic `FUN_00487160`, rotor animation
  `FUN_00487740`, doors/winch `FUN_00487bb0`, smoke/burn effects `FUN_00489ac0`),
  `out_heli_landing.txt` (bucket drip `FUN_00488060`, rotor-wash dust `FUN_004881b0`, shadow node
  `FUN_00485d90`, searchlight ray-march `FUN_00489250`, tile-object AABB collision `FUN_0048ad50`,
  crash/respawn sequence `FUN_0048a8b0`), `out_heli_math.txt` (16.16 fixed mul/div
  `FUN_0046c49d`/`FUN_0046c4bf`, Euler matrix `FUN_0047b330`, axis rotators, transpose, normalize),
  `out_heli_terrain.txt` (terrain height + flat-flag sampler `FUN_004ae7a0`, landing-surface query
  `FUN_00488850`, forward-vector renormalize `FUN_004882f0`, tenth-degree sin/cos `FUN_0046c4dc`,
  fire scan `FUN_004a5c10`), `out_heli_inputfns.txt` (virtual-control reads `FUN_0041c2a0`/
  `FUN_0041c2c0`/`FUN_0041c2e0`), `out_heli_statics.txt` (per-type static block at `0x5040e4`:
  seats, tail-rotor offsets, NOTAR flag), `out_heli_globals.txt` (landing/rope/damage statics,
  altitude ceiling `DAT_0050404c` = 800 units), `out_heli_fwdvec.txt` (forward basis = local +Z),
  `out_heli_input_xref.txt`, `out_dt_xref.txt`, `out_helitick_callers.txt`. Source for
  `Source/SimCopterRemake/{Public,Private}/Flight/SimCopterFlightModel.*` and the canonical notes
  in `Docs/memory/simcopter-heli-flight-model.md`.
- `out_ped_render_xrefs.txt`, `out_ped_anim.txt`, `out_figure_instantiate.txt`, `out_privanim_bind.txt`, `out_figure_vtable.txt`, `out_rendernode_vtables.txt`, `out_rendernode_vtables2.txt`, `out_figure_rendervtable.txt`: the pedestrian render chain (state -> `DAT_0058de80` anim id -> `FUN_004c7090`/`FUN_004c7c00` -> 12-segment figure `FUN_004ce630`/`FUN_004ce6c0`). Source for the `OriginalGameFileFormats.md` render-pipeline section.
- `out_privanim_parser.txt`, `out_privanim_read.txt`, `out_privanim_chunks.txt`, `out_iff_api.txt`, `out_figure_draw.txt`, `out_figure_recordread.txt`: the `privanim.df` IFF "Doug" container - reader (`FUN_004ce320`), chunk-type register + endian fixup (`FUN_004d1ed0`, handlers `FUN_004d0090`/`FUN_004d00e0`), and the IFF node API. Source for the `privanim.df` on-disk format section.
- 2026-07-01 pass (privanim container decode finished + walker VM identified): `out_chunkfetch.txt`
  (chunk get-by-index `FUN_004cd7b0`, get-by-id `FUN_004cd700`, node-dir scans), `out_chunkfetch2.txt`
  (record-array factory `FUN_004d1b60`, name lookup `FUN_004cdfa0`, section reverse-lookup
  `FUN_004cde50`), `out_dirload.txt`..`out_dirload4.txt` (reader open `FUN_004cd5a0`, file header
  `FUN_004cd3e0`, directory build `FUN_004cdb50`/`FUN_004cda40`, entry byte-swaps
  `FUN_004cdfe0`/`FUN_004ce010`, chunk file read `FUN_004cdcb0`, fread/fseek primitives),
  `out_nodemethods.txt` (BODC/ANIP node methods, record swap handlers `FUN_004d0090`/`FUN_004d00e0`,
  empty ARPP handler `FUN_004cea20`, ARPP row/col accessor `FUN_004cf3d0`), `out_nodevtables.txt`
  (node subclass vtables `0x4f50b8` ANIP / `0x4f50e8` BODC), `out_figwalk.txt` (the walker VM
  `FUN_004ce7b0` decoded: 16-bit tokens, <0x100 = opcode via vtable[0], >=0x100 = named-child link;
  12-deep stack), `out_vm_handlers.txt` (the 88 opcode thunks at `0x4c84e0+0x20*n` with their real
  target functions), `out_vm_ops0-6.txt` (first behavior handlers decompiled). Source for
  `Docs/OriginalGameFileFormats.md` "Exact Container Spec" and the rewritten
  `Tools/privanim_extract.py`. Renderer trace (same day): `out_scan_224.txt` (all code touching
  person+0x224), `out_clipbind.txt` (clip binder `FUN_004c68f0` + alt driver `FUN_004c65e0` + draw
  setup `FUN_004c7f10`), `out_figrender.txt` (figure evaluator `FUN_004cfb30` + ARLU resolver
  `FUN_004cf7b0`), `out_partdraw.txt` (per-part primitive dispatch `FUN_004cf8f0`, endpoint unpack
  `FUN_004cea30`, transform `FUN_004d0520`, depth comparator `FUN_004d0060`), `out_thickline.txt`
  (thick-line blitter `FUN_004d11d0`, head-sprite blit `FUN_004d0b70` reading SIM3D.BMP). Source
  for the "figure renderer" doc section.
- 2026-06-26 deep pass (privanim full decode + draw-path trace; **container/record claims superseded
  by the 2026-07-01 pass above**): `out_leaf_handlers.txt`, `out_leaf_force.txt`, `out_geom_parse.txt`, `out_recordarray.txt`, `out_linkresolve.txt` (load path: loader `FUN_004ceab0`, node parse `FUN_004cfed0`/`FUN_004d18e0`, record-array `FUN_004d1a00`/`FUN_004d1df0`/`FUN_004d1b60`, link resolver `FUN_004cf8b0`); `out_figdraw_consumer.txt`, `out_figure_loop.txt`, `out_figure_vtables_full.txt`, `out_rendernode_methods.txt`, `out_fig_attach.txt`, `out_scenenode_iface.txt`, `out_disasm_4d4800.txt` (draw path: `FUN_004c6450`, `FUN_004c7c00`, the figure/render-node vtables, and the `0x4d4800` stub); `out_scan_vahb.txt`, `out_fig0100.txt`, `out_vtable_4fa190.txt`, `out_text_bytes.txt` (behavior-VM "VAHB"/`BHAV` anchor + the SimCopterX `.text` relocation caveat). Source for `Docs/OriginalGameFileFormats.md` "Faithful Extraction Method" and the `Tools/privanim_extract.py` extractor.

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
| `0x004ce630` | `FUN_004ce630` | Walker-context ctor: a **12-deep stack of 20-byte frames** (cursor `+0xf4`, depth `+0xf6`) - NOT "12 body segments"; the person object embeds the same layout. |
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
| `0x00484d20` | `FUN_00484d20` | Helicopter per-frame master tick (load factor, sub-steps, fuel burn, ground-impact bounce/damage); ported as `FSimCopterFlightModel::Step`. |
| `0x00486a30` | `FUN_00486a30` | Helicopter attitude integrator: pitch clamp (+ground-proximity bonus), bank clamped to \|pitch\|+30deg, EMA lag N=((1000-PitchRate)/500)*fps (fps capped 20), heading += yawSmoothed*15*dt, bank-inherits-slide display quirk. |
| `0x00485f50` | `FUN_00485f50` | Control reader: keys RAMP targets (pitch/slide at Ctrl6 SlideRate*load, turn at RollRate+YawRate*load), analog seeks -axis*{3,6,2}; no-input decay (1-2dt)/(1-4dt); collective +/-1; weapons/doors. |
| `0x00489800` | `FUN_00489800` | Turbulence + fire damage: amplitude 3 healthy, +(250-fireDist)+(damage/20) otherwise; 9-sample ring buffers averaged into the attitude targets. |
| `0x00486e90` | `FUN_00486e90` | Velocity integrator: forward speed chases smoothed pitch (1/32 per frame), slide velocity x0.488, pos += vel*dt*0.610 units; bounce timer forces speed = pitchTarget/8. |
| `0x00487160` | `FUN_00487160` | Vertical/ground: climb ramps (2x rate, cap 4x*load), rotor spool gate 300 before lift, 800-unit AGL ceiling, neutral decay 5%/10% per frame, landing rules vs Heli Landing tweaks + flat-terrain flag, +1.2-unit settle, parked<->flying transitions. |
| `0x00487740` | `FUN_00487740` | Rotor animation: spool +100/s flying (to 360), -50/s parked, -200/s dying; blade step min(speed*32*dt, 39.1deg)/frame; face-type-11 blur discs toggle at 300; per-type tail offsets + NOTAR hide flag. |
| `0x00487bb0` | `FUN_00487bb0` | Doors (frames 3..0x11) and winch/bucket: fill +30 lb/frame over water, load capped at MaxLoad, water-throw on hook jerk. |
| `0x0048ad50` | `FUN_0048ad50` | Tile-object AABB collision test -> damage event + attitude kick; fire-flagged hits set the extinguish latch `DAT_00504058`. |
| `0x0048a8b0` | `FUN_0048a8b0` | Crash sequence: unlink, explosion + debris ring, burn timers, respawn at nearest pad. |
| `0x004ae7a0` | `FUN_004ae7a0` | Terrain height sample (two-triangle bilinear over the 16-bit heightfield) + "locally flat" out-flag (corners within 9.0 units) used as the landing gate. |
| `0x00488850` | `FUN_00488850` | Landing-surface query: max object top under the helicopter footprint, else terrain height. |
| `0x004a5c10` | `FUN_004a5c10` | Fire scan on the current tile: returns helicopter height above the fire (drives fire damage + shake + AGL HUD). |
| `0x0046c49d` | `FUN_0046c49d` | 16.16 fixed-point multiply (64-bit intermediate); `FUN_0046c4bf` = divide. |
| `0x0046c4dc` | `FUN_0046c4dc` | Sin/cos lookup in tenth-degrees (901-entry quarter table at `0x46b530`, wrap 3600). |
| `0x004d5490` | `FUN_004d5490` | MSVCRT random seed wrapper writing `_holdrand`; relevant to terrain detail and random behavior. |
| `0x004cfed0` | `FUN_004cfed0` | `privanim` BODC figure-node init: builds the figure's ARCP record-array at `node+0x28` (key `name+"c"`) and ARLU at `node+0x2c` (key `name+"L"`). |
| `0x004d18e0` | `FUN_004d18e0` | `privanim` ANIP clip-node init: builds the ARPP record-array at `node+0x28` (key `name+"i"`). |
| `0x004d1a00` | `FUN_004d1a00` | Record-array loader: reads a chunk's `(stride,rows,cols)` header (3 BE u16) and iterates `rows*cols` records via a callback. |
| `0x004d1df0` | `FUN_004d1df0` | Builds the in-place row-pointer table for a record-array (data starts at chunk+8). |
| `0x004d1b60` | `FUN_004d1b60` | Record-array factory (lazy: dims start `-1`, stride at `+0x18`, bank/tag at `+0x24/+0x28`, key name at `+0x3c`). |
| `0x004cf8b0` | `FUN_004cf8b0` | ARCP skeleton link resolver: matches `record+8` (part id) to resolve `record+0xc` (parent id) into a pointer = bone hierarchy. |
| `0x004d1d70` | `FUN_004d1d70` | Chunk locator + lazy allocator (`(stride*cols+4)*rows+8`, `LocalAlloc` zeroed; records stream from file on demand). |
| `0x004cd3e0` | `FUN_004cd3e0` | DF file header read: `@0 dataBase(0x100)`, `@4 dirOffset`, `@0xc dirSize`; seeds the directory load. |
| `0x004cdb50` | `FUN_004cdb50` | Directory blob load + share-by-fileId; builds the 0x20-byte directory object (`FUN_004cda40`). |
| `0x004cda40` | `FUN_004cda40` | Directory object build: swaps section entries (`FUN_004cdfe0`) and node entries (`FUN_004ce010`), computes node-region and string-table offsets. |
| `0x004ce010` | `FUN_004ce010` | Node entry byte-swap; proves the 12-byte entry layout `[u16 id][u16 nameOff][u8 flags][u24 chunkOff][u32 scratch]`. |
| `0x004cdcb0` | `FUN_004cdcb0` | Chunk file read: `fseek(dataBase+chunkOff)`, `[BE u32 len]` then payload into LocalAlloc; caches handle in entry+8. |
| `0x004cd7b0` | `FUN_004cd7b0` | Get chunk by (tag, 1-based index) with optional per-chunk fixup callback. |
| `0x004cd700` | `FUN_004cd700` | Get chunk by (tag, entry id) - how record arrays pair with their owner node. |
| `0x004cdfa0` | `FUN_004cdfa0` | Node name fetch: Pascal string at stringTable + entry.nameOff. |
| `0x004d0090` | `FUN_004d0090` | ARCP record swap: u32 name@+8, u32 parent@+0xc, f32 dims@+0x1c/+0x20/+0x24. |
| `0x004d00e0` | `FUN_004d00e0` | ARLU record swap: two u32s = [mnemonic][clip name]. |
| `0x004cea20` | `FUN_004cea20` | ARPP record handler - EMPTY: pose records are raw bytes (s8 segment endpoints). |
| `0x004cf3d0` | `FUN_004cf3d0` | ANIP node accessor: ARPP record at `rowPtr[row] + col*8` (rows=frames, cols=parts). |
| `0x004c84e0` | (thunk table) | The 88 behavior-VM opcode thunks, 0x20 bytes apart, each tail-calling the real handler (map in `out_vm_handlers.txt`). |
| `0x00470650` | `FUN_00470650` | 16.16 fixed-point 4x4 affine matrix multiply (`out = local x parent`, bottom row forced to `[0,0,0,1.0]`). |
| `0x004704d1` | `FUN_004704d1` | Set a scene node's current transform (copies the 16-dword matrix, calls `FUN_00470650` into `node+0x50`). |
| `0x004c6450` | `FUN_004c6450` | Per-person, per-frame figure driver: runs behavior (`FUN_004ce7b0`) LOD-gated by `DAT_0058dc26`, advances `frame@+0x14c` (wraps at clip ARPP row count). |
| `0x004ce7b0` | `FUN_004ce7b0` | The walker VM (decoded 2026-07-01): reads 16-bit tokens; `<0x100` = opcode via `this->vtable[0]`, `>=0x100` = 4-char node name push (`FUN_004ce700`); depth-guard 0x80, stack overflow err 1000. |
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

- `X/privanim.df` (2026-07-01, code-derived and file-validated 437/437 chunks): header/directory/
  string table/chunk layout; 21 named figures (`pilot`..`Woman`, incl. `Elvis`/`Nessie`); per figure
  an `ARCP` skeleton tree (29..88 named parts with parent links + f32 dimensions), an `ARLU`
  18-entry behavior-mnemonic->clip map (`1Wal`->`101!` etc.), and per clip `ARPP` = frames x parts
  8-byte records, each **one line segment (two s8 xyz endpoints)** of the body wireframe per frame.
  See `Docs/OriginalGameFileFormats.md` "Exact Container Spec"; `Tools/privanim_extract.py`.
  Still open: how the scene engine fleshes segments into filled flat-shaded polygons (ARCP type
  byte + dimension floats + palette), and the `DAT_0058de80` anim-id -> clip binding hop.

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
