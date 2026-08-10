"""Bounding box of every .MAX object, in metres, sorted so tall-thin things surface first.

Used to identify an object seen on screen but not findable by name - a windmill is a small
footprint with a lot of height.

  python list_object_bounds.py             # sorted by height/footprint ratio
  python list_object_bounds.py --id        # sorted by object id
"""

import sys

from dump_maxis_object import GEO_DIR, load_objects

UNITS_PER_M = 262144.0


def main(argv):
    catalog = {}
    for geo in sorted(GEO_DIR.iterdir()):
        if geo.suffix.lower() == ".max":
            catalog.update(load_objects(geo))

    rows = []
    for name, obj in catalog.items():
        vs = obj["vertices"]
        if not vs:
            continue
        ext = [(max(v[a] for v in vs) - min(v[a] for v in vs)) / UNITS_PER_M for a in range(3)]
        foot = max(ext[0], ext[2], 0.01)
        types = sorted({f["type"] for f in obj["faces"]})
        rows.append((ext[1] / foot, obj["id"], name, obj["file"], ext, types))

    rows.sort(key=lambda r: -r[0] if "--id" not in argv else r[1])
    print(f"{'ratio':>6} {'id':>7} {'name':<10} {'pack':<12} "
          f"{'sizeX':>7} {'sizeY':>7} {'sizeZ':>7}  faceTypes")
    for ratio, obj_id, name, pack, ext, types in rows:
        print(f"{ratio:>6.2f} 0x{obj_id:04x} {name:<10} {pack:<12} "
              f"{ext[0]:>7.2f} {ext[1]:>7.2f} {ext[2]:>7.2f}  {types}")


if __name__ == "__main__":
    main(sys.argv[1:])
