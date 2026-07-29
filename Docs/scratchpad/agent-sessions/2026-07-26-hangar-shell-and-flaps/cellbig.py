import struct, sys, zlib
from pathlib import Path
geo = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO\sim3d1.max")
tex = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\SIM3D.BMP")
out = Path(r"C:\Users\james\AppData\Local\Temp\claude\s--Repos-sim-copter-remake\3a85a4ba-824b-43fe-ba10-ef2776b66e91\scratchpad\png")

def u32(d, o): return int.from_bytes(d[o:o+4], "little")

g = geo.read_bytes()
co = u32(g, 57)
pal = [tuple(g[co + i*3: co + i*3 + 3]) for i in range(256)]

d = tex.read_bytes()
res = u32(d, 12)
cur = 16 + res * 12
imgs = []
for i in range(u32(d, 8)):
    w = u32(d, cur); h = u32(d, cur + 4)
    rt = cur + 12; do = rt + h * 4
    px = bytearray(w * h)
    for r in range(h):
        ro = u32(d, rt + r * 4)
        px[(h - 1 - r) * w:(h - 1 - r) * w + w] = d[do + ro: do + ro + w]
    imgs.append((w, h, bytes(px)))
    cur = do + w * h

page = int(sys.argv[1]); cells = [int(c) for c in sys.argv[2:]]
w, h, px = imgs[page]
S = 8
cw = 32 * S
W = cw * len(cells); H = cw
raw = bytearray()
for y in range(H):
    raw.append(0)
    for x in range(W):
        c = cells[x // cw]
        col = c % 8; row = 7 - (c // 8)
        sx = col * 32 + ((x % cw) // S); sy = row * 32 + (y // S)
        raw += bytes(pal[px[sy * w + sx]])
def chunk(t, data):
    cc = t + data
    return struct.pack(">I", len(data)) + cc + struct.pack(">I", zlib.crc32(cc) & 0xFFFFFFFF)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
(out / "cellbig.png").write_bytes(png)
print(out / "cellbig.png", cells)
