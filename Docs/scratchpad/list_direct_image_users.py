"""Which SIM3D direct images do face-type-13 textured faces sample, and from which objects."""
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dump_geo_faces as g

want_type = int(sys.argv[1]) if len(sys.argv) > 1 else 13

objs = []
for p in g.PACKS:
    objs += g.parse(os.path.join(g.ROOT, p))

by = defaultdict(lambda: defaultdict(int))
for o in objs:
    for f in o["faces"]:
        if f["type"] == want_type:
            by[f["mat"]][o["name"]] += 1

for image in sorted(by):
    names = by[image]
    print("image %3d  faces=%-5d objects(%d): %s" % (
        image, sum(names.values()), len(names), ", ".join(sorted(names))[:240]))
