"""Scratch: follow a PTR_DAT_* slot in SimCopter.exe and print the C string it points at.

The Ghidra export lists the pointer but not always its target, and several filename suffixes
(".sc2", the career-screen picture suffix) only exist behind one of these slots.

    python dump_strptr.py 0x004f8290 0x004f8294 0x004f8298
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


def file_off(va):
    rva = va - image_base
    for (name, vaddr, vsize, rawptr, rawsize) in sections:
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            off = rva - vaddr
            return (name, rawptr + off) if off < rawsize else (name, None)
    return None, None


def cstr(off, limit=160):
    end = d.index(b"\0", off, off + limit)
    return d[off:end].decode("latin-1")


for arg in sys.argv[1:]:
    va = int(arg, 16)
    sec, off = file_off(va)
    if off is None:
        print("%08x  not backed by file data (%s)" % (va, sec))
        continue
    value = struct.unpack_from("<I", d, off)[0]
    tsec, toff = file_off(value)
    text = cstr(toff) if toff is not None else "<uninitialised>"
    print("%08x (%s) -> %08x (%s)  %r" % (va, sec, value, tsec, text))
