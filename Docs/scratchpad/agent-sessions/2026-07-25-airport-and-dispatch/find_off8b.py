import struct, json, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

exe = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
data = open(exe, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
sizeopt = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
base = struct.unpack_from("<I", data, opt + 28)[0]
secoff = opt + sizeopt
for i in range(nsec):
    o = secoff + i * 40
    if data[o:o+8].rstrip(b"\0") == b".text":
        vaddr = struct.unpack_from("<I", data, o + 12)[0]
        raw = struct.unpack_from("<I", data, o + 20)[0]
        rawsz = struct.unpack_from("<I", data, o + 16)[0]
start_va = base + vaddr
code = data[raw:raw+rawsz]

funcs = sorted(int(k, 16) for k in json.load(open(r"S:\Repos\sim-copter-remake\.ghidra-exports\_index.json")))
md = Cs(CS_ARCH_X86, CS_MODE_32)

# The field we are chasing, as a byte offset in the object.
OFFSET = int(sys.argv[1], 0) if len(sys.argv) > 1 else 8
suffix = f" + {OFFSET}" if OFFSET != 0 else ""

writers = []
for idx, fstart in enumerate(funcs):
    fend = funcs[idx + 1] if idx + 1 < len(funcs) else start_va + len(code)
    off = fstart - start_va
    if off < 0 or off >= len(code):
        continue
    for insn in md.disasm(code[off:off + (fend - fstart)], fstart):
        ops = insn.op_str
        if insn.mnemonic not in ("mov", "or", "and", "inc", "dec", "add"):
            continue
        if not ops.startswith("dword ptr ["):
            continue
        close = ops.find("]")
        mem = ops[len("dword ptr ["):close]
        if suffix and not mem.endswith(suffix):
            continue
        if not suffix and ("+" in mem or "-" in mem):
            continue
        reg = mem[:-len(suffix)].strip() if suffix else mem.strip()
        if reg in ("esp", "ebp") or "+" in reg or "*" in reg:
            continue
        # writes only: destination is the memory operand
        writers.append((insn.address, fstart, insn.mnemonic, ops))

print(f"{len(writers)} dword writes to [reg+{OFFSET}] across {len(funcs)} functions\n")
print("In the vehicle / mission code:")
for addr, fn, m, ops in writers:
    if (0x00490000 <= fn <= 0x004cffff):
        print(f"  {addr:08x}  FUN_{fn:08x}   {m} {ops}")
