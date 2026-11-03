#!/usr/bin/env python3
"""make_icon.py OUT_DIR: xbased's icon (xbased.ico, 16-256 px) and logo
(logo.png, a wordmark beside the orb) drawn with Pillow: a glowing green
orb with three tilted orbit rings, as the dashboard draws it (ui.c ui_orb)."""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont


def orb(size):
    s = size * 4                      # drawn large, scaled down (antialiasing)
    im = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    c = s / 2
    r = s * 0.30
    # the glow
    glow = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse((c - r * 1.45, c - r * 1.45, c + r * 1.45, c + r * 1.45), fill=(110, 230, 40, 150))
    glow = glow.filter(ImageFilter.GaussianBlur(s * 0.06))
    im.alpha_composite(glow)
    # the sphere: a radial gradient, its light up and left
    sph = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    px = sph.load()
    lx, ly = c - r * 0.35, c - r * 0.40
    for y in range(int(c - r), int(c + r) + 1):
        for x in range(int(c - r), int(c + r) + 1):
            d = math.hypot(x - c, y - c)
            if d > r:
                continue
            k = math.hypot(x - lx, y - ly) / (r * 1.6)
            k = min(1.0, k)
            edge = max(0.0, (d - r * 0.92) / (r * 0.08))
            g = (1 - k)
            col = (int(30 + 170 * g * g), int(90 + 165 * g), int(10 + 60 * g * g), 255)
            a = int(255 * (1 - edge * 0.6))
            px[x, y] = (col[0], col[1], col[2], a)
    im.alpha_composite(sph)
    # the rings
    rings = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    for i, (rx, ry, rot) in enumerate(((1.42, 0.46, 0.5), (1.55, 0.52, -0.6), (1.30, 0.40, 1.6))):
        ring = Image.new('RGBA', (s, s), (0, 0, 0, 0))
        rd = ImageDraw.Draw(ring)
        w = max(2, int(s * 0.018))
        rd.ellipse((c - r * rx, c - r * ry, c + r * rx, c + r * ry), outline=(215, 255, 130, 230 - i * 40), width=w)
        ring = ring.rotate(math.degrees(rot), resample=Image.BICUBIC, center=(c, c))
        rings.alpha_composite(ring)
    im.alpha_composite(rings)
    return im.resize((size, size), Image.LANCZOS)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    big = orb(256)
    big.save(os.path.join(out, 'icon256.png'))
    big.save(os.path.join(out, 'xbased.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    # the logo: the orb and the name beside it, on transparency
    font_path = os.path.join(os.path.dirname(__file__), '..', '..', 'native', 'app', 'ui', 'fonts', 'Play-Bold.ttf')
    font = ImageFont.truetype(font_path, 300)
    o = orb(512)
    w = 512 + 40 + int(font.getlength('xbased')) + 40
    logo = Image.new('RGBA', (w, 512), (0, 0, 0, 0))
    logo.alpha_composite(o, (0, 0))
    txt = Image.new('RGBA', logo.size, (0, 0, 0, 0))
    td = ImageDraw.Draw(txt)
    td.text((552, 70), 'xbased', font=font, fill=(150, 255, 60, 255))
    glow = txt.filter(ImageFilter.GaussianBlur(18))
    logo.alpha_composite(glow)
    logo.alpha_composite(txt)
    logo.save(os.path.join(out, 'logo.png'))
    print('icon and logo in', out)


if __name__ == '__main__':
    main()
