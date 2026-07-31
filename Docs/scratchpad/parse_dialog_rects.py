"""Scratch: pull any SimCopter bitmap dialog's control layout out of its build method's ASM.

Generalises Docs/scratchpad/parse_checkup_rects.py to the Settings sub-dialogs. Same rule and
same trap: Ghidra aliases the reused stack slots so the DECOMPILE shows one rect for all of them,
while the assembly writes each control's four dwords to consecutive [ESP + n] slots. A PUSH
between the stores shifts every later displacement by four, so the four slots are paired up by
ADDRESS ORDER, never by the literal offsets.

Each control is then configured through the same three vtable slots the Check-up dialog uses:

    [vt + 0xe0](height, 0, 0)   font height, in Windows pixels
    [vt + 0xe4](n)              justification
    [vt + 0xe8](ptr)            colour / font resource

    python parse_dialog_rects.py asm-0043f7c0-sound.txt
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

MOV = re.compile(r"^([0-9a-f]{8}) MOV dword ptr \[ESP \+ (0x[0-9a-f]+)\],(0x[0-9a-f]+)$")
PUSH = re.compile(r"^([0-9a-f]{8}) PUSH (0x[0-9a-f]+)$")
CALL = re.compile(r"^([0-9a-f]{8}) CALL (0x[0-9a-f]+)$")
VCALL = re.compile(r"^([0-9a-f]{8}) CALL dword ptr \[E[A-Z]{2} \+ (0x[0-9a-f]+)\]$")

# Control constructors, identified by which allocation size precedes them and by the Check-up
# dialog's already-decoded set.
CONTROL = {
    "0x004090d0": "text",
    "0x0040af00": "slider",
    "0x0043b240": "button",
    "0x0043b3e0": "toggle",
}
SETTER = {0xE0: "font", 0xE4: "just", 0xE8: "colour"}


def parse(path):
    movs, pushes, controls = [], [], []

    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.strip()
        if not line:
            continue

        m = MOV.match(line)
        if m:
            movs.append((int(m.group(2), 16), int(m.group(3), 16)))
            continue
        p = PUSH.match(line)
        if p:
            pushes.append(int(p.group(2), 16))
            continue

        v = VCALL.match(line)
        if v:
            what = SETTER.get(int(v.group(2), 16))
            if what and controls and pushes:
                # Arguments are pushed right to left, so the first one is always the last PUSH:
                # the font height, the justification code, the colour resource.
                val = pushes[-1]
                controls[-1]["set"].append(
                    "%s=%s" % (what, hex(val) if what == "colour" else val))
            movs, pushes = [], []
            continue

        c = CALL.match(line)
        if not c:
            # LEA / MOV ECX and friends are interleaved with the stores; only a call ends a run.
            continue
        if c.group(2) in CONTROL and len(movs) >= 4:
            rect = [value for _, value in movs[-4:]]
            controls.append({
                "addr": c.group(1),
                "kind": CONTROL[c.group(2)],
                "rect": rect,
                "push": list(pushes),
                "set": [],
            })
        movs, pushes = [], []

    return controls


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "asm-0043f7c0-sound.txt"
    path = name if os.path.isabs(name) else os.path.join(HERE, name)

    for ctl in parse(path):
        l, t, r, b = ctl["rect"]
        # Text controls carry a STRINGTABLE id; sliders, buttons and toggles carry a command id
        # instead, and both land in the same PUSH run, so print the whole run and let the reader
        # match it against the strings.
        print("%s  %-8s (%4d,%4d)-(%4d,%4d) %4dx%-4d push=%-22s %s"
              % (ctl["addr"], ctl["kind"], l, t, r, b, r - l, b - t,
                 ",".join(str(p) for p in ctl["push"]), "  ".join(ctl["set"])))


if __name__ == "__main__":
    main()
