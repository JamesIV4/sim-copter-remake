"""Extract the XBLD property table at DAT_00504848 straight out of SimCopter.exe's .data.

FUN_0049a4d0(id) returns `&DAT_00504848 + id * 0x14` for 0 <= id < 0x100, so the table is
256 records of 0x14 bytes. Nothing in the Ghidra exports writes it, so it is statically
initialised data and lives in the image file - no live-process rip needed.

Reads the PE section headers from the file itself, so the RVA -> offset mapping is this
executable's own and stays right even if the copy has been patched.
"""
import struct
import sys
from pathlib import Path

REPO = Path(r"S:\Repos\sim-copter-remake")
EXE = REPO / "Reference/SimCopterOriginalGame/SimCopter.exe"

TABLE_VA = 0x00504848
RECORD_SIZE = 0x14
RECORD_COUNT = 0x100


def parse_pe(data: bytes):
    """Return (image_base, [(name, va, vsize, raw_off, raw_size), ...])."""
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e_lfanew:e_lfanew + 4] == b"PE\0\0", "not a PE"
    coff = e_lfanew + 4
    num_sections = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    assert magic == 0x10B, f"expected PE32, got {magic:#x}"
    image_base = struct.unpack_from("<I", data, opt + 28)[0]

    sections = []
    sec = opt + opt_size
    for i in range(num_sections):
        o = sec + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("latin1")
        vsize, va, raw_size, raw_off = struct.unpack_from("<IIII", data, o + 8)
        sections.append((name, va, vsize, raw_off, raw_size))
    return image_base, sections


def va_to_offset(va, image_base, sections):
    rva = va - image_base
    for name, sva, vsize, raw_off, raw_size in sections:
        if sva <= rva < sva + max(vsize, raw_size):
            delta = rva - sva
            if delta >= raw_size:
                return None, name  # in the zero-filled tail (.bss-like), not in the file
            return raw_off + delta, name
    return None, None


def main():
    data = EXE.read_bytes()
    image_base, sections = parse_pe(data)

    print(f"# {EXE.name}  {len(data)} bytes  image base {image_base:#010x}")
    print("# sections:")
    for name, va, vsize, raw_off, raw_size in sections:
        print(f"#   {name:8s} VA {image_base + va:#010x}  vsize {vsize:#08x}  "
              f"raw {raw_off:#08x}+{raw_size:#08x}")

    off, section = va_to_offset(TABLE_VA, image_base, sections)
    print(f"# table VA {TABLE_VA:#010x} -> section {section}, file offset "
          f"{off:#x}" if off is not None else f"# table VA {TABLE_VA:#010x} -> NOT IN FILE ({section})")
    if off is None:
        sys.exit("table is in uninitialised data; a live rip would be required")

    blob = data[off:off + RECORD_SIZE * RECORD_COUNT]
    assert len(blob) == RECORD_SIZE * RECORD_COUNT

    out = REPO / "Docs/scratchpad/agent-sessions/2026-08-05-mission-authenticity/xbld_property_table.bin"
    out.write_bytes(blob)
    print(f"# wrote {out.name} ({len(blob)} bytes)")
    print()

    # Per-record hex plus the fields the decompiles actually read.
    print("#  id   byte0  |  full record (0x14 bytes)")
    for i in range(RECORD_COUNT):
        r = blob[i * RECORD_SIZE:(i + 1) * RECORD_SIZE]
        if not any(r):
            continue  # skip the empty rows so the interesting ids stand out
        print(f"  0x{i:02x}  0x{r[0]:02x}   | " + " ".join(f"{b:02x}" for b in r))

    print()
    print("# byte-0 flag histogram")
    hist = {}
    for i in range(RECORD_COUNT):
        b = blob[i * RECORD_SIZE]
        hist.setdefault(b, []).append(i)
    for b in sorted(hist):
        ids = hist[b]
        rng = f"{len(ids)} ids"
        print(f"  0x{b:02x}: {rng:10s} " + ", ".join(f"{x:#04x}" for x in ids[:24]) +
              (" ..." if len(ids) > 24 else ""))


if __name__ == "__main__":
    main()
