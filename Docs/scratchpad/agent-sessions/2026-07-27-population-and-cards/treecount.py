import struct, os
from pathlib import Path
from collections import Counter

def read_iff(path):
    data = open(path,'rb').read()
    pos = 12; chunks = {}
    while pos + 8 <= len(data):
        cid = data[pos:pos+4].decode('latin1')
        clen = struct.unpack('>I', data[pos+4:pos+8])[0]
        chunks[cid] = chunks.get(cid, b'') + data[pos+8:pos+8+clen]
        pos += 8 + clen
    return chunks

def unpack_rle(b):
    out = bytearray(); i = 0
    while i < len(b):
        c = b[i]; i += 1
        if c == 0: break
        if c < 128:
            out += b[i:i+c]; i += c
        else:
            n = c - 127; out += bytes([b[i]]) * n; i += 1
    return bytes(out)

root = Path(r'S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\cities')
rows = []
for p in sorted(list(root.glob('*.sc2')) + list((root/'career').glob('*.sc2'))):
    try:
        xbld = unpack_rle(read_iff(p)['XBLD'])
    except Exception:
        continue
    if len(xbld) != 16384: continue
    h = Counter(xbld)
    trees = sum(h[b] for b in range(0x06, 0x0D))
    rows.append((trees, p.name, 100.0*trees/16384))

rows.sort()
print('%-26s %8s %8s' % ('city', 'trees', '% of map'))
for trees, nm, pct in rows:
    print('%-26s %8d %7.1f%%' % (nm, trees, pct))
vals = [r[0] for r in rows]
print('\nmedian %d   mean %.0f   cape wells rank %d of %d'
      % (sorted(vals)[len(vals)//2], sum(vals)/len(vals),
         [r[1] for r in rows].index('cape wells.sc2')+1, len(rows)))
