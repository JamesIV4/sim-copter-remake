import struct, sys
from pathlib import Path
root = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP")

def read_bmp(path):
    d = path.read_bytes()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h, planes, bpp = struct.unpack_from("<iiHH", d, 18)
    pal_off = 14 + struct.unpack_from("<I", d, 14)[0]
    pal = [tuple(reversed(d[pal_off+i*4: pal_off+i*4+3])) for i in range((off-pal_off)//4)]
    flip = h > 0; h = abs(h)
    stride = ((w*bpp+31)//32)*4
    rows = [d[off+y*stride: off+y*stride+stride] for y in range(h)]
    if flip: rows.reverse()
    return w, h, pal, rows

name, x, target = sys.argv[1], int(sys.argv[2]), tuple(int(v) for v in sys.argv[3].split(","))
w, h, pal, rows = read_bmp(root/name)
ys = [y for y in range(h) if sum(abs(a-b) for a, b in zip(pal[rows[y][x]], target)) < 40]
print(name, f"col {x} colour{target}: rows", (ys[0], ys[-1]) if ys else None, "count", len(ys))
