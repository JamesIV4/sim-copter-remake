"""Scratch: pull the Check-up dialog's control layout straight out of FUN_00443c20's ASM.

The DECOMPILE cannot be trusted for these - Ghidra aliases the same stack slots across all nine
controls, so the C output shows one rect reused. The assembly does not lie: each control writes
its four dwords to consecutive [ESP + n] slots, pushes its STRINGTABLE id, and is then configured
through three vtable slots:

    [vt + 0xe0](size, 0, 0)   font size, in pixels
    [vt + 0xe4](n)            justification
    [vt + 0xe8](ptr)          colour / font resource

A PUSH between the rect stores bumps every later displacement by 4, so the four slots are matched
by ADDRESS ORDER, not by literal displacement.
"""
import re

ASM = r"S:\Repos\sim-copter-remake\Docs\scratchpad\FUN_00443c20.asm"

MOV = re.compile(r"^([0-9a-f]{8}) MOV dword ptr \[ESP \+ (0x[0-9a-f]+)\],(0x[0-9a-f]+)$")
PUSH = re.compile(r"^([0-9a-f]{8}) PUSH (0x[0-9a-f]+)$")
CALL = re.compile(r"^([0-9a-f]{8}) CALL (0x[0-9a-f]+)$")
VCALL = re.compile(r"^([0-9a-f]{8}) CALL dword ptr \[E[A-Z]{2} \+ (0x[0-9a-f]+)\]$")

CONTROL = {
    "0x004090d0": "static text",
    "0x0040af00": "slider",
    "0x0043b240": "button",
}
SETTER = {0xe0: "font", 0xe4: "justify", 0xe8: "colour"}

movs, pushes, controls = [], [], []

for raw in open(ASM, encoding="utf-8", errors="replace"):
    line = raw.strip()
    if not line:
        continue

    m = MOV.match(line)
    if m:
        movs.append(int(m.group(3), 16))
        continue
    p = PUSH.match(line)
    if p:
        pushes.append(int(p.group(2), 16))
        continue

    v = VCALL.match(line)
    if v:
        what = SETTER.get(int(v.group(2), 16))
        if what and controls and pushes:
            val = pushes[-3] if what == "font" and len(pushes) >= 3 else pushes[-1]
            controls[-1]["set"].append(
                "%s=%s" % (what, hex(val) if what == "colour" else val))
        movs, pushes = [], []
        continue

    # Anything that is not a CALL (LEA, MOV ECX, ...) is interleaved with the stores and must
    # not reset the accumulators - only a call boundary ends a control's setup.
    c = CALL.match(line)
    if not c:
        continue
    if c.group(2) in CONTROL and len(movs) >= 4:
        l, t, r, b = movs[-4:]
        ids = [i for i in pushes if 560 <= i <= 620]
        controls.append({
            "addr": c.group(1), "kind": CONTROL[c.group(2)],
            "rect": (l, t, r, b), "str": ids[0] if ids else None, "set": [],
        })
    movs, pushes = [], []

for ctl in controls:
    l, t, r, b = ctl["rect"]
    print("%s  %-11s (%3d,%3d)-(%3d,%3d) %3dx%-3d str=%-4s %s"
          % (ctl["addr"], ctl["kind"], l, t, r, b, r - l, b - t,
             ctl["str"] if ctl["str"] else "-", "  ".join(ctl["set"])))
