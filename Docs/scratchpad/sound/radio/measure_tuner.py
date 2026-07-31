"""Measure the dash4 radio tuner strip from DASH4.BMP pixels.

The scale is a lit (light-grey) horizontal band inside the dark radio body. Find it by scanning
for the run of rows/columns whose palette entries are noticeably brighter than the body, then
report the band's bounding box and the x centre of each printed frequency label.
"""
import struct
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
from bmp2png import read_bmp  # noqa: E402

PATH = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\DASH4.BMP"
w, h, pal, rows = read_bmp(PATH)


def lum(idx):
    r, g, b = pal[idx]
    return (r * 299 + g * 587 + b * 114) // 1000


print(f"DASH4.BMP {w}x{h}")

# The colour key (cyan) is the transparent background; ignore it.
def is_key(idx):
    return pal[idx] == (0, 255, 255) or pal[idx] == (0, 254, 254)


# Row profile over the radio body (x 10..100): mean luminance of non-key pixels.
print("\nrow profile x=10..100")
for y in range(h):
    vals = [lum(rows[y][x]) for x in range(10, 100) if not is_key(rows[y][x])]
    if not vals:
        continue
    mean = sum(vals) // len(vals)
    bright = sum(1 for v in vals if v > 90)
    print(f"  y={y:2}  mean={mean:3}  bright={bright:3}  {'#' * (mean // 6)}")

# Column profile across the band we just identified.
band = [int(a) for a in sys.argv[1:3]] if len(sys.argv) > 2 else None
if band:
    y0, y1 = band
    print(f"\ncolumn profile y={y0}..{y1}")
    for x in range(0, 110):
        vals = [lum(rows[y][x]) for y in range(y0, y1 + 1) if not is_key(rows[y][x])]
        if not vals:
            continue
        mean = sum(vals) // len(vals)
        print(f"  x={x:3}  mean={mean:3}  {'#' * (mean // 6)}")
