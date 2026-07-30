"""Scratch: inventory FACE records in the SimCopter GEO packs.

Mirrors MaxisMeshReader.cpp's parse so the field offsets stay in one place mentally:
OBJX header is 124 bytes, vertices are 3x int32, then FACE records:
  +0  'FACE'   +4  size   +8  vertexCount   +10 flags   +12 lightType
  +14 faceInfo +18 faceType +19 materialIndex +20 textureAtlasIndex
"""
import struct
import sys
import os
from collections import defaultdict

ROOT = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO"
PACKS = ["sim3d1.max", "SIM3D2.MAX", "SIM3D3.MAX"]


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def i32(d, o):
    return struct.unpack_from("<i", d, o)[0]


def cstr(d, o, n):
    raw = d[o:o + n]
    z = raw.find(b"\0")
    return raw[:z if z >= 0 else n].decode("ascii", "replace")


def parse(path):
    d = open(path, "rb").read()
    assert d[28:32] == b"CMAP"
    geom_off = u32(d, 24)  # header field, same as MaxisMeshFile::GeometryTableOffset
    assert d[geom_off:geom_off + 4] == b"GEOM", path
    entry_count = u32(d, geom_off + 8)
    obj_count = u32(d, geom_off + 12)
    entry_off = u32(d, geom_off + 16)

    entries = []
    for i in range(entry_count):
        o = entry_off + i * 53
        entries.append((cstr(d, o, 17), u32(d, o + 17)))

    offsets = sorted({e[1] for e in entries[1:]})

    objects = []
    for name, off in entries[1:]:
        assert d[off:off + 4] == b"OBJX", (path, name, off)
        vcount = u16(d, off + 8)
        fcount = u16(d, off + 10)
        attrs = u32(d, off + 12)
        obj_id = i32(d, off + 120)
        nxt = next((x for x in offsets if x > off), len(d))
        cursor = off + 124 + vcount * 12
        faces = []
        for _ in range(fcount):
            if d[cursor:cursor + 4] != b"FACE":
                break
            size = u32(d, cursor + 4)
            faces.append({
                "verts": u16(d, cursor + 8),
                "flags": u16(d, cursor + 10),
                "light": u16(d, cursor + 12),
                "info": i32(d, cursor + 14),
                "type": d[cursor + 18],
                "mat": d[cursor + 19],
                "tex": d[cursor + 20],
                "idx": list(struct.unpack_from("<%dH" % u16(d, cursor + 8), d, cursor + 21)),
                "off": cursor,
            })
            cursor += size
            if cursor >= nxt:
                break
        verts = [struct.unpack_from("<3i", d, off + 124 + k * 12) for k in range(vcount)]
        objects.append({"name": name, "id": obj_id, "attrs": attrs,
                        "faces": faces, "verts": verts, "pack": os.path.basename(path)})
    return objects


def main():
    allobjs = []
    for p in PACKS:
        allobjs += parse(os.path.join(ROOT, p))

    mode = sys.argv[1] if len(sys.argv) > 1 else "summary"

    if mode == "summary":
        combo = defaultdict(int)
        for o in allobjs:
            for f in o["faces"]:
                combo[(f["type"], f["light"], f["verts"])] += 1
        print("faceType lightType verts  count")
        for k in sorted(combo):
            print("%8d %9d %5d  %d" % (k[0], k[1], k[2], combo[k]))

    elif mode == "lights":
        # Every object carrying a non-zero lightType face.
        for o in allobjs:
            lit = [f for f in o["faces"] if f["light"] != 0]
            if lit:
                by = defaultdict(int)
                for f in lit:
                    by[(f["type"], f["light"], f["mat"])] += 1
                print("%-10s id=0x%-4x %-10s %s" % (
                    o["name"], o["id"], o["pack"],
                    " ".join("t%d/l%d/m%d x%d" % (k[0], k[1], k[2], v) for k, v in sorted(by.items()))))

    elif mode == "obj":
        want = sys.argv[2].upper()
        for o in allobjs:
            if o["name"].upper() == want:
                print("%s id=0x%x attrs=0x%x verts=%d faces=%d pack=%s" % (
                    o["name"], o["id"], o["attrs"], len(o["verts"]), len(o["faces"]), o["pack"]))
                for i, f in enumerate(o["faces"]):
                    pos = [o["verts"][k] for k in f["idx"] if k < len(o["verts"])]
                    print("  face %3d type=%3d light=%3d flags=0x%04x info=%d mat=%3d tex=%3d verts=%d %s" % (
                        i, f["type"], f["light"], f["flags"], f["info"], f["mat"], f["tex"], f["verts"],
                        pos if f["verts"] <= 2 else ""))


if __name__ == "__main__":
    main()
