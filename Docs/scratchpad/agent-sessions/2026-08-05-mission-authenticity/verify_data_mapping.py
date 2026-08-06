"""Verify the VA->file mapping (and that this exe IS the Ghidra project's) before trusting a dump.

Cross-checks known .data strings the ghidra-bridge `strings` command reported at exact addresses.
Then prints the XBLD property table over the building id range that actually matters.
"""
import struct
import sys
from pathlib import Path

REPO = Path(r"S:\Repos\sim-copter-remake")
EXE = REPO / "Reference/SimCopterOriginalGame/SimCopter.exe"
sys.path.insert(0, str(Path(__file__).parent))
from dump_xbld_property_table import parse_pe, va_to_offset  # noqa: E402

# (VA, expected bytes) - all reported by `ghidra-bridge strings` earlier this session.
CHECKS = [
    (0x00506100, b"Criminal Miss"),
    (0x00506110, b"Fire Miss"),
    (0x00506148, b"Riot Miss"),
    (0x0050650c, b"kissA.WAV"),
    (0x00506500, b"kissB.WAV"),
    (0x005060e0, b"Traffic Miss"),
]


def main():
    data = EXE.read_bytes()
    image_base, sections = parse_pe(data)

    print("=== mapping verification ===")
    ok = True
    for va, expected in CHECKS:
        off, sec = va_to_offset(va, image_base, sections)
        if off is None:
            print(f"  {va:#010x} -> NOT IN FILE")
            ok = False
            continue
        got = data[off:off + len(expected)]
        match = "OK " if got == expected else "BAD"
        if got != expected:
            ok = False
        print(f"  [{match}] {va:#010x} ({sec} +{off:#x}) = {got!r}  expected {expected!r}")
    print(f"  => {'mapping and binary both match the Ghidra project' if ok else 'MISMATCH'}")
    if not ok:
        sys.exit(1)

    print()
    print("=== DAT_00504848 over the ids the fire placer cares about ===")
    off, _ = va_to_offset(0x00504848, image_base, sections)
    print("  building ids are 0x70..0xdb; bit 2 (0x04) of byte 0 is the flag FUN_004a92f0 tests")
    for i in list(range(0x6c, 0x80)) + list(range(0xd0, 0xe0)):
        r = data[off + i * 0x14: off + (i + 1) * 0x14]
        dwords = struct.unpack("<5i", r)
        flag = "SET" if (r[0] & 4) else "   "
        print(f"  0x{i:02x}  byte0=0x{r[0]:02x} {flag}  dwords={[hex(d & 0xffffffff) for d in dwords]}")

    print()
    print("=== byte-0 values across the whole 256 ===")
    seen = {}
    for i in range(0x100):
        b = data[off + i * 0x14]
        seen.setdefault(b, []).append(i)
    for b in sorted(seen):
        ids = seen[b]
        print(f"  byte0 0x{b:02x} (bit2={'Y' if b & 4 else 'n'}): {len(ids):3d} ids  "
              + ", ".join(f"{x:#04x}" for x in ids[:16]) + (" ..." if len(ids) > 16 else ""))


if __name__ == "__main__":
    main()
