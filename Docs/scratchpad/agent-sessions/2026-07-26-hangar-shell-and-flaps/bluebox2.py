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
ylimit = int(sys.argv[2]) if len(sys.argv) > 2 else 10**9
w, h, pal, rows = read_bmp(root/name)
colcount = [0]*w
rowcount = [0]*h
for y in range(min(h, ylimit)):
    r = rows[y]
    for x in range(w):
        cr, cg, cb = pal[r[x]]
        if cb > cr + 25 and cb > 120 and cr > 60:
            colcount[x] += 1
            rowcount[y] += 1
cols = [x for x in range(w) if colcount[x] > 5]
rws = [y for y in range(min(h, ylimit)) if rowcount[y] > 5]
print(f"{name}: cols {cols[0]}..{cols[-1]}   rows {rws[0]}..{rws[-1]}")
