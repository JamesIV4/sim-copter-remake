"""Scratch: print the CMAP entries the light faces use as their material index."""
import struct

PATH = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO\SIM3D2.MAX"
d = open(PATH, "rb").read()
assert d[28:32] == b"CMAP"
color_off = struct.unpack_from("<I", d, 57)[0]
for i in list(range(240, 256)):
    r, g, b = d[color_off + i * 3: color_off + i * 3 + 3]
    print("%3d 0x%02x  #%02X%02X%02X" % (i, i, r, g, b))
