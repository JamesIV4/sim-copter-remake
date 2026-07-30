"""Scratch: side-by-side of the Check-up page's printed slider slot and SLIDCHK.BMP.

Left  = CHECKUP.BMP cropped to the Damage slider's control rect (91,108)-(117,310).
Right = SLIDCHK.BMP, the bar bitmap FUN_00443c20 hands the slider control.
Far right = SLIDERTV.BMP, the thumb it falls back to (vertical default).
"""
import struct
import zlib

ROOT = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP"
OUT = r"S:\Repos\sim-copter-remake\Docs\scratchpad\art-track-compare.png"
SCALE = 4


def load(path):
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    hdr = struct.unpack_from("<I", d, 14)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    pal_off = 14 + hdr
    pal = [tuple(d[pal_off + i * 4: pal_off + i * 4 + 3][::-1]) for i in range(256)]
    stride = (w + 3) & ~3
    return w, h, [[pal[d[off + (h - 1 - y) * stride + x]] for x in range(w)] for y in range(h)]


pw, ph, page = load(ROOT + r"\CHECKUP.BMP")
tw, th, trk = load(ROOT + r"\SLIDCHK.BMP")
uw, uh, thumb = load(ROOT + r"\SLIDERTV.BMP")

crop = [[page[108 + y][91 + x] for x in range(26)] for y in range(202)]

GAP = 6
W = 26 + GAP + tw + GAP + uw
H = max(202, th, uh)
canvas = [[(255, 0, 255)] * W for _ in range(H)]
for y in range(202):
    for x in range(26):
        canvas[y][x] = crop[y][x]
for y in range(th):
    for x in range(tw):
        canvas[y][26 + GAP + x] = trk[y][x]
for y in range(uh):
    for x in range(uw):
        canvas[y][26 + GAP + tw + GAP + x] = thumb[y][x]

rows = []
for y in range(H):
    row = bytearray()
    for x in range(W):
        row += bytes(canvas[y][x]) * SCALE
    rows.extend([bytes(row)] * SCALE)
raw = b"".join(b"\x00" + r for r in rows)


def chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


open(OUT, "wb").write(
    b"\x89PNG\r\n\x1a\n"
    + chunk(b"IHDR", struct.pack(">IIBBBBB", W * SCALE, H * SCALE, 8, 2, 0, 0, 0))
    + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
print("wrote", OUT, "%dx%d" % (W * SCALE, H * SCALE))
