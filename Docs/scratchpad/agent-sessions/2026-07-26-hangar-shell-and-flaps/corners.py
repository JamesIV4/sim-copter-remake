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
    corners = {(0,0): rows[0][0], (w-1,0): rows[0][w-1], (0,h-1): rows[h-1][0], (w-1,h-1): rows[h-1][w-1]}
    print(name, w, "x", h)
    for k, idx in corners.items():
        print("   ", k, "idx", idx, pal[idx])
    print("    pal[0]", pal[0], "pal[254]", pal[254] if len(pal) > 254 else None, "pal[255]", pal[255] if len(pal) > 255 else None)
