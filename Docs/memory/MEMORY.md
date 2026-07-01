# Memory Index

- [SimCopter mesh orientation rules](simcopter-mesh-orientation-rules.md) — original game uses no per-tile mesh rotation; col axis is negated in world placement.
- [SimCopter Ghidra workflow](simcopter-ghidra-workflow.md) — how to decompile SimCopter.exe headlessly on this machine.
- [SimCopter privanim DECODED](simcopter-privanim-decoded.md) — exact DF container spec (2026-07-01); 21 named figures incl. Elvis/Nessie; ARPP = per-frame line segments; OLD display rules were wrong; people.df = same container.
- [SimCopter UE figure component](simcopter-ue-figure-component.md) — Phase 3 done: privanim figures render in UE; ARPP Z is y-down (negate!); unity builds disabled in Build.cs; engine at C:\GameDev\UE_5.8.
- [SimCopter population rendering](simcopter-population-rendering.md) — car headlights are face-type-11 beam cards; people aren't in GEO (use procedural box body); ground agents must Camera-trace from high up or they hover. (privanim claims in it superseded by simcopter-privanim-decoded.)
- [SimCopter live memory rip](simcopter-live-memory-rip.md) — read live process with ReadProcessMemory for .data ONLY (SimCopterX relocates .text in memory; read code from Ghidra); per-region .data calibration.
- [SimCopter people logic (next goal)](simcopter-people-logic-next.md) — next RE target: decode the 88-handler behavior VM + spawn rules; VM = walker FUN_004ce7b0 opcodes, thunk table 0x4c84e0+0x20n (map in out_vm_handlers.txt).
