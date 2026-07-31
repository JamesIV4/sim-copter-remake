"""Scratch: dump one people1.bmp portrait cell as ASCII.

FUN_00453f70 blits column (person+0x18e)+1, row = the opcode-54 mood, from a 12x3 grid of
27x33 cells; column 0 row 0 is the empty seat. FUN_004c7090 gives every state-6 medevac victim
head index 10, i.e. column 11 - this is the check that column 11 really is the bandaged face.

    python probe_people1.py <column> <row>
"""
import struct
import sys

BMP = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\PEOPLE1.BMP"
d = open(BMP, "rb").read()

pixel_off = struct.unpack_from("<I", d, 10)[0]
hdr = struct.unpack_from("<I", d, 14)[0]
w, h = struct.unpack_from("<ii", d, 18)
bpp = struct.unpack_from("<H", d, 28)[0]
print("size %dx%d bpp %d  (expect 324x99, 12x3 cells of 27x33)" % (w, h, bpp))

pal_off = 14 + hdr
palette = [struct.unpack_from("<BBBB", d, pal_off + i * 4) for i in range(256)]
stride = (w + 3) & ~3
height = abs(h)
top_down = h < 0

col = int(sys.argv[1]) if len(sys.argv) > 1 else 11
row = int(sys.argv[2]) if len(sys.argv) > 2 else 0

RAMP = " .:-=+*#%@"
for y in range(row * 33, row * 33 + 33):
    line = ""
    for x in range(col * 27, col * 27 + 27):
        src = y if top_down else (height - 1 - y)
        i = d[pixel_off + src * stride + x]
        if i == 254:
            line += " "
            continue
        b, g, r, _ = palette[i]
        line += RAMP[min(9, (r * 299 + g * 587 + b * 114) // 1000 * 10 // 256)]
    print(line)
