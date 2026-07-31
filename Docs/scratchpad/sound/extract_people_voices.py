"""Extract the people voice-event table out of FUN_004c5210's decompile.

Shape of the function:

    switch(param_2) {            // voice event id
    case 1:
      uVar5 = FUN_004cea00(4);   // rand(4)
      switch(uVar5) {
      case 0: pcVar11 = s_assert1_wav_00506ad0; break;
      ...

Some events use `sVar6 = FUN_004cea00(2); if (sVar6 == 0) ... else if (sVar6 == 1) ...`
instead of an inner switch, and some assign a single filename with no pick at all.
Both are handled by simply collecting every `s_<name>_wav` symbol that appears
between one `case <n>:` of the OUTER switch and the next.
"""
import re
import sys

SRC = r"S:\Repos\sim-copter-remake\Docs\scratchpad\sound\callers\4c5210.txt"

lines = open(SRC, encoding="utf-8", errors="replace").read().splitlines()

# Locate the outer switch and its brace depth.
start = next(i for i, l in enumerate(lines) if l.strip() == "switch(param_2) {")
depth0 = None
depth = 0
for i, l in enumerate(lines[:start + 1]):
    depth += l.count("{") - l.count("}")
depth0 = depth  # depth just inside the outer switch

CASE = re.compile(r"^\s*case (0x[0-9a-fA-F]+|\d+):\s*$")
WAV = re.compile(r"\bs_([A-Za-z0-9_#]+?)_(?:wav|WAV)_[0-9a-f]{8}\b")

events = {}
cur = None
depth = depth0
for i in range(start + 1, len(lines)):
    l = lines[i]
    m = CASE.match(l)
    if m and depth == depth0:
        cur = int(m.group(1), 0)
        events.setdefault(cur, [])
    if cur is not None:
        for w in WAV.finditer(l):
            name = w.group(1)
            if name not in events[cur]:
                events[cur].append(name)
    depth += l.count("{") - l.count("}")
    if depth < depth0:
        break

print(f"# {len(events)} voice events, "
      f"{len({w for v in events.values() for w in v})} distinct clips")
for k in sorted(events):
    print(f"{k:5} 0x{k:02x}  {', '.join(events[k]) if events[k] else '<none>'}")

if "--cpp" in sys.argv:
    print("\n// ---- C++ ----")
    for k in sorted(events):
        if not events[k]:
            continue
        clips = ", ".join(f'TEXT("{w}")' for w in events[k])
        print(f"\t{{ {k:3}, {{ {clips} }} }},")
