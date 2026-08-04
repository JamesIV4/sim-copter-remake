"""Measure the SKYDARK night atlas pages so the emissive window mask can be given a defensible
threshold instead of a guessed one.

For each page the original swaps (FUN_004606d0), prints the luminance histogram of the night art
and how much of the page a given emissive threshold would light up.

    python Docs/scratchpad/analyse_night_windows.py
"""

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BMP = ROOT / "Reference" / "SimCopterOriginalGame" / "BMP"
GEO = ROOT / "Reference" / "SimCopterOriginalGame" / "GEO"

IMAGE_TO_ATLAS_PAGE = {1: 2, 2: 39, 3: 40, 4: 20, 5: 13}
THRESHOLDS = (0.35, 0.45, 0.55, 0.65, 0.75)


def read_palette(path):
    d = path.read_bytes()
    off = struct.unpack_from("<I", d, 57)[0]
    return [tuple(d[off + i * 3: off + i * 3 + 3]) for i in range(256)]


def decode_composite(path):
    d = path.read_bytes()
    image_count = struct.unpack_from("<i", d, 8)[0]
    resolution_count = struct.unpack_from("<i", d, 12)[0]
    cursor = 16 + resolution_count * 12
    images = []
    for _ in range(image_count):
        w, h, _unk = struct.unpack_from("<iii", d, cursor)
        row_table = cursor + 12
        data_offset = row_table + h * 4
        idx = bytearray(w * h)
        for row in range(h):
            row_offset = struct.unpack_from("<i", d, row_table + row * 4)[0]
            base = data_offset + row_offset
            idx[(h - 1 - row) * w:(h - 1 - row) * w + w] = d[base:base + w]
        images.append((w, h, bytes(idx)))
        cursor = data_offset + w * h
    return images


def luminance(rgb):
    r, g, b = rgb
    return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0


def main():
    palette = read_palette(GEO / "sim3d1.max")
    lum = [luminance(c) for c in palette]

    day = decode_composite(BMP / "SKY.BMP")
    night = decode_composite(BMP / "SKYDARK.BMP")

    for image_index, page in sorted(IMAGE_TO_ATLAS_PAGE.items()):
        _w, _h, di = day[image_index]
        _w, _h, ni = night[image_index]

        night_lum = [lum[i] for i in ni]
        day_lum = [lum[i] for i in di]
        n = len(night_lum)

        buckets = [0] * 10
        for value in night_lum:
            buckets[min(int(value * 10), 9)] += 1

        print(f"=== atlas page {page} (SKYDARK image {image_index}) ===")
        print("  night luminance histogram (deciles): "
              + " ".join(f"{100.0 * b / n:5.1f}%" for b in buckets))
        print(f"  night mean luminance {sum(night_lum) / n:.3f}, "
              f"day mean luminance {sum(day_lum) / n:.3f}")
        for t in THRESHOLDS:
            above = sum(1 for value in night_lum if value > t)
            brighter = sum(1 for a, b in zip(night_lum, day_lum) if a > t and a > b)
            print(f"  lum > {t:.2f}: {100.0 * above / n:5.2f}% of page "
                  f"({100.0 * brighter / n:5.2f}% also brighter than the day art)")
        print()


if __name__ == "__main__":
    main()
