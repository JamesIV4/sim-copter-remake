import struct
PATH = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO\SIM3D2.MAX"
d = open(PATH, "rb").read()
assert d[28:32] == b"CMAP"
color_off = struct.unpack_from("<I", d, 57)[0]
for label, idxs in (("family1 light smoke", [0x39,0x3a,0x3b,0x3c]),
                    ("family2 dark smoke", [0x30,0x31,0x32,0x33]),
                    ("family0 fire", [0x13,0x17,0x73,0x7b,0x64,0x1d,0x1f,0x7f]),
                    ("family3 water", [0x94,0xa8,0x96,0xaa,0x9a,0xac,0x92,0xab])):
    out = []
    for i in idxs:
        r, g, b = d[color_off + i * 3: color_off + i * 3 + 3]
        out.append("0x%02x=#%02X%02X%02X" % (i, r, g, b))
    print(label, " ".join(out))
