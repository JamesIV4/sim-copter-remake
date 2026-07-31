"""Scratch: draw the decoded career/city-select geometry over CAREER.BMP and CARSEL.BMP.

  FUN_00457c90   the three city panel rects (screen[0x1e..0x29])
  FUN_004580b0   OK/Cancel origins and the two readout rects
  FUN_004590b0   the four highlight strips per panel, and FUN_00458e70's +0x168 (360) shift
                 that picks the unselected copy out of the lower half of CARSEL.BMP
"""
from PIL import Image, ImageDraw

BMP = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP"
OUT = r"S:\Repos\sim-copter-remake\Docs\scratchpad\mainmenu-art"

PANEL = [
    (0x4D, 0x47, 0x115, 0xB3),   # (77,71)-(277,179)
    (0x153, 0x47, 0x21B, 0xB3),  # (339,71)-(539,179)
    (0x4D, 0xF9, 0x115, 0x165),  # (77,249)-(277,357)
]
NAME_RECT = (0x14E, 0xEC, 0x216, 0x106)   # (334,236)-(534,262)  string 240 + city
LEVEL_RECT = (0x14E, 0x10F, 0x216, 0x129)  # (334,271)-(534,297) string 290 + level
BUTTON_W, BUTTON_H = 100, 28
OK_XY, CANCEL_XY, OK_ONLY_XY = (0x147, 0x152), (0x1AF, 0x152), (0x17C, 0x152)

# FUN_004590b0, per panel: left / top / right / bottom strips.
HILITE = [
    [(0x36, 0x33, 0x4D, 0xD8), (0x4D, 0x33, 0x114, 0x45), (0x114, 0x33, 0x131, 0xD8), (0x4D, 0xB4, 0x114, 0xD8)],
    [(0x138, 0x33, 0x153, 0xD8), (0x153, 0x33, 0x219, 0x45), (0x219, 0x33, 0x22C, 0xD8), (0x153, 0xB4, 0x22C, 0xD8)],
    [(0x36, 0xD9, 0x4D, 0x17E), (0x4D, 0xD9, 0x114, 0xF8), (0x114, 0xD9, 0x131, 0x17E), (0x4D, 0x167, 0x114, 0x17E)],
]
UNSELECTED_SOURCE_DY = 0x168


def draw_on(path, out_name, rects_by_colour, pad=10):
    page = Image.open(path).convert("RGB")
    canvas = Image.new("RGB", (page.width + pad * 2, page.height + pad * 2), (24, 24, 28))
    canvas.paste(page, (pad, pad))
    draw = ImageDraw.Draw(canvas)
    for colour, rects in rects_by_colour:
        for (l, t, r, b) in rects:
            draw.rectangle([l + pad, t + pad, r + pad, b + pad], outline=colour)
    canvas.save(f"{OUT}/{out_name}")
    print("wrote", out_name)


buttons = [
    (OK_XY[0], OK_XY[1], OK_XY[0] + BUTTON_W, OK_XY[1] + BUTTON_H),
    (CANCEL_XY[0], CANCEL_XY[1], CANCEL_XY[0] + BUTTON_W, CANCEL_XY[1] + BUTTON_H),
    (OK_ONLY_XY[0], OK_ONLY_XY[1], OK_ONLY_XY[0] + BUTTON_W, OK_ONLY_XY[1] + BUTTON_H),
]
draw_on(f"{BMP}/CAREER.BMP", "CAREER-overlay.png", [
    ((255, 64, 64), PANEL),
    ((64, 255, 255), [NAME_RECT, LEVEL_RECT]),
    ((255, 220, 0), buttons),
])

# On CARSEL the selected copy sits at the page's own coordinates and the unselected copy 360 rows
# lower, so both sets should land on printed frames.
flat = [r for panel in HILITE for r in panel]
shifted = [(l, t + UNSELECTED_SOURCE_DY, r, b + UNSELECTED_SOURCE_DY) for (l, t, r, b) in flat]
draw_on(f"{BMP}/CARSEL.BMP", "CARSEL-overlay.png", [
    ((64, 255, 128), flat),
    ((255, 64, 255), shifted),
])
