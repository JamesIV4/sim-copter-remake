#!/usr/bin/env python3
"""Read-only probe for SimCopter/Streets Maxis composite bitmap files."""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path


def i32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little", signed=True)


def can_read(data: bytes, offset: int, size: int) -> bool:
    return offset >= 0 and size >= 0 and offset <= len(data) and size <= len(data) - offset


def probe_file(path: Path) -> tuple[str, list[str]]:
    data = path.read_bytes()
    errors: list[str] = []

    if len(data) < 16:
        return "", [f"{path}: too small for Maxis composite bitmap header"]

    if data[:2] == b"BM":
        return "", []

    declared_size = i32(data, 0)
    image_count = i32(data, 8)
    resolution_count = i32(data, 12)
    if declared_size != len(data):
        errors.append(f"{path}: declared file size {declared_size} does not match actual {len(data)}")
    if image_count <= 0 or resolution_count <= 0:
        errors.append(f"{path}: invalid counts images={image_count} resolutions={resolution_count}")
        return "", errors

    cursor = 16 + resolution_count * 12
    if not can_read(data, 16, resolution_count * 12):
        errors.append(f"{path}: resolution table is outside the file")
        return "", errors

    dims: Counter[tuple[int, int]] = Counter()
    for image_index in range(image_count):
        if not can_read(data, cursor, 12):
            errors.append(f"{path}: truncated before image {image_index} header")
            break

        width = i32(data, cursor)
        height = i32(data, cursor + 4)
        unknown = i32(data, cursor + 8)
        if width <= 0 or height <= 0 or width > 4096 or height > 4096 or unknown != 0:
            errors.append(f"{path}: image {image_index} invalid header width={width} height={height} unknown={unknown}")
            break

        row_table = cursor + 12
        data_start = row_table + height * 4
        pixel_count = width * height
        if not can_read(data, row_table, height * 4) or not can_read(data, data_start, pixel_count):
            errors.append(f"{path}: image {image_index} data is outside the file")
            break

        for row in range(height):
            row_offset = i32(data, row_table + row * 4)
            if row_offset < 0 or row_offset > pixel_count - width:
                errors.append(f"{path}: image {image_index} row {row} invalid row offset {row_offset}")
                break

        dims[(width, height)] += 1
        cursor = data_start + pixel_count

    if cursor != len(data):
        errors.append(f"{path}: parser stopped at byte {cursor}, file length is {len(data)}")

    dims_text = ", ".join(f"{width}x{height}:{count}" for (width, height), count in sorted(dims.items()))
    summary = f"{path.name}: images={image_count} resolutions={resolution_count} dims={dims_text}"
    return summary, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "root",
        nargs="?",
        default="Reference/SimCopterOriginalGame/BMP/SIM3D.BMP",
        help="Composite bitmap file or directory containing .BMP files.",
    )
    args = parser.parse_args()

    root = Path(args.root)
    paths = [root] if root.is_file() else sorted(root.glob("*.[bB][mM][pP]"))

    composite_count = 0
    all_errors: list[str] = []
    for path in paths:
        summary, errors = probe_file(path)
        if summary:
            composite_count += 1
            print(summary)
        all_errors.extend(errors)

    print(f"composite_bitmaps {composite_count} errors {len(all_errors)}")
    for error in all_errors:
        print(error)

    return 1 if all_errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
