"""Scan the original SIM3D*.MAX geometry for Maxis face type 11 - the "light card" faces the
original shows only at night (FUN_004a03a0 / FUN_004834f0 clear their 0x80000000 hide bit when
DAT_004f9720 == 1, and FUN_00483700 sets it back for day).

Layout mirrors SimCopterRemake/Source/.../Formats/MaxisMeshReader.cpp.

    python Docs/scratchpad/scan_face_type_11.py [--all-types]
"""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GEO_DIR = ROOT / "Reference" / "SimCopterOriginalGame" / "GEO"


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def i32(b, o):
    return struct.unpack_from("<i", b, o)[0]


def ascii_at(b, o, n):
    raw = b[o:o + n]
    end = raw.find(b"\0")
    return raw[:end if end >= 0 else n].decode("latin-1")


def load(path):
    data = path.read_bytes()
    assert data[0:4] == b"DIRC", path
    geom = u32(data, 24)
    assert data[geom:geom + 4] == b"GEOM"
    entry_count = u32(data, geom + 8)
    entry_off = u32(data, geom + 16)

    entries = []
    for i in range(entry_count):
        o = entry_off + i * 53
        entries.append((ascii_at(data, o, 17), u32(data, o + 17)))

    offsets = sorted({e[1] for e in entries[1:]})

    objects = []
    for name, obj_off in entries[1:]:
        assert data[obj_off:obj_off + 4] == b"OBJX", (path, name)
        vert_count = u16(data, obj_off + 8)
        face_count = u16(data, obj_off + 10)
        obj_id = i32(data, obj_off + 120)
        obj_name = ascii_at(data, obj_off + 24, 88)

        cursor = obj_off + 124 + vert_count * 12
        faces = []
        for _ in range(face_count):
            assert data[cursor:cursor + 4] == b"FACE", (path, name)
            size = u32(data, cursor + 4)
            faces.append({
                "verts": u16(data, cursor + 8),
                "flags": u16(data, cursor + 10),
                "light": u16(data, cursor + 12),
                "info": i32(data, cursor + 14),
                "type": data[cursor + 18],
                "material": data[cursor + 19],
                "atlas": data[cursor + 20],
            })
            cursor += size

        objects.append({
            "table": name, "name": obj_name, "id": obj_id,
            "verts": vert_count, "faces": faces,
        })
    return objects


def main():
    show_all = "--all-types" in sys.argv
    grand = {}
    for path in sorted(GEO_DIR.iterdir()):
        if path.suffix.lower() != ".max":
            continue
        objects = load(path)
        hits = []
        for obj in objects:
            n11 = sum(1 for f in obj["faces"] if f["type"] == 11)
            for f in obj["faces"]:
                grand[f["type"]] = grand.get(f["type"], 0) + 1
            if n11:
                mats = sorted({f["material"] for f in obj["faces"] if f["type"] == 11})
                verts = sorted({f["verts"] for f in obj["faces"] if f["type"] == 11})
                lights = sorted({f["light"] for f in obj["faces"] if f["type"] == 11})
                flags = sorted({f["flags"] for f in obj["faces"] if f["type"] == 11})
                hits.append((obj, n11, mats, verts, lights, flags))

        print(f"=== {path.name}: {len(objects)} objects, {len(hits)} with type-11 faces ===")
        for obj, n11, mats, verts, lights, flags in hits:
            print(f"  id={obj['id']:<5} 0x{obj['id']:04x}  {obj['table']:<18}"
                  f" type11={n11:<4}/{len(obj['faces']):<4}"
                  f" mat={mats} nverts={verts} light={lights} flags={flags}"
                  f"  \"{obj['name'][:40]}\"")
        print()

    if show_all:
        print("=== face type histogram (all files) ===")
        for t in sorted(grand):
            print(f"  type {t:>3}: {grand[t]}")


if __name__ == "__main__":
    main()
