"""List SIM3D.BMP page dimensions, and dump one page as ASCII.

The map's two icon sheets are SIM3D pages 3 (FUN_004a2740's first FUN_0046cd20 call, 8 cells)
and 12 (the second, 3 cells). Usage: dump_sim3d_pages.py [pageIndex]
"""
import struct
import sys

SIM3D = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\SIM3D.BMP"


def load(path):
    d = open(path, "rb").read()
    (size,) = struct.unpack_from("<i", d, 0)
    (count,) = struct.unpack_from("<i", d, 8)
    (rescount,) = struct.unpack_from("<i", d, 12)
    cur = 16 + rescount * 12
    pages = []
    for i in range(count):
        w, h, unk = struct.unpack_from("<iii", d, cur)
        rowtab = cur + 12
        data = rowtab + h * 4
        rows = struct.unpack_from("<%di" % h, d, rowtab)
        px = d[data:data + w * h]
        pages.append((w, h, rows, px))
        cur = data + w * h
    return pages


def main():
    pages = load(SIM3D)
    if len(sys.argv) < 2:
        for i, (w, h, _rows, _px) in enumerate(pages):
            note = ""
            if h and w % h == 0:
                note = "  (%d square cells of %dx%d)" % (w // h, h, h)
            print("page %2d: %dx%d%s" % (i, w, h, note))
        return

    idx = int(sys.argv[1])
    w, h, rows, px = pages[idx]
    print("page %d: %dx%d, %d cells of %d" % (idx, w, h, w // h if h else 0, h))
    for r in range(h):
        off = rows[r]
        line = "".join("%02x" % px[off + c] if px[off + c] else ".." for c in range(w))
        print("%2d %s" % (r, line))


if __name__ == "__main__":
    main()
