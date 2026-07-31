"""Disassemble a virtual-address range of SimCopter.exe (functions Ghidra folded away)."""
import struct
import sys

import capstone

EXE = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    out, off = [], pe + 24 + opt
    for i in range(nsec):
        s = data[off + i * 40: off + (i + 1) * 40]
        vsize, va, rsize, raw = struct.unpack_from("<IIII", s, 8)
        out.append((base + va, max(vsize, rsize), raw))
    return out


def main():
    data = open(EXE, "rb").read()
    secs = sections(data)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for arg in sys.argv[1:]:
        va, n = (arg.split(":") + ["64"])[:2]
        va, n = int(va, 16), int(n)
        off = next(raw + (va - st) for st, sz, raw in secs if st <= va < st + sz)
        print(f"\n===== {va:#010x} ({n} bytes)")
        for ins in md.disasm(data[off:off + n], va):
            print(f"  {ins.address:08x}  {ins.mnemonic:8} {ins.op_str}")
            if ins.mnemonic == "ret":
                break


if __name__ == "__main__":
    main()
