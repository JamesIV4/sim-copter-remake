import struct, sys, os
from collections import Counter

def read_iff(path):
    data = open(path, 'rb').read()
    assert data[0:4] == b'FORM'
    size = struct.unpack('>I', data[4:8])[0]
    assert data[8:12] == b'SCDH'
    pos = 12
    chunks = {}
    while pos + 8 <= len(data):
        cid = data[pos:pos+4].decode('latin1')
        clen = struct.unpack('>I', data[pos+4:pos+8])[0]
        body = data[pos+8:pos+8+clen]
        pos += 8 + clen
        chunks.setdefault(cid, b'')
        chunks[cid] += body
    return chunks

def unpack_rle(b):
    out = bytearray()
    i = 0
    while i < len(b):
        c = b[i]; i += 1
        if c == 0:
            break
        if c < 128:
            out += b[i:i+c]; i += c
        else:
            n = c - 127
            out += bytes([b[i]]) * n; i += 1
    return bytes(out)

# ---- ported rules -------------------------------------------------------
def tile_class(bid):
    def r(a,b): return a <= bid <= b
    if bid == 0: return 2
    if r(1,4): return 4
    if r(6,0x0C): return 3
    if bid in (0x0D,0xD5,0xDA): return 5
    if r(0x0E,0x1C): return 6
    if r(0x1D,0x2B) or r(0x43,0x44) or r(0x49,0x50) or r(0x61,0x69): return 7
    if r(0x51,0x5A) or r(0x6A,0x6B): return 8
    if r(0x2C,0x3E) or r(0x3F,0x50) or r(0x45,0x49) or r(0x5B,0x60): return 9
    if r(0x70,0x7B) or r(0x8C,0x93) or r(0xAA,0xB1) or r(0xFB,0xFF): return 10
    if r(0xB2,0xBB) or r(0xD0,0xD1) or bid in (0xD9,0xE1,0xF1,0xF3,0xF7): return 12
    if bid == 0x82 or r(0x84,0x8B) or r(0x9E,0xA9) or r(0xBC,0xC5) or r(0xC8,0xCF) \
       or r(0xE2,0xEF) or bid == 0xF2 or bid == 0xF4 or r(0xF9,0xFA): return 13
    if not (r(0x7C,0x83) or r(0x94,0x9D) or r(0xD2,0xDC) or r(0xE1,0xE5) or r(0xE8,0xF5)
            or bid == 0xF7 or r(0xF9,0xFA)): return 1
    return 11

FOOT70 = (
    [1]*16 +
    [1]*12 + [2]*4 +
    [2]*16 +
    [2]*14 + [3]*2 +
    [3]*16 +
    [3]*6 + [1]*3 + [4]*7 +
    [3]*7 + [4]*4 + [1]*5 +
    [1]*11 + [2]*5 +
    [2]*8 + [3]*3 + [4]*5)
assert len(FOOT70) == 0x90

def footprint(bid):
    if 0x49 <= bid <= 0x50 or 0x61 <= bid <= 0x6B: return 2
    if bid < 0x70: return 1
    return FOOT70[bid-0x70]

AMBIENT_SPAWN_CLASSES = {12,13,11,10,5,4,3}

def ec00_row(cls):
    if cls == 6: return {13}
    if cls in (10,17): return {5,4,3}
    if cls == 16: return {4}
    return {12,13,11,10}

# ---- load ---------------------------------------------------------------
path = sys.argv[1]
ch = read_iff(path)
xbld = unpack_rle(ch['XBLD'])
xzon = unpack_rle(ch['XZON'])
xter = unpack_rle(ch['XTER'])
assert len(xbld) == 128*128, len(xbld)

def at(g,x,y): return g[y*128+x]

cls_hist = Counter()
for y in range(128):
    for x in range(128):
        cls_hist[tile_class(at(xbld,x,y))] += 1

print(os.path.basename(path))
print(' tile-class histogram (whole 128x128 map):')
for c in sorted(cls_hist): print('   class %2d : %6d' % (c, cls_hist[c]))

# water: XTER low bit / values >= 0x10 indicate water in SC2K
def is_water(x,y):
    t = at(xter,x,y)
    return t >= 0x10 and t != 0x3E  # crude; surface water flags

# nodes: which tiles get a pedestrian node under the remake rules
node_owner = {}
node_for_tile = {}
rejected_by_zone = Counter()
for y in range(128):
    for x in range(128):
        bid = at(xbld,x,y)
        c = tile_class(bid)
        if c not in AMBIENT_SPAWN_CLASSES: continue
        if is_water(x,y): continue
        size = footprint(bid)
        if bid >= 0x70:
            z = at(xzon,x,y)
            if (z & 0xF0) != 0xF0 and (z & 0x80) == 0:
                rejected_by_zone[c] += 1
                continue
        if size <= 0 or x+size > 128 or y+size > 128: continue
        ok = all(at(xbld,x+dx,y+dy) == bid for dy in range(size) for dx in range(size))
        if not ok: continue
        node_owner[(x,y)] = (size,c)
        for dy in range(size):
            for dx in range(size):
                node_for_tile[(x+dx,y+dy)] = (x,y,c)

print(' pedestrian nodes: %d owners, %d covered tiles' % (len(node_owner), len(node_for_tile)))
print(' tiles rejected by the XZON 0x80 owner filter, by class:', dict(rejected_by_zone))

covered = Counter(c for (_,_,c) in node_for_tile.values())
print(' covered tiles by class:', dict(covered))

field = sum(v for k,v in covered.items() if k in (3,4,5))
bldg  = sum(v for k,v in covered.items() if k in (10,11,12,13))
print(' field(3/4/5) covered tiles = %d, building(10-13) covered tiles = %d' % (field,bldg))

# expected accept probability per tile visit
p_field = 1 - (0.9 ** 5)      # only dog(10)/cow(17) rows contain 3/4/5
p_bldg  = 1 - (0.1 ** 5)      # rolls 2..19 -> common class 0..9, all accepted
print(' P(accept) field=%.3f  building=%.5f' % (p_field, p_bldg))
exp_dog = field * p_field * 0.5
exp_cow = field * p_field * 0.5
exp_hum = bldg * p_bldg
tot = exp_dog+exp_cow+exp_hum
print(' whole-map expected spawn mix: dogs %.0f (%.1f%%), cows %.0f (%.1f%%), humans %.0f (%.1f%%)'
      % (exp_dog, 100*exp_dog/tot, exp_cow, 100*exp_cow/tot, exp_hum, 100*exp_hum/tot))
