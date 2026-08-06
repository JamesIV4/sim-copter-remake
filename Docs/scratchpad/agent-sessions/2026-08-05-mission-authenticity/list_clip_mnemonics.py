"""Dump the ARLU behaviour-mnemonic -> clip table for every privanim figure.

The mnemonics ARE the emote vocabulary: op1 ('bind-anim') in a BHAV names one of these, so
whatever a person can be seen doing at the player is in this list.
"""
import sys
from pathlib import Path

REPO = Path(r"S:\Repos\sim-copter-remake")
sys.path.insert(0, str(REPO / "Tools"))
from privanim_extract import Privanim  # noqa: E402


def main():
    df = Privanim((REPO / "Reference/SimCopterOriginalGame/X/privanim.df").read_bytes())
    all_mnemonics = {}
    for fig in df.figures():
        try:
            table = df.clip_map(fig["name"])
        except Exception as exc:  # noqa: BLE001
            print(f"{fig['name']}: {exc}")
            continue
        print(f"=== figure {fig['id']:5d} {fig['name']} ===")
        for mnem, clip in table.items():
            print(f"    {mnem}  -> {clip}")
            all_mnemonics.setdefault(mnem, set()).add(fig["name"])
    print()
    print("=== all mnemonics ===")
    for mnem in sorted(all_mnemonics):
        print(f"{mnem}  ({len(all_mnemonics[mnem])} figures)")


if __name__ == "__main__":
    main()
