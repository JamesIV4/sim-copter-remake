# Agent workspace conventions — everything lives in the repo

James's standing rule: **anything an agent produces for this project goes in the repo**, not in
Claude Code's per-machine built-in directories.

| What | Put it here | NOT here |
| --- | --- | --- |
| Memories / durable notes | `Docs/memory/` + a line in `Docs/memory/MEMORY.md` | `C:\Users\james\.claude\projects\s--Repos-sim-copter-remake\memory\` |
| Scratch work: build logs, throwaway scripts, screenshots, decompile dumps, render comparisons | `Docs/scratchpad/` | `%LOCALAPPDATA%\Temp\claude\...\scratchpad` |
| Plans, walkthroughs, port plans | `Docs/` (e.g. `Docs/Milestone5SimulationPlan.md`) | — |

**Why:** the built-in memory and scratchpad directories are machine-local, per-session, and
invisible to git. Notes written there are lost to anyone else on the project, lost on a new
machine, and cannot be reviewed in a diff. The repo already established this — `Docs/memory/`
carries the canonical decode notes and `Docs/scratchpad/` holds `build*.log`,
`export_tiled1.py`, `city_terrain_render.png`, the `ghidra/out_*.txt` dumps cited by the flight
model notes, and so on.

**How to apply:**

- Writing a memory → create `Docs/memory/<slug>.md`, add a one-line pointer to
  `Docs/memory/MEMORY.md`. Only a short pointer line belongs in the built-in `MEMORY.md`, and
  only so a cold session knows to come read this index.
- Starting a session → read `Docs/memory/MEMORY.md`; it is the real index.
- Any temp file → `Docs/scratchpad/`. Use it freely; that is what it is for.

**Known drift (2026-07-29):** the built-in memory dir has 22 topic notes, `Docs/memory/` has 9.
Roughly fourteen notes written between 2026-07-02 and 2026-07-27 (terrain flattening, mission
system, fire/water FX, instanced buildings, water gameplay, heli tools/models, emergency
dispatch, in-game verification, airport spawn, speeder pursuit, hangar shell, cockpit flaps,
ambient vehicles, vertex-animation WPO) exist only outside the repo and still need copying in.
