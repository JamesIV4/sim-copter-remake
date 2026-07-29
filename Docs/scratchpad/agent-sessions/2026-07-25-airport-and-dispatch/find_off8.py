import struct, json, bisect
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
text = None
for i in range(nsec):
    o = secoff + i * 40
    name = data[o:o+8].rstrip(b"\0").decode()
    if name == ".text":
        vaddr = struct.unpack_from("<I", data, o + 12)[0]
        vsize = struct.unpack_from("<I", data, o + 8)[0]
        raw = struct.unpack_from("<I", data, o + 20)[0]
        rawsz = struct.unpack_from("<I", data, o + 16)[0]
        text = (base + vaddr, data[raw:raw+rawsz])
start_va, code = text

funcs = sorted(int(k, 16) for k in json.load(open(r"S:\Repos\sim-copter-remake\.ghidra-exports\_index.json")))
def owner(addr):
    i = bisect.bisect_right(funcs, addr) - 1
    return funcs[i] if i >= 0 else None

md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = False
hits = []
for insn in md.disasm(code, start_va):
    m, ops = insn.mnemonic, insn.op_str
    if m not in ("mov", "and", "or", "inc", "dec"):
        continue
    # destination must be a dword memory operand [reg + 8], base register not esp/ebp-frame
    if not ops.startswith("dword ptr ["):
        continue
    close = ops.find("]")
    mem = ops[len("dword ptr ["):close]
    if not mem.endswith(" + 8"):
        continue
    reg = mem[:-4].strip()
    if reg in ("esp", "ebp"):
        continue
    if "+" in reg or "*" in reg:
        continue
    hits.append((insn.address, owner(insn.address), m, ops))

print(f"{len(hits)} dword writes to [reg+8]\n")
# Only report the ones inside the vehicle code ranges we care about.
for addr, fn, m, ops in hits:
    if fn is None:
        continue
    if 0x0049b000 <= fn <= 0x0049ffff or 0x004a0000 <= fn <= 0x004a9fff or 0x004b8000 <= fn <= 0x004bffff:
        print(f"  {addr:08x}  in FUN_{fn:08x}   {m} {ops}")
