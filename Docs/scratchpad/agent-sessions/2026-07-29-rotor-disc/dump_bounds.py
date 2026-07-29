#!/usr/bin/env python3
"""Print Unreal-space local bounds (cm) of GEO objects, to see how a prop is
authored relative to the helicopter body it mounts on."""

from __future__ import annotations

import argparse
from pathlib import Path

from dump_rotor_faces import i32, load, u16

# Matches ASimCopterHelicopterPawn: ModelUnitsPerCentimeter 2621.44, ModelScale 0.25.
UNITS_PER_CM = 2621.44
MODEL_SCALE = 0.25


def bounds(d, obj_off):
    vcount = u16(d, obj_off + 8)
    voff = obj_off + 124
    pts = []
    for i in range(vcount):
        o = voff + i * 12
        x, y, z = i32(d, o), i32(d, o + 4), i32(d, o + 8)
        # Maxis (X,Y,Z) -> Unreal (Z,X,Y), then /units *scale.
        pts.append((z / UNITS_PER_CM * MODEL_SCALE,
                    x / UNITS_PER_CM * MODEL_SCALE,
                    y / UNITS_PER_CM * MODEL_SCALE))
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    return lo, hi


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="Reference/SimCopterOriginalGame/GEO")
    ap.add_argument("--ids", nargs="*", required=True)
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
            lo, hi = bounds(d, off)
            print(
                f"0x{obj_id:03x} {name:10s} X[{lo[0]:8.1f}..{hi[0]:8.1f}] "
                f"Y[{lo[1]:8.1f}..{hi[1]:8.1f}] Z[{lo[2]:8.1f}..{hi[2]:8.1f}] cm"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
