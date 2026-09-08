#!/usr/bin/env python3
"""Transcode user-provided INTRO1/2.SMK, preserving frames, timing and intro2 audio."""
import argparse
import json
import struct
import subprocess
from pathlib import Path
from fractions import Fraction

ROOT = Path(__file__).resolve().parents[2]


def probe(path):
    return json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-count_frames', '-show_streams', '-of', 'json', str(path)
    ]))['streams']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'Reference/SimCopterOriginalGame/smk')
    parser.add_argument('--output', type=Path, default=ROOT / 'SimCopterRemake/Content/Generated/Movies/Intro')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for index, expected in [(1, (640, 280, 202, -5000)), (2, (460, 200, 838, -5000))]:
        source = args.source / f'intro{index}.smk'
        header = source.read_bytes()[:20]
        if header[:4] != b'SMK2' or struct.unpack_from('<4i', header, 4) != expected:
            raise SystemExit(f'Unexpected original movie header: {source}: {header!r}')
        output = args.output / f'INTRO{index}.mp4'
        temporary = output.with_suffix('.tmp.mp4')
        try:
            subprocess.run([
                'ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(source),
                '-map', '0:v:0', '-map', '0:a?', '-c:v', 'libx264', '-crf', '10',
                '-preset', 'slow', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '192k',
                '-ar', '44100', '-movflags', '+faststart', str(temporary)
            ], check=True)
            streams = probe(temporary)
            video = next(s for s in streams if s['codec_type'] == 'video')
            assert (video['width'], video['height'], int(video['nb_read_frames'])) == expected[:3]
            assert Fraction(video['r_frame_rate']) == 20
            assert abs(float(video['duration']) - expected[2] / 20) < .001
            audio = [s for s in streams if s['codec_type'] == 'audio']
            assert len(audio) == index - 1
            if audio:
                assert audio[0]['channels'] == 2
                assert abs(float(audio[0]['duration']) - 41.9) < .15
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)
        print(f'Verified {output}: {expected[2]} frames at 20 fps; {len(audio)} audio tracks')


if __name__ == '__main__':
    main()
