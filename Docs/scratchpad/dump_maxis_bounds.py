"""Local-space bounding box of named .MAX objects, in ORIGINAL UNITS (16.16 / 65536).

The question this answers: does a road decoration carry its own in-tile offset, or does the city
builder place it? A tile is 64 units, so an object centred on 0 sits at the tile origin and one
whose X runs 20..28 is parked against a curb.

  python dump_maxis_bounds.py LAMP35 LAMP36 LAMP37 LAMP38
"""

import sys

from dump_maxis_object import GEO_DIR, load_objects

UNIT = 65536.0


def main(names):
    catalog = {}
    for geo in sorted(GEO_DIR.iterdir()):
        if geo.suffix.lower() == ".max":
            catalog.update(load_objects(geo))

    print(f"{'name':<10} {'id':>7}  "
          f"{'Xlo':>8} {'Xhi':>8} | {'Ylo':>8} {'Yhi':>8} | {'Zlo':>8} {'Zhi':>8}   (original units)")
    for name in names:
        obj = catalog.get(name.upper())
        if obj is None:
            print(f"{name:<10}  NOT FOUND")
            continue
        vs = obj["vertices"]
        xs, ys, zs = [v[0] for v in vs], [v[1] for v in vs], [v[2] for v in vs]
        print(f"{name:<10} 0x{obj['id']:04x}  "
              f"{min(xs) / UNIT:8.2f} {max(xs) / UNIT:8.2f} | "
              f"{min(ys) / UNIT:8.2f} {max(ys) / UNIT:8.2f} | "
              f"{min(zs) / UNIT:8.2f} {max(zs) / UNIT:8.2f}")


if __name__ == "__main__":
    main(sys.argv[1:] or ["LAMP35"])
