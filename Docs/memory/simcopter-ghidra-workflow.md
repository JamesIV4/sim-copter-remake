# SimCopter ghidra workflow

*How to decompile SimCopter.exe — MAIN PATH is ghidra-bridge queries over the .ghidra-exports JSON dump; analyzeHeadless is the fallback*

*Recorded 2026-07-26; ported into the repo 2026-07-29.*

**Main path (2026-07-07, replaces analyzeHeadless for most queries):** `ghidra-ai-bridge` +
`auto-re-agent` (both Dryxio, MIT, source-audited) installed in `Tools/re-agent/.venv` (gitignored).
Run from repo root so the committed `ghidra-bridge.yaml` / `re-agent.yaml` are found. Full docs:
repo `Docs/DecompilationWorkflow.md` § "Main Decode Path".

- Instant queries over a one-time export (`.ghidra-exports/`, 2764 functions w/ decompile+xrefs,
  1430 globals, 1108 strings): `Tools\re-agent\.venv\Scripts\ghidra-bridge.exe decompile 0x4abce0 |
  xrefs-to | xrefs-from | strings <pat> | containing <addr> | global <addr> | search <pat> | info`.
- Refresh export after changing the Ghidra project: `ghidra-bridge.exe export all` (~3 min via
  PyGhidra; Ghidra 12.1.2 at scoop path).
- Ported C++ gets `// SCHOOK: Name 0x00xxxxxx` comments; `ghidra-bridge build-map` +
  `re-agent parity --address <a>` heuristic-check ports vs decompile (no LLM needed).
- `re-agent reverse --address <a>` = LLM reverser/checker loop (needs ANTHROPIC_API_KEY or ant
  profile, bills usage; `--dry-run` is free). Config model: claude-opus-4-8.

**Fallback (byte scans, raw dumps, vtable-only code):** Ghidra at
`C:\Users\james\scoop\apps\ghidra\current` (headless at `support\analyzeHeadless.bat`, NOT on PATH);
project `Docs/scratchpad/ghidra` / name `SimCopter` / program `SimCopter.exe`. Reusable script
`Tools/Ghidra/ReverseExplore.java` (commands: `scan <hexbytes>`, `bytes`, `disasm`,
`decompileforce`, plus strings/xrefsto/decompile which the bridge now covers):
`analyzeHeadless Docs/scratchpad/ghidra SimCopter -process SimCopter.exe -noanalysis -postScript
ReverseExplore.java <outFile> <cmd> ...`. After `decompileforce` creates new functions, re-run
`export decompiled` so the bridge sees them.

Fallback details carried over from the older note (still true):

- Java 21 (temurin) is on PATH; `analyzeHeadless` itself is not.
- `ReverseExplore.java` also has `func <name>` and `callers <hex>`, and writes clean UTF-8 to a
  file. The older `Tools/Ghidra/DecompileAddresses.java` prints to stdout and gets **UTF-16
  mangled** when captured by PowerShell — always prefer ReverseExplore.
- First-time import + analyze (~40 s, only needed once):
  `analyzeHeadless <projDir> SimCopter -import <exe> -scriptPath Tools/Ghidra`

The exe is PE32 x86, image base 0x400000, no symbols (all funcs `FUN_xxxx`). See
[[simcopter-mesh-orientation-rules]] for city geometry findings.

**When the bridge comes up empty, go to the bytes** (proven 2026-07-26 chasing a struct field's
writer). `pip install capstone`, parse the PE section table, and:
- *vtables*: the bridge has no vtable export; read the entries straight out of `.rdata` at the
  `PTR_FUN_*` address. Some vtable targets are not exported as functions at all (Ghidra folds
  them into a neighbour's tail) - disassemble those by hand.
- *"no callers found"* from `xrefs-to` is often wrong. If the address appears nowhere as a 4-byte
  pointer it is not a vtable entry, so scan every function for `call rel32` targets instead.
- *finding who writes a struct field*: disassemble **per function** using the index's boundaries
  and filter for `dword ptr [reg + N]` writes. A linear sweep of the whole `.text` silently
  desyncs on padding and returns partial results - always sanity-check the scanner against a
  write you already know exists before trusting a negative.
