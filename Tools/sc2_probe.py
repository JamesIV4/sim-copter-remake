#!/usr/bin/env python3
"""Read-only SimCity 2000 .sc2 probe used to validate reverse-engineering notes."""

from __future__ import annotations

import argparse
from pathlib import Path


EXPECTED_SIZES = {
    "CNAM": 32,
    "MISC": 4800,
    "ALTM": 32768,
    "XTER": 16384,
    "XBLD": 16384,
    "XZON": 16384,
    "XUND": 16384,
    "XTXT": 16384,
    "XLAB": 6400,
    "XMIC": 1200,
    "XTHG": 480,
    "XBIT": 16384,
    "XTRF": 4096,
    "XPLT": 4096,
    "XVAL": 4096,
    "XCRM": 4096,
    "XPLC": 1024,
    "XFIR": 1024,
    "XPOP": 1024,
    "XROG": 1024,
    "XGRP": 3328,
}

UNCOMPRESSED_CHUNKS = {"ALTM", "CNAM", "TEXT", "SCEN", "PICT", "TMPL"}


def decode_rle(data: bytes) -> bytes:
    out = bytearray()
    offset = 0

    while offset < len(data):
        control = data[offset]
        offset += 1

        if control <= 127:
            literal_count = control
            out.extend(data[offset : offset + literal_count])
            offset += literal_count
        elif control >= 129:
            if offset >= len(data):
                raise ValueError("repeat run is missing its byte value")
            out.extend(bytes([data[offset]]) * (control - 127))
            offset += 1
        else:
            raise ValueError("control byte 128 is reserved")

    return bytes(out)


def read_city_name(cnam: bytes, fallback: str) -> str:
    chars: list[str] = []
    for value in cnam[1:32]:
        if value == 0 or value < 32 or value > 126:
            break
        chars.append(chr(value))
    return "".join(chars) or fallback.upper()


def probe_file(path: Path) -> tuple[str, list[str]]:
    data = path.read_bytes()
    errors: list[str] = []

    if len(data) < 12 or data[:4] != b"FORM" or data[8:12] != b"SCDH":
        return "", [f"{path}: missing FORM/SCDH header"]

    declared_size = int.from_bytes(data[4:8], "big")
    if declared_size + 8 != len(data):
        errors.append(f"{path}: declared size {declared_size} does not match actual {len(data)}")

    city_name = path.stem.upper()
    offset = 12
    chunk_count = 0

    while offset < len(data):
        if offset + 8 > len(data):
            errors.append(f"{path}: truncated chunk header at {offset}")
            break

        chunk_id = data[offset : offset + 4].decode("ascii", "replace")
        stored_size = int.from_bytes(data[offset + 4 : offset + 8], "big")
        offset += 8
        payload = data[offset : offset + stored_size]
        offset += stored_size
        chunk_count += 1

        decoded = payload if chunk_id in UNCOMPRESSED_CHUNKS else decode_rle(payload)
        expected_size = EXPECTED_SIZES.get(chunk_id)
        if expected_size is not None and len(decoded) != expected_size:
            errors.append(f"{path}: {chunk_id} decoded {len(decoded)}, expected {expected_size}")

        if chunk_id == "CNAM":
            city_name = read_city_name(decoded, path.stem)

    if offset != len(data):
        errors.append(f"{path}: ended at {offset}, file has {len(data)} bytes")

    return f"{path.name}: name={city_name!r} chunks={chunk_count}", errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "root",
        nargs="?",
        default="Reference/SimCopterOriginalGame/cities",
        help="City file or directory containing .sc2 files.",
    )
    args = parser.parse_args()

    root = Path(args.root)
    paths = [root] if root.is_file() else sorted(root.glob("**/*.sc2"))

    all_errors: list[str] = []
    for path in paths:
        summary, errors = probe_file(path)
        if summary:
            print(summary)
        all_errors.extend(errors)

    print(f"cities {len(paths)} errors {len(all_errors)}")
    for error in all_errors:
        print(error)

    return 1 if all_errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
