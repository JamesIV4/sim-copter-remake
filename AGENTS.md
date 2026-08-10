# AGENTS.md

Instructions for AI agents working on **sim-copter-remake** — a from-scratch Unreal Engine 5.8
re-implementation of Maxis's *SimCopter* (1996), ported by decompiling the original
`SimCopter.exe` and reproducing its behaviour rather than approximating it.

Read this first, then `Docs/memory/MEMORY.md`.

---

## 1. Everything you produce goes in the repo

| What | Where |
| --- | --- |
| Durable notes / memories | `Docs/memory/<slug>.md`, indexed in `Docs/memory/MEMORY.md` |
| Scratch: build logs, throwaway scripts, screenshots, decompile dumps | `Docs/scratchpad/` |
| Plans, walkthroughs, format specs | `Docs/` |

Do **not** write these to an agent's machine-local memory or temp scratchpad directory. Those
are untracked and per-machine: nobody else sees them, they die with the machine, and they never
appear in a diff. Full rationale in `Docs/memory/agent-workspace-conventions.md`.

`Docs/memory/MEMORY.md` is the real index — read it at the start of a session. It carries hard-won
traps (unit conventions, sparse dispatch tables, which Ghidra decompiles are wrong) that will cost
you hours to rediscover.

## 2. Building

```powershell
cmd /c "S:\Repos\sim-copter-remake\RebuildUnrealCpp.bat < nul"
```

**Always use `RebuildUnrealCpp.bat`.** Never call `Build.bat` directly — the wrapper pins the
engine root, the `SimCopterRemakeEditor Win64 Development` target, and `-NoLiveCoding`. A Live
Coding session holding `UnrealEditor-SimCopterRemake.dll` otherwise makes the build fail to link
or silently hot-patch, leaving you testing stale code.

The script ends in `pause`, so feed it empty stdin or it hangs. PowerShell 5.1 reserves bare `<`,
which is why the redirect lives inside the `cmd /c` string. A clean build is ~60 s and ends with
`Result: Succeeded`. Engine: `C:\GameDev\UE_5.8`. Details in `Docs/memory/build-and-run.md`.

## 3. Testing

Automation tests live in `Source/SimCopterRemake/Private/Tests/` (20 files) and are named
`SimCopter.<Area>.<Case>`, e.g. `SimCopter.Winch.Constants`.

```powershell
& "C:\GameDev\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "S:\Repos\sim-copter-remake\SimCopterRemake\SimCopterRemake.uproject" `
  -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput `
  -ExecCmds="Automation RunTests SimCopter.Winch; Quit"
```

Prefer a headless test over a manual check when the logic is pure (fixed-point maths, table
lookups, parsers). Gameplay and rendering still need the real game — see §7.

## 4. Layout

```
SimCopterRemake/Source/SimCopterRemake/{Public,Private}/
    City/     SC2 city load, terrain, buildings, hangar
    Flight/   helicopter physics, controls, tools
    Formats/  original file-format readers (GEO, DF, SC2, TWK, SIM3D)
    Game/     game modes, session/career subsystems
    Ground/   people, traffic, dispatch, ambient vehicles, particle FX
    Missions/ mission scheduler, fire sim, HUD markers
    UI/       Slate front end, cockpit, hangar shell
    Debug/    dev-only helpers
Docs/         plans, walkthroughs, memory, scratchpad
Tools/        re-agent + ghidra-bridge (Python, venv is gitignored)
Reference/SimCopterOriginalGame   original game files — user-provided and gitignored, but the
                                  source for packaged runtime data
SimCopter/    optional developer override for the same data; `../SimCopter` from the .uproject is
              the repo root here and the automatically populated folder beside the .exe in a
              packaged build
```

Every reader finds that data through `Formats/SimCopterOriginalGamePaths.h` — one candidate list,
not a per-file copy. Add a search root there, never at a call site.

Game-target builds automatically stage the runtime-required original directories (`bmp`, `cities`,
`geo`, `sound`, `tweak`, and `x`) from `Reference/SimCopterOriginalGame` into
`<package root>/SimCopter`. `SimCopterRemake.Build.cs` declares them as loose NonUFS runtime
dependencies through a gitignored `Intermediate/OriginalGameStaging` tree, and
`Config/DefaultGame.ini` remaps that tree to the package root. Do not replace this with a manual
post-package copy, include the original executable/DLLs/manuals/saves, or stage the files inside a
pak: the runtime readers need ordinary filesystem paths. A Game build must fail if any of those six
source directories or its representative required files are absent; an unplayable package is not
an acceptable fallback. Verify packaging against `Manifest_NonUFSFiles_Win64.txt`, as documented in
`Docs/memory/simcopter-packaged-build.md`.

`bUseUnity = false` in `SimCopterRemake.Build.cs`, deliberately: format readers reuse
same-named helpers in anonymous namespaces, and unity chunking collided them whenever a file
was added. Do not turn it back on.

## 5. Porting from the original executable

This is a **decompile-and-port** project, not a reimagining. When you touch ported behaviour:

- Find ground truth first. Main path is the ghidra-bridge over the `.ghidra-exports/` dump:
  `Tools\re-agent\.venv\Scripts\ghidra-bridge.exe decompile 0x4abce0` (also `xrefs-to`,
  `xrefs-from`, `strings`, `search`, `global`). Run from the repo root so `ghidra-bridge.yaml`
  resolves. See `Docs/DecompilationWorkflow.md` and `Docs/memory/simcopter-ghidra-workflow.md`.
- Cite the original in comments: ported functions carry `// SCHOOK: Name 0x00xxxxxx`, and
  explanatory comments name the `FUN_004xxxxx` they came from (~41 in the codebase already).
  Match that — the citation is how the next person re-verifies your port.
- Keep the original's units and arithmetic. Most of the sim is **16.16 fixed point**; angles are
  **tenth-degrees**; distances are 1/64 of a city tile. Converting early loses parity.
- Ghidra's decompile is sometimes wrong about types and signatures. When it looks incoherent,
  read the disassembly or the raw `.rdata` bytes before believing it. The memory notes list
  several functions where this bit.

## 6. The editor MCP

The project runs Unreal's **ModelContextProtocol** plugin, so a running editor can be queried and
driven directly: the loaded level, any actor or asset's properties, the output log, the automation
tests, the Slate UI. Reach for it before guessing at a `.umap` you cannot read, and before launching
the game.

**The config is `SimCopterRemake/.mcp.json` — in the `.uproject` folder, not the repo root.** A
client started at the repo root silently comes up with no MCP tools; that is the usual reason they
appear "missing". Start from `SimCopterRemake/`, or use the raw-HTTP fallback:

```powershell
Tools\Unreal\McpCall.ps1 tools/call '{"name":"list_toolsets","arguments":{}}'
```

Two things surprise everyone: `tools/list` returns only `list_toolsets` / `describe_toolset` /
`call_tool`, so every real call is nested inside `call_tool`; and the schemas are strict, requiring
arguments that look optional. `describe_toolset` first, always.

**Do not run a headless `UnrealEditor-Cmd` (§3's automation tests, the Python bakes) while you need
the editor's MCP.** It binds port 8000 too, and the instance that loses the race logs
`HttpListener unable to bind` and never retries — the editor then has no server while looking fine.
`ModelContextProtocol.StartServer` in the editor console fixes it without a restart. Full workflow,
the toolsets worth knowing and the traps: [Docs/EditorMcpWorkflow.md](Docs/EditorMcpWorkflow.md).

It cannot build C++ — the open editor holds a Live Coding session and the link fails (§2). Build
with the editor closed, then reopen and place new classes through MCP.

## 7. Verifying in-game

**Don't.** Not as a routine step. Launching the game, driving the front end, boarding a
helicopter and synthesizing input is slow, it takes over the machine's foreground window and
keyboard while someone may be using it, and it is fragile: the front end and the possessed pawn
are two different command scopes, centred panels move, and synthesized keys reach Slate and the
console but *not* gameplay input, so most interactive checks cannot be driven from a script
anyway.

Default instead to: build clean, cover the logic with an automation test (§3), get ground truth
from the decompile (§5), ask the running editor over MCP (§6) — then say plainly what you did *not*
verify on screen and leave that last check to whoever is at the keyboard. "Built and unit-tested; not verified in-game" is a
complete report, not an admission.

Reserve an actual run for a genuinely complex problem that nothing cheaper can settle — a
rendering or timing bug that only appears in a live city, say — and prefer to ask first. When it
really is warranted:

```powershell
Start-Process "C:\GameDev\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList `
  '"S:\Repos\sim-copter-remake\SimCopterRemake\SimCopterRemake.uproject"','-game','-windowed','-ResX=1600','-ResY=900','-log'
```

The game boots to `/Game/MainMenu`; no city loads until one is chosen. Console commands shortcut
that: `SimNewCareer <city>`, `SimNewUserGame <index>` on the front end; `SimFreeRoam <city>`,
`SimCityJobs <city>`, `SimLoadMission <index> [city]` (`-1` lists them), `SimMainMenu` in the city
level. `Docs/memory/simcopter-ingame-verification.md` covers driving and screenshotting the Slate
UI from PowerShell — including the trap that centred panels shift, so stale click coordinates
silently no-op.

## 8. Style

- Match the surrounding code: Unreal naming (`F`/`U`/`A` prefixes, `b` for bools), tabs, and the
  existing comment density.
- Comments here explain *why*, and usually cite the original — that is the house style, not
  over-commenting. Keep it.
- Don't add a dependency or an engine plugin without saying so; the enabled set is small
  (`ProceduralMeshComponent`, `ModelingToolsEditorMode`, `ModelContextProtocol`).
- Report honestly. If a build fails, show the output; if you didn't verify in-game, say so.
