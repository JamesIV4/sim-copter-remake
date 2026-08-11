"""Scratch: read the grounded-firebomb effect tables out of SimCopter.exe's .data.

FUN_0048ed00's class-0x10 arm puffs every 0x3333 of its sub-timer with

    puVar11[0xd] = (puVar11[0xd] + 1) & 7;
    pos = node.xyz; pos.y += DAT_00504538[idx];
    FUN_004af220(cell, &pos, DAT_00504518[idx]);

so DAT_00504518 is eight EFFECT TYPES and DAT_00504538 eight 16.16 vertical offsets.
Both are statically initialised, so read them straight from the file (same PE walk as
read_va.py). Nothing here is in the Ghidra export.
"""
import struct

EXE = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
d = open(EXE, "rb").read()

pe = struct.unpack_from("<I", d, 0x3C)[0]
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


def to_file(va):
    rva = va - image_base
    for (name, vaddr, vsize, rawptr, rawsize) in sections:
        if vaddr <= rva < vaddr + vsize:
            off = rva - vaddr
            return name, (rawptr + off if off < rawsize else None)
    return None, None


def dwords(va, count):
    name, off = to_file(va)
    if off is None:
        return name, None
    return name, list(struct.unpack_from("<%dI" % count, d, off))


for label, va in (("DAT_00504518 effect types", 0x00504518),
                  ("DAT_00504538 y offsets  ", 0x00504538)):
    name, vals = dwords(va, 8)
    if vals is None:
        print("%s @ %08x -> %s, not file-backed" % (label, va, name))
        continue
    print("%s @ %08x (%s)" % (label, va, name))
    for i, v in enumerate(vals):
        signed = v - (1 << 32) if v & 0x80000000 else v
        print("   [%d] 0x%08x  = %d  (16.16 = %.4f)" % (i, v, signed, signed / 65536.0))
