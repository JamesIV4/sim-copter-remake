import struct, sys, importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "pbd", r"S:\Repos\sim-copter-remake\Tools\people_bhav_dump.py")
pbd = importlib.util.module_from_spec(spec)
sys.argv = ["x"]          # keep its main() from running on import
spec.loader.exec_module(pbd)

df = pbd.DougFile(Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\X\people.df").read_bytes())

WANTED = int(sys.argv[1]) if len(sys.argv) > 1 else 60
progs = {}
for e in df.entries("BHAV"):
    off, _ = df.chunk(e)
    count = struct.unpack_from(">H", df.d, off)[0]
    recs = []
    for i in range(count):
        o = off + 2 + i * 12
        recs.append((struct.unpack_from(">H", df.d, o)[0],
                     pbd._s8(df.d[o + 2]), pbd._s8(df.d[o + 3]),
                     struct.unpack_from(">4H", df.d, o + 4)))
    progs[e["id"]] = (e["name"], recs)

print(f"{len(progs)} BHAV programs in people.df; searching for opcode {WANTED}\n")
hits = 0
for pid, (name, recs) in sorted(progs.items()):
    for i, (op, tn, fn, args) in enumerate(recs):
        if op == WANTED:
            hits += 1
            print(f"  BHAV {pid} '{name}' record[{i}]  op={op} args={args} T->{tn} F->{fn}")
print(f"\n{hits} use(s)")

# Who calls those programs?
callers = {}
for pid, (name, recs) in progs.items():
    for op, tn, fn, args in recs:
        if op == 1:      # CALL
            callers.setdefault(args[0], set()).add((pid, name))
print("\ncallers of each hit program:")
for pid, (name, recs) in sorted(progs.items()):
    if any(op == WANTED for op, _, _, _ in recs):
        for cpid, cname in sorted(callers.get(pid, [])):
            print(f"  BHAV {pid} '{name}'  <- called by {cpid} '{cname}'")
