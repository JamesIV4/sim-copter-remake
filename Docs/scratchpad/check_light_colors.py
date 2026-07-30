"""Scratch: confirm the material-index domain of face type 25 vs 26 across all three GEO packs."""
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dump_geo_faces as G  # noqa: E402  (module runs main() on import; see below)

objs = []
for p in G.PACKS:
    objs += G.parse(os.path.join(G.ROOT, p))

c25, c26 = Counter(), Counter()
verts25 = Counter()
for o in objs:
    for f in o["faces"]:
        if f["type"] == 25:
            c25[f["mat"]] += 1
            verts25[f["verts"]] += 1
        elif f["type"] == 26:
            c26[f["mat"]] += 1

print("type 25 material indices:", sorted(c25.items()))
print("type 25 vertex counts   :", sorted(verts25.items()))
print("type 26 material indices:", sorted(c26.items()))
print("type 25 total faces     :", sum(c25.values()))
