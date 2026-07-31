"""Scratch: which people.df BHAV programs call a given BHAV id (op >= 0x100 is a subprogram call).

    python find_bhav_callers.py <people.df> <id...>
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Tools"))
from privanim_extract import DougFile

df = DougFile(Path(sys.argv[1]).read_bytes())
wanted = {int(a, 0) for a in sys.argv[2:]}

names = {e["id"]: e["name"] for e in df.entries("BHAV")}

for e in df.entries("BHAV"):
    off, _ = df.chunk(e)
    count = struct.unpack_from(">H", df.d, off)[0]
    for i in range(count):
        o = off + 2 + i * 12
        op = struct.unpack_from(">H", df.d, o)[0]
        if op in wanted:
            print("BHAV %-5d %-40s rec[%2d] -> %d %r"
                  % (e["id"], repr(e["name"]), i, op, names.get(op)))
