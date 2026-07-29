import struct, sys, glob, os
UNCOMPRESSED = {b"ALTM", b"CNAM"}
def load(path):
    data = open(path, "rb").read()
    pos, chunks = 12, {}
    while pos + 8 <= len(data):
        cid = data[pos:pos+4]; size = struct.unpack(">I", data[pos+4:pos+8])[0]
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

PADS = [(2,3),(1,3),(3,3),(0,3),(3,2),(3,1),(0,1),(0,2),(0,0),(3,0),(1,0),(2,0)]

def stamped_ids(ox, oy, x, y):
    dx, dy = x - ox, y - oy
    if not (0 <= dx < 4 and 0 <= dy < 4): return None, None
    if 1 <= dx <= 2 and 1 <= dy <= 2:
        hi = 0x80 if (dx == 1 and dy == 1) else 0x40 if (dx == 2 and dy == 1) else \
             0x10 if (dx == 1 and dy == 2) else 0x20
        return 0xf6, hi
    return 0xde, 0xf0

# ResolveOriginalMeshFootprint, transcribed from SimCity2000CityActor.cpp:2406
def resolve(bld, zon, x, y):
    b = bld(x, y)
    if b < 0x70: return None
    hi, lo = zon(x, y) & 0xF0, zon(x, y) & 0x0F
    if hi == 0xF0: return (1, 1)
    if (zon(x, y) & 0x80) == 0: return None          # suppressed child
    w = 1
    for xx in range(x, 128):
        if bld(xx, y) != b or (zon(xx, y) & 0x0F) != lo: break
        if zon(xx, y) & 0x40: w = xx - x + 1; break
    h = 1
    for yy in range(y, 128):
        if bld(x, yy) != b or (zon(x, yy) & 0x0F) != lo: break
        if zon(x, yy) & 0x10: h = yy - y + 1; break
    return (w, h)

def check(path):
    c = load(path)
    if "XZON" not in c: return
    xzon, xbld = bytearray(c["XZON"]), bytearray(c["XBLD"])
    zon = lambda x, y: xzon[y*128 + x]
    bld = lambda x, y: xbld[y*128 + x]
    origin = None
    for x in range(128):
        for y in range(128):
            if (zon(x, y) & 0x0f) != 8: continue
            if all(xx < 128 and yy < 128 and (zon(xx, yy) & 0x0f) == 8
                   for xx in range(x, x+4) for yy in range(y, y+4)):
                origin = (x, y); break
        if origin: break
    name = os.path.basename(path)
    if origin is None:
        print(f"{name:22s} no airport"); return
    ox, oy = origin

    before = {}
    for dy in range(4):
        for dx in range(4):
            before[(dx, dy)] = resolve(bld, zon, ox+dx, oy+dy)

    for dy in range(4):
        for dx in range(4):
            b, hi = stamped_ids(ox, oy, ox+dx, oy+dy)
            xbld[(oy+dy)*128 + ox+dx] = b
            xzon[(oy+dy)*128 + ox+dx] = (zon(ox+dx, oy+dy) & 0x0f) | hi

    after = {}
    covered = {}
    bad = []
    for dy in range(4):
        for dx in range(4):
            r = resolve(bld, zon, ox+dx, oy+dy)
            after[(dx, dy)] = r
            if r:
                w, h = r
                for yy in range(h):
                    for xx in range(w):
                        covered[(dx+xx, dy+yy)] = covered.get((dx+xx, dy+yy), 0) + 1
    overlaps = {k: v for k, v in covered.items() if v > 1}
    outside = [k for k in covered if not (0 <= k[0] < 4 and 0 <= k[1] < 4)]
    pads_ok = all(after[p] == (1, 1) for p in PADS)
    term_ok = after[(1, 1)] == (2, 2) and after[(2, 1)] is None and \
              after[(1, 2)] is None and after[(2, 2)] is None
    before_bad = sum(1 for v in before.values() if v and v != (1, 1))
    status = "OK " if (pads_ok and term_ok and not overlaps and not outside) else "FAIL"
    print(f"{status} {name:22s} origin=({ox:3d},{oy:3d})  "
          f"pads 1x1: {pads_ok}  terminal 2x2 once: {term_ok}  "
          f"overlapping tiles after: {len(overlaps)}  spilling outside block: {len(outside)}")
    if before_bad:
        print(f"       (before the zone fix, {before_bad} block tiles measured as a multi-tile footprint)")

for p in sorted(glob.glob(sys.argv[1] + "/*.sc2")):
    check(p)
