import struct, sys, zlib
from pathlib import Path
root = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP")
out_root = Path(r"C:\Users\james\AppData\Local\Temp\claude\s--Repos-sim-copter-remake\3a85a4ba-824b-43fe-ba10-ef2776b66e91\scratchpad\png")
out_root.mkdir(parents=True, exist_ok=True)

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

def write_png(path, w, h, raw):
    def chunk(t, data):
        c = t + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)

name, x0, y0, x1, y1, scale = sys.argv[1], *map(int, sys.argv[2:7])
w, h, pal, rows = read_bmp(root/name)
cw, ch = x1-x0, y1-y0
raw = bytearray()
for y in range(y0, y1):
    for _ in range(scale):
        raw.append(0)
        r = rows[y]
        for x in range(x0, x1):
            c = pal[r[x]]
            for _ in range(scale):
                raw += bytes(c)
        # grid marks every 10 source px
    # tint a marker line every 20 px
o = out_root / f"{Path(name).stem}_crop.png"
write_png(o, cw*scale, ch*scale, bytes(raw))
print(o, cw*scale, ch*scale, "origin", x0, y0)
