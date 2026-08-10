#!/usr/bin/env python3
"""Transcode the original CITY<N>_S.SMK career previews for Unreal playback.

The user-provided Smacker files remain gitignored. The output preserves each preview's
200x108 image, 75 frames, 71 ms cadence, and 5.325-second loop.
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
from pathlib import Path


EXPECTED_SIGNATURE = b"SMK2"
EXPECTED_WIDTH = 200
EXPECTED_HEIGHT = 108
EXPECTED_FRAMES = 75
EXPECTED_FRAME_TIME = -7100
EXPECTED_RATE = "1000/71"
EXPECTED_DURATION = "5.325000"
CITY_COUNT = 30


def find_program(requested: str, name: str) -> str:
	resolved = shutil.which(requested)
	if resolved is None:
		raise SystemExit(f"Could not find {name} ('{requested}') on PATH.")
	return resolved


def source_files(source_dir: Path) -> dict[str, Path]:
	if not source_dir.is_dir():
		raise SystemExit(f"Career movie folder does not exist: {source_dir}")
	return {path.name.lower(): path for path in source_dir.iterdir() if path.is_file()}


def validate_source(path: Path) -> None:
	if not path.is_file() or path.stat().st_size < 20:
		raise SystemExit(f"Missing original career preview: {path}")
	signature, width, height, frames, frame_time = struct.unpack_from("<4s4i", path.read_bytes()[:20])
	actual = (signature, width, height, frames, frame_time)
	expected = (EXPECTED_SIGNATURE, EXPECTED_WIDTH, EXPECTED_HEIGHT, EXPECTED_FRAMES, EXPECTED_FRAME_TIME)
	if actual != expected:
		raise SystemExit(f"Unexpected career preview header in {path.name}: {actual!r}; expected {expected!r}")


def validate_output(ffprobe: str, path: Path) -> None:
	result = subprocess.run(
		[
			ffprobe,
			"-v", "error",
			"-count_frames",
			"-select_streams", "v:0",
			"-show_entries", "stream=codec_name,width,height,r_frame_rate,nb_read_frames,duration",
			"-of", "default=noprint_wrappers=1",
			str(path),
		],
		check=True,
		capture_output=True,
		text=True,
	)
	values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
	expected = {
		"codec_name": "h264",
		"width": str(EXPECTED_WIDTH),
		"height": str(EXPECTED_HEIGHT),
		"r_frame_rate": EXPECTED_RATE,
		"nb_read_frames": str(EXPECTED_FRAMES),
		"duration": EXPECTED_DURATION,
	}
	wrong = {key: (values.get(key), wanted) for key, wanted in expected.items() if values.get(key) != wanted}
	if wrong:
		raise SystemExit(f"Baked movie failed verification ({path.name}): {wrong}")


def main() -> None:
	repo_root = Path(__file__).resolve().parents[2]
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument(
		"--source-dir",
		type=Path,
		default=repo_root / "Reference" / "SimCopterOriginalGame" / "SMK",
	)
	parser.add_argument(
		"--output-dir",
		type=Path,
		default=repo_root / "SimCopterRemake" / "Content" / "Generated" / "Movies" / "Career",
	)
	parser.add_argument("--ffmpeg", default="ffmpeg")
	parser.add_argument("--ffprobe", default="ffprobe")
	args = parser.parse_args()

	ffmpeg = find_program(args.ffmpeg, "FFmpeg")
	ffprobe = find_program(args.ffprobe, "FFprobe")
	available = source_files(args.source_dir.resolve())
	output_dir = args.output_dir.resolve()
	output_dir.mkdir(parents=True, exist_ok=True)

	for city in range(CITY_COUNT):
		file_name = f"CITY{city}_S.SMK"
		source = available.get(file_name.lower())
		if source is None:
			raise SystemExit(f"Missing original career preview: {args.source_dir / file_name}")
		validate_source(source)

		output = output_dir / f"CITY{city}_S.mp4"
		temporary = output.with_name(f"{output.stem}.tmp{output.suffix}")
		try:
			subprocess.run(
				[
					ffmpeg,
					"-hide_banner", "-loglevel", "error", "-y",
					"-i", str(source),
					"-map", "0:v:0", "-an",
					"-c:v", "libx264", "-preset", "slow", "-crf", "10",
					"-pix_fmt", "yuv420p", "-movflags", "+faststart",
					str(temporary),
				],
				check=True,
			)
			validate_output(ffprobe, temporary)
			temporary.replace(output)
		finally:
			if temporary.exists():
				temporary.unlink()

	print(f"Baked {CITY_COUNT} authentic career preview loops into {output_dir}")


if __name__ == "__main__":
	main()
