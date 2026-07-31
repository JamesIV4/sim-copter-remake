"""Resolve the resource manager's directory-name pointer table in SimCopter.exe .data."""
import struct
import sys

EXE = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
data = open(EXE, "rb").read()

pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
opt = struct.unpack_from("<H", data, pe + 20)[0]
base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
SECS = []
off = pe + 24 + opt
for i in range(nsec):
    s = data[off + i * 40: off + (i + 1) * 40]
    name = s[:8].rstrip(b"\0").decode()
    vsize, va, rsize, raw = struct.unpack_from("<IIII", s, 8)
    SECS.append((name, base + va, max(vsize, rsize), rsize, raw))


def to_off(va):
    for _n, st, vs, rs, raw in SECS:
        if st <= va < st + rs:
            return raw + (va - st)
    return None


def cstr(va, limit=64):
    o = to_off(va)
    if o is None:
        return None
    e = data.find(b"\0", o, o + limit)
    if e < 0:
        return None
    s = data[o:e]
    if not s or not all(32 <= c < 127 for c in s):
        return None
    return s.decode("latin1")


start = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x004F8F00
count = int(sys.argv[2]) if len(sys.argv) > 2 else 48
print(f"# pointer table from {start:#010x}")
for i in range(count):
    va = start + i * 4
    p = struct.unpack_from("<I", data, to_off(va))[0]
    s = cstr(p) if 0x400000 < p < 0x620000 else None
    idx = i
    print(f"  [{idx:2}] {va:#010x} -> {p:#010x}  {s!r}" if s else f"  [{idx:2}] {va:#010x} -> {p:#010x}")
