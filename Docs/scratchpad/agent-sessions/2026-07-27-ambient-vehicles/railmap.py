import struct, sys, os

def read_chunks(path):
    b = open(path, "rb").read()
    assert b[0:4] == b"FORM"
    out = {}
    off = 12
    while off + 8 <= len(b):
        name = b[off:off+4].decode("ascii", "replace")
        size = struct.unpack_from(">I", b, off + 4)[0]
        out.setdefault(name, b[off+8:off+8+size])
        off += 8 + size
    return out

def decompress(data):
    out = bytearray()
    i = 0
    while i < len(data):
        n = data[i]; i += 1
        if n < 128:
            out += data[i:i+n]; i += n
        else:
            out += bytes([data[i]]) * (n - 127); i += 1
    return bytes(out)

RAIL = set([0x2c, 0x2d] + list(range(0x2e, 0x32)) + list(range(0x32, 0x3b)) +
           list(range(0x45, 0x49)) + [0x4d, 0x4e, 0x5a, 0x5b])

path = sys.argv[1]
c = read_chunks(path)
xbld = decompress(c["XBLD"])
xter = decompress(c["XTER"])
xzon = decompress(c["XZON"])
# XALT is packed in ALTM (16-bit big endian per tile: low 5 bits = altitude)
altm = c["ALTM"]

def idx(x, y): return x * 128 + y

ids = {}
for x in range(128):
    for y in range(128):
        v = xbld[idx(x, y)]
        if v in RAIL:
            ids.setdefault(v, 0)
            ids[v] += 1
print("rail id histogram:", {hex(k): v for k, v in sorted(ids.items())})

# Print a window around the densest rail area, showing id / xter / altitude.
best = None
for cx in range(0, 128, 4):
    for cy in range(0, 128, 4):
        n = sum(1 for x in range(cx, min(cx+16,128)) for y in range(cy, min(cy+16,128))
                if xbld[idx(x, y)] in RAIL)
        if best is None or n > best[0]:
            best = (n, cx, cy)
print("densest 16x16 window at tile", best[1], best[2], "count", best[0])

if len(sys.argv) > 3:
    cx, cy = int(sys.argv[2]), int(sys.argv[3])
else:
    cx, cy = best[1], best[2]

print(f"\n      " + " ".join(f"{y:>3}" for y in range(cy, min(cy+18, 128))))
for x in range(cx, min(cx + 18, 128)):
    row = []
    for y in range(cy, min(cy + 18, 128)):
        v = xbld[idx(x, y)]
        row.append(f"{v:3x}" if v in RAIL else ("  ." if v == 0 else f"{v:3x}".replace(" ", "·")))
    print(f"x={x:3} " + " ".join(row))

print("\nXTER (terrain slope code) for the same window:")
print(f"      " + " ".join(f"{y:>3}" for y in range(cy, min(cy+18, 128))))
for x in range(cx, min(cx + 18, 128)):
    row = []
    for y in range(cy, min(cy + 18, 128)):
        row.append(f"{xter[idx(x, y)]:3x}")
    print(f"x={x:3} " + " ".join(row))

print("\nALTM low 5 bits (altitude) for the same window:")
print(f"      " + " ".join(f"{y:>3}" for y in range(cy, min(cy+18, 128))))
for x in range(cx, min(cx + 18, 128)):
    row = []
    for y in range(cy, min(cy + 18, 128)):
        w = struct.unpack_from(">H", altm, idx(x, y) * 2)[0]
        row.append(f"{w & 0x1f:3}")
    print(f"x={x:3} " + " ".join(row))
