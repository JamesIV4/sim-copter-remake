"""Scratch: list every people.df BHAV that uses a given opcode (default 54, the seat-window face).

Opcode 54 = FUN_004ccb40 -> FUN_0048c0e0(manifest, personId, arg0): it writes the seat record's
row, i.e. which of people1.bmp's three rows the passenger's portrait is drawn from.

    python find_op54.py <people.df> [opcode...]
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Tools"))
from privanim_extract import DougFile, _s8

df = DougFile(Path(sys.argv[1]).read_bytes())
wanted = {int(a, 0) for a in sys.argv[2:]} or {54}

for e in df.entries("BHAV"):
    off, _ = df.chunk(e)
    count = struct.unpack_from(">H", df.d, off)[0]
    hits = []
    for i in range(count):
        o = off + 2 + i * 12
        op = struct.unpack_from(">H", df.d, o)[0]
        args = struct.unpack_from(">4H", df.d, o + 4)
        if op in wanted:
            hits.append((i, op, args))
    if hits:
        print("BHAV %-5d %-40s" % (e["id"], repr(e["name"])))
        for (i, op, args) in hits:
            print("    rec[%2d] op%-3d args=%s" % (i, op, ",".join(str(a) for a in args)))
