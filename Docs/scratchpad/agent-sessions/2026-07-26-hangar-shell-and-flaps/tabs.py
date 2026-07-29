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

for name in sys.argv[1:]:
    w, h, pal, rows = read_bmp(root/name)
    y0, y1 = 2, min(h, 22)
    hits = []
    for x in range(w):
        n = 0
        for y in range(y0, y1):
            cr, cg, cb = pal[rows[y][x]]
            mx, mn = max(cr, cg, cb), min(cr, cg, cb)
            if mx > 90 and (mx - mn) > 70:
                n += 1
        hits.append(n >= 6)
    runs = []
    x = 0
    while x < w:
        if hits[x]:
            s = x
            while x < w and hits[x]:
                x += 1
            if x - s >= 18:
                runs.append((s, x - 1))
        else:
            x += 1
    print(name, len(runs), runs)
