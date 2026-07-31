"""Disassemble an address range out of the original SimCopter.exe.

Ghidra folds several of the map widget's vtable methods into their neighbours, so they
never reach the .ghidra-exports dump. This walks the PE section table and disassembles
raw bytes instead. Usage: disasm_range.py <startHex> <endHex> [outFile]
"""
import sys
import capstone

EXE = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"


def load_sections(data):
    pe = int.from_bytes(data[0x3C:0x40], "little")
    nsec = int.from_bytes(data[pe + 6:pe + 8], "little")
    optsize = int.from_bytes(data[pe + 20:pe + 22], "little")
    base = int.from_bytes(data[pe + 24 + 28:pe + 24 + 32], "little")
    tab = pe + 24 + optsize
    secs = []
    for i in range(nsec):
        o = tab + i * 40
        va = int.from_bytes(data[o + 12:o + 16], "little")
        vs = int.from_bytes(data[o + 8:o + 12], "little")
        raw = int.from_bytes(data[o + 20:o + 24], "little")
        secs.append((base + va, vs, raw))
    return secs


def va_to_off(secs, va):
    for start, size, raw in secs:
        if start <= va < start + size:
            return raw + (va - start)
    raise ValueError("VA 0x%x not mapped" % va)


def main():
    start = int(sys.argv[1], 16)
    end = int(sys.argv[2], 16)
    out = sys.argv[3] if len(sys.argv) > 3 else None

    data = open(EXE, "rb").read()
    secs = load_sections(data)
    off = va_to_off(secs, start)
    code = data[off:off + (end - start)]

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = False
    lines = []
    for ins in md.disasm(code, start):
        lines.append("%08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    text = "\n".join(lines) + "\n"
    if out:
        open(out, "w", encoding="utf-8").write(text)
        print("wrote %s (%d instructions)" % (out, len(lines)))
    else:
        print(text)


if __name__ == "__main__":
    main()
