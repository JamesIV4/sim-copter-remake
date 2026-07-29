# Agent session scratch (ported 2026-07-29)

Analysis scripts and dumps that AI agents produced between 2026-07-24 and 2026-07-28, recovered
from Claude Code's machine-local temp scratchpads and moved into the repo per
`Docs/memory/agent-workspace-conventions.md`. Everything new goes straight to `Docs/scratchpad/`
now, so this directory is a one-time rescue, not a pattern to copy.

Session ids were opaque GUIDs; each folder is renamed `<date>-<topic>`, where the topic was read
off the scripts inside it and cross-checked against the note it produced in `Docs/memory/`.

| Folder | Files | What was being worked out | Resulting note |
| --- | --- | --- | --- |
| `2026-07-24-people-behaviors` | 1 | listing every `people.df` BHAV id + name | `simcopter-people-logic-next.md` |
| `2026-07-24-heli-tools` | 11 | decompiles around 0x484d20–0x48ed00 (spotlight, tool dispatch) | `simcopter-heli-tools-models.md` |
| `2026-07-24-cockpit-and-menu` | 22 | dashboard/seat layout, palette column sampling, material-usage fixes, menu map | `simcopter-heli-tools-models.md` |
| `2026-07-25-airport-and-dispatch` | 67 | airport stamp verification, zone bits, GEO ids, vtable/`off8` hunts, dispatch aim funcs | `simcopter-airport-spawn.md`, `simcopter-emergency-dispatch.md` |
| `2026-07-26-hangar-shell-and-flaps` | 27 | SIM3D page/cell extraction, BMP→PNG, tab strips, string table dump | `simcopter-hangar-shell.md`, `simcopter-cockpit-flaps.md` |
| `2026-07-27-ambient-vehicles` | 37 | boat/plane/rail spawn+move disassembly, opcode maps, rail scanning | `simcopter-ambient-vehicles.md` |
| `2026-07-27-privanim-figures` | 10 | privanim part/dimension analysis via `Tools/privanim_extract.py` | `simcopter-privanim-decoded.md` |
| `2026-07-27-population-and-cards` | 12 | dog/tree stats, GEO object dumps, effect-card geometry | `simcopter-population-rendering.md`, `simcopter-fire-water-fx.md` |
| `2026-07-28-ingame-driving` | 1 | Win32 input driver for the running game | `simcopter-ingame-verification.md` |

## What was deliberately left behind

Regenerable byproducts, dropped rather than committed:

| Kind | Size | Why |
| --- | --- | --- |
| 165 `.png` | 139 MB | in-game verification screenshots — point-in-time, huge, and re-shootable |
| 29 `.log` | 8.5 MB | build / automation-test / game logs |
| 44 `.output` | 1.2 MB | background-task stdout captures (harness plumbing, not analysis) |
| `privanim.json` | 11.5 MB | regenerable with `Tools/privanim_extract.py`; the spec it produced is in `simcopter-privanim-decoded.md` |

The migration scripts themselves are `Docs/scratchpad/port_agent_memory.ps1` and
`Docs/scratchpad/port_agent_scratch.ps1`, both re-runnable and idempotent.

## Caveats

These are throwaway analysis scripts, kept for the working-out rather than for reuse. Most
hardcode absolute paths like `S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame`, several
are numbered iterations of the same idea (`dumpobj.py` / `dumpobj2.py`, `find_off8.py` /
`find_off8b.py`), and same-named files across folders are genuinely different (there are four
distinct `shot.ps1`). Read one before trusting it. The conclusions they reached are in
`Docs/memory/` — that is the part that was worth keeping.
