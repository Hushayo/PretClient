"""Render installer/icon.svg (PretClient mark) to PNG + ICO via PIL.

Usage: python3 installer/make_icon.py
Reads:  installer/icon.svg  (kept for reference; geometry is redrawn below)
Writes: PretClient/icon.ico (exe icon via PretClient.rc)
        installer/icon.ico  (Inno SetupIconFile)
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

S = 1024
SC = S / 120.0

INDIGO = (0x43, 0x38, 0xCA, 255)
WHITE = (255, 255, 255, 255)
AMBER = (0xFB, 0xBF, 0x24, 255)


def pt(x, y):
    return (int(x * SC), int(y * SC))


def main():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=int(28 * SC), fill=INDIGO)

    w = int(12 * SC)
    # Stem + top bar (round joint), bowl arc, mid bar.
    d.line([pt(44, 90), pt(44, 32), pt(66, 32)], fill=WHITE, width=w, joint="curve")
    cx, cy, r = 66 * SC, 51 * SC, 19 * SC
    d.arc([cx - r, cy - r, cx + r, cy + r], start=270, end=90, fill=WHITE, width=w)
    d.line([pt(66, 70), pt(44, 70)], fill=WHITE, width=w, joint="curve")
    # Round caps + joins.
    for jx, jy in [(44, 90), (44, 32), (66, 32), (66, 70), (44, 70)]:
        x, y = pt(jx, jy)
        d.ellipse([x - w // 2, y - w // 2, x + w // 2, y + w // 2], fill=WHITE)
    # Amber dot.
    dx, dy, dr = 82 * SC, 88 * SC, 8 * SC
    d.ellipse([dx - dr, dy - dr, dx + dr, dy + dr], fill=AMBER)

    img = img.resize((256, 256), Image.LANCZOS)

    # Sanity: must not be blank, must contain amber pixels.
    small = img.resize((32, 32)).convert("RGB")
    colors = small.getcolors(32 * 32)
    print("distinct colors:", len(colors))
    amberish = sum(n for n, c in colors if c[0] > 200 and c[1] > 150 and c[2] < 100)
    print("amber pixels (32px):", amberish)
    assert len(colors) > 4 and amberish > 0, "icon looks wrong, aborting"

    exe_ico = os.path.join(REPO, "PretClient", "icon.ico")
    setup_ico = os.path.join(HERE, "icon.ico")
    img.save(exe_ico, sizes=[(256, 256), (64, 64), (48, 48), (32, 32), (16, 16)])
    img.save(setup_ico, sizes=[(256, 256), (64, 64), (48, 48), (32, 32), (16, 16)])
    print("wrote", exe_ico, os.path.getsize(exe_ico))
    print("wrote", setup_ico, os.path.getsize(setup_ico))


if __name__ == "__main__":
    main()
