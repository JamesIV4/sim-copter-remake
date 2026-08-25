"""Which shipped BHAV programs use a given opcode. Usage: python find_op.py 37 16 40"""
import struct
import sys
from pathlib import Path

REPO = Path(r"S:\Repos\sim-copter-remake")
sys.path.insert(0, str(REPO / "Tools"))
from privanim_extract import DougFile  # noqa: E402

df = DougFile((REPO / "Reference/SimCopterOriginalGame/X/people.df").read_bytes())
wanted = {int(a, 0) for a in sys.argv[1:]} or {37}

for e in df.entries("BHAV"):
    off, _ = df.chunk(e)
    count = struct.unpack_from(">H", df.d, off)[0]
    hits = []
    for i in range(count):
        op = struct.unpack_from(">H", df.d, off + 2 + i * 12)[0]
        if op in wanted:
            hits.append("rec[%d]=op%d" % (i, op))
    if hits:
        print("BHAV %-5d %-42s %s" % (e["id"], e["name"], ", ".join(hits)))
