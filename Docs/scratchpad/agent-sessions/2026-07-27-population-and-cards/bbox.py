import os, struct

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
    # file rows are bottom-up; flip to top-down like the bake does
    images.append((w, h, rows[::-1]))
    cur = start + w*h

for idx in (7, 30, 32, 33):
    w, h, rows = images[idx]
    top = bottom = left = right = None
    for y in range(h):
        if any(p != 0 for p in rows[y]):
            if top is None: top = y
            bottom = y
    for x in range(w):
        if any(rows[y][x] != 0 for y in range(h)):
            if left is None: left = x
            right = x
    print('image %-3d %2dx%-3d  opaque rows %d..%d  cols %d..%d'
          % (idx, w, h, top, bottom, left, right))
    print('        padding: top %d rows (%.1f%%), BOTTOM %d rows (%.1f%%), left %d, right %d'
          % (top, 100.0*top/h, h-1-bottom, 100.0*(h-1-bottom)/h, left, w-1-right))
    # V range the artwork actually occupies, top-down
    print('        artwork V span (top-down): %.3f .. %.3f' % (top/h, (bottom+1)/h))
