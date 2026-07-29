"""Read N dwords at a virtual address out of SimCopter.exe."""
import struct
import sys
from pathlib import Path

data = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe").read_bytes()
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    name = data[o:o + 8].rstrip(b"\0").decode()
    vsz, va, rawsz, raw = struct.unpack_from("<IIII", data, o + 8)
    secs.append((name, va, vsz, raw, rawsz))


def to_off(rva):
    for name, va, vsz, raw, rawsz in secs:
        if va <= rva < va + max(vsz, rawsz):
            return raw + (rva - va)
    return None


addr = int(sys.argv[1], 0)
count = int(sys.argv[2]) if len(sys.argv) > 2 else 17
off = to_off(addr - base)
print(f"imagebase 0x{base:08x} va 0x{addr:08x} -> file 0x{off:x}")
for i in range(count):
    v = struct.unpack_from("<I", data, off + i * 4)[0]
    print(f"  [{i:2d}] 0x{v:08x}")
