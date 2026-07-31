"""Scan the Ghidra export JSONs for every call into SimCopter's sound API.

The sound manager is the global at 0x0055b1a8; its per-id object array starts at
0x0055b1ac.  Everything below is a free-function wrapper over that array, so a
call site names its sound by a literal id in the first (or second) argument.

Usage:  python scan_sound_calls.py > out.txt
"""

import json
import os
import re
import sys

EXPORT_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "..", ".ghidra-exports")

# addr -> (short name, which argument index holds the sound id)
API = {
    "FUN_0042a2a0": ("Play2D(id,flags)", 0),
    "FUN_0042a1f0": ("Play3D(id,vec3*)", 0),
    "FUN_0042a310": ("Stop(id)", 0),
    "FUN_0042a3a0": ("IsPlaying(id)", 0),
    "FUN_0042a360": ("SetVolume(id,adj)", 0),
    "FUN_0042a2f0": ("SetPos(id,vec3*)", 0),
    "FUN_0042a330": ("AddFreq(id,d)", 0),
    "FUN_0042a3b0": ("PlayQueued(a,id,b)", 1),
    "FUN_0042a100": ("SetFile(id,name)", 0),
}

CALL = re.compile(r"\b(FUN_0042a(?:2a0|1f0|310|3a0|360|2f0|330|3b0|100))\s*\(")


def split_args(text, start):
    """Return the argument list of a call whose '(' is at index `start`."""
    depth = 0
    args, cur = [], []
    i = start
    while i < len(text):
        c = text[i]
        if c == "(":
            depth += 1
            if depth == 1:
                i += 1
                continue
        elif c == ")":
            depth -= 1
            if depth == 0:
                args.append("".join(cur).strip())
                return args, i
        if depth == 1 and c == ",":
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    return args, i


def main():
    rows = []
    for name in sorted(os.listdir(EXPORT_DIR)):
        if not name.endswith(".json"):
            continue
        path = os.path.join(EXPORT_DIR, name)
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            try:
                data = json.load(fh)
            except Exception:
                continue
        code = data.get("decompiled") or data.get("decompile") or data.get("code") or ""
        if not isinstance(code, str) or "FUN_0042a" not in code:
            continue
        caller = data.get("name") or name[:-5]
        if caller in API:
            continue  # the wrapper itself
        lines = code.splitlines()
        offsets, pos = [], 0
        for ln in lines:
            offsets.append(pos)
            pos += len(ln) + 1
        for m in CALL.finditer(code):
            fn = m.group(1)
            label, argidx = API[fn]
            args, _ = split_args(code, m.end() - 1)
            sid = args[argidx] if argidx < len(args) else "?"
            lineno = 0
            for i, off in enumerate(offsets):
                if off > m.start():
                    break
                lineno = i
            rows.append((caller, label, sid, ", ".join(args), lines[lineno].strip()))

    rows.sort()
    print("caller           api                    id-arg        full-args")
    for caller, label, sid, allargs, _src in rows:
        print(f"{caller:16} {label:22} {sid:13} ({allargs})")
    print(f"\n# {len(rows)} call sites across "
          f"{len({r[0] for r in rows})} functions", file=sys.stderr)


if __name__ == "__main__":
    main()
