"""Query helper over .ghidra-exports."""
import json, sys, re, os
from pathlib import Path

EXP = Path(r"S:\Repos\sim-copter-remake\.ghidra-exports")

def load(fn):
    return json.loads((EXP/fn).read_text(encoding="utf-8", errors="replace"))

cmd = sys.argv[1]

if cmd == "keys":
    d = load(sys.argv[2] + ".json")
    print(json.dumps({k: (v if not isinstance(v,(list,dict)) else f"<{type(v).__name__} len={len(v)}>") for k,v in d.items()}, indent=1)[:4000])

elif cmd == "dec":
    d = load(sys.argv[2] + ".json")
    for k in ("decompiled","decompilation","code","c"):
        if k in d:
            print(d[k]); break
    else:
        print(json.dumps(d, indent=1)[:20000])

elif cmd == "strindex":
    idx = load("_strings_index.json")
    pat = sys.argv[2]
    for k,v in idx.items():
        if re.search(pat, k, re.I):
            print(k, "->", v)

elif cmd == "grepstr":
    s = load("_strings.json")
    pat = sys.argv[2]
    items = s if isinstance(s, list) else s.get("strings", [])
    for e in items:
        if re.search(pat, str(e.get("value","")), re.I):
            print(e)

elif cmd == "grepfuncs":
    # search all function json files for a regex in decompiled text
    pat = re.compile(sys.argv[2], re.I)
    for p in sorted(EXP.glob("[0-9a-f]*.json")):
        try:
            t = p.read_text(encoding="utf-8", errors="replace")
        except Exception:
            continue
        if pat.search(t):
            print(p.stem)

elif cmd == "ls":
    print("\n".join(sorted(p.name for p in EXP.glob("_*.json"))))
