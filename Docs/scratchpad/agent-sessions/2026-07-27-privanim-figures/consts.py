import struct
from pathlib import Path
d = Path("Reference/SimCopterOriginalGame/SimCopter.exe").read_bytes()
pe = struct.unpack_from("<I", d, 0x3c)[0]
nsec = struct.unpack_from("<H", d, pe + 6)[0]
optsz = struct.unpack_from("<H", d, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    vsz, va, rsz, raw = struct.unpack_from("<IIII", d, o + 8)
    secs.append((va, vsz, raw, rsz))
def off(a):
    r = a - 0x400000
    for va, vsz, raw, rsz in secs:
        if va <= r < va + max(vsz, rsz):
            return raw + (r - va)
for a in (0x4f53b8, 0x4f53c0, 0x4f53c8, 0x4f53d0, 0x4f53d8, 0x4f53a0, 0x4f5398,
          0x4f53a8, 0x4f5390, 0x506c10, 0x506c18, 0x506c00, 0x506c04, 0x506c08):
    o = off(a); raw = d[o:o+8]
    print(f"{a:#x} {raw.hex()}  f32={struct.unpack('<f', raw[:4])[0]!r:24} f64={struct.unpack('<d', raw)[0]!r:24} i32={struct.unpack('<i', raw[:4])[0]}")
