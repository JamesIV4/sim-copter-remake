#!/usr/bin/env python3
"""Offline render of a city's terrain using the decoded SimCopter type grid + TILED1 atlas."""
import struct, sys
from pathlib import Path
from PIL import Image

REF = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame")
CITY = REF / "cities" / (sys.argv[1] if len(sys.argv) > 1 else "cape wells.sc2")
MAX = REF / "GEO" / "sim3d1.max"
TILED = REF / "BMP" / "TILED1.BMP"
OUT = Path(r"C:\Users\james\AppData\Local\Temp\claude\s--Repos-sim-copter-remake\2bd18189-b198-4105-9d35-8b3729691cf9\scratchpad")

def i32(d, o): return struct.unpack_from("<i", d, o)[0]
def be32(d, o): return struct.unpack_from(">I", d, o)[0]

# ---- parse .sc2 chunks ----
def parse_sc2(path):
    d = path.read_bytes()
    assert d[:4] == b"FORM" and d[8:12] == b"SCDH", "not an sc2"
    chunks = {}
    pos = 12
    while pos + 8 <= len(d):
        cid = d[pos:pos+4]; size = be32(d, pos+4); pos += 8
        chunks[cid.decode("latin1")] = d[pos:pos+size]; pos += size
    return chunks

def sc2_rle(data):
    out = bytearray(); i = 0
    while i < len(data):
        c = data[i]; i += 1
        if c < 128:
            out += data[i:i+c]; i += c
        elif c > 128:
            out += bytes([data[i]]) * (c - 127); i += 1
        # c==128 unused
    return bytes(out)

chunks = parse_sc2(CITY)
XBLD = sc2_rle(chunks["XBLD"]); XTER = sc2_rle(chunks["XTER"])
print(f"{CITY.name}: XBLD={len(XBLD)} XTER={len(XTER)}")
N = 128
def xbld(x, y): return XBLD[y*N + x]
def xter(x, y): return XTER[y*N + x]

# ---- build type grid (passes 1-4, decoded from FUN_004abce0) ----
T = [[0x30]*N for _ in range(N)]
for y in range(N):
    for x in range(N):
        b = xbld(x, y); t = 0x30
        if b == 0 or b > 4:
            if (b < 6 or b > 0x0D) and b != 0xD5 and b != 0xDA:
                if b == 0xF8: t = 0x10
                elif xter(x, y) > 0x0F: t = 5
            else:
                t = 0x20
        else:
            t = 10 + ((x + y) & 1)
        T[y][x] = t

def neigh(x, y, pred):
    for dy in (-1,0,1):
        for dx in (-1,0,1):
            if dx==0 and dy==0: continue
            nx,ny=x+dx,y+dy
            if 0<=nx<N and 0<=ny<N and pred(T[ny][nx]): return True
    return False

for y in range(1,N-1):
    for x in range(1,N-1):
        if T[y][x] in (0x30,0x20) and neigh(x,y,lambda v:v==5): T[y][x]=0x10
for y in range(1,N-1):
    for x in range(1,N-1):
        if T[y][x]==0x30 and neigh(x,y,lambda v:v==0x10): T[y][x]=0x20
for y in range(N):
    for x in range(N):
        if T[y][x]==5 and neigh(x,y,lambda v:v>9): T[y][x]=0

# ---- atlas cells (game order: cell idx -> col=idx&7, row=idx>>3) ----
md = MAX.read_bytes(); cdo = i32(md, 57)
pal = [(md[cdo+i*3], md[cdo+i*3+1], md[cdo+i*3+2]) for i in range(256)]
dd = TILED.read_bytes(); rc = i32(dd,12); cur = 16+rc*12
W = i32(dd,cur); H = i32(dd,cur+4); rt=cur+12; ds=rt+H*4
# linear pixel read (game ignores row table; table is trivial anyway)
def cell_img(idx):
    col=idx&7; row=idx>>3; x0=col*32; y0=row*32
    im=Image.new("RGB",(32,32)); p=im.load()
    for yy in range(32):
        for xx in range(32):
            p[xx,yy]=pal[dd[ds+(y0+yy)*256+(x0+xx)]]
    return im
cache={}
def cell(idx):
    if idx not in cache: cache[idx]=cell_img(idx)
    return cache[idx]

# ---- compose terrain image (y inverted so north is up like the game view is arbitrary) ----
img = Image.new("RGB",(N*8,N*8))
for y in range(N):
    for x in range(N):
        c = cell(T[y][x]).resize((8,8), Image.NEAREST)
        img.paste(c,(x*8,y*8))
out = OUT / "city_terrain_render.png"
img.save(out)
# stats
from collections import Counter
cnt = Counter(T[y][x] for y in range(N) for x in range(N))
print("type histogram:", {hex(k):v for k,v in sorted(cnt.items())})
print("wrote", out)
