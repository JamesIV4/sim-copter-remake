"""Orthographic wireframe of a Maxis GEO object, side (X/Y) and front (Z/Y) views.

Answers "which way up is this modelled?" without booting the editor.
Usage: render_maxis_object.py CAPBOAT1 [BOAT1 ...]
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dump_maxis_object import GEO_DIR, load_objects

OUT = Path(r"S:\Repos\sim-copter-remake\Docs\scratchpad")
SIZE = 420


def view(obj, name, ax, ay, label):
    vs = obj["vertices"]
    xs = [v[ax] for v in vs]
    ys = [v[ay] for v in vs]
    lo_x, hi_x, lo_y, hi_y = min(xs), max(xs), min(ys), max(ys)
    span = max(hi_x - lo_x, hi_y - lo_y, 1)
    pad = 20

    def to_px(v):
        # Maxis Y is up; screen y grows down.
        px = pad + (v[ax] - lo_x) * (SIZE - 2 * pad) / span
        py = SIZE - pad - (v[ay] - lo_y) * (SIZE - 2 * pad) / span
        return px, py

    img = Image.new("RGB", (SIZE, SIZE), (255, 255, 255))
    draw = ImageDraw.Draw(img)
    for f in obj["faces"]:
        pts = [to_px(vs[i]) for i in f["indices"] if i < len(vs)]
        if len(pts) >= 2:
            draw.line(pts + [pts[0]], fill=(20, 20, 200), width=1)
    # y = 0 is the object's own ground plane.
    zero = SIZE - pad - (0 - lo_y) * (SIZE - 2 * pad) / span
    draw.line([(0, zero), (SIZE, zero)], fill=(220, 0, 0), width=1)
    draw.text((6, 6), "%s %s  (red = y 0)" % (name, label), fill=(0, 0, 0))
    return img


def main(names):
    catalog = {}
    for geo in sorted(GEO_DIR.iterdir()):
        if geo.suffix.lower() == ".max":
            catalog.update(load_objects(geo))
    for name in names:
        obj = catalog.get(name.upper())
        if obj is None:
            print(name, "NOT FOUND")
            continue
        sheet = Image.new("RGB", (SIZE * 2, SIZE), (255, 255, 255))
        sheet.paste(view(obj, name, 0, 1, "side X/Y"), (0, 0))
        sheet.paste(view(obj, name, 2, 1, "front Z/Y"), (SIZE, 0))
        path = OUT / ("geo_%s.png" % name.upper())
        sheet.save(path)
        print("wrote", path)


if __name__ == "__main__":
    main(sys.argv[1:] or ["CAPBOAT1"])
