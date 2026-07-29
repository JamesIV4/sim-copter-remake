import sys, math
sys.path.insert(0, "Tools")
from privanim_extract import Privanim
from pathlib import Path

pa = Privanim(Path("Reference/SimCopterOriginalGame/X/privanim.df").read_bytes())

for name in ("2DOGG", "pilot", "2woman"):
    parts = pa.skeleton(name)
    cmap = pa.clip_map(name)
    clip = cmap["1Wal"]
    frames = pa.clip_frames(clip)
    f0 = frames[0]
    # model bbox
    xs=[];ys=[];zs=[]
    for a,b in f0:
        xs += [a[0],b[0]]; ys += [a[1],b[1]]; zs += [a[2],b[2]]
    print(f"=== {name} clip {clip} bbox x[{min(xs)},{max(xs)}] y[{min(ys)},{max(ys)}] z[{min(zs)},{max(zs)}]")
    for p, (a,b) in zip(parts, f0):
        L = math.dist(a,b)
        print(f"  [{p['index']:2}] t=0x{p['type']:02x} dims={p['dims']}  segA={a} segB={b} len={L:6.2f}"
              f"   d0/len={(p['dims'][0]/L if L>0.01 else float('nan')):5.2f}")
    print()
