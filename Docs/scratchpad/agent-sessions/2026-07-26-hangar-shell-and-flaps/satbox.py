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
y0 = int(sys.argv[2]); y1 = int(sys.argv[3])
w, h, pal, rows = read_bmp(root/name)
colc = [0]*w; rowc = [0]*h
for y in range(max(0,y0), min(h,y1)):
    r = rows[y]
    for x in range(w):
        cr, cg, cb = pal[r[x]]
        mx, mn = max(cr,cg,cb), min(cr,cg,cb)
        if mx > 100 and (mx-mn) > 80:
            colc[x]+=1; rowc[y]+=1
cols=[x for x in range(w) if colc[x]>2]
rws=[y for y in range(h) if rowc[y]>2]
print(f"{name}[{y0}:{y1}] saturated cols {cols[0]}..{cols[-1]} rows {rws[0]}..{rws[-1]}")
