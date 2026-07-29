import sys, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

PATH = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
data = open(PATH, "rb").read()
pe = struct.unpack_from("<I", data, 0x3c)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
sect = pe + 24 + optsz
imgbase = struct.unpack_from("<I", data, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = sect + i * 40
    name = data[o:o+8].rstrip(b"\0").decode()
    vsz, va, rsz, raw = struct.unpack_from("<IIII", data, o + 8)
    secs.append((name, va, vsz, raw, rsz))

def va2off(va):
    rva = va - imgbase
    for name, sva, vsz, raw, rsz in secs:
        if sva <= rva < sva + vsz:
            return raw + (rva - sva)
    return None

md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = False

for arg in sys.argv[1:]:
    start = int(arg, 16)
    off = va2off(start)
    if off is None:
        print(f"; {start:#x} not mapped"); continue
    print(f"\n; ================ {start:08x} ================")
    buf = data[off:off + 4096]
    n = 0
    for ins in md.disasm(buf, start):
        print(f"{ins.address:08x} {ins.mnemonic:<7} {ins.op_str}")
        n += 1
        if ins.mnemonic == "ret" or ins.mnemonic.startswith("ret"):
            # stop if the next bytes look like padding / a new function prologue
            nxt = data[off + (ins.address + ins.size - start)]
            if nxt in (0xcc, 0x90):
                break
            # allow continuation (multiple rets in one function)
        if n > 700:
            break
