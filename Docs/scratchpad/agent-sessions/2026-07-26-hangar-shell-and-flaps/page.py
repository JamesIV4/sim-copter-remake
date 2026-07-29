import struct, sys, zlib
from pathlib import Path

geo = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO\sim3d1.max")
tex = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\SIM3D.BMP")
out_root = Path(r"C:\Users\james\AppData\Local\Temp\claude\s--Repos-sim-copter-remake\3a85a4ba-824b-43fe-ba10-ef2776b66e91\scratchpad\png")
out_root.mkdir(parents=True, exist_ok=True)

def u32(d, o): return int.from_bytes(d[o:o+4], "little")

g = geo.read_bytes()
assert g[28:32] == b"CMAP", g[28:32]
color_off = u32(g, 57)
pal = [tuple(g[color_off + i*3: color_off + i*3 + 3]) for i in range(256)]

d = tex.read_bytes()
assert u32(d, 0) == len(d)
image_count = u32(d, 8)
res_count = u32(d, 12)
cursor = 16 + res_count * 12
images = []
for i in range(image_count):
    w = u32(d, cursor); h = u32(d, cursor + 4)
    row_table = cursor + 12
    data_off = row_table + h * 4
    px = bytearray(w * h)
    for row in range(h):
        ro = u32(d, row_table + row * 4)
        dest = (h - 1 - row) * w
        px[dest:dest + w] = d[data_off + ro: data_off + ro + w]
    images.append((w, h, bytes(px)))
    cursor = data_off + w * h

print("images:", image_count, "res:", res_count, file=sys.stderr)

def write_png(path, w, h, get):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw += bytes(get(x, y))
    def chunk(t, data):
        c = t + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    path.write_bytes(png)

page = int(sys.argv[1]) if len(sys.argv) > 1 else 40
w, h, px = images[page]
print(f"page {page}: {w}x{h}", file=sys.stderr)
scale = 3
write_png(out_root / f"page{page}.png", w * scale, h * scale,
          lambda x, y: pal[px[(y // scale) * w + (x // scale)]])

# Cells the airport terminal uses, drawn side by side and labelled by index order.
cells = [int(a) for a in sys.argv[2:]] or [20, 21, 22, 23, 61]
cell = 32
cw = cell * scale
strip_w = cw * len(cells)
def get(x, y):
    idx = x // cw
    c = cells[idx]
    col = c % 8
    raw_row = c // 8
    row = 8 - 1 - raw_row
    sx = col * cell + ((x % cw) // scale)
    sy = row * cell + (y // scale)
    return pal[px[sy * w + sx]]
write_png(out_root / f"page{page}_cells.png", strip_w, cell * scale, get)
print("cells", cells, "->", out_root / f"page{page}_cells.png", file=sys.stderr)
