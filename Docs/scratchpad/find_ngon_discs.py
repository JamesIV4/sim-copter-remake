"""Find every .MAX face with a lot of vertices - i.e. every polygon that renders as a disc.

The windmill draws as one big flat cyan n-gon in the remake; this finds which object owns an
n-gon that large, and what face type / material byte it carries.

  python find_ngon_discs.py [min_vertex_count]     # default 8
"""

import sys

from dump_maxis_object import GEO_DIR, load_objects


def main(argv):
    threshold = int(argv[0]) if argv else 8

    catalog = {}
    for geo in sorted(GEO_DIR.iterdir()):
        if geo.suffix.lower() == ".max":
            catalog.update(load_objects(geo))

    print(f"{'name':<10} {'id':>7} {'pack':<12} {'type':>5} {'mat':>4} {'atlas':>6} {'n':>4}   extent (X,Y,Z)")
    total = 0
    for name, obj in sorted(catalog.items(), key=lambda kv: kv[1]["id"]):
        vs = obj["vertices"]
        for f in obj["faces"]:
            n = len(f["indices"])
            if n < threshold:
                continue
            pts = [vs[i] for i in f["indices"] if i < len(vs)]
            if not pts:
                continue
            ext = tuple(max(p[a] for p in pts) - min(p[a] for p in pts) for a in range(3))
            print(f"{name:<10} 0x{obj['id']:04x} {obj['file']:<12} {f['type']:>5} "
                  f"{f['material']:>4} {f['atlas']:>6} {n:>4}   {ext}")
            total += 1
    print(f"\n{total} face(s) with >= {threshold} vertices")


if __name__ == "__main__":
    main(sys.argv[1:])
