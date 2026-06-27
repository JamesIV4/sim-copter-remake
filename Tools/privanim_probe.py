#!/usr/bin/env python3
"""Read-only probe for SimCopter ``X/privanim.df`` (articulated figure pack).

``privanim.df`` is a big-endian Maxis IFF-style "Doug" container. This probe
walks the structure documented in ``Docs/OriginalGameFileFormats.md``:

- header (version + name + ``RSRC`` directory),
- the end-of-file section directory (``BODC`` / ``ANIP`` / ``ARCP`` / ``ARLU`` /
  ``ARPP`` / ``SPR#``),
- the animation-clip inheritance tree in the ``ANIP``/``ARPP`` region,
- the ``ARCP`` (40-byte) / ``ARLU`` (8-byte) articulation records.

It does not modify anything and skips cleanly when the file is absent, so it can
be run without shipping original game data.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SECTION_TAGS = (b"BODC", b"ANIP", b"ARCP", b"ARLU", b"ARPP", b"SPR#")


def be_u32(data: bytes, off: int) -> int:
    return int.from_bytes(data[off : off + 4], "big")


def find_all(data: bytes, tag: bytes) -> list[int]:
    locs: list[int] = []
    i = data.find(tag)
    while i >= 0:
        locs.append(i)
        i = data.find(tag, i + 1)
    return locs


def probe_file(path: Path) -> list[str]:
    data = path.read_bytes()
    out: list[str] = [f"{path.name}: size 0x{len(data):x} ({len(data)} bytes)"]

    if data[:4] != b"\x00\x00\x01\x00":
        out.append("  WARNING: unexpected version dword (expected 00 00 01 00)")

    # Length-prefixed root name at 0x30 ("privanim").
    name_len = data[0x30]
    name = data[0x31 : 0x31 + name_len].decode("latin1", "replace")
    out.append(f"  root name @0x30: len={name_len} {name!r}")

    rsrc = find_all(data, b"RSRC")
    out.append(f"  RSRC directory entries: {len(rsrc)} at {[hex(x) for x in rsrc]}")

    # Section directory: scan for a run of 8-byte [4CC][be u32] entries whose tag
    # is one of the known section tags.
    section_dir: list[tuple[int, str, int]] = []
    for off in range(0, len(data) - 8):
        if data[off : off + 4] in SECTION_TAGS:
            # Only treat as a directory entry when the following tag is also one
            # (the directory packs them back-to-back).
            nxt = data[off + 8 : off + 12]
            if nxt in SECTION_TAGS or (section_dir and off == section_dir[-1][0] + 8):
                section_dir.append((off, data[off : off + 4].decode("latin1"), be_u32(data, off + 4)))
    if section_dir:
        out.append("  section directory:")
        for off, tag, val in section_dir:
            out.append(f"    @0x{off:06x}  {tag}  be=0x{val:08x}")

    # Animation clips live in fixed ~40-byte ARPP blocks:
    #   +0x00 "ARPP"  +0x04 be-u32 id  +0x08/+0x0c/+0x10 be-floats (pose transform)
    #   +0x14 flag bytes  +0x1c 4-char clip name  +0x20 4-char parent name
    arpp = find_all(data, b"ARPP")
    out.append(f"  ARPP clip blocks: {len(arpp)}" + (f" (first 0x{arpp[0]:x})" if arpp else ""))
    if len(arpp) >= 2:
        out.append(f"    ARPP stride: 0x{arpp[1] - arpp[0]:x} bytes")
    if arpp:
        out.append("    clip[name <- parent]  (be-floats @ +8/+0xc/+0x10); first 12:")
        for off in arpp[:12]:
            name = data[off + 0x1C : off + 0x20].decode("latin1", "replace").strip()
            parent = data[off + 0x20 : off + 0x24].decode("latin1", "replace").strip()
            fa, fb, fc = struct.unpack_from(">fff", data, off + 8)
            out.append(f"      {name:<5} <- {parent:<5}  ({fa:g}, {fb:g}, {fc:g})")

    # ARCP (40-byte) / ARLU (8-byte) record markers.
    for tag, size in ((b"ARCP", 40), (b"ARLU", 8)):
        locs = find_all(data, tag)
        out.append(f"  {tag.decode()} (record size {size}): {len(locs)} marker(s) at {[hex(x) for x in locs[:4]]}")

    # Node-directory table: 12-byte records {be u32 dataOffset, be u32, be u16, be u16 index},
    # introduced by the "03 e8 ff ff" marker just after the section directory.
    marker = data.find(b"\x03\xe8\xff\xff")
    if marker >= 0:
        out.append(f"  node-directory @0x{marker + 4:x} (12-byte records: dataOffset, _, _, index):")
        o = marker + 4
        shown = 0
        while o + 12 <= len(data) and shown < 8:
            data_off, _b, _c, idx = struct.unpack_from(">IIHH", data, o)
            if 0 < data_off < len(data):
                lead = " ".join("%02x" % b for b in data[data_off : data_off + 12])
                out.append(f"    idx={idx:<4} -> 0x{data_off:06x}: {lead}")
                shown += 1
            o += 12

    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path",
        nargs="?",
        default="Reference/SimCopterOriginalGame/X/privanim.df",
        help="path to privanim.df (default: local reference copy)",
    )
    args = parser.parse_args()
    path = Path(args.path)
    if not path.exists():
        print(f"{path}: not found (skipping; original game data is not committed)")
        return 0
    for line in probe_file(path):
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
