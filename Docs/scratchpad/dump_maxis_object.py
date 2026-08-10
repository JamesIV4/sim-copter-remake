"""Dump the faces of a named object out of a SimCopter .MAX (DIRC/CMAP/GEOM) mesh container.

Standalone on purpose: it reads the same layout MaxisMeshReader.cpp parses, so RD67/RD31-style
questions ("which faces are the asphalt, which are the pole") can be answered without booting the
editor or fighting it for the MCP port.

  python dump_maxis_object.py <RD67> [<RD68> ...]
"""

import struct
import sys
from pathlib import Path

GEO_DIR = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO")


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def i32(b, o):
    return struct.unpack_from("<i", b, o)[0]


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def ascii_at(b, o, n):
    raw = b[o:o + n]
    end = raw.find(b"\0")
    return (raw if end < 0 else raw[:end]).decode("latin-1")


def load_objects(path):
    b = path.read_bytes()
    assert b[0:4] == b"DIRC", path
    geom = u32(b, 24)
    assert b[geom:geom + 4] == b"GEOM"
    entry_count = u32(b, geom + 8)
    entry_off = u32(b, geom + 16)

    entries = []
    offsets = set()
    for i in range(entry_count):
        o = entry_off + i * 53
        entries.append((ascii_at(b, o, 17), u32(b, o + 17)))
        if i:
            offsets.add(u32(b, o + 17))
    ordered = sorted(offsets)

    def boundary(obj_off):
        for o in ordered:
            if o > obj_off:
                return o
        return len(b)

    objects = {}
    for name, obj_off in entries[1:]:
        assert b[obj_off:obj_off + 4] == b"OBJX", name
        vertex_count = u16(b, obj_off + 8)
        face_count = u16(b, obj_off + 10)
        obj_id = i32(b, obj_off + 120)
        limit = boundary(obj_off)

        vo = obj_off + 124
        vertices = [
            (i32(b, vo + v * 12), i32(b, vo + v * 12 + 4), i32(b, vo + v * 12 + 8))
            for v in range(vertex_count)
        ]

        faces = []
        fo = vo + vertex_count * 12
        for _ in range(face_count):
            assert b[fo:fo + 4] == b"FACE", (name, fo)
            size = u32(b, fo + 4)
            n = u16(b, fo + 8)
            face = {
                "type": b[fo + 18],
                "material": b[fo + 19],
                "atlas": b[fo + 20],
                "indices": [u16(b, fo + 21 + k * 2) for k in range(n)],
            }
            faces.append(face)
            fo += size
            if fo > limit:
                break

        objects[name.upper()] = {
            "id": obj_id,
            "file": path.name,
            "vertices": vertices,
            "faces": faces,
        }
    return objects


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
        ys = [v[1] for v in vs]
        print(f"\n=== {name}  (object id 0x{obj['id']:x}, {obj['file']}, "
              f"{len(vs)} verts, {len(obj['faces'])} faces)")
        print(f"    Maxis Y (up) range: {min(ys)} .. {max(ys)}")

        buckets = {}
        for f in obj["faces"]:
            key = (f["type"], f["material"], len(f["indices"]))
            fys = [vs[i][1] for i in f["indices"] if i < len(vs)]
            b = buckets.setdefault(key, {"n": 0, "lo": 1 << 30, "hi": -(1 << 30)})
            b["n"] += 1
            if fys:
                b["lo"] = min(b["lo"], min(fys))
                b["hi"] = max(b["hi"], max(fys))

        print("    faceType material verts count   Y range")
        for (t, m, n), b in sorted(buckets.items()):
            print(f"      {t:>8} {m:>8} {n:>5} {b['n']:>5}   {b['lo']:>7} .. {b['hi']:>7}")


if __name__ == "__main__":
    main(sys.argv[1:] or ["RD67"])
