import struct, sys, zlib
from pathlib import Path

src_root = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP")
out_root = Path(r"C:\Users\james\AppData\Local\Temp\claude\s--Repos-sim-copter-remake\3a85a4ba-824b-43fe-ba10-ef2776b66e91\scratchpad\png")
out_root.mkdir(parents=True, exist_ok=True)


def read_bmp(path):
    d = path.read_bytes()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h, planes, bpp = struct.unpack_from("<iiHH", d, 18)
    pal = []
    pal_off = 14 + struct.unpack_from("<I", d, 14)[0]
    n = (off - pal_off) // 4
    for i in range(n):
        b, g, r, _ = d[pal_off + i * 4: pal_off + i * 4 + 4]
        pal.append((r, g, b))
    flip = h > 0
    h = abs(h)
    stride = ((w * bpp + 31) // 32) * 4
    rows = []
    for y in range(h):
        row = d[off + y * stride: off + y * stride + stride]
        rows.append(row)
    if flip:
        rows.reverse()
    out = bytearray()
    for row in rows:
        out.append(0)
        for x in range(w):
            idx = row[x]
            r, g, b = pal[idx] if idx < len(pal) else (255, 0, 255)
            out += bytes((r, g, b))
    return w, h, bytes(out)


def write_png(path, w, h, raw):
    def chunk(t, data):
        c = t + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


for name in sys.argv[1:]:
    p = src_root / name
    w, h, raw = read_bmp(p)
    o = out_root / (p.stem + ".png")
    write_png(o, w, h, raw)
    print(o, w, h)
