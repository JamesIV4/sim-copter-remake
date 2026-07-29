import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

path = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
data = open(path, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
num_sections = struct.unpack_from("<H", data, coff + 2)[0]
size_opt = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
image_base = struct.unpack_from("<I", data, opt + 28)[0]
sec_off = opt + size_opt
sections = []
for i in range(num_sections):
    o = sec_off + i * 40
    sections.append((struct.unpack_from("<I", data, o + 12)[0],
                     struct.unpack_from("<I", data, o + 8)[0],
                     struct.unpack_from("<I", data, o + 20)[0]))

def va_to_off(va):
    rva = va - image_base
    for vaddr, vsize, rawptr in sections:
        if vaddr <= rva < vaddr + vsize:
            return rawptr + (rva - vaddr)
    return None

start = int(sys.argv[1], 16)
length = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x60
off = va_to_off(start)
md = Cs(CS_ARCH_X86, CS_MODE_32)
for insn in md.disasm(data[off:off+length], start):
    print(f"{insn.address:08x}  {insn.bytes.hex():<20s} {insn.mnemonic} {insn.op_str}")
