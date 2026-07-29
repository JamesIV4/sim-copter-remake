"""Build the exact opcode -> handler map for SimCopter's people VM.

FUN_004c3010 fills the 88-entry table at 0x0058ef78 with 0x20-byte thunks starting at
0x004c84e0; each thunk forwards to the real handler with a call at thunk+0x0e.
Opcode index = (table address - 0x58ef78) / 4.
"""
import re
import struct
import sys
from collections import Counter
from pathlib import Path

REPO = Path(r"S:\Repos\sim-copter-remake")
EXE = REPO / "Reference/SimCopterOriginalGame/SimCopter.exe"

data = EXE.read_bytes()
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    vsz, va, rawsz, raw = struct.unpack_from("<IIII", data, o + 8)
    secs.append((va, vsz, raw, rawsz))


def to_off(va):
    rva = va - base
    for sva, vsz, raw, rawsz in secs:
        if sva <= rva < sva + max(vsz, rawsz):
            return raw + (rva - sva)
    return None


# The assignment list lifted verbatim from FUN_004c3010's decompile.
ASSIGNMENTS = """
0058ef78 004c84e0 | 0058ef7c 004c8500 | 0058ef80 004c8520 | 0058ef88 004c8540
0058ef90 004c8560 | 0058ef94 004c8580 | 0058efa8 004c85a0 | 0058efac 004c85c0
0058efb0 004c85e0 | 0058efb4 004c8600 | 0058efb8 004c8620 | 0058efbc 004c8640
0058efc0 004c8660 | 0058efc4 004c8680 | 0058efc8 004c86a0 | 0058efcc 004c86c0
0058efd0 004c86e0 | 0058efd4 004c8700 | 0058efd8 004c8720 | 0058efdc 004c8740
0058efe0 004c8760 | 0058efe4 004c8780 | 0058efe8 004c87a0 | 0058efec 004c87c0
0058eff0 004c87e0 | 0058eff4 004c8800 | 0058eff8 004c8820 | 0058effc 004c8840
0058f000 004c8860 | 0058f004 004c8880 | 0058f008 004c88a0 | 0058f00c 004c88c0
0058f010 004c88e0 | 0058f014 004c8900 | 0058f018 004c8920 | 0058f020 004c8940
0058f024 004c8960 | 0058f028 004c8980 | 0058f02c 004c89a0 | 0058f030 004c89c0
0058f034 004c89e0 | 0058f038 004c8a00 | 0058f03c 004c8a20 | 0058f040 004c8a40
0058f044 004c8a60 | 0058f048 004c8a80 | 0058f04c 004c8aa0 | 0058f050 004c8ac0
0058f054 004c8ae0 | 0058f058 004c8b00 | 0058f05c 004c8b20 | 0058f060 004c8b40
0058f064 004c8b60 | 0058f0d0 004c8b80 | 0058f0d4 004c8ba0 | 0058f068 004c8bc0
0058f06c 004c8be0 | 0058f070 004c8c00 | 0058f074 004c8c20 | 0058f078 004c8c40
0058f07c 004c8c60 | 0058f080 004c8c80 | 0058f084 004c8ca0 | 0058f088 004c8cc0
0058f08c 004c8ce0 | 0058f090 004c8d00 | 0058f094 004c8d20 | 0058f098 004c8d40
0058f09c 004c8d60 | 0058f0a0 004c8d80 | 0058f0a4 004c8da0 | 0058f0a8 004c8dc0
0058f0ac 004c8de0 | 0058f0b0 004c8e00 | 0058f0bc 004c8e20 | 0058f0b4 004c8e40
0058f0b8 004c8e60 | 0058f0c0 004c8e80 | 0058f0c4 004c8ea0 | 0058f0c8 004c8ec0
0058f0cc 004c8ee0
"""

TABLE_BASE = 0x0058EF78
opcode_handler = {}
for slot, thunk in re.findall(r"([0-9a-f]{8})\s+([0-9a-f]{8})", ASSIGNMENTS):
    op = (int(slot, 16) - TABLE_BASE) // 4
    thunk_va = int(thunk, 16)
    off = to_off(thunk_va + 0x0E)
    assert data[off] == 0xE8, f"thunk {thunk_va:#x} does not start with a call"
    rel = struct.unpack_from("<i", data, off + 1)[0]
    opcode_handler[op] = thunk_va + 0x0E + 5 + rel

# What the shipped programs actually use.
sys.path.insert(0, str(REPO / "Tools"))
from privanim_extract import DougFile, _s8  # noqa: E402

df = DougFile((REPO / "Reference/SimCopterOriginalGame/X/people.df").read_bytes())
used = Counter()
programs = {}
for e in df.entries("BHAV"):
    off, _ = df.chunk(e)
    count = struct.unpack_from(">H", df.d, off)[0]
    recs = []
    for i in range(count):
        o = off + 2 + i * 12
        op = struct.unpack_from(">H", df.d, o)[0]
        recs.append((op, _s8(df.d[o + 2]), _s8(df.d[o + 3]), struct.unpack_from(">4H", df.d, o + 4)))
        if op < 0x100:
            used[op] += 1
    programs[e["id"]] = (e["name"], recs)

# What the remake implements: every `case N:` in ExecOpcode.
vm = (REPO / "SimCopterRemake/Source/SimCopterRemake/Private/Ground/SimCopterBehaviorVM.cpp").read_text()
body = vm[vm.index("EOpResult ExecOpcode"):vm.index("} // namespace")]
ported = {int(m) for m in re.findall(r"^\tcase (\d+):", body, re.M)}

print(f"{len(opcode_handler)} opcodes in the table, {len(used)} used by shipped programs, "
      f"{len(ported)} ported\n")
print(f"{'op':>4} {'handler':>10}  {'uses':>5}  ported")
for op in sorted(opcode_handler):
    mark = "yes" if op in ported else ("NO" if used[op] else "-")
    print(f"{op:>4} FUN_{opcode_handler[op]:08x}  {used[op]:>5}  {mark}")

missing = sorted(op for op in used if op not in ported)
print("\nUsed by shipped programs but NOT ported:")
for op in missing:
    print(f"  op {op:>3}  FUN_{opcode_handler.get(op, 0):08x}  {used[op]} sites")
