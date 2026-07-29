import glob, os, re, sys
root = sys.argv[1]
targets = [b"AICON", b"PICON", b"FICON", b"CARPOLIC", b"CARFIRET"]
hits = {}
for path in glob.glob(os.path.join(root, "**", "*"), recursive=True):
    if not os.path.isfile(path):
        continue
    try:
        data = open(path, "rb").read()
    except OSError:
        continue
    for t in targets:
        if t in data:
            hits.setdefault(t.decode(), []).append((os.path.relpath(path, root), data.count(t)))
for t in targets:
    k = t.decode()
    print(f"{k}: " + (", ".join(f"{p} x{n}" for p, n in hits.get(k, [])) or "NOT FOUND"))
