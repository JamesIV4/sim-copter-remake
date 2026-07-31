"""Find every 4-byte little-endian occurrence of an address inside SimCopter.exe's .text/.rdata.

The Ghidra export only carries functions it analysed; the radio loader is not one of them, so
locate its code by scanning for `push imm32` / `mov reg, imm32` references to the path strings.
"""
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


def off_to_va(o):
    for name, va, vsize, rsize, raw in SECS:
        if raw <= o < raw + rsize:
            return va + (o - raw), name
    return None, None


for arg in sys.argv[1:]:
    target = int(arg, 16)
    needle = struct.pack("<I", target)
    print(f"\n===== refs to {target:#010x}")
    start = 0
    while True:
        i = data.find(needle, start)
        if i < 0:
            break
        start = i + 1
        va, sec = off_to_va(i)
        if va is None:
            continue
        prev = data[i - 1] if i > 0 else 0
        # 0x68 = push imm32, 0xB8..0xBF = mov r32, imm32
        kind = "push" if prev == 0x68 else ("mov" if 0xB8 <= prev <= 0xBF else "data/other")
        print(f"  {va:#010x}  ({sec}, {kind}, prev={prev:#04x})")
