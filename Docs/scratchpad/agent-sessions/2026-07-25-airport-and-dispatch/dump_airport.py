import struct, sys, os

UNCOMPRESSED = {b"ALTM", b"CNAM"}

def load(path):
    data = open(path, "rb").read()
    assert data[0:4] == b"FORM", data[0:4]
    assert data[8:12] == b"SCDH", data[8:12]
    pos = 12
    chunks = {}
    while pos + 8 <= len(data):
        cid = data[pos:pos+4]
        size = struct.unpack(">I", data[pos+4:pos+8])[0]
        payload = data[pos+8:pos+8+size]
        pos += 8 + size
        if cid not in UNCOMPRESSED:
            out = bytearray()
            i = 0
            while i < len(payload):
                b = payload[i]; i += 1
                if b < 128:
                    out += payload[i:i+b]; i += b
                else:
                    out += bytes([payload[i]]) * (b - 127); i += 1
            payload = bytes(out)
        chunks[cid.decode("ascii")] = payload
    return chunks

path = sys.argv[1]
c = load(path)
print(os.path.basename(path), {k: len(v) for k, v in sorted(c.items())})
xzon = c["XZON"]; xbld = c["XBLD"]

# index convention question: SC2 stores row-major with index = y*128 + x (row = y).
def zon(x, y): return xzon[y*128 + x] & 0x0f
def bld(x, y): return xbld[y*128 + x]

# Find every airport-zone tile
tiles = [(x, y) for y in range(128) for x in range(128) if zon(x, y) == 8]
print("airport-zone tiles:", len(tiles))
if tiles:
    xs = [t[0] for t in tiles]; ys = [t[1] for t in tiles]
    print("bbox x", min(xs), max(xs), " y", min(ys), max(ys))

def valid(ox, oy):
    for x in range(ox, ox+4):
        for y in range(oy, oy+4):
            if x > 127 or y > 127: return False
            if zon(x, y) != 8: return False
    return True

# sweep x outer, y inner (original order)
origin = None
for x in range(128):
    for y in range(128):
        if zon(x, y) != 8: continue
        if valid(x, y):
            origin = (x, y); break
    if origin: break
print("first 4x4 all-airport block (x-outer sweep):", origin)

if origin:
    ox, oy = origin
    print("\nXBLD in the 6x6 around the block (rows = y, cols = x):")
    print("      " + " ".join(f"x{ox-1+i:3d}" for i in range(6)))
    for y in range(oy-1, oy+5):
        row = " ".join(f"{bld(ox-1+i, y):#04x}" for i in range(6))
        print(f"y{y:3d}: {row}")
    print("\nXZON low nibble:")
    for y in range(oy-1, oy+5):
        row = "  ".join(f"{zon(ox-1+i, y):2d}" for i in range(6))
        print(f"y{y:3d}: {row}")

    pads = [(2,3),(1,3),(3,3),(0,3),(3,2),(3,1),(0,1),(0,2),(0,0),(3,0),(1,0),(2,0)]
    print("\npad table:")
    for i,(dx,dy) in enumerate(pads):
        print(f"  pad {i:2d} -> tile ({ox+dx:3d},{oy+dy:3d})  XBLD {bld(ox+dx,oy+dy):#04x}")
    print(f"  terminal -> ({ox+1},{oy+1}) XBLD {bld(ox+1,oy+1):#04x}")

# also show all distinct XBLD ids on airport-zone tiles
ids = {}
for (x,y) in tiles:
    ids[bld(x,y)] = ids.get(bld(x,y), 0) + 1
print("\nXBLD histogram on airport-zone tiles:", {hex(k): v for k, v in sorted(ids.items())})
