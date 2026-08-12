"""Classify each ARLU clip by WHERE on the body it moves, not by where it is bound.

Frame 0 of every clip gives each part's height, so a part can be called upper or lower body from
the geometry. A clip whose motion is confined to the top third is a gesture; one that swings the
bottom third is a leg animation. This is the check that was never done - the repo's names for
these clips were inferred from their bind sites in people.df.

Run from the repo root:  python Docs/scratchpad/classify_arlu_clips.py [figure]
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Tools"))
from privanim_extract import Privanim  # noqa: E402

DF = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\x\privanim.df")


def main():
    df = Privanim(DF.read_bytes())
    figure_name = sys.argv[1] if len(sys.argv) > 1 else "SUIT"
    clips = df.clip_map(figure_name)

    # Body height from the standing walk clip's frame 0.
    stand = df.clip_frames(clips["1Wal"])[0]
    zs = [z for seg in stand for (_, _, z) in seg]
    z_min, z_max = min(zs), max(zs)
    span = max(1, z_max - z_min)
    # ARPP z is Y-DOWN (the reader negates it), so the most negative z is the TOP of the head and
    # the most positive is the feet. Getting this backwards makes a wave look like a kick, which
    # is exactly the mistake this script exists to avoid.
    print(f"=== {figure_name}: raw z range {z_min}..{z_max} (span {span}), z is DOWN ===")
    print("    'height' below is 1.0 at the top of the head and 0.0 at the feet\n")

    for mnemonic, clip in clips.items():
        frames = df.clip_frames(clip)
        if len(frames) < 2:
            print(f"  {mnemonic!r:8s} {clip!r:7s} {len(frames)} frame(s): STATIC POSE")
            continue

        # Weight each part's height by how far it travels, so the answer is "where the motion is".
        weighted_height = 0.0
        total_motion = 0.0
        peak_by_third = [0.0, 0.0, 0.0]  # feet-to-hip, hip-to-shoulder, shoulder-to-head
        for part_index in range(len(frames[0])):
            base = frames[0][part_index]
            # 1 - t, because z runs downward.
            height = 1.0 - (((base[0][2] + base[1][2]) * 0.5 - z_min) / span)
            motion = 0.0
            for frame in frames[1:]:
                here = frame[part_index]
                for end in (0, 1):
                    motion = max(motion, max(abs(here[end][axis] - base[end][axis]) for axis in range(3)))
            if motion <= 0:
                continue
            weighted_height += height * motion
            total_motion += motion
            third = 0 if height < 1 / 3 else (1 if height < 2 / 3 else 2)
            peak_by_third[third] = max(peak_by_third[third], motion)

        centre = weighted_height / total_motion if total_motion > 0 else 0.0
        print(f"  {mnemonic!r:8s} {clip!r:7s} {len(frames):2d} frames | "
              f"motion centred at height {centre:.2f} | "
              f"peak travel legs {peak_by_third[0]:4.0f}  torso {peak_by_third[1]:4.0f}  "
              f"head/arms {peak_by_third[2]:4.0f}")


if __name__ == "__main__":
    main()
