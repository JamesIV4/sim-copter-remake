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

name = sys.argv[1]
y = int(sys.argv[2])
w, h, pal, rows = read_bmp(root/name)
prev = None
start = 0
for x in range(w):
    c = pal[rows[y][x]]
    if prev is None:
        prev = c; start = x; continue
    if abs(c[0]-prev[0]) + abs(c[1]-prev[1]) + abs(c[2]-prev[2]) > 90:
        print(f"  x {start:3d}..{x-1:3d} ({x-start:3d}) rgb{prev}")
        start = x
    prev = c
print(f"  x {start:3d}..{w-1:3d} ({w-start:3d}) rgb{prev}")
