"""Export SIM3D.BMP pages to PNG (palette from sim3d1.max's CMAP).

Used to identify which direct image a face-type-2 sprite card is sampling.
Usage: dump_sim3d_page_png.py <pageIndex> [pageIndex...]
"""
import struct
import sys
from pathlib import Path

from PIL import Image

REF = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame")
SIM3D = REF / "BMP" / "SIM3D.BMP"
MAX = REF / "GEO" / "sim3d1.max"
OUT = Path(r"S:\Repos\sim-copter-remake\Docs\scratchpad")


def i32(d, o):
    return struct.unpack_from("<i", d, o)[0]


def load_pages(path):
    d = path.read_bytes()
    count = i32(d, 8)
    rescount = i32(d, 12)
    cur = 16 + rescount * 12
    pages = []
    for _ in range(count):
        w, h, _unk = struct.unpack_from("<iii", d, cur)
        rowtab = cur + 12
        data = rowtab + h * 4
        rows = struct.unpack_from("<%di" % h, d, rowtab)
        px = d[data:data + w * h]
        pages.append((w, h, rows, px))
        cur = data + w * h
    return pages


def load_palette():
    md = MAX.read_bytes()
    off = i32(md, 57)
    return [(md[off + i * 3], md[off + i * 3 + 1], md[off + i * 3 + 2]) for i in range(256)]


def main():
    pages = load_pages(SIM3D)
    pal = load_palette()
    for arg in sys.argv[1:]:
        idx = int(arg)
        w, h, rows, px = pages[idx]
        img = Image.new("RGB", (w, h))
        out = img.load()
        for r in range(h):
            base = rows[r]
            for c in range(w):
                out[c, h - 1 - r] = pal[px[base + c]]
        path = OUT / ("sim3d_page_%d.png" % idx)
        img.resize((w * 3, h * 3), Image.NEAREST).save(path)
        print("wrote", path, "%dx%d" % (w, h))


if __name__ == "__main__":
    main()
