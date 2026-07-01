---
name: simcopter-ghidra-workflow
description: How to decompile SimCopter.exe with Ghidra headless on this machine
metadata: 
  node_type: memory
  type: reference
  originSessionId: 2bd18189-b198-4105-9d35-8b3729691cf9
---

Ghidra is installed via scoop at `C:\Users\james\scoop\apps\ghidra\current` (headless at `support\analyzeHeadless.bat`); Java 21 (temurin) is on PATH. `analyzeHeadless` is NOT on PATH.

Reusable exploration script: `Tools/Ghidra/ReverseExplore.java` (writes clean UTF-8 to a file; commands: `strings <substr>`, `xrefsto <hex>`, `decompile <hex>...`, `func <name>`, `callers <hex>`, `bytes <hex> <n>`). The older `Tools/Ghidra/DecompileAddresses.java` prints to stdout (gets UTF-16-mangled when captured by PowerShell — prefer ReverseExplore).

Workflow:
1. Import + analyze once (~40s): `analyzeHeadless <projDir> SimCopter -import <exe> -scriptPath Tools/Ghidra`
2. Re-run scripts against the saved program with `-process SimCopter.exe -noanalysis -postScript ReverseExplore.java <outFile> <cmd> ...`

The exe is PE32 x86, image base 0x400000, no symbols (all funcs are `FUN_xxxx`). See [[simcopter-mesh-orientation-rules]] for the city geometry findings.
