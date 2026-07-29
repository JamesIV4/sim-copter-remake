import struct, sys
from pathlib import Path
root = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP")

def read_bmp(path):
    d = path.read_bytes()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h, planes, bpp = struct.unpack_from("<iiHH", d, 18)
    pal_off = 14 + struct.unpack_from("<I", d, 14)[0]
    pal = []
    for i in range((off - pal_off) // 4):
        b, g, r, _ = d[pal_off + i*4: pal_off + i*4 + 4]
        pal.append((r, g, b))
    flip = h > 0; h = abs(h)
    stride = ((w * bpp + 31)//32)*4
    rows = [d[off + y*stride: off + y*stride + stride] for y in range(h)]
    if flip: rows.reverse()
    return w, h, pal, rows

name = sys.argv[1]
w, h, pal, rows = read_bmp(root/name)
# blue-ish grid: b noticeably greater than r, and fairly bright
minx, miny, maxx, maxy, n = w, h, -1, -1, 0
for y in range(h):
    r = rows[y]
    for x in range(w):
        cr, cg, cb = pal[r[x]]
        if cb > cr + 25 and cb > 120 and cr > 60:
            n += 1
            if x < minx: minx = x
            if x > maxx: maxx = x
            if y < miny: miny = y
            if y > maxy: maxy = y
print(f"{name}: blueish n={n} bbox=({minx},{miny})-({maxx},{maxy}) size={maxx-minx+1}x{maxy-miny+1}")
