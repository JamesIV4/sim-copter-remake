import sys
from pathlib import Path

def u16(d,o): return int.from_bytes(d[o:o+2],'little')
def u32(d,o): return int.from_bytes(d[o:o+4],'little')
def i32(d,o): return int.from_bytes(d[o:o+4],'little',signed=True)
def name(d,o,n): return d[o:o+n].split(b'\0',1)[0].decode('ascii','replace')
def fx(v): return v / 65536.0

want = [int(a,0) for a in sys.argv[1:]] or [0x10D,0x10E,0x110]
root = Path(r'S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO')

for path in sorted(root.glob('*.[mM][aA][xX]')):
    d = path.read_bytes()
    if d[:4] != b'DIRC': continue
    geom = u32(d,24); entry_count = u32(d,geom+8); entry_offset = u32(d,geom+16)
    entries = [(name(d,entry_offset+i*53,17), u32(d,entry_offset+i*53+17)) for i in range(1,entry_count)]
    for tbl, oo in entries:
        if d[oo:oo+4] != b'OBJX': continue
        oid = i32(d, oo+120)
        if oid not in want: continue
        vc = u16(d,oo+8); fc = u16(d,oo+10)
        print('=== id 0x%X %s (%s) verts=%d faces=%d radius=%.2f' % (oid, tbl, path.name, vc, fc, fx(i32(d,oo+16))))
        verts=[]
        for v in range(vc):
            vo = oo+124+v*12
            verts.append((fx(i32(d,vo)), fx(i32(d,vo+4)), fx(i32(d,vo+8))))
        for i,v in enumerate(verts):
            print('    v%-2d  x=%8.2f  y=%8.2f  z=%8.2f' % (i, v[0], v[1], v[2]))
        fo = oo+124+vc*12
        for f in range(fc):
            fsize = u32(d,fo+4); fverts = u16(d,fo+8)
            flags = u16(d,fo+10); light = u16(d,fo+12)
            faceinfo = i32(d,fo+14); ftype = d[fo+18]; mat = d[fo+19]; atlas = d[fo+20]
            idx = [u16(d, fo+21+k*2) for k in range(fverts)]
            uvbase = fo+21+fverts*2
            uvs = [(i32(d,uvbase+k*8), i32(d,uvbase+k*8+4)) for k in range(fverts)]
            print('    face%-2d type=%-3d verts=%d idx=%s flags=0x%04x light=%d faceInfo=%d(%.3f) mat=%d atlas=%d size=%d'
                  % (f, ftype, fverts, idx, flags, light, faceinfo, fx(faceinfo), mat, atlas, fsize))
            print('             extra=%s' % (uvs,))
            fo += fsize
        print()
