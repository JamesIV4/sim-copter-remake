import struct, sys, os
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

c = load(sys.argv[1])
xzon, xbld = c["XZON"], c["XBLD"]
zone = lambda x, y: xzon[y*128 + x]
bld  = lambda x, y: xbld[y*128 + x]

FOOT = {}
for i in range(256):
    if (0x49 <= i <= 0x50) or (0x61 <= i <= 0x6b): FOOT[i] = 2
    elif i < 0x70: FOOT[i] = 1
TABLE = ([1]*16 + [1]*12 + [2]*4 + [2]*16 + [2]*14 + [3]*2 + [3]*16 +
         [3]*6 + [1]*3 + [4]*7 + [3]*7 + [4]*4 + [1]*5 + [1]*11 + [2]*5 +
         [2]*8 + [3]*3 + [4]*5)
for i in range(0x70, 0x100): FOOT[i] = TABLE[i - 0x70]

# For every building >= 0x70, group tiles by contiguous same-id runs and print
# the high nibble pattern for each footprint size we find.
seen = {}
for y in range(128):
    for x in range(128):
        b = bld(x, y)
        if b < 0x70: continue
        n = FOOT[b]
        hi = zone(x, y) & 0xF0
        # position within its own footprint block, inferred from the 0x80 anchor
        seen.setdefault(n, {}).setdefault(hi, 0)
        seen[n][hi] += 1

for n in sorted(seen):
    print(f"footprint {n}x{n}: " + ", ".join(f"0x{h:02x}:{c}" for h, c in sorted(seen[n].items())))

print("\nHigh-nibble layout of concrete blocks:")
def show(x0, y0, w, label):
    print(f"  {label}  XBLD={bld(x0,y0):#04x} at ({x0},{y0})")
    for y in range(y0, y0+w):
        print("    " + "  ".join(f"0x{zone(x,y)&0xF0:02x}" for x in range(x0, x0+w)))

# find one example of each footprint size, anchored on a tile whose 0x80 bit is set
examples = {}
for y in range(128):
    for x in range(128):
        b = bld(x, y)
        if b < 0x70: continue
        n = FOOT[b]
        if n in examples: continue
        if zone(x, y) & 0x80:
            if all(bld(x+dx, y+dy) == b for dx in range(n) for dy in range(n)
                   if x+dx < 128 and y+dy < 128):
                examples[n] = (x, y, n)
for n in sorted(examples):
    x, y, w = examples[n]
    show(x, y, w, f"{n}x{n}")
