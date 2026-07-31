"""Scratch: locate MENU4.BMP's title band and its bottom-right button plate.

Both are dark, near-neutral rectangles sitting on the lighter diamond-plate frame, so scanning a
horizontal line through each and taking the run of dark pixels bounds them.
"""
from PIL import Image

img = Image.open(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\MENU4.BMP").convert("RGB")
w, h = img.size
px = img.load()


def dark_runs(y, threshold=70, min_len=40):
    runs, start = [], None
    for x in range(w):
        c = px[x, y]
        is_dark = max(c) < threshold
        if is_dark and start is None:
            start = x
        elif not is_dark and start is not None:
            if x - start >= min_len:
                runs.append((start, x - 1))
            start = None
    if start is not None and w - start >= min_len:
        runs.append((start, w - 1))
    return runs


def vertical_extent(x, y_seed, threshold=70):
    top = y_seed
    while top > 0 and max(px[x, top - 1]) < threshold:
        top -= 1
    bottom = y_seed
    while bottom < h - 1 and max(px[x, bottom + 1]) < threshold:
        bottom += 1
    return top, bottom


for label, y in (("title band", 50), ("button plate", 375)):
    runs = dark_runs(y)
    print(label, "y=%d runs=%s" % (y, runs))
    for (x0, x1) in runs:
        mid = (x0 + x1) // 2
        print("   x %d..%d  y %s" % (x0, x1, vertical_extent(mid, y)))
