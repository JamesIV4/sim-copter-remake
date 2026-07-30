"""Scratch: draw FUN_00443c20's ground-truth control rectangles over CHECKUP.BMP.

The rects come from Docs/scratchpad/parse_checkup_rects.py, which reads them out of the ASM
(the decompile aliases the stack slots and cannot be used). This renders them so the layout can
be eyeballed against the printed page art before it is committed to the Slate widget.
"""
import struct

BMP = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\CHECKUP.BMP"
OUT = r"S:\Repos\sim-copter-remake\Docs\scratchpad\art-CHECKUP-rects.png"
SCALE = 2

d = open(BMP, "rb").read()
data_off = struct.unpack_from("<I", d, 10)[0]
hdr = struct.unpack_from("<I", d, 14)[0]
w, h = struct.unpack_from("<ii", d, 18)
pal_off = 14 + hdr
pal = [tuple(d[pal_off + i * 4: pal_off + i * 4 + 3][::-1]) for i in range(256)]
stride = (w + 3) & ~3

px = [[pal[d[data_off + (h - 1 - y) * stride + x]] for x in range(w)] for y in range(h)]

# (label, colour, left, top, right, bottom)
RECTS = [
    ("title 590",      (255, 64, 64),   112, 36, 344, 66),
    ("funds 591",      (64, 255, 64),    72, 70, 148, 85),
    ("funds value",    (64, 255, 64),   154, 70, 206, 85),
    ("total 592",      (64, 160, 255),  236, 70, 328, 85),
    ("total value",    (64, 160, 255),  332, 70, 380, 85),
    ("damage 593",     (255, 255, 64),   52, 327, 154, 347),
    ("damage value",   (255, 255, 64),   64, 341, 147, 358),
    ("fuel 594",       (255, 128, 255), 147, 107, 268, 127),
    ("fuel value",     (255, 128, 255), 167, 122, 255, 138),
    ("teargas 595",    (255, 160, 64),  287, 327, 405, 347),
    ("teargas value",  (255, 160, 64),  305, 341, 389, 358),
    ("slider 3",       (255, 0, 255),    91, 108, 117, 310),
    ("slider 4",       (255, 0, 255),   191, 176, 217, 378),
    ("slider 5",       (255, 0, 255),   333, 108, 359, 310),
    # The button rects are degenerate 1x1 origins; BUTTON.BMP frames are 100x28.
    ("ok 596",         (0, 255, 255),   186, 390, 286, 418),
    ("cancel 597",     (0, 255, 255),   288, 390, 388, 418),
]

for (_, c, l, t, r, b) in RECTS:
    for x in range(max(l, 0), min(r, w)):
        for y in (t, b - 1):
            if 0 <= y < h:
                px[y][x] = c
    for y in range(max(t, 0), min(b, h)):
        for x in (l, r - 1):
            if 0 <= x < w:
                px[y][x] = c

rows = []
for y in range(h):
    row = bytearray()
    for x in range(w):
        row += bytes(px[y][x]) * SCALE
    rows.extend([bytes(row)] * SCALE)

# Minimal PNG writer so this needs no third-party module.
import zlib

raw = b"".join(b"\x00" + r for r in rows)


def chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", w * SCALE, h * SCALE, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw, 9))
       + chunk(b"IEND", b""))
open(OUT, "wb").write(png)
print("wrote", OUT)
