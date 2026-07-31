"""Scratch: find every place in SimCopter.exe where a VA appears as a 4-byte pointer.

The bridge's xrefs-to only knows call sites, so a function that is only ever reached
through a vtable slot comes back "no callers found". Scanning the raw image for the
little-endian dword finds the slot instead, and printing the section tells you whether
it is a vtable (.rdata) or an initialised data table.

    python find_ptr.py 0x0044c710
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


def va_of(file_off):
    for (name, vaddr, vsize, rawptr, rawsize) in sections:
        if rawptr <= file_off < rawptr + rawsize:
            return name, image_base + vaddr + (file_off - rawptr)
    return None, None


target = int(sys.argv[1], 16)
needle = struct.pack("<I", target)

pos = 0
while True:
    pos = d.find(needle, pos)
    if pos < 0:
        break
    name, va = va_of(pos)
    if name is not None:
        print("  %08x  (%s)  file %08x" % (va, name, pos))
    pos += 1
