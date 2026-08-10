"""Every Maxis face-type-26 (0x1a) EFFECT marker in the three .MAX packs, by object.

Type 25 is the blink marker whose material byte is a VGA palette index (see
Docs/memory/simcopter-flashing-lights.md). Type 26 is its neighbour: a 1-vertex anchor whose
material byte is an EFFECT CLASS, and it is what a smoking chimney is made of. This prints the
class, the count and the marker's own Maxis Y so a stack top can be told from a ground vent.

  python scan_face_type_26.py
"""

from dump_maxis_object import GEO_DIR, load_objects

MARKER_TYPE = 26


def main():
    catalog = {}
    for geo in sorted(GEO_DIR.iterdir()):
        if geo.suffix.lower() == ".max":
            catalog.update(load_objects(geo))

    rows = []
    for name, obj in catalog.items():
        verts = obj["vertices"]
        markers = [f for f in obj["faces"] if f["type"] == MARKER_TYPE]
        if not markers:
            continue
        classes = {}
        for face in markers:
            ys = [verts[i][1] for i in face["indices"] if i < len(verts)]
            entry = classes.setdefault(face["material"], {"n": 0, "lo": 1 << 30, "hi": -(1 << 30),
                                                          "nverts": len(face["indices"])})
            entry["n"] += 1
            if ys:
                entry["lo"] = min(entry["lo"], min(ys))
                entry["hi"] = max(entry["hi"], max(ys))
        objectYs = [v[1] for v in verts] or [0]
        rows.append((obj["id"], name, obj["file"], max(objectYs), classes))

    print(f"{'id':>8}  {'name':<10} {'pack':<12} {'objYhi':>9}  effect classes (count @ markerY)")
    for obj_id, name, pack, obj_top, classes in sorted(rows):
        parts = []
        for cls, e in sorted(classes.items()):
            frac = f"{e['hi'] / obj_top:.2f}" if obj_top else "-"
            parts.append(f"class {cls}: {e['n']}x @ {e['lo']}..{e['hi']} ({frac} of top, {e['nverts']}v)")
        print(f"  0x{obj_id:04x}  {name:<10} {pack:<12} {obj_top:>9}  " + "; ".join(parts))
    print(f"\n{len(rows)} object(s) carry a type-{MARKER_TYPE} marker")


if __name__ == "__main__":
    main()
