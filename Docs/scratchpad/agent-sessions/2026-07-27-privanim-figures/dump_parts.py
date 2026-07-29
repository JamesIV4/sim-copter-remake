import sys, collections
sys.path.insert(0, "Tools")
from privanim_extract import Privanim
from pathlib import Path

pa = Privanim(Path("Reference/SimCopterOriginalGame/X/privanim.df").read_bytes())

TYPES = collections.Counter()
for fe in pa.figures():
    for p in pa.skeleton(fe["name"]):
        TYPES[p["type"]] += 1
print("type histogram (all figures):", dict(sorted(TYPES.items())))
print()

for name in ("2DOGG", "pilot", "2woman", "Woman", "Coww", "Child"):
    parts = pa.skeleton(name)
    print(f"=== {name} ({len(parts)} parts) ===")
    for p in parts:
        print(f"  [{p['index']:2}] type=0x{p['type']:02x} ref={p['ref']:3} seq={p['seq']:3} "
              f"col={p['f3'][0]:3} lod=0x{p['f3'][1]:02x} fixed={p['f3'][2]} "
              f"f6={p['f3'][3]:3} f7={p['f3'][4]:3} "
              f"name={p['name']!r} parent={p['parent']!r} dims={p['dims']}")
    print()
