"""Scratch: measure the readout boxes and slider tracks printed on CHECKUP.BMP.

SUPERSEDED (2026-07-30) by parse_checkup_rects.py. Only the DECOMPILE of FUN_00443c20 aliases the
stack slots - the assembly states all sixteen control rectangles outright, so the layout no longer
has to be inferred from the page art. Kept because eyeballing the printed furniture is still a
good cross-check on the numbers the parser reads out (see overlay_checkup_rects.py).

Dark near-black regions are the readout boxes and the slider tracks.
"""
import struct
from collections import defaultdict

PATH = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\CHECKUP.BMP"
d = open(PATH, "rb").read()

data_off = struct.unpack_from("<I", d, 10)[0]
hdr = struct.unpack_from("<I", d, 14)[0]
w, h = struct.unpack_from("<ii", d, 18)
bpp = struct.unpack_from("<H", d, 28)[0]
pal_off = 14 + hdr
print("bmp %dx%d bpp=%d data=0x%x" % (w, h, bpp, data_off))

pal = []
for i in range(256):
    b, g, r, _ = d[pal_off + i * 4: pal_off + i * 4 + 4]
    pal.append((r, g, b))

stride = (w + 3) & ~3


def px(x, y):
    # bottom-up rows
    return d[data_off + (h - 1 - y) * stride + x]


def lum(idx):
    r, g, b = pal[idx]
    return (r * 299 + g * 587 + b * 114) // 1000


# Horizontal runs of dark pixels, merged into boxes.
DARK = 45
rows = defaultdict(list)
for y in range(h):
    x = 0
    while x < w:
        if lum(px(x, y)) < DARK:
            start = x
            while x < w and lum(px(x, y)) < DARK:
                x += 1
            if x - start >= 18:
                rows[y].append((start, x - 1))
        else:
            x += 1

# Merge vertically adjacent runs with similar spans into rectangles.
boxes = []
for y in sorted(rows):
    for (x0, x1) in rows[y]:
        placed = False
        for bx in boxes:
            if bx["y1"] == y - 1 and abs(bx["x0"] - x0) <= 3 and abs(bx["x1"] - x1) <= 3:
                bx["y1"] = y
                bx["x0"] = min(bx["x0"], x0)
                bx["x1"] = max(bx["x1"], x1)
                placed = True
                break
        if not placed:
            boxes.append({"x0": x0, "x1": x1, "y0": y, "y1": y})

boxes = [b for b in boxes if (b["y1"] - b["y0"]) >= 12]
boxes.sort(key=lambda b: (b["y0"], b["x0"]))
print("\ndark rectangles (x0,y0)-(x1,y1)  w x h")
for b in boxes:
    print("  (%3d,%3d)-(%3d,%3d)  %3d x %3d" % (
        b["x0"], b["y0"], b["x1"], b["y1"], b["x1"] - b["x0"] + 1, b["y1"] - b["y0"] + 1))
