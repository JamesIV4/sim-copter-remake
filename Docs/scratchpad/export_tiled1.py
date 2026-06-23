#!/usr/bin/env python3
import struct
from pathlib import Path
from PIL import Image, ImageDraw

REF = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame")
MAX = REF / "GEO" / "sim3d1.max"
TILED = REF / "BMP" / "TILED1.BMP"
OUT = Path(r"C:\Users\james\AppData\Local\Temp\claude\s--Repos-sim-copter-remake\2bd18189-b198-4105-9d35-8b3729691cf9\scratchpad")

def i32(d, o): return struct.unpack_from("<i", d, o)[0]

# --- palette from sim3d1.max CMAP ---
md = MAX.read_bytes()
color_data_off = i32(md, 57)
pal = [(md[color_data_off+i*3], md[color_data_off+i*3+1], md[color_data_off+i*3+2]) for i in range(256)]

# --- TILED1 image 0 ---
d = TILED.read_bytes()
res_count = i32(d, 12)
cur = 16 + res_count*12
w = i32(d, cur); h = i32(d, cur+4)
row_table = cur + 12
data_start = row_table + h*4
print(f"TILED1 image0 {w}x{h} data_start={data_start}")

# pixels: row r uses row_offset table; rows are bottom-up -> flip to top-down
img = Image.new("RGB", (w, h))
px = img.load()
for r in range(h):
    row_off = i32(d, row_table + r*4)
    for c in range(w):
        idx = d[data_start + row_off + c]
        # store bottom-up row r at top-down y = h-1-r
        px[c, h-1-r] = pal[idx]

img = img.resize((512, 512), Image.NEAREST)
draw = ImageDraw.Draw(img)
cell = 512//8
# Label each 8x8 cell with the remake index: col = idx%8, row_from_bottom = idx//8
# In this top-down 512 image, row_from_top = 7 - row_from_bottom.
for idx in range(64):
    col = idx % 8
    rfb = idx // 8
    rft = 7 - rfb
    x = col*cell; y = rft*cell
    draw.rectangle([x, y, x+cell-1, y+cell-1], outline=(255,0,0))
    draw.text((x+2, y+2), str(idx), fill=(255,0,0))
out = OUT / "tiled1_atlas_indexed.png"
img.save(out)
print("wrote", out)
