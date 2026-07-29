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
lookups, parsers). Gameplay and rendering still need the real game — see §6.

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
Reference/SimCopterOriginalGame   original game files — user-provided, gitignored
```

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

## 6. Verifying in-game

Pure logic → automation test. Anything visual or interactive → run it:

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

## 7. Style

- Match the surrounding code: Unreal naming (`F`/`U`/`A` prefixes, `b` for bools), tabs, and the
  existing comment density.
- Comments here explain *why*, and usually cite the original — that is the house style, not
  over-commenting. Keep it.
- Don't add a dependency or an engine plugin without saying so; the enabled set is small
  (`ProceduralMeshComponent`, `ModelingToolsEditorMode`, `ModelContextProtocol`).
- Report honestly. If a build fails, show the output; if you didn't verify in-game, say so.
