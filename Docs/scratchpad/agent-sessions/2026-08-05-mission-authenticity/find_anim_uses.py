"""Which shipped BHAV programs bind which animation mnemonic (op1)?

Answers "is there a kiss / a wave / an emote at the player, and who runs it?" from the data.
"""
import struct
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(r"S:\Repos\sim-copter-remake")
sys.path.insert(0, str(REPO / "Tools"))
from privanim_extract import DougFile, _s8  # noqa: E402


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


def mnemonic(args):
    return "".join(chr(b) for a in args[:2] for b in (a >> 8, a & 0xFF))


def main():
    programs = load()
    by_anim = defaultdict(list)
    callers = defaultdict(set)
    for pid, (name, recs) in programs.items():
        for i, (op, _tn, _fn, args) in enumerate(recs):
            if op == 1:
                by_anim[mnemonic(args)].append(f"{pid} '{name}' rec[{i}]")
            elif op >= 0x100:
                callers[op].add(pid)

    print("=== op1 bind-anim sites, by mnemonic ===")
    for anim in sorted(by_anim):
        print(f"--- {anim} ({len(by_anim[anim])} sites)")
        for site in by_anim[anim]:
            print(f"      {site}")

    print()
    print("=== programs nobody calls (entry points / reaction targets) ===")
    for pid in sorted(programs):
        if pid not in callers:
            print(f"{pid:5d}  {programs[pid][0]}")

    print()
    print("=== call graph (who calls whom) ===")
    for pid in sorted(callers):
        who = ", ".join(str(c) for c in sorted(callers[pid]))
        print(f"{pid:5d} '{programs.get(pid, ('?',))[0]}'  <- {who}")


if __name__ == "__main__":
    main()
