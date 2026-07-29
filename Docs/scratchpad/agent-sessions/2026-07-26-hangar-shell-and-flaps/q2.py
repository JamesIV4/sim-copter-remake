import json, sys, re
from pathlib import Path
EXP = Path(r"S:\Repos\sim-copter-remake\.ghidra-exports")
s = json.loads((EXP/"_strings.json").read_text(encoding="utf-8", errors="replace"))
pat = re.compile(sys.argv[1], re.I)
n = 0
for addr, e in s.items():
    v = e.get("value", "") if isinstance(e, dict) else str(e)
    if pat.search(v):
        print(addr, repr(v))
        n += 1
        if n > 300:
            break
