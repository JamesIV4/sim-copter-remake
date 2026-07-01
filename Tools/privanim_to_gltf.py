#!/usr/bin/env python3
"""Export decoded ``privanim.df`` figures to glTF for visual inspection.

Uses the exact decode from ``privanim_extract.py`` (2026-07-01 rewrite):
an ARPP pose record is one body-part LINE SEGMENT per frame
(two s8 (x,y,z) endpoints, z up). The exporter writes:

  * one node per figure: frame 0 of its walk clip ('1Wal'), laid out in a grid;
  * one node per frame of a chosen figure's clip (default pilot '1Wal'),
    laid out in a row below the grid - scrub visually to see the walk cycle.

Axis map: figure (x, y, z) -> glTF (y, z, x) so z(up) -> +Y.
Output (derived geometry) defaults to the session scratchpad / CWD - it is NOT
meant to be committed (original-asset-derived data).

Usage: python Tools/privanim_to_gltf.py [privanim.df] [--out figures.gltf]
       [--clipfig pilot] [--clip 1Wal]
"""
from __future__ import annotations
import argparse, base64, json, struct
from pathlib import Path
from privanim_extract import Privanim, DEFAULT


def seg_mesh_bytes(segs):
    """Positions (2 verts per segment); returns (blob, count, min, max)."""
    floats = bytearray()
    mn = [1e9] * 3; mx = [-1e9] * 3
    for a, b in segs:
        for p in (a, b):
            g = (float(p[1]), float(p[2]), float(p[0]))   # z-up -> +Y
            floats.extend(struct.pack("<fff", *g))
            for i in range(3):
                mn[i] = min(mn[i], g[i]); mx[i] = max(mx[i], g[i])
    return bytes(floats), len(segs) * 2, mn, mx


def build_gltf(items, cols=7, spacing=60.0):
    """items: list of (label, segments, row_hint or None)."""
    accessors, bufviews, meshes, nodes = [], [], [], []
    blob = bytearray()
    grid_i = 0
    for label, segs, rowpos in items:
        data, n, mn, mx = seg_mesh_bytes(segs)
        off = len(blob); blob.extend(data)
        bufviews.append({"buffer": 0, "byteOffset": off, "byteLength": len(data),
                         "target": 34962})
        accessors.append({"bufferView": len(bufviews) - 1, "componentType": 5126,
                          "count": n, "type": "VEC3", "min": mn, "max": mx})
        meshes.append({"name": label, "primitives": [
            {"attributes": {"POSITION": len(accessors) - 1}, "mode": 1}]})  # LINES
        if rowpos is None:
            tx = (grid_i % cols) * spacing
            tz = (grid_i // cols) * spacing
            grid_i += 1
        else:
            tx = rowpos * spacing
            tz = ((len(items) // cols) + 3) * spacing
        nodes.append({"mesh": len(meshes) - 1, "name": label,
                      "translation": [tx, 0.0, tz]})
    uri = "data:application/octet-stream;base64," + base64.b64encode(bytes(blob)).decode()
    return {"asset": {"version": "2.0", "generator": "privanim_to_gltf"},
            "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}],
            "nodes": nodes, "meshes": meshes, "accessors": accessors,
            "bufferViews": bufviews,
            "buffers": [{"byteLength": len(blob), "uri": uri}]}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", nargs="?", default=DEFAULT)
    ap.add_argument("--out", default="figures.gltf")
    ap.add_argument("--clipfig", default="pilot")
    ap.add_argument("--clip", default="1Wal")
    a = ap.parse_args()
    p = Path(a.path)
    if not p.exists():
        print(f"{p}: not found (original game data is not committed); skipping.")
        return 0
    pa = Privanim(p.read_bytes())

    items = []
    for fe in pa.figures():
        clip = pa.clip_map(fe["name"]).get("1Wal")
        if not clip:
            continue
        frames = pa.clip_frames(clip)
        items.append((fe["name"], frames[0], None))

    clip_name = pa.clip_map(a.clipfig).get(a.clip)
    if clip_name:
        for i, segs in enumerate(pa.clip_frames(clip_name)):
            items.append((f"{a.clipfig}_{a.clip}_f{i}", segs, i))

    gltf = build_gltf(items)
    Path(a.out).write_text(json.dumps(gltf))
    print(f"wrote {a.out}: {len(items)} meshes "
          f"({len(pa.figures())} figures + {a.clipfig}/{a.clip} frames)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
