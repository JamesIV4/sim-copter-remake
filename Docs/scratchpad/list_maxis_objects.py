"""List every object in the three SimCopter .MAX packs by object id.

Companion to dump_maxis_object.py: that one answers "what is inside RD67", this one answers
"what IS object 0x185". Ordered by id so a range out of FUN_0047c0c0's decoration switch
(0x181..0x18d, say) can be read straight off.

  python list_maxis_objects.py                # everything
  python list_maxis_objects.py 0x181 0x18d    # an inclusive id range
  python list_maxis_objects.py --name LP       # every object whose name starts with LP
"""

import sys

from dump_maxis_object import GEO_DIR, load_objects


def main(argv):
    catalog = {}
    for geo in sorted(GEO_DIR.iterdir()):
        if geo.suffix.lower() == ".max":
            catalog.update(load_objects(geo))

    prefix = None
    lo, hi = -(1 << 30), 1 << 30
    if argv and argv[0] == "--name":
        prefix = argv[1].upper()
    elif len(argv) == 2:
        lo, hi = int(argv[0], 0), int(argv[1], 0)
    elif len(argv) == 1:
        lo = hi = int(argv[0], 0)

    rows = []
    for name, obj in catalog.items():
        if prefix is not None:
            if not name.startswith(prefix):
                continue
        elif not (lo <= obj["id"] <= hi):
            continue
        types = sorted({f["type"] for f in obj["faces"]})
        ys = [v[1] for v in obj["vertices"]] or [0]
        rows.append((obj["id"], name, obj["file"], len(obj["vertices"]),
                     len(obj["faces"]), min(ys), max(ys), types))

    print(f"{'id':>8}  {'name':<10} {'pack':<12} {'v':>5} {'f':>5} {'Ylo':>8} {'Yhi':>8}  faceTypes")
    for obj_id, name, pack, nv, nf, ylo, yhi, types in sorted(rows):
        print(f"  0x{obj_id:04x}  {name:<10} {pack:<12} {nv:>5} {nf:>5} {ylo:>8} {yhi:>8}  {types}")
    print(f"\n{len(rows)} object(s)")


if __name__ == "__main__":
    main(sys.argv[1:])
