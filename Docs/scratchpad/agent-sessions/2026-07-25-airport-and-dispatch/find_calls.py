import struct, json, sys, bisect
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
code = data[raw:raw + rawsz]

funcs = sorted(int(k, 16) for k in json.load(open(r"S:\Repos\sim-copter-remake\.ghidra-exports\_index.json")))
def owner(a):
    i = bisect.bisect_right(funcs, a) - 1
    return funcs[i] if i >= 0 else None

target = int(sys.argv[1], 16)
md = Cs(CS_ARCH_X86, CS_MODE_32)
found = 0
for idx, f in enumerate(funcs):
    fend = funcs[idx + 1] if idx + 1 < len(funcs) else start_va + len(code)
    off = f - start_va
    if off < 0 or off >= len(code):
        continue
    for insn in md.disasm(code[off:off + (fend - f)], f):
        if insn.mnemonic in ("call", "jmp") and insn.op_str.startswith("0x"):
            if int(insn.op_str, 16) == target:
                print(f"  {insn.address:08x} in FUN_{f:08x}: {insn.mnemonic} {insn.op_str}")
                found += 1
print(f"{found} direct call/jmp site(s) to {target:08x}")
