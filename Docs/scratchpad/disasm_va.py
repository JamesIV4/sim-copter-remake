"""Scratch: disassemble SimCopter.exe at a virtual address with capstone.

The main-menu page's vtable slot +0x04 is 0x0045f3d0, which Ghidra folded into the gap after
FUN_0045f3a0 and never exported, so ghidra-bridge cannot decompile or dump-asm it. Falling back
to the bytes is the documented path (Docs/memory/simcopter-ghidra-workflow.md).

    python disasm_va.py 0x0045f3d0 [instruction_count]
"""
import struct
import sys

import capstone

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
        if vaddr <= rva < vaddr + vsize:
            off = rva - vaddr
            return rawptr + off if off < rawsize else None
    return None


base = int(sys.argv[1], 16)
count = int(sys.argv[2]) if len(sys.argv) > 2 else 300
# A third argument keeps going past the first RET - Ghidra folds several of these menu methods
# into one another, so the interesting tail often sits after an early return.
run_past_ret = len(sys.argv) > 3
off = file_off(base)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = False

n = 0
for insn in md.disasm(d[off:off + count * 8], base):
    print("%08x  %-8s %s" % (insn.address, insn.mnemonic, insn.op_str))
    n += 1
    if n >= count:
        break
    if insn.mnemonic == "ret" and not run_past_ret:
        break
