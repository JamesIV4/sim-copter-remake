"""Render a 256x256 SIM3D page with its 32x32 cell grid and the remake's cell indices.

Cell index convention matches the atlas reader: col = idx % 8, row-from-BOTTOM = idx / 8.
Usage: label_sim3d_page.py <pageIndex>
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dump_sim3d_page_png import load_pages, load_palette, SIM3D, OUT

idx = int(sys.argv[1]) if len(sys.argv) > 1 else 2
w, h, rows, px = load_pages(SIM3D)[idx]
pal = load_palette()

img = Image.new("RGB", (w, h))
out = img.load()
for r in range(h):
    base = rows[r]
    for c in range(w):
        out[c, h - 1 - r] = pal[px[base + c]]

scale = 3
img = img.resize((w * scale, h * scale), Image.NEAREST)
draw = ImageDraw.Draw(img)
cell = 32 * scale
for cell_index in range(64):
    col = cell_index % 8
    row_from_top = 7 - cell_index // 8
    x, y = col * cell, row_from_top * cell
    draw.rectangle([x, y, x + cell - 1, y + cell - 1], outline=(255, 0, 0))
    draw.text((x + 3, y + 3), str(cell_index), fill=(255, 0, 0))

path = OUT / ("sim3d_page_%d_cells.png" % idx)
img.save(path)
print("wrote", path)
