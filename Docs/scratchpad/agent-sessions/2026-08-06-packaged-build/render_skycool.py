#!/usr/bin/env python3
"""Render SKYCOOL.BMP the way the front end's missing-movie fallback does.

The fallback in SSimCopterMainMenu stretches the whole 400x66 strip over the full viewport, so
this reproduces what a packaged build shows when Content/Movies/MENUSKY.mp4 is not staged.
"""

from pathlib import Path

from PIL import Image

SOURCE = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\bmp\skycool.bmp")
OUT = Path(__file__).with_name("skycool-stretched.png")

image = Image.open(SOURCE).convert("RGB")
print(f"{SOURCE.name}: {image.size}")
image.resize((1280, 720), Image.NEAREST).save(OUT)
print(f"wrote {OUT}")
