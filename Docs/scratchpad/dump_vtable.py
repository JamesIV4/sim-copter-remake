"""Scratch: dump a vtable's function pointers straight out of SimCopter.exe's .rdata.

The ghidra-bridge export has no vtable table, and the seat-window widget class
(FUN_00453840, "seatwin2.bmp" + "people1.bmp") reaches everything through
PTR_LAB_004f2f78. Walk the PE section table, map the VA to a file offset and print
N dwords with the section each target lands in.

    python dump_vtable.py 0x004f2f78 32
"""
import struct
import sys

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


def section_of(va):
    rva = va - image_base
    for (name, vaddr, vsize, rawptr, rawsize) in sections:
        if vaddr <= rva < vaddr + vsize:
            off = rva - vaddr
            return name, (rawptr + off if off < rawsize else None)
    return None, None


base = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x004F2F78
count = int(sys.argv[2]) if len(sys.argv) > 2 else 32

name, off = section_of(base)
print("vtable %08x in %s @ file %08x" % (base, name, off))
for i in range(count):
    entry = struct.unpack_from("<I", d, off + i * 4)[0]
    tsec, _ = section_of(entry)
    print("  [%2d] +0x%02x  %08x  (%s)" % (i, i * 4, entry, tsec))
