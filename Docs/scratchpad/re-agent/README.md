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

Current seed runs:

- `0x004c9cc0` (`FUN_004c9cc0`) - PASS.
- `0x004c2450` (`FUN_004c2450`) - FAIL after 4 rounds; logic mostly matched, but the final checker
  rejected modeling Ghidra's incoming `unaff_ESI` register artifact as an `extern` global.

Current queue:

- `people-spawn-overhaul-targets.txt` - Codex/checker target list for fully faithful spawning
  rules with modernized capacity limits. Launch with
  `Tools/re-agent/watch-people-spawn-overhaul.ps1`.
