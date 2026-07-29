import sys, struct
from pathlib import Path
from collections import Counter

def u16(d,o): return int.from_bytes(d[o:o+2],'little')
def u32(d,o): return int.from_bytes(d[o:o+4],'little')
def i32(d,o): return int.from_bytes(d[o:o+4],'little',signed=True)
def name(d,o,n): return d[o:o+n].split(b'\0',1)[0].decode('ascii','replace')

want = [int(a,0) for a in sys.argv[1:]] or [0x10D,0x10E,0x113,0x143]
root = Path(r'S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO')

for path in sorted(root.glob('*.[mM][aA][xX]')):
    d = path.read_bytes()
    if d[:4] != b'DIRC': continue
    geom = u32(d,24)
    entry_count = u32(d, geom+8)
    entry_offset = u32(d, geom+16)
    offsets = []
    for i in range(1, entry_count):
        o = entry_offset + i*53
        offsets.append((name(d,o,17), u32(d,o+17)))
    sorted_offsets = sorted(set(x[1] for x in offsets))
    for tbl, oo in offsets:
        if d[oo:oo+4] != b'OBJX': continue
        oid = i32(d, oo+120)
        if oid not in want: continue
        attrs = u32(d, oo+12)
        vc = u16(d, oo+8); fc = u16(d, oo+10)
        objname = name(d, oo+24, 88)
        boundary = next((c for c in sorted_offsets if c > oo), len(d))
        print('=== id 0x%X  %s / %s  file=%s dup=%s' % (oid, tbl, objname, path.name, bool(attrs & 8)))
        print('    vertices=%d faces=%d radius=%d yradius=%d' % (vc, fc, i32(d,oo+16), i32(d,oo+20)))
        xs=[];ys=[];zs=[]
        for v in range(vc):
            vo = oo+124+v*12
            xs.append(i32(d,vo)); ys.append(i32(d,vo+4)); zs.append(i32(d,vo+8))
        if xs:
            print('    bounds X[%d..%d] Y[%d..%d] Z[%d..%d]  (64 units = 1 tile)'
                  % (min(xs),max(xs),min(ys),max(ys),min(zs),max(zs)))
        fo = oo+124+vc*12
        types = Counter(); vcounts = Counter(); mats = Counter(); lights = Counter()
        for f in range(fc):
            if d[fo:fo+4] != b'FACE': print('    !! bad FACE at', fo); break
            fsize = u32(d, fo+4)
            fverts = u16(d, fo+8)
            flags = u16(d, fo+10)
            light = u16(d, fo+12)
            ftype = d[fo+18]; mat = d[fo+19]; atlas = d[fo+20]
            types[ftype]+=1; vcounts[fverts]+=1; mats[(ftype,mat,atlas)]+=1; lights[(flags,light)]+=1
            fo += fsize
        print('    faceType histogram :', dict(sorted(types.items())))
        print('    vertsPerFace       :', dict(sorted(vcounts.items())))
        print('    (type,material,atlas):', dict(list(mats.items())[:10]))
        print('    (flags,lightType)  :', dict(list(lights.items())[:10]))
        print()
