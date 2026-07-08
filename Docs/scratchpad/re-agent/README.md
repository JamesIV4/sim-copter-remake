# re-agent Runs

Tracked outputs from Codex-backed `re-agent` reversal runs. These are evidence artifacts, not
accepted source ports.

- `code/` - latest generated C++ candidate per function address. New runs should start with a
  compact `RE_AGENT_NOTE` comment documenting purpose, where to use/port it, evidence, and caveats.
- `re-agent-progress.json` - current pass/fail summary by function address.

Raw reverser/checker JSON and live watch transcripts are intentionally local-only under
`reports/re-agent/logs/`. They are useful while a run is hot, but too noisy to track by default.

Treat `RE_AGENT_NOTE` comments as review notes, not confirmed documentation. Promote claims into
the main docs only after outside review against xrefs, surrounding callers, data files, and the
remake implementation.

Current accepted runs:

- `0x004c9cc0` (`FUN_004c9cc0`) - PASS; ambient tile/density gate.
- `0x004c2550` (`FUN_004c2550`) - PASS; per-tile ambient spawn attempt wrapper.
- `0x004c25b0` (`FUN_004c25b0`) - PASS; scripted building ambient spawns.
- `0x004c2450` (`FUN_004c2450`) - PASS; ambient behavior-class selector retry.
- `0x004c2ba0` (`FUN_004c2ba0`) - PASS; ambient spawn driver / camera-edge population pressure.
- `0x004c3eb0` (`FUN_004c3eb0`) - PASS; spawn wrapper into `FUN_004c4190`.
- `0x004c92a0` (`FUN_004c92a0`) - PASS; leaf `DAT_005bde80` tile-byte gate.

Current failed-but-saved runs:

- `0x004c4190` (`FUN_004c4190`) - FAIL after 4 rounds; candidate captures the main spawn
  configurator structure, but checker/objective still found control-flow mismatches in the large
  spawn-mode switch.
- `0x004c02a0` (`FUN_004c02a0`) - FAIL after 4 rounds; objective verifier passed, but the checker
  rejected case 2's hardcoded `unaff_EDI` high half and premature `CellY + 3` truncation in
  fixed-point world-Y expressions.

Current queue:

- `people-spawn-overhaul-targets.txt` - Codex/checker target list for fully faithful spawning
  rules with modernized capacity limits. Launch with
  `Tools/re-agent/watch-people-spawn-overhaul.ps1`.

Queue entries still not run in this pass:

- `0x004abce0` (`FUN_004abce0`) - terrain/density grid builder for `DAT_005bde80`.
- `0x004c3010` (`FUN_004c3010`) - people runtime table initialization, including
  `DAT_0058d6d4` caps.
- `0x004c9470` (`FUN_004c9470`) - per-step move gate and per-tile occupancy cap use.
