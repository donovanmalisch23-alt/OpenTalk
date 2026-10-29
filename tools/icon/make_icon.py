#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Draws OpenTalk's application icon: a white speech bubble with sound bars on blue.

The icon is original to OpenTalk (no third-party artwork). It is drawn large and scaled
down, so every size is smooth. Run from the repository root after changing it:

    python3 tools/icon/make_icon.py

Writes src/app/resources/opentalk.ico (for the Windows program and installer) and
PNG files for the window icon. Needs Pillow.
"""

from pathlib import Path

from PIL import Image, ImageDraw

BLUE = (30, 90, 168, 255)  # #1E5AA8: white on it has a contrast ratio of about 6.7:1
WHITE = (255, 255, 255, 255)
SIZE = 1024
OUT = Path(__file__).resolve().parents[2] / "src" / "app" / "resources"


def draw() -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    # Background: a rounded square.
    d.rounded_rectangle((32, 32, SIZE - 32, SIZE - 32), radius=200, fill=BLUE)
    # Speech bubble with a tail at the bottom left.
    d.rounded_rectangle((176, 200, 848, 704), radius=150, fill=WHITE)
    d.polygon([(300, 640), (450, 690), (250, 850)], fill=WHITE)
    # Sound bars in the bubble.
    centre = 452
    for x, height in ((330, 150), (430, 270), (530, 210), (630, 110)):
        d.rounded_rectangle((x, centre - height // 2, x + 64, centre + height // 2), radius=32, fill=BLUE)
    return image


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    big = draw()
    big.save(OUT / "opentalk.ico", sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
    for size in (16, 32, 48, 256):
        big.resize((size, size), Image.LANCZOS).save(OUT / f"opentalk-{size}.png", optimize=True)
    print(f"Wrote the icon files to {OUT}")


if __name__ == "__main__":
    main()
