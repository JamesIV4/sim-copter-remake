"""Say what each ARLU clip actually animates, by measuring which body parts move in it.

Every claim in the memory about "Wave is the panic clip" and friends was inferred from where a
mnemonic is bound in people.df, never from the geometry. This reads the ARPP pose records and
reports, per clip, which named skeleton parts move and by how much - so the clip can be named from
what it draws instead of from where it is used.

Run from the repo root:  python Docs/scratchpad/describe_arlu_clips.py [figure]
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Tools"))
from privanim_extract import Privanim  # noqa: E402

DF = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\x\privanim.df")


def main():
    df = Privanim(DF.read_bytes())
    figure_name = sys.argv[1] if len(sys.argv) > 1 else "SUIT"
    parts = df.skeleton(figure_name)
    part_names = [p["name"] for p in parts]

    print(f"=== {figure_name}: {len(parts)} parts ===")
    print("  " + ", ".join(f"{i}:{n}" for i, n in enumerate(part_names)))

    for mnemonic, clip in df.clip_map(figure_name).items():
        frames = df.clip_frames(clip)
        if not frames:
            continue
        # Per part, the peak displacement of either endpoint away from frame 0.
        motion = []
        for part_index in range(len(frames[0])):
            base = frames[0][part_index]
            peak = 0.0
            peak_axis = ""
            for frame in frames[1:]:
                here = frame[part_index]
                for end in (0, 1):
                    for axis, label in enumerate("xyz"):
                        delta = abs(here[end][axis] - base[end][axis])
                        if delta > peak:
                            peak = delta
                            peak_axis = label
            if peak > 0:
                motion.append((peak, part_names[part_index], peak_axis))
        motion.sort(reverse=True)
        moving = ", ".join(f"{name}({int(peak)}{axis})" for peak, name, axis in motion[:6])
        print(f"\n  {mnemonic!r:8s} {clip!r:7s} {len(frames):2d} frames, "
              f"{len(motion):2d}/{len(part_names)} parts move")
        print(f"      top movers: {moving if moving else '(static pose)'}")


if __name__ == "__main__":
    main()
