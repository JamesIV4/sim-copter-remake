"""Which animation mnemonics do the shipped BHAVs actually bind, and from where?

Opcode 1 is bind-anim; the mnemonic is the first two u16 args as four chars.
Prints every bind site grouped by mnemonic, so "which wave does the game use" is answerable
from the data rather than from a guess.
"""
import struct
import sys
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
    sites = {}
    for pid, (name, recs) in sorted(programs.items()):
        for i, (op, _tn, _fn, args) in enumerate(recs):
            if op == 1:
                sites.setdefault(mnemonic(args), []).append((pid, name, i))

    for mnem in sorted(sites):
        print(f"=== '{mnem}' : {len(sites[mnem])} bind site(s) ===")
        for pid, name, i in sites[mnem]:
            print(f"    BHAV {pid:4d} '{name}' rec[{i}]")
        print()


if __name__ == "__main__":
    main()
