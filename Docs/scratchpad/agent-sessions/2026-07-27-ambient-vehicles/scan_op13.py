"""Find every BHAV record that runs op 13 (FUN_004caac0 -> FUN_004ccf50 mission-event post)."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, r"S:\Repos\sim-copter-remake\Tools")
from privanim_extract import DougFile, _s8

df = DougFile(Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\X\people.df").read_bytes())
programs = {}
for e in df.entries("BHAV"):
    off, _ = df.chunk(e)
    count = struct.unpack_from(">H", df.d, off)[0]
    recs = []
    for i in range(count):
        o = off + 2 + i * 12
        op = struct.unpack_from(">H", df.d, o)[0]
        tn = _s8(df.d[o + 2])
        fn = _s8(df.d[o + 3])
        args = struct.unpack_from(">4H", df.d, o + 4)
        recs.append((op, tn, fn, args))
    programs[e["id"]] = (e["name"], recs)

print(f"{len(programs)} BHAV programs")
print("--- op 13 (mission-event post) sites ---")
for pid in sorted(programs):
    name, recs = programs[pid]
    for i, (op, tn, fn, args) in enumerate(recs):
        if op == 13:
            print(f"BHAV {pid:4d} '{name}' rec[{i:2d}] args={args} T->{tn} F->{fn}")

print()
print("--- callers of each op-13 program ---")
targets = {pid for pid in programs if any(r[0] == 13 for r in programs[pid][1])}
for pid in sorted(programs):
    name, recs = programs[pid]
    for i, (op, tn, fn, args) in enumerate(recs):
        if op >= 0x100 and op in targets:
            print(f"BHAV {pid:4d} '{name}' rec[{i:2d}] CALL {op} '{programs[op][0]}'")
