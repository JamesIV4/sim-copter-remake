#!/usr/bin/env python3
"""Read-only probe for SimCopter/Streets Maxis .MAX mesh packs."""

from __future__ import annotations

import argparse
from pathlib import Path


def u16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little")


def u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")


def fixed_name(data: bytes, offset: int, length: int) -> str:
    return data[offset : offset + length].split(b"\0", 1)[0].decode("ascii", "replace")


def probe_file(path: Path) -> tuple[str, list[str]]:
    data = path.read_bytes()
    errors: list[str] = []

    if len(data) < 853 or data[:4] != b"DIRC" or data[12:16] != b"CMAP" or data[20:24] != b"GEOM":
        return "", [f"{path}: missing DIRC/CMAP/GEOM header"]

    if u32(data, 4) != len(data):
        errors.append(f"{path}: declared file size {u32(data, 4)} does not match actual {len(data)}")

    geom_offset = u32(data, 24)
    if data[geom_offset : geom_offset + 4] != b"GEOM":
        errors.append(f"{path}: geometry table offset {geom_offset} does not point to GEOM")
        return "", errors

    entry_count = u32(data, geom_offset + 8)
    object_count = u32(data, geom_offset + 12)
    entry_offset = u32(data, geom_offset + 16)

    names: list[str] = []
    offsets: list[int] = []
    face_total = 0
    vertex_total = 0

    for index in range(1, entry_count):
        offset = entry_offset + index * 53
        mesh_name = fixed_name(data, offset, 17)
        object_offset = u32(data, offset + 17)
        names.append(mesh_name)
        offsets.append(object_offset)

    sorted_offsets = sorted(set(offsets))
    for index, object_offset in enumerate(offsets):
        if data[object_offset : object_offset + 4] != b"OBJX":
            errors.append(f"{path}: object {names[index]} at {object_offset} missing OBJX")
            continue

        next_offset = next((candidate for candidate in sorted_offsets if candidate > object_offset), len(data))
        vertex_count = u16(data, object_offset + 8)
        face_count = u16(data, object_offset + 10)
        vertex_total += vertex_count
        face_total += face_count

        face_offset = object_offset + 124 + vertex_count * 12
        for face_index in range(face_count):
            if data[face_offset : face_offset + 4] != b"FACE":
                errors.append(f"{path}: {names[index]} face {face_index} at {face_offset} missing FACE")
                break
            face_size = u32(data, face_offset + 4)
            face_vertices = u16(data, face_offset + 8)
            minimum = 21 + face_vertices * 10
            if face_size < minimum or face_offset + face_size > next_offset:
                errors.append(f"{path}: {names[index]} face {face_index} has invalid size {face_size}")
                break
            face_offset += face_size

    summary = (
        f"{path.name}: geometry_entries={entry_count} objects={object_count} "
        f"vertices={vertex_total} faces={face_total} first={names[:5]}"
    )
    return summary, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "root",
        nargs="?",
        default="Reference/SimCopterOriginalGame/GEO",
        help="Mesh file or directory containing .MAX files.",
    )
    args = parser.parse_args()

    root = Path(args.root)
    paths = [root] if root.is_file() else sorted(root.glob("*.[mM][aA][xX]"))

    all_errors: list[str] = []
    for path in paths:
        summary, errors = probe_file(path)
        if summary:
            print(summary)
        all_errors.extend(errors)

    print(f"mesh_files {len(paths)} errors {len(all_errors)}")
    for error in all_errors:
        print(error)

    return 1 if all_errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
