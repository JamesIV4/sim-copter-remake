"""Dump shipped people.df BHAV programs with the full decoded opcode legend.

    python dump_bhav.py 666 914 1051 ...

Record layout (FUN_004ce7b0 / FUN_004ce8f0): [BE u16 op][s8 trueNext][s8 falseNext][4 x BE u16].
Op >= 0x100 calls that BHAV id; edge -2 returns TRUE, -1 returns FALSE, -3 = unreachable/stop.
Opcode names are the decoded handlers - see Docs/scratchpad/ghidra/people_vm_opcode_table_*.md.
"""
import struct
import sys
from pathlib import Path

REPO = Path(r"S:\Repos\sim-copter-remake")
sys.path.insert(0, str(REPO / "Tools"))
from privanim_extract import DougFile, _s8  # noqa: E402

OPS = {
    0: "wait local[a0]--",
    1: "bind-anim",
    2: "expr",
    4: "timed-move local[a0]-- @movespeed",
    6: "bind-figure",
    7: "local[a0] := rand(a1)",
    12: "walk to selection AND board it",
    13: "post mission outcome a0",
    14: "proximity test case a0",
    15: "select nearest obj class a0 within a1 -> local[a3]",
    16: "deactivate person",
    17: "alight here (test + do)",
    18: "face selection",
    19: "tileclass == a0",
    20: "tileclass in my behavior row",
    21: "may I stand here (test only)",
    22: "same tile as player -> l[a0]=speed l[a1]=facing",
    23: "logic speed += a0",
    24: "bearing/dist to selection -> locals",
    25: "XBLD at my tile == a0",
    26: "(unused)",
    27: "reaction force",
    28: "riot create probe",
    29: "facing := local[a0] & 7",
    30: "throw projectile on facing",
    31: "face away from linked obj",
    32: "face away from interaction source",
    33: "face toward interaction source",
    34: "wander out of bad tile local[a0]--",
    35: "conditional despawn vs tuning",
    36: "face toward obj class a0",
    37: "leave the map",
    38: "walk to selection local[a0]--",
    39: "push BHAV a0 onto selection",
    40: "tear down (despawn)",
    44: "put selected person on me",
    46: "select the person I carry",
    47: "drop selected person",
    48: "board selection (no walk)",
    50: "local[a0] := id of selection",
    51: "set down + select whom I carry",
    53: "select player heli within 24u",
    54: "seat portrait mood := a0",
    55: "local[a0] := player heli speed",
    56: "is my tile serviceable",
    57: "sound a0",
    58: "get on heli if harness raised",
    59: "is my carrier the player heli",
    60: "throw projectile on facing",
    61: "message owning vehicle a0",
    62: "select my vehicle else player heli",
    63: "am I riding something",
    66: "fall and die",
    67: "copy my selection to caller",
    68: "copy caller selection to me",
    69: "is selection the player heli",
    70: "snap Z to surface/carrier",
    71: "am I carrying anyone",
    72: "clear selection",
    73: "hidden state-5 medic exists",
    74: "local[a0] := difficulty tier",
    75: "local[a0] := my tile X",
    76: "local[a0] := my tile Y",
    77: "local[a0] := abs(local[a0])",
    78: "home in on +0x1a8 @movespeed",
    79: "local[a0] := DAT_00506448",
    80: "re-run post-move selector vs source",
    82: "is selection within 25u",
    83: "face selection, throw at it",
    84: "select medevac victim aboard player",
    85: "ambient audio",
    86: "is my carrier the harness",
    87: "am I on my home tile",
}
SCOPES = {7: "lit", 9: "local", 3: "attr"}
ATTRS = {0: "facing", 3: "class", 4: "state", 5: "loop", 6: "frame", 8: "logicspeed",
         9: "visible", 10: "prevspeed", 18: "movespeed", 20: "ambient", 21: "autoturn",
         23: "crimcaught", 34: "medhealth", 40: "thruwalls", 41: "failcount"}


def operand(scope, val):
    s = SCOPES.get(scope, f"scope{scope}")
    if s == "attr":
        return ATTRS.get(val, f"attr{val}")
    return str(val) if s == "lit" else f"l{val}"


def expr(args):
    tscope, sscope = args[3] >> 8, args[3] & 0xFF
    ops = {0: ">", 1: "<", 2: "==", 3: "+=", 4: "-=", 5: ":=", 6: "*=", 7: "/=", 8: ":=rand"}
    return f"{operand(tscope, args[0])} {ops.get(args[2], args[2])} {operand(sscope, args[1])}"


def mnemonic(args):
    return "".join(chr(b) for a in args[:2] for b in (a >> 8, a & 0xFF))


def load():
    df = DougFile((REPO / "Reference/SimCopterOriginalGame/X/people.df").read_bytes())
    programs = {}
    for e in df.entries("BHAV"):
        off, _ = df.chunk(e)
        count = struct.unpack_from(">H", df.d, off)[0]
        recs = []
        for i in range(count):
            o = off + 2 + i * 12
            recs.append((struct.unpack_from(">H", df.d, o)[0], _s8(df.d[o + 2]), _s8(df.d[o + 3]),
                         struct.unpack_from(">4H", df.d, o + 4)))
        programs[e["id"]] = (e["name"], recs)
    return programs


def main():
    programs = load()
    for pid in [int(a, 0) for a in sys.argv[1:]]:
        if pid not in programs:
            print(f"--- BHAV {pid}: MISSING ---")
            continue
        name, recs = programs[pid]
        print(f"=== BHAV {pid} '{name}' ({len(recs)} records) ===")
        for i, (op, tn, fn, args) in enumerate(recs):
            if op >= 0x100:
                desc = f"CALL {op} '{programs.get(op, ('?',))[0]}'"
            elif op == 1:
                desc = f"bind-anim '{mnemonic(args)}'"
            elif op == 2:
                desc = f"expr {expr(args)}"
            else:
                desc = f"op{op} {OPS.get(op, '?')}  args=" + ",".join(
                    str(a if a < 0x8000 else a - 0x10000) for a in args)
            e = {-2: "retT", -1: "retF"}
            print(f"  [{i:2d}] {desc:60s} T->{e.get(tn, tn)} F->{e.get(fn, fn)}")
        print()


if __name__ == "__main__":
    main()
