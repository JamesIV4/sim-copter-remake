#!/usr/bin/env python3
"""Mock the front end's no-artwork message box to check composition and contrast.

Not a Slate render - it just lays out the same page-local rectangles with the same colours, so a
geometry or contrast mistake shows up without launching the game. Numbers mirror
SSimCopterMessageBox.cpp / SimCopterFrontEndPage.cpp.
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SCREEN = (640, 480)
PLATE_W = 430.0
PLATE_PADDING = 26.0
PLATE_BUTTON_GAP = 14.0
ACCENT_H = 5.0
FRAME = 3.0  # 2px edge + 1px bevel
BUTTON_W, BUTTON_H = 100.0, 28.0


def srgb(linear):
    return tuple(round(c * 255) for c in linear)


PLATE_EDGE = srgb((0.31, 0.33, 0.19))
PLATE_BEVEL = srgb((0.20, 0.21, 0.19))
PLATE_FILL = srgb((0.075, 0.080, 0.070))
PLATE_ACCENT = (0x80, 0x85, 0x4A)
PLATE_TEXT = (0xEA, 0xEF, 0x9A)
BUTTON_TEXT = srgb((0.94, 0.94, 0.90))

MESSAGE = (
    "Cannot find cities/career/city0.sc2.\n"
    "\n"
    "The original SimCopter game files have to be in place.\n"
    "Put them in:\n"
    "S:/SimCopter Stuff/Builds/pre-release 0.8/Windows/SimCopter"
)


def load_font(size, bold=False):
    name = "arialbd.ttf" if bold else "arial.ttf"
    try:
        return ImageFont.truetype(name, size)
    except OSError:
        return ImageFont.load_default(size)


def wrap(draw, text, font, width):
    lines = []
    for paragraph in text.split("\n"):
        if not paragraph:
            lines.append("")
            continue
        current = ""
        for word in paragraph.split(" "):
            trial = f"{current} {word}".strip()
            if draw.textlength(trial, font=font) <= width or not current:
                current = trial
            else:
                lines.append(current)
                current = word
        # Per-character wrapping, for the path that has no spaces to break at.
        while draw.textlength(current, font=font) > width:
            cut = len(current)
            while cut > 1 and draw.textlength(current[:cut], font=font) > width:
                cut -= 1
            lines.append(current[:cut])
            current = current[cut:]
        lines.append(current)
    return lines


image = Image.new("RGB", SCREEN, (44, 62, 74))
draw = ImageDraw.Draw(image)

# Font height 18 -> 13.5pt, per WindowsHeightToSlatePoints.
body = load_font(14)
label = load_font(11, bold=True)

well_w = PLATE_W - 2 * FRAME - 2 * PLATE_PADDING
lines = wrap(draw, MESSAGE, body, well_w)
line_h = round(body.size * 1.2 * 1.2)
block_h = line_h * len(lines)

# The vertical box, slot by slot: accent, text (padded), rule, buttons (padded).
text_slot = PLATE_PADDING + block_h + PLATE_PADDING * 0.8
button_slot = PLATE_BUTTON_GAP + BUTTON_H + PLATE_PADDING * 0.7
plate_h = 2 * FRAME + ACCENT_H + text_slot + 1 + button_slot

px = round((SCREEN[0] - PLATE_W) / 2)
py = round((SCREEN[1] - plate_h) / 2)

draw.rectangle([px, py, px + PLATE_W, py + plate_h], fill=PLATE_EDGE)
draw.rectangle([px + 2, py + 2, px + PLATE_W - 2, py + plate_h - 2], fill=PLATE_BEVEL)
draw.rectangle([px + FRAME, py + FRAME, px + PLATE_W - FRAME, py + plate_h - FRAME], fill=PLATE_FILL)
draw.rectangle([px + FRAME, py + FRAME, px + PLATE_W - FRAME, py + FRAME + ACCENT_H], fill=PLATE_ACCENT)

y = py + FRAME + ACCENT_H + PLATE_PADDING
for line in lines:
    w = draw.textlength(line, font=body)
    draw.text((px + (PLATE_W - w) / 2, y), line, font=body, fill=PLATE_TEXT)
    y += line_h

rule_y = py + FRAME + ACCENT_H + text_slot
draw.rectangle(
    [px + FRAME + PLATE_PADDING, rule_y, px + PLATE_W - FRAME - PLATE_PADDING, rule_y],
    fill=PLATE_BEVEL,
)

bx = px + (PLATE_W - BUTTON_W) / 2
by = rule_y + 1 + PLATE_BUTTON_GAP
draw.rectangle([bx, by, bx + BUTTON_W, by + BUTTON_H], fill=PLATE_BEVEL)
lw = draw.textlength("OK", font=label)
draw.text((bx + (BUTTON_W - lw) / 2, by + 7), "OK", font=label, fill=BUTTON_TEXT)

out = Path(__file__).with_name("message-box-fallback-mock.png")
image.resize((SCREEN[0] * 2, SCREEN[1] * 2), Image.LANCZOS).save(out)
print(f"{len(lines)} wrapped lines -> plate {PLATE_W:.0f}x{plate_h:.0f} in a 640x480 screen")
print(f"wrote {out}")
