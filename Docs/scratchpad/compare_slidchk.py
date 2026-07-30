"""Scratch: is SLIDCHK.BMP (the Check-up slider's track bitmap) also baked into CHECKUP.BMP?

FUN_00443c20 passes SLIDCHK.BMP to the slider constructor as the BAR bitmap and leaves the THUMB
argument null, so the thumb falls back to the vertical default SLIDERTV.BMP. The page art appears
to print the same tracks, so check whether the control is drawing over identical pixels.
"""
import struct


def load(path):
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    hdr = struct.unpack_from("<I", d, 14)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    pal_off = 14 + hdr
    pal = [tuple(d[pal_off + i * 4: pal_off + i * 4 + 3][::-1]) for i in range(256)]
    stride = (w + 3) & ~3
    px = [[d[off + (h - 1 - y) * stride + x] for x in range(w)] for y in range(h)]
    return w, h, pal, px


ROOT = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP"
pw, ph, ppal, page = load(ROOT + r"\CHECKUP.BMP")
tw, th, tpal, trk = load(ROOT + r"\SLIDCHK.BMP")
print("page %dx%d   slidchk %dx%d" % (pw, ph, tw, th))
print("palettes identical:", ppal == tpal)

# Control rects straight out of FUN_00443c20's ASM.
for name, (l, t) in (("damage", (91, 108)), ("fuel", (191, 176)), ("teargas", (333, 108))):
    best = None
    for dy in range(-8, 9):
        for dx in range(-4, 5):
            same = 0
            total = 0
            for y in range(th):
                for x in range(tw):
                    py, pxx = t + y + dy, l + x + dx
                    if not (0 <= py < ph and 0 <= pxx < pw):
                        continue
                    total += 1
                    if ppal[page[py][pxx]] == tpal[trk[y][x]]:
                        same += 1
            if total and (best is None or same / total > best[0]):
                best = (same / total, dx, dy)
    print("%-8s best match %.1f%% at offset (%+d,%+d)" % (name, best[0] * 100, best[1], best[2]))
