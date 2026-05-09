# OpenLitter - Generate PNG launcher icons from the SVG design.
# Copyright (C) 2024 David Lopes (https://github.com/davdlic)
# Licensed under the GNU General Public License v3.0
#
# Run with `python scripts/make_icons.py`. Outputs go into data/.
#
# We re-draw the same shapes that data/icon.svg uses, instead of
# rasterising the SVG, to avoid pulling in cairo/cairosvg on Windows.

import os
from PIL import Image, ImageDraw

OUT_DIR = os.path.join(os.path.dirname(__file__), os.pardir, "data")

# Colours from the SVG
BG       = (15, 16, 32, 255)     # #0f1020
GREEN    = (74, 222, 128, 255)   # #4ade80
BLUE     = (96, 165, 250, 255)   # #60a5fa
YELLOW   = (251, 191, 36, 255)   # #fbbf24


def render(size: int, maskable: bool = False) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    if maskable:
        # Maskable safe zone is the inner 80%; we use 70% to be generous.
        # The full canvas must be filled with the bg colour (no rounded corners).
        draw.rectangle((0, 0, size, size), fill=BG)
        scale = 0.7
    else:
        # Standard icon: rounded square, content fills the canvas.
        radius = int(size * 96 / 512)
        draw.rounded_rectangle((0, 0, size - 1, size - 1), radius=radius, fill=BG)
        scale = 1.0

    cx = cy = size / 2

    def ring(r_native: int, stroke_native: int, color):
        r = int(r_native * size / 512 * scale)
        sw = max(2, int(stroke_native * size / 512 * scale))
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline=color, width=sw)

    def disc(r_native: int, color):
        r = int(r_native * size / 512 * scale)
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=color)

    ring(160, 20, GREEN)   # outer ring
    ring(100, 14, BLUE)    # inner ring (Pillow can't dash; solid is fine for icon)
    disc(22, YELLOW)       # centre dot

    return img.convert("RGB")


def main():
    targets = [
        (180, False, "icon-180.png"),    # iOS apple-touch-icon
        (192, False, "icon-192.png"),    # Android home screen
        (512, False, "icon-512.png"),    # PWA install / splash
        (512, True,  "icon-maskable-512.png"),  # Adaptive Android icon
    ]
    out_dir = os.path.abspath(OUT_DIR)
    os.makedirs(out_dir, exist_ok=True)
    for size, maskable, name in targets:
        path = os.path.join(out_dir, name)
        render(size, maskable).save(path, "PNG", optimize=True)
        print(f"  {name:28s} {os.path.getsize(path):>5d} bytes")


if __name__ == "__main__":
    main()
