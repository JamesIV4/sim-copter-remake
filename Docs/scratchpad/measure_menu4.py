"""Scratch: find MENU4.BMP's printed panels.

The remake reuses this page (the original's "Show All Keyboard Shortcuts" list dialog) as the
stand-in for the Win32 file dialog the main menu's three loading items open, so the list and the
button need to land inside the printed furniture. The pale list panel is by far the brightest
region on the page, which is enough to bound it.
"""
from PIL import Image

BMP = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\MENU4.BMP"
img = Image.open(BMP).convert("RGB")
w, h = img.size
px = img.load()
print("MENU4.BMP %dx%d" % (w, h))


def bounds(pred):
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            if pred(px[x, y]):
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return (min(xs), min(ys), max(xs), max(ys))


pale = bounds(lambda c: c[0] > 170 and c[1] > 170 and c[2] > 170 and max(c) - min(c) < 24)
print("pale list panel:", pale)

# Row/column profiles across the middle, to see where the panel edges actually fall.
mid_y = h // 2
row = [x for x in range(w) if px[x, mid_y][0] > 170 and max(px[x, mid_y]) - min(px[x, mid_y]) < 24]
mid_x = w // 2
col = [y for y in range(h) if px[mid_x, y][0] > 170 and max(px[mid_x, y]) - min(px[mid_x, y]) < 24]
print("mid row pale span:", (min(row), max(row)) if row else None)
print("mid col pale span:", (min(col), max(col)) if col else None)

# The colour key (palette index 254 renders cyan) marks the page's non-rectangular edge.
cyan = bounds(lambda c: c[0] < 40 and c[1] > 200 and c[2] > 200)
print("colour-keyed region:", cyan)
