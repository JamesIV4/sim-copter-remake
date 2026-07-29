import sys, collections, struct
sys.path.insert(0, "Tools")
from privanim_extract import Privanim
from pathlib import Path

pa = Privanim(Path("Reference/SimCopterOriginalGame/X/privanim.df").read_bytes())

# LOD mask histogram per type
lod = collections.Counter()
for fe in pa.figures():
    for p in pa.skeleton(fe["name"]):
        lod[(p["type"], p["f3"][1])] += 1
print("(type, lodmask) histogram:")
for k in sorted(lod):
    print(f"   type=0x{k[0]:02x} lod=0x{k[1]:02x} -> {lod[k]}")
print()

# per figure: head part (type 9) dims, and all circle parts
for fe in pa.figures():
    name = fe["name"]
    parts = pa.skeleton(name)
    heads = [p for p in parts if p["type"] == 9]
    circles = [p for p in parts if p["type"] in (8, 0x0d, 0x0e)]
    px = [p for p in parts if p["type"] == 0x0c]
    print(f"{name:10} head={[ (p['index'], p['dims'], hex(p['f3'][1])) for p in heads ]}")
    print(f"           circles={len(circles)} " +
          " ".join(f"[{p['index']}:t{p['type']:x} d0={p['dims'][0]} lod={p['f3'][1]:#x}]" for p in circles[:14]))
    print(f"           pixels={len(px)}")
print()

# exe constants
d = Path("Reference/SimCopterOriginalGame/SimCopter.exe").read_bytes()
pe = struct.unpack_from("<I", d, 0x3c)[0]
nsec = struct.unpack_from("<H", d, pe + 6)[0]
optsz = struct.unpack_from("<H", d, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    vsz, va, rsz, raw = struct.unpack_from("<IIII", d, o + 8)
    secs.append((d[o:o+8].rstrip(b"\0").decode(), va, vsz, raw, rsz))
def off(addr):
    r = addr - 0x400000
    for nm, va, vsz, raw, rsz in secs:
        if va <= r < va + max(vsz, rsz):
            return raw + (r - va)
for a in (0x506428, 0x50642c, 0x506430, 0x506434, 0x4f4fa0, 0x4f4fa8):
    o = off(a)
    raw = d[o:o+4]
    print(f"{a:#x} raw={raw.hex()} int={struct.unpack('<i', raw)[0]} float={struct.unpack('<f', raw)[0]}")
