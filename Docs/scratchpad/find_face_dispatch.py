"""Scratch: locate the face-type handler table that points at the light rasteriser.

FUN_00496c00 has no Ghidra callers, which for this exe usually means it is reached through a
function-pointer table in .data/.rdata. Find every occurrence of the address as a little-endian
dword, then print the surrounding table so the index (= Maxis face type) can be read off.
"""
import struct
import sys

EXE = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
TARGET = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x00496c00
SPAN = int(sys.argv[2]) if len(sys.argv) > 2 else 32

d = open(EXE, "rb").read()

pe = struct.unpack_from("<I", d, 0x3c)[0]
assert d[pe:pe + 4] == b"PE\0\0"
n_sections = struct.unpack_from("<H", d, pe + 6)[0]
opt_size = struct.unpack_from("<H", d, pe + 20)[0]
image_base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
sec_off = pe + 24 + opt_size

sections = []
for i in range(n_sections):
    o = sec_off + i * 40
    name = d[o:o + 8].rstrip(b"\0").decode()
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", d, o + 8)
    sections.append((name, vaddr, vsize, raddr, rsize))

print("image base 0x%08x" % image_base)
for s in sections:
    print("  %-8s va=0x%08x vsize=0x%-8x raw=0x%-8x rsize=0x%x" % (s[0], image_base + s[1], s[2], s[3], s[4]))


def file_to_va(off):
    for name, vaddr, vsize, raddr, rsize in sections:
        if raddr <= off < raddr + rsize:
            return image_base + vaddr + (off - raddr), name
    return None, None


def va_to_file(va):
    rva = va - image_base
    for name, vaddr, vsize, raddr, rsize in sections:
        if vaddr <= rva < vaddr + vsize:
            return raddr + (rva - vaddr)
    return None


needle = struct.pack("<I", TARGET)
pos = 0
hits = []
while True:
    pos = d.find(needle, pos)
    if pos < 0:
        break
    hits.append(pos)
    pos += 1

print("\n%d occurrence(s) of 0x%08x" % (len(hits), TARGET))
for h in hits:
    va, sec = file_to_va(h)
    print("\n file 0x%x -> va 0x%08x (%s)" % (h, va if va else 0, sec))
    if va is None:
        continue
    start = h - SPAN * 4
    for i in range(SPAN * 2 + 1):
        o = start + i * 4
        if o < 0 or o + 4 > len(d):
            continue
        val = struct.unpack_from("<I", d, o)[0]
        eva, _ = file_to_va(o)
        mark = "  <<<< " if o == h else "        "
        note = ""
        if 0x00401000 <= val < 0x00500000:
            note = "code?"
        print("   [%+3d] va=0x%08x  0x%08x %s%s" % (i - SPAN, eva, val, mark, note))
