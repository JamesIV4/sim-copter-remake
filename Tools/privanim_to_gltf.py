#!/usr/bin/env python3
"""Export the decoded ``privanim.df`` figures to a glTF for visual inspection.

This is a *diagnostic* exporter: it renders the raw ARCP 4-byte coordinate
records (`[s8 x][s8 y][u8 z][u8 t]`) of each of the 21 figures as 3D geometry so
we can eyeball whether the decode produces recognizable human figures, and
whether the data reads as connected lines or a point cloud. Each figure is drawn
twice (POINTS + LINE_STRIP) sharing one position buffer, vertex-coloured as a
blue->red gradient by record order so the traversal is visible.

Axis map: figure (x, y, z) -> glTF (x, z, y) so the byte ``z`` (0..~37, the
height) points up (+Y). Figures are laid out in a grid.

Usage: python Tools/privanim_to_gltf.py [privanim.df] --out figures.gltf
Output (derived geometry) defaults to the session scratchpad - it is NOT meant
to be committed (original-asset-derived data).
"""
from __future__ import annotations
import argparse, base64, json, struct
from pathlib import Path
from privanim_extract import Privanim, DEFAULT  # same directory


def line_pairs(recs):
    """Records that share a t value (= same k index) are the two endpoints of a
    line segment (a limb/edge); a lone record is a point. Returns index pairs."""
    pairs = []
    i, n = 0, len(recs)
    while i < n:
        if i + 1 < n and recs[i][3] == recs[i + 1][3]:
            pairs.append((i, i + 1)); i += 2
        else:
            i += 1
    return pairs


def split_parts(recs):
    """Each ARCP stream packs two parts distinguished by the z high-bit:
    bit7=0 -> z is a signed offset (0..~47); bit7=1 -> a second part in a higher
    z band (mask the flag bit). Returns a list of (suffix, records)."""
    s8 = lambda v: v - 256 if v > 127 else v
    g0 = [(s8(r[0]), s8(r[1]), s8(r[2]), r[3]) for r in recs if r[2] < 128]
    g1 = [(s8(r[0]), s8(r[1]), r[2] & 0x7f, r[3]) for r in recs if r[2] >= 128]
    out = []
    if g0:
        out.append(("a", g0))
    if g1:
        out.append(("b", g1))
    return out


def build_gltf(figures, cols=8, spacing=40.0):
    accessors, bufviews, meshes, nodes = [], [], [], []
    floats = bytearray()
    idxblob = bytearray()
    plan = []  # (label, pos_off,col_off,n,minv,maxv, idx_off,nidx)

    for fig in figures:
        for suffix, recs in split_parts(fig["records"]):
            if not recs:
                continue
            n = len(recs)
            # normalize each part to its own origin so it sits cleanly at 0
            mnx = min(r[0] for r in recs); mnz = min(r[2] for r in recs)
            cy = (min(r[1] for r in recs) + max(r[1] for r in recs)) / 2.0
            pos_off = len(floats)
            gxs, gys, gzs = [], [], []
            for r in recs:
                gx = float(r[0] - mnx); gy = float(r[2] - mnz); gz = float(r[1] - cy)
                floats.extend(struct.pack("<fff", gx, gy, gz))   # byte z -> +Y up
                gxs.append(gx); gys.append(gy); gzs.append(gz)
            col_off = len(floats)
            for i in range(n):
                t = i / max(1, n - 1)
                floats.extend(struct.pack("<ffff", t, 0.25, 1.0 - t, 1.0))
            idx_off = len(idxblob)
            pairs = line_pairs(recs)
            for a, b in pairs:
                idxblob.extend(struct.pack("<II", a, b))
            plan.append((f"fig{fig['node']}{suffix}", pos_off, col_off, n,
                         [min(gxs), min(gys), min(gzs)], [max(gxs), max(gys), max(gzs)],
                         idx_off, len(pairs) * 2))

    # buffer = all floats, then (4-byte aligned) all uint32 indices
    while len(floats) % 4:
        floats.append(0)
    idx_base = len(floats)
    blob = bytes(floats) + bytes(idxblob)

    for pi, p in enumerate(plan):
        label, pos_off, col_off, n, minv, maxv, idx_off, nidx = p
        bufviews.append({"buffer": 0, "byteOffset": pos_off, "byteLength": n * 12, "target": 34962})
        pos_bv = len(bufviews) - 1
        bufviews.append({"buffer": 0, "byteOffset": col_off, "byteLength": n * 16, "target": 34962})
        col_bv = len(bufviews) - 1
        pos_acc = len(accessors)
        accessors.append({"bufferView": pos_bv, "componentType": 5126, "count": n,
                          "type": "VEC3", "min": minv, "max": maxv})
        col_acc = len(accessors)
        accessors.append({"bufferView": col_bv, "componentType": 5126, "count": n, "type": "VEC4"})
        attrs = {"POSITION": pos_acc, "COLOR_0": col_acc}
        prims = [{"attributes": attrs, "mode": 0}]   # POINTS
        if nidx:
            bufviews.append({"buffer": 0, "byteOffset": idx_base + idx_off,
                             "byteLength": nidx * 4, "target": 34963})
            idx_bv = len(bufviews) - 1
            idx_acc = len(accessors)
            accessors.append({"bufferView": idx_bv, "componentType": 5125,
                              "count": nidx, "type": "SCALAR"})
            prims.insert(0, {"attributes": attrs, "indices": idx_acc, "mode": 1})  # LINES
        meshes.append({"name": label, "primitives": prims})
        gx = (pi % cols) * spacing
        gz = (pi // cols) * spacing
        nodes.append({"mesh": len(meshes) - 1, "translation": [gx, 0.0, gz], "name": label})

    uri = "data:application/octet-stream;base64," + base64.b64encode(blob).decode()
    return {
        "asset": {"version": "2.0", "generator": "privanim_to_gltf"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "accessors": accessors,
        "bufferViews": bufviews,
        "buffers": [{"byteLength": len(blob), "uri": uri}],
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", nargs="?", default=DEFAULT)
    ap.add_argument("--out", default="figures.gltf")
    a = ap.parse_args()
    p = Path(a.path)
    if not p.exists():
        print(f"{p}: not found (original game data is not committed); skipping.")
        return 0
    pa = Privanim(p.read_bytes())
    pa.parse_sections()
    sorted_offs = pa.all_offsets_sorted()
    figures = []
    for e in pa.node_entries("ARCP"):
        if not (0 < e["off"] < pa.N):
            continue
        end = pa.payload_end(e["off"], sorted_offs)
        figures.append({"node": e["i"], "off": e["off"],
                        "records": pa.read_coord_stream(e["off"], end)})
    gltf = build_gltf(figures)
    Path(a.out).write_text(json.dumps(gltf))
    nverts = sum(len(f["records"]) for f in figures)
    print(f"wrote {a.out}: {len(figures)} figures, {nverts} total vertices")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
