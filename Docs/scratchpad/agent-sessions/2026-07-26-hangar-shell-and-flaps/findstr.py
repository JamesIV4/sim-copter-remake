import re, sys
from pathlib import Path

targets = [
    Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"),
]
pat = sys.argv[1].encode("ascii")
for p in targets:
    d = p.read_bytes()
    for m in re.finditer(re.escape(pat), d, re.I):
        s = max(0, m.start() - 120)
        e = min(len(d), m.end() + 260)
        ctx = d[s:e].decode("latin-1")
        ctx = "".join(c if 32 <= ord(c) < 127 else ("\n" if c == "\n" else ".") for c in ctx)
        print(f"--- {p.name} @0x{m.start():x}")
        print(ctx)
    # also utf-16
    pat16 = pat.decode("ascii").encode("utf-16-le")
    for m in re.finditer(re.escape(pat16), d, re.I):
        print(f"--- UTF16 {p.name} @0x{m.start():x}")
        print(d[max(0,m.start()-100):m.end()+300].decode("utf-16-le", "replace"))
