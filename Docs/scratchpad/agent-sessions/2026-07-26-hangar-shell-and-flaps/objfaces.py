import struct, sys
from pathlib import Path
root = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO")

def u16(d, o): return int.from_bytes(d[o:o+2], "little")
def u32(d, o): return int.from_bytes(d[o:o+4], "little")
def i32(d, o): return int.from_bytes(d[o:o+4], "little", signed=True)
def nm(d, o, n): return d[o:o+n].split(b"\0", 1)[0].decode("ascii", "replace")

want = [int(a, 0) for a in sys.argv[1:]]

for p in sorted(root.glob("*.[Mm][Aa][Xx]")):
    d = p.read_bytes()
    geom = u32(d, 24)
    entry_count = u32(d, geom + 8)
    entry_offset = u32(d, geom + 16)
    offsets = []
    for i in range(1, entry_count):
        off = entry_offset + i * 53
        offsets.append((nm(d, off, 17), u32(d, off + 17)))
    sorted_offsets = sorted(set(o for _, o in offsets))

    for name, oo in offsets:
        if d[oo:oo+4] != b"OBJX":
            continue
        oid = i32(d, oo + 120)
        if oid not in want:
            continue
        vc = u16(d, oo + 8)
        fc = u16(d, oo + 10)
        print(f"=== object 0x{oid:03x} {name} in {p.name}: {vc} verts {fc} faces")
        verts = []
        for v in range(vc):
            vo = oo + 124 + v * 12
            verts.append((i32(d, vo), i32(d, vo + 4), i32(d, vo + 8)))
        xs = [v[0] for v in verts]; ys = [v[1] for v in verts]; zs = [v[2] for v in verts]
        S = 2621.44
        print(f"    bounds cm  x {min(xs)/S:.1f}..{max(xs)/S:.1f}  y {min(ys)/S:.1f}..{max(ys)/S:.1f}  z {min(zs)/S:.1f}..{max(zs)/S:.1f}")
        fo = oo + 124 + vc * 12
        stats = {}
        for f in range(fc):
            if d[fo:fo+4] != b"FACE":
                print("    bad FACE at", fo); break
            size = u32(d, fo + 4)
            fvc = u16(d, fo + 8)
            ftype = d[fo + 18]
            mat = d[fo + 19]
            page = d[fo + 20]
            uvs = []
            cur = fo + 21 + fvc * 2
            for k in range(fvc):
                uvs.append((i32(d, cur), i32(d, cur + 4)))
                cur += 8
            key = (ftype, page, mat)
            stats[key] = stats.get(key, 0) + 1
            if f < 6:
                print(f"    face{f}: type={ftype} page={page} cell={mat} verts={fvc} uv0={uvs[:3]}")
            fo += size
        print("    face (type,page,cell) histogram:")
        for k, n in sorted(stats.items(), key=lambda kv: -kv[1]):
            print(f"      type={k[0]:3d} page={k[1]:3d} cell={k[2]:3d}  x{n}")
