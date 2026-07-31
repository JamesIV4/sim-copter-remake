"""Print each sound-API call in a decompiled caller with N lines of surrounding context."""
import glob
import os
import re
import sys

DIR = os.path.dirname(__file__)
CALL = re.compile(r"FUN_0042a(?:2a0|1f0|310|3a0|360|2f0|330|3b0|100)\(|FUN_00429ff0\(")
N = int(os.environ.get("CTX", "8"))

for pat in sys.argv[1:]:
    for path in sorted(glob.glob(os.path.join(DIR, "callers", f"*{pat}*.txt"))):
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        hits = [i for i, l in enumerate(lines) if CALL.search(l)]
        if not hits:
            continue
        print(f"\n{'='*78}\n=== {os.path.basename(path)}")
        shown = set()
        for h in hits:
            for i in range(max(0, h - N), min(len(lines), h + 3)):
                if i in shown:
                    continue
                shown.add(i)
                mark = ">>" if CALL.search(lines[i]) else "  "
                print(f"{mark}{i:5}| {lines[i]}")
            print("      ...")
