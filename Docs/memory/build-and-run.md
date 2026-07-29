# Building the C++ — always use RebuildUnrealCpp.bat

To compile, run `RebuildUnrealCpp.bat` at the repo root. Do **not** invoke
`C:\GameDev\UE_5.8\Engine\Build\BatchFiles\Build.bat` directly.

**Why:** the wrapper pins the engine root, the project file, the
`SimCopterRemakeEditor Win64 Development` target and — the part that matters —
`-NoLiveCoding`. A Live Coding session holding `UnrealEditor-SimCopterRemake.dll` makes a raw
Build.bat run fail to link, or silently hot-patch instead of relinking, so you end up testing
stale code. James rejected a direct Build.bat invocation and asked for the wrapper.

**How to run it non-interactively:** the script ends in `pause`, so feed it empty stdin or it
hangs waiting for a keypress. From PowerShell:

```powershell
cmd /c "S:\Repos\sim-copter-remake\RebuildUnrealCpp.bat < nul" 2>&1 | Select-Object -Last 35
```

(PowerShell 5.1 reserves bare `<`, hence the redirect living inside the `cmd /c` string.)
A clean build is ~60 s and ends with `Result: Succeeded`.

Launching the built game to verify a change: see `simcopter-ingame-verification` notes —
`-game -windowed`, then drive and screenshot the Slate UI from PowerShell.
Long build logs belong in `Docs/scratchpad/` (see `agent-workspace-conventions.md`).
