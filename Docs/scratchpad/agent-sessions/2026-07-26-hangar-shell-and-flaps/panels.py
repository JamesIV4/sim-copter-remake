"""Find the light UI panel rectangles in a paletted BMP."""
import struct, sys
from collections import deque
from pathlib import Path

root = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP")


def read_bmp(path):
    d = path.read_bytes()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h, planes, bpp = struct.unpack_from("<iiHH", d, 18)
    pal_off = 14 + struct.unpack_from("<I", d, 14)[0]
    n = (off - pal_off) // 4
    pal = []
    for i in range(n):
        b, g, r, _ = d[pal_off + i * 4: pal_off + i * 4 + 4]
        pal.append((r, g, b))
    flip = h > 0
    h = abs(h)
    stride = ((w * bpp + 31) // 32) * 4
    rows = [d[off + y * stride: off + y * stride + stride] for y in range(h)]
    if flip:
        rows.reverse()
    return w, h, pal, rows


name = sys.argv[1]
thr = int(sys.argv[2]) if len(sys.argv) > 2 else 195
minarea = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
w, h, pal, rows = read_bmp(root / name)

lum = bytearray(w * h)
for y in range(h):
    r = rows[y]
    for x in range(w):
        cr, cg, cb = pal[r[x]]
        lum[y * w + x] = 1 if (cr * 299 + cg * 587 + cb * 114) // 1000 >= thr else 0

seen = bytearray(w * h)
out = []
for sy in range(h):
    for sx in range(w):
        i = sy * w + sx
        if not lum[i] or seen[i]:
            continue
        q = deque([i])
        seen[i] = 1
        minx = maxx = sx
        miny = maxy = sy
        n = 0
        while q:
            j = q.popleft()
            jy, jx = divmod(j, w)
            n += 1
            if jx < minx: minx = jx
            if jx > maxx: maxx = jx
            if jy < miny: miny = jy
            if jy > maxy: maxy = jy
            for nx, ny in ((jx-1, jy), (jx+1, jy), (jx, jy-1), (jx, jy+1)):
                if 0 <= nx < w and 0 <= ny < h:
                    k = ny * w + nx
                    if lum[k] and not seen[k]:
                        seen[k] = 1
                        q.append(k)
        if n >= minarea:
            out.append((n, minx, miny, maxx, maxy))

out.sort(reverse=True)
print(f"{name} {w}x{h} threshold={thr}")
for n, x0, y0, x1, y1 in out[:25]:
    print(f"  px={n:7d}  rect=({x0},{y0})-({x1},{y1})  size={x1-x0+1}x{y1-y0+1}")
