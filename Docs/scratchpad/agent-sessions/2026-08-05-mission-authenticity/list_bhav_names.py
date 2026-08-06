"""List every shipped people.df BHAV id + name, and every privanim clip mnemonic.

Used to answer "what interactions can a person have with the player?" from the shipped data
rather than from memory: the interaction verbs are BHAV program names ('Rxn: ...') and the
emotes are ANIP clip mnemonics.
"""
import sys
from pathlib import Path

REPO = Path(r"S:\Repos\sim-copter-remake")
sys.path.insert(0, str(REPO / "Tools"))
from privanim_extract import DougFile  # noqa: E402


def main():
    df = DougFile((REPO / "Reference/SimCopterOriginalGame/X/people.df").read_bytes())
    print("=== people.df BHAV programs ===")
    for e in sorted(df.entries("BHAV"), key=lambda x: x["id"]):
        print(f"{e['id']:5d}  {e['name']}")

    print()
    for name in ("privanim.df",):
        pa = DougFile((REPO / "Reference/SimCopterOriginalGame/X" / name).read_bytes())
        for tag in sorted({s["tag"] for s in pa.sections}):
            print(f"=== {name} section {tag} ===")
            for e in sorted(pa.entries(tag), key=lambda x: x["id"]):
                print(f"{e['id']:5d}  {e['name']}")
            print()


if __name__ == "__main__":
    main()
