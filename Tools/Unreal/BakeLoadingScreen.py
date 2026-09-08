#!/usr/bin/env python3
"""Losslessly bake the user-provided HRGLASS.SMK for the threaded Slate loader."""
import argparse
import struct
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=root / 'Reference/SimCopterOriginalGame/smk/hrglass.smk')
args = parser.parse_args()
header = struct.unpack('<4s4i', args.source.read_bytes()[:20])
if header != (b'SMK2', 300, 200, 75, -10000):
    raise SystemExit(f'Unexpected HRGLASS header: {header}')
output = root / 'SimCopterRemake/Content/Generated/Loading/HRGLASS.png'
output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(['ffmpeg', '-y', '-v', 'error', '-i', str(args.source),
                '-vf', 'tile=10x8:nb_frames=75', '-frames:v', '1', str(output)], check=True)
print(f'Baked 75 original 300x200 frames at 100 ms/frame to {output}')
