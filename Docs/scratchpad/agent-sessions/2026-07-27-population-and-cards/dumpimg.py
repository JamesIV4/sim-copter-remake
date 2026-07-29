import sys, struct, zlib
from pathlib import Path

def i32(d,o): return int.from_bytes(d[o:o+4],'little',signed=True)

# Find the composite bitmap file that face type 13/2 "texture file 0" resolves to.
root = Path(r'S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame')
cands = sorted([p for p in root.rglob('*') if p.is_file() and p.suffix.lower() in ('.bmp','.max','') ])

def load_composite(path):
    d = path.read_bytes()
    if len(d) < 16 or d[:2] == b'BM': return None
    if i32(d,0) != len(d): return None
    n = i32(d,8); rescount = i32(d,12)
    if n <= 0 or rescount <= 0: return None
    cur = 16 + rescount*12
    images = []
    for k in range(n):
        if cur+12 > len(d): break
        w = i32(d,cur); h = i32(d,cur+4); unk = i32(d,cur+8)
        if w<=0 or h<=0 or w>4096 or h>4096 or unk!=0: break
        rowtab = cur+12; start = rowtab + h*4
        px = d[start:start+w*h]
        images.append((w,h,px))
        cur = start + w*h
    return images

# palette from a PAL/BMP in the game dir
def find_palette():
    for p in root.rglob('*.BMP'):
        d = p.read_bytes()
        if d[:2]==b'BM':
            off = int.from_bytes(d[10:14],'little')
            hdr = int.from_bytes(d[14:18],'little')
            bpp = int.from_bytes(d[28:30],'little')
            if bpp == 8:
                pal = []
                po = 14+hdr
                for i in range(256):
                    b,g,r,_ = d[po+i*4:po+i*4+4]
                    pal.append((r,g,b))
                return pal, p
    return None, None

pal, palsrc = find_palette()
print('palette from', palsrc)

def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + bytes(rgb[y*w*3:(y+1)*w*3]) for y in range(h))
    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw))
    png += chunk(b'IEND', b'')
    path.write_bytes(png)

want = [int(a,0) for a in sys.argv[2:]] or [7,30,32,33]
out = Path(sys.argv[1])
for p in root.rglob('*'):
    if not p.is_file(): continue
    try: imgs = load_composite(p)
    except Exception: continue
    if not imgs or len(imgs) < 34: continue
    print('composite', p.name, 'images', len(imgs))
    for idx in want:
        if idx >= len(imgs): continue
        w,h,px = imgs[idx]
        rgb = bytearray()
        for v in px:
            r,g,b = pal[v] if pal else (v,v,v)
            rgb += bytes((r,g,b))
        write_png(out / ('img_%s_%d.png' % (p.stem, idx)), w, h, rgb)
        print('   wrote %s image %d  %dx%d' % (p.name, idx, w, h))
    break
