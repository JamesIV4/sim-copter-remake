"""Find every .MAX object that carries animation data, and every object with a big n-gon disc.

The windmill renders as a flat cyan disc in the remake, so the question is which object owns it
and whether the original animates that object's geometry (OBJX +112 count / +116 pointer, the two
header fields MaxisMeshReader.cpp reads and nothing consumes).

  python find_animated_objects.py
"""

import struct
import sys
from pathlib import Path

from dump_maxis_object import GEO_DIR, ascii_at, i32, u16, u32


def scan(path):
    b = path.read_bytes()
    geom = u32(b, 24)
    entry_count = u32(b, geom + 8)
    entry_off = u32(b, geom + 16)

    rows = []
    for i in range(1, entry_count):
        o = entry_off + i * 53
        name = ascii_at(b, o, 17)
        obj_off = u32(b, o + 17)
        if b[obj_off:obj_off + 4] != b"OBJX":
            continue
        rows.append({
            "name": name.upper(),
            "file": path.name,
            "off": obj_off,
            "verts": u16(b, obj_off + 8),
            "faces": u16(b, obj_off + 10),
            "attr": u32(b, obj_off + 12),
            "anim_count": i32(b, obj_off + 112),
            "anim_ptr": i32(b, obj_off + 116),
            "id": i32(b, obj_off + 120),
        })
    return rows


def main():
    rows = []
    for geo in sorted(GEO_DIR.iterdir()):
        if geo.suffix.lower() == ".max":
            rows += scan(geo)

    print(f"{len(rows)} objects total\n")
    print("--- objects with a non-zero animation count ---")
    print(f"{'id':>8}  {'name':<10} {'pack':<12} {'animCnt':>8} {'animPtr':>10} {'attr':>10}")
    hits = [r for r in rows if r["anim_count"]]
    for r in sorted(hits, key=lambda r: r["id"]):
        print(f"  0x{r['id']:04x}  {r['name']:<10} {r['file']:<12} "
              f"{r['anim_count']:>8} 0x{r['anim_ptr']:08x} 0x{r['attr']:08x}")
    print(f"{len(hits)} animated object(s)")

    print("\n--- distinct attribute-flag words ---")
    attrs = {}
    for r in rows:
        attrs.setdefault(r["attr"], []).append(r["name"])
    for a, names in sorted(attrs.items()):
        print(f"  0x{a:08x}  {len(names):>4}  e.g. {', '.join(names[:8])}")


if __name__ == "__main__":
    main()
