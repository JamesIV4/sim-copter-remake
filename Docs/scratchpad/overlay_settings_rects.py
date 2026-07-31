"""Scratch: draw the decoded Settings-screen geometry back over the original page art.

Every rectangle here came out of the assembly, not off the bitmap:

  FUN_00437d10   playmenu.bmp page descriptor (item x/y/stride/font, the two colours)
  FUN_0043f7c0   sound.bmp    control rects  (dialog 0x7d6)
  FUN_00440370   cityset.bmp  control rects  (dialog 0x7d8)
  FUN_0043df80   render.bmp   control rects  (dialog 0x7d5)

If the boxes land on the printed furniture the decode is right - same check the front-end port
used (overlay_mainmenu_rects.py).

    python overlay_settings_rects.py
"""
import os

from PIL import Image, ImageDraw

BMP = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP"
OUT = r"S:\Repos\sim-copter-remake\Docs\scratchpad\settings-art"

TEXT = (255, 220, 64)
SLIDER = (64, 255, 255)
BUTTON = (255, 96, 96)
TOGGLE = (128, 255, 128)

# FUN_00437d10's descriptor. Two variants: DAT_00518d50 == 1 shows eight items from string 60,
# otherwise seven from string 61 - the extra "City Settings" row is added at the TOP.
PLAYMENU_ITEM_X = 0x66          # 102
PLAYMENU_STRIDE = 0x28          # 40
PLAYMENU_FONT = 0x1A            # 26
PLAYMENU = [(0x40, 8), (0x68, 7)]   # (first item y, item count)

SOUND = [
    ("text", 150, 368, 280, 382),
    ("text", 110, 287, 186, 301),
    ("text", 192, 287, 230, 301),
    ("text", 238, 287, 316, 301),
    ("text", 348, 287, 382, 300),
    ("toggle", 108, 253, 109, 254),
    ("toggle", 196, 253, 197, 254),
    ("toggle", 286, 253, 287, 255),
    ("slider", 120, 334, 312, 366),
    ("slider", 350, 78, 382, 270),
    ("slider", 393, 91, 439, 279),
    ("button", 334, 331, 434, 359),
    ("button", 334, 359, 434, 387),
]

CITYSET = [
    ("text", 42, 47, 128, 70),
    ("text", 52, 327, 136, 350),
    ("text", 179, 327, 265, 350),
    ("text", 187, 47, 273, 69),
    ("text", 314, 47, 400, 69),
    ("text", 323, 327, 408, 350),
    ("text", 452, 327, 536, 350),
    ("text", 461, 47, 544, 70),
] + [("slider", x, 96, x + 26, 298) for x in (42, 111, 179, 248, 316, 385, 454, 522)] + [
    ("button", 130, 376, 230, 404),
    ("button", 364, 376, 464, 404),
]

RENDER = [
    ("text", 138, 70, 276, 90),
    ("text", 138, 112, 276, 132),
    ("text", 138, 156, 276, 176),
    ("text", 74, 293, 252, 316),
    ("text", 74, 322, 252, 342),
    ("text", 74, 352, 252, 372),
    ("text", 76, 245, 138, 261),
    ("text", 222, 245, 288, 261),
    ("text", 142, 245, 218, 261),
    ("toggle", 78, 64, 81, 67),
    ("toggle", 78, 104, 81, 107),
    ("toggle", 78, 146, 81, 149),
    ("slider", 72, 213, 75, 216),
    ("button", 328, 318, 428, 346),
    ("button", 432, 318, 532, 346),
]

COLOUR = {"text": TEXT, "slider": SLIDER, "button": BUTTON, "toggle": TOGGLE}


def draw_page(name, controls, extra=None):
    page = Image.open(os.path.join(BMP, name)).convert("RGB")
    canvas = Image.new("RGB", (page.width + 40, page.height + 40), (24, 24, 28))
    canvas.paste(page, (20, 20))
    draw = ImageDraw.Draw(canvas)

    for (kind, l, t, r, b) in controls:
        draw.rectangle([l + 20, t + 20, r + 20, b + 20], outline=COLOUR[kind])

    if extra is not None:
        extra(draw)

    out = os.path.join(OUT, os.path.splitext(name)[0] + "-overlay.png")
    canvas.save(out)
    print("wrote", out, page.size)


def playmenu_items(draw):
    for (y0, count) in PLAYMENU:
        for i in range(count):
            y = y0 + PLAYMENU_STRIDE * i
            draw.rectangle([PLAYMENU_ITEM_X + 20, y + 20, PLAYMENU_ITEM_X + 220 + 20,
                            y + PLAYMENU_FONT + 20], outline=TEXT)


os.makedirs(OUT, exist_ok=True)
draw_page("PLAYMENU.BMP", [], playmenu_items)
draw_page("SOUND.BMP", SOUND)
draw_page("CITYSET.BMP", CITYSET)
draw_page("RENDER.BMP", RENDER)
