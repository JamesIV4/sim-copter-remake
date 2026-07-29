import struct, sys
from pathlib import Path
root = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP")
names = sys.argv[1:] if len(sys.argv) > 1 else [p.name for p in sorted(root.glob("*.BMP"))]
for n in names:
    p = root / n
    if not p.exists():
        print(n, "MISSING"); continue
    d = p.read_bytes()
    if d[:2] != b"BM":
        print(n, "not BM", d[:4]); continue
    fsize, _, _, off = struct.unpack_from("<IHHI", d, 2)
    hdr = struct.unpack_from("<I", d, 14)[0]
    if hdr >= 40:
        w, h, planes, bpp, comp = struct.unpack_from("<iiHHI", d, 18)
    else:
        w, h, planes, bpp = struct.unpack_from("<hhHH", d, 18); comp = 0
    print(f"{n:<14} {w}x{h} bpp={bpp} comp={comp} hdr={hdr} off={off} size={len(d)}")
