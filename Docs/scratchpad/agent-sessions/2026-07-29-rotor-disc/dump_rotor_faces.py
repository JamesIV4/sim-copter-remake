#!/usr/bin/env python3
"""Dump the face-type-11 (blur disc) faces of a GEO object, to see how many
circles the rotor blur is actually made of.

Layout mirrors Source/SimCopterRemake/Private/Formats/MaxisMeshReader.cpp.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path


def u16(d, o):
    return int.from_bytes(d[o:o + 2], "little")


def u32(d, o):
    return int.from_bytes(d[o:o + 4], "little")


def i32(d, o):
    return int.from_bytes(d[o:o + 4], "little", signed=True)


def name_at(d, o, n):
    return d[o:o + n].split(b"\0", 1)[0].decode("ascii", "replace")


def load(path: Path):
    d = path.read_bytes()
    geom = u32(d, 24)
    entry_count = u32(d, geom + 8)
    object_count = u32(d, geom + 12)
    entry_off = u32(d, geom + 16)
    dup_off = u32(d, geom + 20)

    names = {}
    for i in range(1, entry_count):
        o = entry_off + i * 53
        names[u32(d, o + 17)] = name_at(d, o, 17)

    objects = {}  # object id -> (name, offset)
    for i in range(object_count):
        o = dup_off + i * 36
        obj_id = i32(d, o)
        obj_off = u32(d, o + 4)
        objects[obj_id] = (names.get(obj_off, "?"), obj_off)
    return d, objects


def dump_object(d, obj_off, label):
    vcount = u16(d, obj_off + 8)
    fcount = u16(d, obj_off + 10)
    verts = []
    voff = obj_off + 124
    for i in range(vcount):
        o = voff + i * 12
        # Unreal conversion is (Z, X, Y) / units-per-cm.
        x, y, z = i32(d, o), i32(d, o + 4), i32(d, o + 8)
        verts.append((z, x, y))

    print(f"== {label}: {vcount} vertices, {fcount} faces")
    foff = voff + vcount * 12
    for fi in range(fcount):
        size = u32(d, foff + 4)
        fverts = u16(d, foff + 8)
        flags = u16(d, foff + 10)
        light = u16(d, foff + 12)
        ftype = d[foff + 18]
        material = d[foff + 19]
        idx = [u16(d, foff + 21 + k * 2) for k in range(fverts)]
        if ftype == 11:
            pts = [verts[i] for i in idx if i < len(verts)]
            zs = [p[2] for p in pts]
            radii = [math.hypot(p[0], p[1]) for p in pts]
            cx = sum(p[0] for p in pts) / max(1, len(pts))
            cy = sum(p[1] for p in pts) / max(1, len(pts))
            print(
                f"  face {fi}: type=11 verts={fverts} flags=0x{flags:04x} light={light} "
                f"mat={material} z=[{min(zs)}..{max(zs)}] "
                f"r=[{min(radii):.0f}..{max(radii):.0f}] centre=({cx:.0f},{cy:.0f})"
            )
        foff += size

    # Face-type histogram for context.
    hist = {}
    foff = voff + vcount * 12
    for fi in range(fcount):
        size = u32(d, foff + 4)
        hist[d[foff + 18]] = hist.get(d[foff + 18], 0) + 1
        foff += size
    print(f"  face types: {dict(sorted(hist.items()))}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="Reference/SimCopterOriginalGame/GEO")
    ap.add_argument("--ids", nargs="*", default=["0x117", "0x078", "0x11a", "0x126",
                                                 "0x127", "0x142", "0x154", "0x172",
                                                 "0x173", "0x083"])
    args = ap.parse_args()

    wanted = {int(v, 0) for v in args.ids}
    for path in sorted(Path(args.root).glob("*.[mM][aA][xX]")):
        try:
            d, objects = load(path)
        except Exception as exc:  # noqa: BLE001 - probe script
            print(f"{path.name}: {exc}")
            continue
        for obj_id in sorted(wanted & set(objects)):
            name, off = objects[obj_id]
            dump_object(d, off, f"{path.name} id=0x{obj_id:03x} {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
