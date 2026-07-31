"""Scratch: find the printed seat wells in SEATWIN2.BMP.

FUN_00453f70 blits each 27x33 people1 cell to
    x = (slot % cols) * 0x20 + 0x0e,  y = (slot / cols) * 0x23 + 0x0a
with cols = manifest[+0x14] = 5. The dashboard currently uses stride 29/34 from x 13, y 10.
Dump the page as coarse ASCII so the printed wells can be read off directly.

    python probe_seatwin.py
"""
import struct

BMP = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\SEATWIN2.BMP"
d = open(BMP, "rb").read()

assert d[:2] == b"BM"
pixel_off = struct.unpack_from("<I", d, 10)[0]
hdr = struct.unpack_from("<I", d, 14)[0]
w, h = struct.unpack_from("<ii", d, 18)
bpp = struct.unpack_from("<H", d, 28)[0]
print("size %dx%d bpp %d" % (w, h, bpp))

pal_off = 14 + hdr
palette = [struct.unpack_from("<BBBB", d, pal_off + i * 4) for i in range(256)]
stride = (w + 3) & ~3
height = abs(h)
top_down = h < 0


def idx(x, y):
    row = y if top_down else (height - 1 - y)
    return d[pixel_off + row * stride + x]


def lum(i):
    b, g, r, _ = palette[i]
    return (r * 299 + g * 587 + b * 114) // 1000


RAMP = " .:-=+*#%@"
print("    " + "".join(str((x // 10) % 10) for x in range(w)))
print("    " + "".join(str(x % 10) for x in range(w)))
for y in range(height):
    line = "".join(RAMP[min(9, lum(idx(x, y)) * 10 // 256)] for x in range(w))
    print("%3d %s" % (y, line))
