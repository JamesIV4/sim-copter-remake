"""Composite the tuner needle onto DASH4.BMP at every station detent.

Reproduces the exact mapping SSimCopterRadioTuner paints with, so the result shows whether a
tuned station lines up with a printed frequency without launching the game.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from bmp2png import read_bmp, write_png  # noqa: E402

SRC = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\DASH4.BMP"
DST = r"S:\Repos\sim-copter-remake\Docs\scratchpad\sound\radio\dash4_needle_preview.png"

# Must match SSimCopterDashboard.cpp.
SCALE_X, SCALE_Y = 17.0, 24.0
SCALE_W, SCALE_H = 76.0, 7.0
DIAL_FIRST_X = 22.0 - SCALE_X
DIAL_LAST_X = 84.0 - SCALE_X

STATIONS = ["kcla", "Kjaz", "KMIX", "kroc", "ktec"]
NEEDLE = (255, 41, 31)
ZOOM = 8


def needle_page_x(index, count):
    alpha = 0.5 if count == 1 else index / (count - 1)
    return SCALE_X + DIAL_FIRST_X + alpha * (DIAL_LAST_X - DIAL_FIRST_X)


def main():
    w, h, pal, rows = read_bmp(SRC)
    crop_x, crop_w = 8, 100
    count = len(STATIONS)

    positions = [needle_page_x(i, count) for i in range(count)]
    print("needle x per station (page pixels):")
    for name, x in zip(STATIONS, positions):
        print(f"  {name:6} x={x:6.2f}")
    print("printed label centres (measured): 22, 38, 53, 69, 84")

    # One band per station, stacked, so all five detents can be compared at once.
    out = []
    for index in range(count):
        nx = int(round(positions[index]))
        for y in range(h):
            line = []
            for x in range(crop_x, crop_x + crop_w):
                on_needle = (x == nx and SCALE_Y <= y <= SCALE_Y + SCALE_H - 1)
                rgb = NEEDLE if on_needle else pal[rows[y][x]]
                line.extend(rgb)
            for _ in range(ZOOM):
                out.append([v for x in range(crop_x, crop_x + crop_w)
                            for _ in range(ZOOM)
                            for v in (NEEDLE if (x == nx and SCALE_Y <= y <= SCALE_Y + SCALE_H - 1)
                                      else pal[rows[y][x]])])
        # Separator row.
        for _ in range(2):
            out.append([0, 0, 0] * (crop_w * ZOOM))

    write_png(DST, crop_w * ZOOM, len(out), out)
    print(f"wrote {DST}")


if __name__ == "__main__":
    main()
