import struct, sys, os, glob

UNCOMPRESSED = {b"ALTM", b"CNAM"}

def load(path):
    data = open(path, "rb").read()
    if data[0:4] != b"FORM": return None
    pos, chunks = 12, {}
    while pos + 8 <= len(data):
        cid = data[pos:pos+4]
        size = struct.unpack(">I", data[pos+4:pos+8])[0]
        payload = data[pos+8:pos+8+size]; pos += 8 + size
        if cid not in UNCOMPRESSED:
            out = bytearray(); i = 0
            while i < len(payload):
                b = payload[i]; i += 1
                if b < 128: out += payload[i:i+b]; i += b
                else: out += bytes([payload[i]]) * (b - 127); i += 1
            payload = bytes(out)
        chunks[cid.decode("ascii", "replace")] = payload
    return chunks

bad = 0
for path in sorted(glob.glob(sys.argv[1] + "/*.sc2")):
    c = load(path)
    if not c or "XZON" not in c: continue
    xzon, altm, xter = c["XZON"], c["ALTM"], c["XTER"]
    zon = lambda x, y: xzon[y*128 + x] & 0x0f
    def alt(x, y):
        w = struct.unpack(">H", altm[(y*128 + x)*2:(y*128 + x)*2 + 2])[0]
        # low 5 bits of each byte are the two altitudes; SC2: bits 0-4 = alt of tile,
        # bits 8-12 = secondary. Report both nibble fields.
        return (w >> 8) & 0x1f, w & 0x1f
    def ter(x, y): return xter[y*128 + x]
    origin = None
    for x in range(128):
        for y in range(128):
            if zon(x, y) != 8: continue
            if all(xx <= 127 and yy <= 127 and zon(xx, yy) == 8
                   for xx in range(x, x+4) for yy in range(y, y+4)):
                origin = (x, y); break
        if origin: break
    if origin is None: continue
    ox, oy = origin
    # 5x5 tile neighbourhood the corner flatten spans (corners ox..ox+4)
    prim = {alt(x, y)[0] for x in range(ox, ox+4) for y in range(oy, oy+4)}
    sec  = {alt(x, y)[1] for x in range(ox, ox+4) for y in range(oy, oy+4)}
    ters = {ter(x, y) for x in range(ox, ox+4) for y in range(oy, oy+4)}
    flat = len(prim) == 1 and len(sec) == 1 and ters == {0}
    if not flat:
        bad += 1
        print(f"{os.path.basename(path):22s} origin=({ox:3d},{oy:3d}) NOT flat  alt1={sorted(prim)} alt2={sorted(sec)} xter={sorted(ters)}")
print(f"\ncities whose 4x4 airport block is not already flat/level XTER=0: {bad}")
