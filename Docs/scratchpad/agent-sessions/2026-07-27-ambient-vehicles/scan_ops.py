"""List every BHAV program name, and every site of the ops given on the command line."""
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
        recs.append((struct.unpack_from(">H", df.d, o)[0], _s8(df.d[o + 2]), _s8(df.d[o + 3]),
                     struct.unpack_from(">4H", df.d, o + 4)))
    programs[e["id"]] = (e["name"], recs)

want = {int(a, 0) for a in sys.argv[1:]}
if want:
    for pid in sorted(programs):
        name, recs = programs[pid]
        for i, (op, tn, fn, args) in enumerate(recs):
            if op in want:
                print(f"op{op:3d}  BHAV {pid:4d} '{name}' rec[{i:2d}] args={args} T->{tn} F->{fn}")
else:
    for pid in sorted(programs):
        print(f"{pid:5d}  {programs[pid][0]}")
