"""Face-type-18 users of one atlas page, grouped by cell index.

Usage: list_atlas_cell_users.py <pageIndex> [cellIndex]
"""
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dump_geo_faces as g

page = int(sys.argv[1])
want_cell = int(sys.argv[2]) if len(sys.argv) > 2 else None

objs = []
for p in g.PACKS:
    objs += g.parse(os.path.join(g.ROOT, p))

by = defaultdict(lambda: defaultdict(int))
for o in objs:
    for f in o["faces"]:
        if f["type"] == 18 and f["tex"] == page:
            by[f["mat"]][o["name"]] += 1

for cell in sorted(by):
    if want_cell is not None and cell != want_cell:
        continue
    names = by[cell]
    print("page %d cell %3d  faces=%-4d objects(%d): %s" % (
        page, cell, sum(names.values()), len(names), ", ".join(sorted(names))))
