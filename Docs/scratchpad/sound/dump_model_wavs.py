"""Read the per-helicopter-model rotor-loop WAV name (record +0x58) out of SimCopter.exe."""
import struct

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
    SECS.append((name, base + va, max(vsize, rsize), raw))


def to_off(va):
    for _n, st, sz, raw in SECS:
        if st <= va < st + sz:
            return raw + (va - st)
    return None


def dw(va):
    return struct.unpack_from("<I", data, to_off(va))[0]


def cstr(va):
    o = to_off(va)
    if o is None:
        return None
    e = data.index(b"\0", o)
    s = data[o:e]
    return s.decode("latin1") if all(32 <= c < 127 for c in s) and s else None


print("# helicopter model records @ 0x005040e0, stride 0x5c")
for i in range(12):
    rec = 0x005040E0 + i * 0x5C
    p = dw(rec + 0x58)
    print(f"  model {i:2}  rec={rec:#08x}  +0x58 -> {p:#010x}  {cstr(p)!r}")

print("\n# .data strings 0x503f80..0x504010")
o = to_off(0x00503F80)
cur, start = [], 0x00503F80
for i in range(0x90):
    c = data[o + i]
    if c == 0:
        if len(cur) >= 3:
            print(f"  {start:#08x}  {bytes(cur).decode('latin1')!r}")
        cur, start = [], 0x00503F80 + i + 1
    else:
        cur.append(c)
