"""Scratch: rebuild the career progression table out of FUN_00408370.

The 30 career records live in uninitialised .data and are filled in at startup by
FUN_00408370, which is one long run of `_DAT_005xxxxx = value;` stores emitted in whatever order
the compiler felt like. Records are 0x50 bytes with base 0x518dc8:

    +0x00 career level (0..11 -> STRINGTABLE 290 "Level 1" .. 301 "Final Level")
    +0x04 difficulty, +0x08..+0x20 the seven mission weights, +0x24 day/night   (career.twk)
    +0x28/+0x2c/+0x30 the successor trio the career screen offers (-1 = fewer)
    +0x34/+0x38/+0x3c a second trio, all -1 in the shipped table
    +0x40 the city's own index, +0x44 map base name, +0x48 points needed, +0x4c $ earned

Run it against `ghidra-bridge decompile 0x00408370` output.
"""
import re
import subprocess
import sys

BRIDGE = r"S:\Repos\sim-copter-remake\Tools\re-agent\.venv\Scripts\ghidra-bridge.exe"
BASE = 0x518DC8
STRIDE = 0x50
COUNT = 30

text = subprocess.run(
    [BRIDGE, "decompile", "0x00408370"],
    capture_output=True, text=True, cwd=r"S:\Repos\sim-copter-remake",
).stdout

fields = {}
for m in re.finditer(r'_?DAT_(005[0-9a-f]{5})\s*=\s*(.+?);', text):
    addr = int(m.group(1), 16)
    raw = m.group(2).strip()
    if raw.startswith('"'):
        value = raw.strip('"')
    elif raw.startswith("&DAT_"):
        value = "&" + raw[5:]
    else:
        try:
            value = int(raw, 0)
        except ValueError:
            value = raw
    fields[addr] = value

print("stores parsed:", len(fields))
rows = []
for city in range(COUNT):
    rec = BASE + city * STRIDE
    row = {off: fields.get(rec + off) for off in (0x00, 0x28, 0x2C, 0x30, 0x40, 0x44)}
    rows.append(row)

print("%-4s %-6s %-22s %-6s %s" % ("city", "level", "successors", "index", "map"))
for city, row in enumerate(rows):
    succ = [row[0x28], row[0x2C], row[0x30]]
    succ = [s for s in succ if s is not None and s != 0xFFFFFFFF and s != -1]
    print("%-4d %-6s %-22s %-6s %s" % (
        city,
        row[0x00],
        ",".join(str(s) for s in succ),
        row[0x40],
        row[0x44],
    ))

missing = [(c, hex(o)) for c, row in enumerate(rows) for o, v in row.items() if v is None]
if missing:
    print("MISSING:", missing, file=sys.stderr)
