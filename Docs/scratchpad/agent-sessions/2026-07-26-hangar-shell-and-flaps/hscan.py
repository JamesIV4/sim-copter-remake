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

name, x, y0, y1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
w, h, pal, rows = read_bmp(root/name)
prev = None; start = y0
for y in range(y0, min(y1, h)):
    c = pal[rows[y][x]]
    if prev is None:
        prev = c; start = y; continue
    if sum(abs(a-b) for a, b in zip(c, prev)) > 70:
        print(f"  y {start:3d}..{y-1:3d} ({y-start:3d}) rgb{prev}")
        start = y
    prev = c
print(f"  y {start:3d}..{min(y1,h)-1:3d} rgb{prev}")
