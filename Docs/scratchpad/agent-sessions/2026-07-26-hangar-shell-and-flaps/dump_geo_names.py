import sys, struct
from pathlib import Path

root = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO")

def u16(d,o): return int.from_bytes(d[o:o+2],"little")
def u32(d,o): return int.from_bytes(d[o:o+4],"little")
def i32(d,o): return int.from_bytes(d[o:o+4],"little",signed=True)
def name(d,o,n): return d[o:o+n].split(b"\0",1)[0].decode("ascii","replace")

rows = []
for p in sorted(root.glob("*.[Mm][Aa][Xx]")):
    d = p.read_bytes()
    geom = u32(d,24)
    entry_count = u32(d, geom+8)
    entry_offset = u32(d, geom+16)
    for i in range(1, entry_count):
        off = entry_offset + i*53
        nm = name(d, off, 17)
        oo = u32(d, off+17)
        if d[oo:oo+4] != b"OBJX": continue
        oid = i32(d, oo+120)
        flags = u32(d, oo+12)
        vc = u16(d, oo+8); fc = u16(d, oo+10)
        rows.append((oid, nm, p.name, flags, vc, fc))

rows.sort()
for oid, nm, f, flags, vc, fc in rows:
    print(f"0x{oid:03x} {oid:4d}  {nm:<18} {f:<12} flags=0x{flags:x} v={vc} f={fc}")
