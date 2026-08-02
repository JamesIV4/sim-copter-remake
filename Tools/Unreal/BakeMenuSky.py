#!/usr/bin/env python3
"""Transcode the original MENUSKY.SMK into UE's generated media folder.

The original file stays user-provided and gitignored. This changes only the container/codec:
the 640x480 image, 201 frames, 71 ms frame cadence, and 14.271 s loop are preserved.
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
from pathlib import Path


EXPECTED_SIGNATURE = b"SMK2"
EXPECTED_WIDTH = 640
EXPECTED_HEIGHT = 480
EXPECTED_FRAMES = 201
EXPECTED_FRAME_TIME = -7100  # Smacker: negative hundred-thousandths -> 71 ms.
EXPECTED_RATE = "1000/71"
EXPECTED_DURATION = "14.271000"


def validate_source(path: Path) -> None:
	if not path.is_file() or path.stat().st_size < 20:
		raise SystemExit(
			f"{path} is missing or is the zero-byte installed CD stub. "
			"Pass --source pointing to MENUSKY.SMK from the original SimCopter CD."
		)
	header = path.read_bytes()[:20]
	signature = header[:4]
	width, height, frames, frame_time = struct.unpack_from("<4i", header, 4)
	actual = (signature, width, height, frames, frame_time)
	expected = (
		EXPECTED_SIGNATURE,
		EXPECTED_WIDTH,
		EXPECTED_HEIGHT,
		EXPECTED_FRAMES,
		EXPECTED_FRAME_TIME,
	)
	if actual != expected:
		raise SystemExit(f"Unexpected MENUSKY.SMK header: {actual!r}; expected {expected!r}")


def find_program(requested: str, name: str) -> str:
	resolved = shutil.which(requested)
	if resolved is None:
		raise SystemExit(f"Could not find {name} ('{requested}') on PATH.")
	return resolved


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
		raise SystemExit(f"Baked movie failed verification: {wrong}")


def main() -> None:
	repo_root = Path(__file__).resolve().parents[2]
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument(
		"--source",
		type=Path,
		default=repo_root / "Reference" / "SimCopterOriginalGame" / "SMK" / "MENUSKY.SMK",
	)
	parser.add_argument(
		"--output",
		type=Path,
		default=repo_root / "SimCopterRemake" / "Content" / "Generated" / "Movies" / "MENUSKY.mp4",
	)
	parser.add_argument("--ffmpeg", default="ffmpeg")
	parser.add_argument("--ffprobe", default="ffprobe")
	args = parser.parse_args()

	source = args.source.resolve()
	output = args.output.resolve()
	validate_source(source)
	ffmpeg = find_program(args.ffmpeg, "FFmpeg")
	ffprobe = find_program(args.ffprobe, "FFprobe")
	output.parent.mkdir(parents=True, exist_ok=True)
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

	print(f"Baked authentic 201-frame MENUSKY loop: {source} -> {output}")


if __name__ == "__main__":
	main()
