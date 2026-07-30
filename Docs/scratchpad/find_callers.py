"""Scratch: brute-force every relative CALL/JMP in .text whose target is a given VA.

Ghidra reports "0 callers" for functions reached from regions it never analysed, so scan the raw
section for E8/E9 rel32 encodings and resolve them by hand.
"""
import struct
import sys

EXE = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
TARGET = int(sys.argv[1], 16)

d = open(EXE, "rb").read()
pe = struct.unpack_from("<I", d, 0x3c)[0]
n_sections = struct.unpack_from("<H", d, pe + 6)[0]
opt_size = struct.unpack_from("<H", d, pe + 20)[0]
image_base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
sec_off = pe + 24 + opt_size

text = None
for i in range(n_sections):
    o = sec_off + i * 40
    name = d[o:o + 8].rstrip(b"\0").decode()
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", d, o + 8)
    if name == ".text":
        text = (image_base + vaddr, raddr, rsize)

base_va, raw, size = text
for off in range(raw, raw + size - 5):
    op = d[off]
    if op not in (0xE8, 0xE9):
        continue
    rel = struct.unpack_from("<i", d, off + 1)[0]
    src_va = base_va + (off - raw)
    if src_va + 5 + rel == TARGET:
        print("%s at va 0x%08x (file 0x%x)" % ("call" if op == 0xE8 else "jmp ", src_va, off))
