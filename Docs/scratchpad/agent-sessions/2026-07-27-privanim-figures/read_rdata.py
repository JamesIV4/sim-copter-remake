import struct
from pathlib import Path

p = Path("Reference/SimCopterOriginalGame/SimCopter.exe")
d = p.read_bytes()
pe = struct.unpack_from("<I", d, 0x3c)[0]
assert d[pe:pe+4] == b"PE\0\0"
nsec = struct.unpack_from("<H", d, pe + 6)[0]
optsz = struct.unpack_from("<H", d, pe + 20)[0]
imagebase = struct.unpack_from("<I", d, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    name = d[o:o+8].rstrip(b"\0").decode()
    vsz, va, rsz, raw = struct.unpack_from("<IIII", d, o + 8)
    secs.append((name, va, vsz, raw, rsz))
print("imagebase", hex(imagebase))
for s in secs:
    print(f"  {s[0]:8} va={s[1]:#010x} vsz={s[2]:#x} raw={s[3]:#x} rsz={s[4]:#x}")

def rva(addr):
    r = addr - imagebase
    for name, va, vsz, raw, rsz in secs:
        if va <= r < va + max(vsz, rsz):
            return raw + (r - va)
    return None

for addr, kind in ((0x4f5110, "double"), (0x4f5118, "double"), (0x4f5120, "float"),
                   (0x506c18, "double"), (0x4f5388, "double"), (0x4f5390, "double"),
                   (0x506bf0, "float")):
    off = rva(addr)
    if off is None:
        print(f"{addr:#x}: not mapped")
        continue
    raw = d[off:off+8]
    f = struct.unpack_from("<f", raw)[0]
    dbl = struct.unpack_from("<d", raw)[0]
    print(f"{addr:#x} [{kind}] bytes={raw.hex()} float={f!r} double={dbl!r}")
