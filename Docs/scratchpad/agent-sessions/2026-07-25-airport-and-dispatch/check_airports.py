import struct, sys, os, glob

UNCOMPRESSED = {b"ALTM", b"CNAM"}

def load(path):
    data = open(path, "rb").read()
    if data[0:4] != b"FORM":
        return None
    pos = 12
    chunks = {}
    while pos + 8 <= len(data):
        cid = data[pos:pos+4]
        size = struct.unpack(">I", data[pos+4:pos+8])[0]
        payload = data[pos+8:pos+8+size]
        pos += 8 + size
        if cid not in UNCOMPRESSED:
            out = bytearray(); i = 0
            while i < len(payload):
                b = payload[i]; i += 1
                if b < 128:
                    out += payload[i:i+b]; i += b
                else:
                    out += bytes([payload[i]]) * (b - 127); i += 1
            payload = bytes(out)
        chunks[cid.decode("ascii", "replace")] = payload
    return chunks

# 2x2 footprint ids, from FSimCopterPeopleCityRules::GetFootprintSizeForBuildingId
FOOT = {}
for i in range(256):
    if (0x49 <= i <= 0x50) or (0x61 <= i <= 0x6b):
        FOOT[i] = 2
    elif i < 0x70:
        FOOT[i] = 1
TABLE = (
    [1]*16 +                                   # 70..7f
    [1]*12 + [2]*4 +                           # 80..8f
    [2]*16 +                                   # 90..9f
    [2]*14 + [3]*2 +                           # a0..af
    [3]*16 +                                   # b0..bf
    [3]*6 + [1]*3 + [4]*7 +                    # c0..cf
    [3]*7 + [4]*4 + [1]*5 +                    # d0..df
    [1]*11 + [2]*5 +                           # e0..ef
    [2]*8 + [3]*3 + [4]*5                      # f0..ff
)
assert len(TABLE) == 0x90, len(TABLE)
for i in range(0x70, 0x100):
    FOOT[i] = TABLE[i - 0x70]

FLAT_NET = set([0x1d, 0x1e]) | set(range(0x23, 0x2e)) | set(range(0x32, 0x3b))

for path in sorted(glob.glob(sys.argv[1] + "/*.sc2")):
    c = load(path)
    if not c or "XZON" not in c:
        print(f"{os.path.basename(path):22s}  (unreadable)"); continue
    xzon, xbld = c["XZON"], c["XBLD"]
    zon = lambda x, y: xzon[y*128 + x] & 0x0f
    bld = lambda x, y: xbld[y*128 + x]
    origin = None
    for x in range(128):
        for y in range(128):
            if zon(x, y) != 8: continue
            ok = all(xx <= 127 and yy <= 127 and zon(xx, yy) == 8
                     for xx in range(x, x+4) for yy in range(y, y+4))
            if ok: origin = (x, y); break
        if origin: break
    if origin is None:
        print(f"{os.path.basename(path):22s}  no airport -> fallback (128,128)")
        continue
    ox, oy = origin
    term = bld(ox+1, oy+1)
    term_flattens = term >= 0x70 or term in FLAT_NET
    pads = [(2,3),(1,3),(3,3),(0,3),(3,2),(3,1),(0,1),(0,2),(0,0),(3,0),(1,0),(2,0)]
    occupied = sum(1 for dx, dy in pads if bld(ox+dx, oy+dy) >= 0x70)
    print(f"{os.path.basename(path):22s} origin=({ox:3d},{oy:3d}) terminal XBLD={term:#04x} "
          f"flattens={'Y' if term_flattens else 'N'}  pads with a building on them: {occupied}/12")
