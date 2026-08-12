"""Print the ARLU clip table for one figure in file order, with frame counts.

The question this answers: which clip does each behaviour mnemonic actually bind to, and is the
18-entry order the one the remake assumes? Run from the repo root.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Tools"))
from privanim_extract import Privanim  # noqa: E402

DF = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\x\privanim.df")


def main():
    df = Privanim(DF.read_bytes())
    figures = [e["name"] for e in df.entries("BODC")]
    target = sys.argv[1] if len(sys.argv) > 1 else figures[0]

    clips = df.clip_map(target)
    print(f"=== {target}: ARLU in file order ===")
    for index, (mnemonic, clip) in enumerate(clips.items()):
        frames = len(df.clip_frames(clip))
        print(f"  [{index:2d}] {mnemonic!r:8s} -> {clip!r:7s}  {frames:2d} frames")

    print("\n=== same table, sorted by clip id ===")
    for mnemonic, clip in sorted(clips.items(), key=lambda kv: kv[1]):
        print(f"  {clip!r:7s} {mnemonic!r}")

    # Every figure must agree on the mnemonic -> ordinal mapping, or a shared table is impossible.
    print("\n=== per-figure order agreement ===")
    reference = list(df.clip_map(figures[0]).keys())
    for name in figures:
        order = list(df.clip_map(name).keys())
        print(f"  {name:12s} {'SAME' if order == reference else 'DIFFERENT: ' + ','.join(order)}")


if __name__ == "__main__":
    main()
