"""Scratch: draw the decoded main-menu geometry back over MAIN1.BMP.

Everything here comes out of the decompile, not out of the bitmap:

  FUN_00411900   page descriptor  -> item x/y/stride/font, the two text colours
  FUN_0045fe10   MAIN4.BMP (the round LEDs) blit table
  FUN_0045fed0   MAIN5.BMP (the left arrow keys) blit table
  FUN_0045f3d0   MAIN2/MAIN3 hose decorations, both 1x1 "size from the bitmap"

If the boxes land on the printed furniture the decode is right.
"""
from PIL import Image, ImageDraw

BMP = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP"
OUT = r"S:\Repos\sim-copter-remake\Docs\scratchpad\mainmenu-art\MAIN1-overlay.png"

# FUN_00411900's descriptor, non-hi-res branch values in brackets.
ITEM_X = 0x74           # 116
ITEM_Y0 = 0x2a          # 42   (0x2d = 45 when DAT_004f86d4 is not 1/2)
ITEM_STRIDE = 0x40      # 64
ITEM_FONT = 0x1a        # 26   (0x14 = 20 otherwise)
ITEM_COUNT = 5

# FUN_0045fe10 - dest (x, y) then src (l, t, r, b) in MAIN4.BMP.
LED = [
    (0x14e, 0x01f, (0, 0x000, 0x3c, 0x038)),
    (0x14e, 0x057, (0, 0x038, 0x3c, 0x078)),
    (0x14e, 0x097, (0, 0x078, 0x3c, 0x0b8)),
    (0x14e, 0x0d7, (0, 0x0b8, 0x3c, 0x0f8)),
    (0x14e, 0x117, (0, 0x0f8, 0x3c, 0x138)),
]

# FUN_0045fed0 - dest (x, y) then src (l, t, r, b) in MAIN5.BMP.
ARROW = [
    (0x21, 0x023, (0, 0x000, 0x27, 0x041)),
    (0x21, 0x064, (0, 0x041, 0x27, 0x081)),
    (0x21, 0x0a4, (0, 0x081, 0x27, 0x0c0)),
    (0x21, 0x0e3, (0, 0x0c0, 0x27, 0x0fe)),
    (0x21, 0x121, (0, 0x0fe, 0x27, 0x129)),
]

# FUN_0045fc60's hit test: x in (0x1d, 0x18a), y inside the item's own rect.
HIT_LEFT, HIT_RIGHT = 0x1d, 0x18a

page = Image.open(f"{BMP}/MAIN1.BMP").convert("RGB")
canvas = Image.new("RGB", (page.width + 40, page.height + 40), (24, 24, 28))
canvas.paste(page, (20, 20))
draw = ImageDraw.Draw(canvas)


def box(x0, y0, x1, y1, colour):
    draw.rectangle([x0 + 20, y0 + 20, x1 + 20, y1 + 20], outline=colour)


for i in range(ITEM_COUNT):
    y = ITEM_Y0 + ITEM_STRIDE * i
    box(HIT_LEFT, y, HIT_RIGHT, y + ITEM_FONT, (255, 64, 64))
    draw.line([(ITEM_X + 20, y + 20), (ITEM_X + 20, y + 20 + ITEM_FONT)], fill=(255, 255, 0))

for (dx, dy, (sl, st, sr, sb)) in LED:
    box(dx, dy, dx + (sr - sl), dy + (sb - st), (64, 255, 255))

for (dx, dy, (sl, st, sr, sb)) in ARROW:
    box(dx, dy, dx + (sr - sl), dy + (sb - st), (128, 255, 128))

canvas.save(OUT)
print("wrote", OUT)
