"""Show every face that samples SIM3D page 2 cell 0 (the Maxis debug portrait), with geometry."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dump_geo_faces as g

PAGE, CELL = 2, 0

objs = []
for p in g.PACKS:
    objs += g.parse(os.path.join(g.ROOT, p))

for o in objs:
    hits = [(i, f) for i, f in enumerate(o["faces"])
            if f["type"] == 18 and f["tex"] == PAGE and f["mat"] == CELL]
    if not hits:
        continue
    print("%s id=0x%x %s  verts=%d faces=%d" % (o["name"], o["id"], o["pack"], len(o["verts"]), len(o["faces"])))
    for i, f in hits:
        pts = [o["verts"][k] for k in f["idx"] if k < len(o["verts"])]
        print("  face %3d verts=%d flags=0x%04x light=%d" % (i, f["verts"], f["flags"], f["light"]))
        for p in pts:
            print("      (%9d, %9d, %9d)" % p)
