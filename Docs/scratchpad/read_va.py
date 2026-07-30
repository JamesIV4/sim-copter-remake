"""Scratch: read initialised bytes at a SimCopter.exe virtual address straight from the file.

FUN_00443c20 configures every Check-up control with [vt+0xe8](ptr) - 0x51a48c for the title and
0x51a494 for the body text - and neither address is in the Ghidra export. They are only 8 bytes
apart, so they are probably adjacent font-name strings. Walk the PE section table to map VA to
file offset.
"""
import struct
import sys

EXE = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
d = open(EXE, "rb").read()

pe = struct.unpack_from("<I", d, 0x3c)[0]
assert d[pe:pe + 4] == b"PE\0\0", "not a PE"
n_sections = struct.unpack_from("<H", d, pe + 6)[0]
opt_size = struct.unpack_from("<H", d, pe + 20)[0]
image_base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
sect_off = pe + 24 + opt_size

sections = []
for i in range(n_sections):
    o = sect_off + i * 40
    name = d[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
    vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", d, o + 8)
    sections.append((name, vaddr, vsize, rawptr, rawsize))
    print("%-8s VA %08x  vsize %8x  raw %08x" % (name, image_base + vaddr, vsize, rawptr))
print("image base %08x" % image_base)


def to_file(va):
    rva = va - image_base
    for (name, vaddr, vsize, rawptr, rawsize) in sections:
        if vaddr <= rva < vaddr + vsize:
            off = rva - vaddr
            return name, (rawptr + off if off < rawsize else None)
    return None, None


for va in (int(a, 16) for a in sys.argv[1:]) if len(sys.argv) > 1 else (0x51a48c, 0x51a494):
    name, off = to_file(va)
    if off is None:
        print("%08x -> section %s, not backed by file data" % (va, name))
        continue
    blob = d[off:off + 32]
    text = blob.split(b"\0")[0]
    print("%08x (%s @ %08x) bytes=%s  ascii=%r"
          % (va, name, off, blob[:16].hex(" "), text.decode("ascii", "replace")))
