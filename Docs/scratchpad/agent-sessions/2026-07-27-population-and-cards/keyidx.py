import os, struct
from collections import Counter

root = r'S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame'
d = open(os.path.join(root, 'BMP', 'SIM3D.BMP'), 'rb').read()

def i32(o): return struct.unpack_from('<i', d, o)[0]

n = i32(8); rescount = i32(12)
cur = 16 + rescount * 12
images = []
for k in range(n):
    w = i32(cur); h = i32(cur+4)
    rowtab = cur+12; start = rowtab + h*4
    rows = []
    for r in range(h):
        ro = i32(rowtab + r*4)
        rows.append(d[start+ro : start+ro+w])
    images.append((w, h, rows))
    cur = start + w*h

for idx in (7, 30, 32, 33):
    w, h, rows = images[idx]
    border = Counter()
    for x in range(w):
        border[rows[0][x]] += 1
        border[rows[h-1][x]] += 1
    for y in range(h):
        border[rows[y][0]] += 1
        border[rows[y][w-1]] += 1
    allpx = Counter()
    for r in rows: allpx.update(r)
    print('image %-3d %dx%d  border top-5=%s  overall top-5=%s'
          % (idx, w, h, border.most_common(5), allpx.most_common(5)))
