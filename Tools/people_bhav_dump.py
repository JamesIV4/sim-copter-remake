"""Dump people.df BHAV behavior programs as an annotated record graph.

Usage:
    python people_bhav_dump.py <people.df> [entry ids...]   (default entry: 600 = ambient)

Record layout (decoded from FUN_004ce7b0/FUN_004ce8f0, see Docs/memory/simcopter-people-logic-next.md):
    payload = [BE u16 recordCount] + count x 12-byte records
    record  = [BE u16 op][s8 trueNext][s8 falseNext][4 x BE u16 args]
    op >= 0x100 calls that BHAV id as a subprogram; edge -2 returns TRUE, -1 returns FALSE.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from privanim_extract import DougFile, _s8

OPS = {
    0: "wait(local[a0]--)",
    1: "bind-anim",
    2: "expr",
    4: "timed-move(local[a0]--; speed=+0x164)",
    6: "bind-figure",
    7: "local[a0]:=rand(resolve(a1,scope=a2))",
    13: "side-effect",
    15: "threat-probe",
    16: "deactivate",
    17: "threat-response",
    18: "face-runtime-obj",
    19: "tileclass==a0",
    20: "tileclass-in-behavior-row",
    21: "threat-nearby",
    22: "player-tile-probe(l[a0]=speed,l[a1]=facing)",
    23: "speed(+0x150)+=a0",
    24: "obj-bearing-probe",
    27: "reaction",
    28: "riot-create",
    29: "facing:=local[a0]&7",
    31: "face-away-linked",
    34: "wander-out-of-bad-tile(local[a0]--)",
    36: "face-obj-class",
    57: "sound",
    59: "carried-is-player",
    70: "snap-z",
    85: "ambient-audio",
    86: "player/carried-probe",
}
SCOPES = {7: "lit", 9: "local", 3: "attr"}
ATTRS = {0: "facing(+140)", 3: "class(+146)", 4: "state(+148)", 5: "loop(+14a)",
         6: "frame(+14c)", 8: "speed(+150)", 9: "visible(+152)", 10: "prevspeed(+154)",
         18: "movespeed(+164)", 20: "ambient(+168)", 21: "autoturn(+16a)",
         40: "movethruwalls(+190)", 41: "failcount(+192)"}


def fmt_operand(scope, val):
    s = SCOPES.get(scope, f"scope{scope}")
    if s == "attr":
        return ATTRS.get(val, f"attr{val}")
    if s == "lit":
        return str(val)
    return f"local{val}"


def fmt_expr(args):
    tscope, sscope = args[3] >> 8, args[3] & 0xFF
    ops = {0: ">", 1: "<", 2: "==", 3: "+=", 4: "-=", 5: ":=", 6: "*=", 7: "/=", 8: ":=rand"}
    o = ops.get(args[2], f"op{args[2]}")
    return f"{fmt_operand(tscope, args[0])} {o} {fmt_operand(sscope, args[1])}"


def mnemonic(args):
    return "".join(chr(b) for a in args[:2] for b in (a >> 8, a & 0xFF))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    df = DougFile(Path(sys.argv[1]).read_bytes())
    programs = {}
    for e in df.entries("BHAV"):
        off, _ = df.chunk(e)
        count = struct.unpack_from(">H", df.d, off)[0]
        recs = []
        for i in range(count):
            o = off + 2 + i * 12
            op = struct.unpack_from(">H", df.d, o)[0]
            tn = _s8(df.d[o + 2])
            fn = _s8(df.d[o + 3])
            args = struct.unpack_from(">4H", df.d, o + 4)
            recs.append((op, tn, fn, args))
        programs[e["id"]] = (e["name"], recs)

    queue = [int(a, 0) for a in sys.argv[2:]] or [600]
    seen = set()
    while queue:
        pid = queue.pop(0)
        if pid in seen:
            continue
        seen.add(pid)
        if pid not in programs:
            print(f"--- BHAV {pid}: MISSING ---")
            continue
        name, recs = programs[pid]
        print(f"=== BHAV {pid} '{name}' ({len(recs)} records) ===")
        for i, (op, tn, fn, args) in enumerate(recs):
            if op >= 0x100:
                desc = f"CALL {op} '{programs.get(op, ('?',))[0]}'"
                if op not in seen:
                    queue.append(op)
            elif op == 1:
                desc = f"bind-anim '{mnemonic(args)}'"
            elif op == 2:
                desc = f"expr {fmt_expr(args)}"
            else:
                desc = OPS.get(op, f"op{op}?")
                desc += "  args=" + ",".join(str(a) for a in args)
            e = {-2: "retT", -1: "retF"}
            print(f"  [{i:2d}] {desc:55s} T->{e.get(tn, tn)} F->{e.get(fn, fn)}")
        print()


if __name__ == "__main__":
    main()
