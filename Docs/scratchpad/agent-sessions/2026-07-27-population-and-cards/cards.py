import struct
from pathlib import Path

def u16(d,o): return int.from_bytes(d[o:o+2],'little')
def u32(d,o): return int.from_bytes(d[o:o+4],'little')
def i32(d,o): return int.from_bytes(d[o:o+4],'little',signed=True)
def nm(d,o,n): return d[o:o+n].split(b'\0',1)[0].decode('ascii','replace')
def fx(v): return v/65536.0

want = set(range(0x10D, 0x114)) | {0x143}
root = Path(r'S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO')

reversed_order = 0
total = 0
for path in sorted(root.glob('*.[mM][aA][xX]')):
    d = path.read_bytes()
    if d[:4] != b'DIRC': continue
    geom = u32(d,24); ec = u32(d,geom+8); eo = u32(d,geom+16)
    for i in range(1, ec):
        o = eo + i*53
        tbl, oo = nm(d,o,17), u32(d,o+17)
        if d[oo:oo+4] != b'OBJX': continue
        oid = i32(d, oo+120)
        if oid not in want: continue
        vc = u16(d,oo+8); fc = u16(d,oo+10)
        V = [(fx(i32(d,oo+124+v*12)), fx(i32(d,oo+124+v*12+4)), fx(i32(d,oo+124+v*12+8))) for v in range(vc)]
        allY = [v[1] for v in V]
        print('--- 0x%X %s  objMinY=%.2f' % (oid, tbl, min(allY)))
        fo = oo+124+vc*12
        for f in range(fc):
            fsz = u32(d,fo+4); fv = u16(d,fo+8); ft = d[fo+18]; mat = d[fo+19]
            idx = [u16(d, fo+21+k*2) for k in range(fv)]
            if ft == 2 and fv == 2:
                total += 1
                a, b = V[idx[0]], V[idx[1]]
                rev = a[1] > b[1]
                if rev: reversed_order += 1
                midY, topY = min(a[1],b[1]), max(a[1],b[1])
                bottom = 2*midY - topY
                width = abs(b[0]-a[0])
                print('    f%-2d mat=%-3d a=(%7.2f,%7.2f) b=(%7.2f,%7.2f) | order=%s'
                      '  width=%6.2f height=%6.2f bottom=%6.2f  ratio=%.2f'
                      % (f, mat, a[0],a[1], b[0],b[1], 'REV' if rev else 'fwd',
                         width, topY-bottom, bottom, width/max(0.01,(topY-bottom))))
            fo += fsz
print('\ncards=%d  reversed(a.y > b.y)=%d' % (total, reversed_order))
