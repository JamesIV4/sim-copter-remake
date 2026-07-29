# CLAUDE.md

**Read [AGENTS.md](AGENTS.md) — it holds the instructions for this repo, and they apply to you.**

Then read `Docs/memory/MEMORY.md`, the project's memory index.

The three things that bite most often, so they are also here:

1. **Write everything in the repo.** Memories go in `Docs/memory/` (indexed in
   `Docs/memory/MEMORY.md`); scratch files, build logs and screenshots go in `Docs/scratchpad/`.
   Not in Claude's machine-local memory directory, not in the session temp scratchpad — those are
   untracked and invisible to everyone else.

2. **Build with `RebuildUnrealCpp.bat` at the repo root**, never `Build.bat` directly:
   ```powershell
   cmd /c "S:\Repos\sim-copter-remake\RebuildUnrealCpp.bat < nul"
   ```
   The wrapper pins `-NoLiveCoding`; the trailing `pause` needs the empty stdin.

3. **This is a decompile-and-port project.** Get ground truth from the original executable before
   changing ported behaviour, cite the `FUN_004xxxxx` you ported from, and keep the original's
   16.16 fixed-point units.

Everything else — layout, tests, in-game verification, style — is in [AGENTS.md](AGENTS.md).
