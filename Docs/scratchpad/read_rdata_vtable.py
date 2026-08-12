"""Read the sound class's vtable out of SimCopter.exe's .rdata.

The vtable is PTR_FUN_004f1568 (FUN_00429f90 writes it into every sound object). Ghidra's
exports only carry named globals, so the slot addresses are read straight from the image the
way Docs/memory/simcopter-mission-authenticity.md section 8 describes: parse the PE section
headers from the file, map VA -> file offset, and print the slots.

RESULT, 2026-08-12: THIS DOES NOT WORK against the copy in Reference/SimCopterOriginalGame.
That exe reports .rdata at VA 0x004f5000 / .data at 0x004fd000, while the exe the Ghidra
project was built from has .data at 0x004f8000 (verify_data_mapping.py proves it against six
known strings). 0x004f1568 lands inside .text here and reads as instruction bytes. It is a
differently-patched build - see AGENTS.md and simcopter-sound.md on SimCopterX-relocated
copies. To finish the question this was written for ("does the sound object's Play mode 2
restart an already-playing buffer, or leave it alone?") you need the matching executable, or
read it out of the Ghidra project itself. Kept so the next person does not re-derive the
mapping and then trust the numbers.
"""

import struct
import sys

EXE = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
VTABLE_VA = 0x004F1568
SLOTS = 40


def load_sections(data):
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe_off:pe_off + 4] == b"PE\0\0", "not a PE"
    num_sections = struct.unpack_from("<H", data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    image_base = struct.unpack_from("<I", data, pe_off + 24 + 28)[0]
    table = pe_off + 24 + opt_size
    sections = []
    for i in range(num_sections):
        off = table + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, off + 8)
        sections.append((name, va, max(vsize, rawsize), raw))
    return image_base, sections


def va_to_off(image_base, sections, va):
    rva = va - image_base
    for name, sec_rva, size, raw in sections:
        if sec_rva <= rva < sec_rva + size:
            return raw + (rva - sec_rva), name
    return None, None


def main():
    with open(EXE, "rb") as handle:
        data = handle.read()
    image_base, sections = load_sections(data)
    print(f"image base 0x{image_base:08x}")
    for name, va, size, raw in sections:
        print(f"  {name:8s} va 0x{image_base + va:08x} size 0x{size:06x} raw 0x{raw:06x}")

    off, sec = va_to_off(image_base, sections, VTABLE_VA)
    if off is None:
        print("vtable VA not mapped", file=sys.stderr)
        return 1
    print(f"\nvtable 0x{VTABLE_VA:08x} -> file 0x{off:06x} in {sec}")
    for i in range(SLOTS):
        value = struct.unpack_from("<I", data, off + i * 4)[0]
        print(f"  +0x{i * 4:02x}  0x{value:08x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
