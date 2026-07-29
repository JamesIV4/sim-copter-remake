import sys
sys.path.insert(0, "Tools")
from privanim_extract import Privanim
from pathlib import Path
pa = Privanim(Path("Reference/SimCopterOriginalGame/X/privanim.df").read_bytes())
for nm in ("pilot", "2woman"):
    parts = pa.skeleton(nm)
    f0 = pa.clip_frames(pa.clip_map(nm)["1Wal"])[0]
    print("===", nm)
    for p, (a, b) in zip(parts, f0):
        print("  [%2d] t=0x%02x lod=%#x col=%2d fix=%d dims=%s %r parent=%r A=%s B=%s" % (
            p["index"], p["type"], p["f3"][1], p["f3"][0], p["f3"][2], p["dims"],
            p["name"], p["parent"], a, b))
