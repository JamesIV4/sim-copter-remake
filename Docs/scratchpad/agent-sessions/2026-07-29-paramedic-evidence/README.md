# Paramedic evidence pass - 2026-07-29

This pass corrected an earlier dispatch decode that had mistaken the stopped
criminal-car deployment for the ambulance deployment.

## Executable evidence

- `ambulance_vtable_methods.txt` contains the forced decompile of
  `FUN_004b8f60`, reached from the ambulance vtable at `0x004f4d20`.
  Its on-scene arms call `FUN_004bd980(0x0c, 5, ...)`.
- `object_class_10_12_disasm.txt` contains the raw `FUN_004cac70` jump-table
  cases. Classes 10..12 pass kinds 0..2 to `FUN_0049b060`.
- Ambulance construction (`FUN_004b8e10`) binds kind 0 to
  `DAT_00582b20`; police uses kind 1 / `DAT_00582b50`; fire uses kind 2 /
  `DAT_00582b38`.
- `FUN_004c4190` calls `FUN_004c4e10(param_7)` on the vehicle-deployed person
  path, recording the starting vehicle at `person+0x170`.

The executable work used the repo's Ghidra project and bridge workflow. The
forced decompile was needed because the normal export did not contain the
ambulance vtable method.

## Shipped behavior evidence

Run:

```powershell
& Tools/re-agent/.venv/Scripts/python.exe `
  Tools/people_bhav_dump.py `
  Reference/SimCopterOriginalGame/people.df `
  801 262 272 275 285 269 263 282
```

The relevant chain is:

```text
state 5 -> BHAV 801
street -> 262 -> search state-6 victim -> op44 tote
       -> 272 -> object class 10 -> 275 -> op51 set down
       -> push 285 -> outcome 0 -> outcome 1 -> disappear
       -> 269 -> select starting vehicle -> board -> opcode 61
hospital D1 -> 263 -> select patient aboard -> op47 -> op44
            -> set down -> patient BHAV 282 delivers and leaves map
```

No BHAV in either chain creates a building or doorway. The popup doorway was a
remake-side scripted handoff and is not original behavior.
