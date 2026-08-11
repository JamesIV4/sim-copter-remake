"""Scratch: find who initialises DAT_00504518 / DAT_00504538.

Both tables read zero out of the file, so an init function writes them at startup. Scan .text
for the little-endian immediates 0x00504518..0x00504534 and 0x00504538..0x00504554 (any element
of either table) and report the containing VA so ghidra-bridge can decompile the writer.
"""
import struct
import sys

EXE = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
d = open(EXE, "rb").read()

pe = struct.unpack_from("<I", d, 0x3C)[0]
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


def section_of_file_offset(off):
    for (name, vaddr, vsize, rawptr, rawsize) in sections:
        if rawptr <= off < rawptr + rawsize:
            return name, image_base + vaddr + (off - rawptr)
    return None, None


targets = [0x00504518 + 4 * i for i in range(8)] + [0x00504538 + 4 * i for i in range(8)]
for target in targets:
    needle = struct.pack("<I", target)
    start = 0
    while True:
        idx = d.find(needle, start)
        if idx < 0:
            break
        start = idx + 1
        name, va = section_of_file_offset(idx)
        if name in (".text", "CODE", ".rdata", ".data"):
            print("%08x referenced from %s VA %08x (file %08x)" % (target, name, va, idx))
