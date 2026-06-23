#!/usr/bin/env python3
import struct
from pathlib import Path

REF = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame")
MAX = REF / "GEO" / "sim3d1.max"
TILED = REF / "BMP" / "TILED1.BMP"

def i32(d, o): return struct.unpack_from("<i", d, o)[0]

md = MAX.read_bytes()
cdo = i32(md, 57)
pal = [(md[cdo+i*3], md[cdo+i*3+1], md[cdo+i*3+2]) for i in range(256)]

d = TILED.read_bytes()
res_count = i32(d, 12)
cur = 16 + res_count*12
w = i32(d, cur); h = i32(d, cur+4)
row_table = cur + 12
data_start = row_table + h*4

# raw[y][x] in file order (row 0 = first stored row)
raw = [[d[data_start + i32(d, row_table + r*4) + c] for c in range(w)] for r in range(h)]

def classify(rr, gg, bb):
    if bb > rr + 20 and bb > gg + 5: return "WATER"
    if gg > rr + 15 and gg > bb + 15: return "GRASS"
    if abs(rr-gg) < 25 and abs(gg-bb) < 25 and rr > 90: return "GREY/ROCK"
    if rr > gg and gg > bb and rr > 110: return "TAN/SAND"
    return "mixed"

# Game cell idx: col = idx&7, row = idx>>3 (row from start of buffer = bottom of visual image).
print("idx (col,rowFromBottom)  avgRGB  class")
for idx in range(64):
    col = idx & 7
    row = idx >> 3
    x0 = col*32; y0 = row*32
    rs=gs=bs=0
    for yy in range(y0, y0+32):
        for xx in range(x0, x0+32):
            r,g,b = pal[raw[yy][xx]]
            rs+=r; gs+=g; bs+=b
    n=32*32
    rr,gg,bb = rs//n, gs//n, bs//n
    print(f"{idx:2d} ({col},{row})  ({rr:3d},{gg:3d},{bb:3d})  {classify(rr,gg,bb)}")
