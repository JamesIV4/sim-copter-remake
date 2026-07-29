import struct
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
want = {0x11c, 0x11d, 0x11e, 0x11f, 0x121, 0x122, 0x123}
for p in sorted(root.iterdir()):
    d = p.read_bytes()
    geom = struct.unpack_from('<I', d, 24)[0]
    entry_count = struct.unpack_from('<I', d, geom + 8)[0]
    entry_offset = struct.unpack_from('<I', d, geom + 16)[0]
    for i in range(1, entry_count):
        off = entry_offset + i * 53
        name = d[off:off + 17].split(b'\x00')[0].decode('latin-1')
        oo = struct.unpack_from('<I', d, off + 17)[0]
        if d[oo:oo + 4] != b'OBJX':
            continue
        oid = struct.unpack_from('<i', d, oo + 120)[0]
        if oid in want:
            objname = d[oo + 24:oo + 24 + 88].split(b'\x00')[0].decode('latin-1')
            print('%s: id=0x%03x table=%r object=%r' % (p.name, oid, name, objname))
