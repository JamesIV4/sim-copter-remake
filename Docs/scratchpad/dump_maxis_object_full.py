"""Dump every face of a .MAX object with its actual vertex coordinates.

dump_maxis_object.py buckets faces by (type, material, vertexCount); this one prints them, so a
question like "which faces are the windmill's blades and which is the disc" can be answered by
looking at where the geometry actually is.

  python dump_maxis_object_full.py PP203
"""

import sys

from dump_maxis_object import GEO_DIR, load_objects

UNITS_PER_M = 262144.0


def main(names):
    catalog = {}
    for geo in sorted(GEO_DIR.iterdir()):
        if geo.suffix.lower() == ".max":
            catalog.update(load_objects(geo))

    for name in names:
        obj = catalog.get(name.upper())
        if obj is None:
            print(f"{name}: NOT FOUND")
            continue

        vs = obj["vertices"]
        print(f"\n=== {name}  (id 0x{obj['id']:x}, {obj['file']}, "
              f"{len(vs)} verts, {len(obj['faces'])} faces)   metres, Maxis axes (X, Y=up, Z)")
        for i, f in enumerate(obj["faces"]):
            pts = [vs[k] for k in f["indices"] if k < len(vs)]
            coords = " ".join(
                f"({p[0] / UNITS_PER_M:.2f},{p[1] / UNITS_PER_M:.2f},{p[2] / UNITS_PER_M:.2f})"
                for p in pts
            )
            print(f"  [{i:>3}] type={f['type']:<3} mat={f['material']:<4} atlas={f['atlas']:<3} "
                  f"n={len(f['indices'])}  {coords}")


if __name__ == "__main__":
    main(sys.argv[1:] or ["PP203"])
