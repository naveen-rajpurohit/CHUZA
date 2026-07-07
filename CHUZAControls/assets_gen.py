"""Dev-time asset generator for CHUZA Control's dark jungle gaming theme.

Run manually (`python assets_gen.py`) whenever the art needs regenerating.
The shipped app never runs this - it only loads the resulting PNG/ICO
files from assets/, which are checked into git.
"""
import os
import random

from PIL import Image, ImageDraw, ImageFont

from theme import (
    ACCENT, BG_DEEP, BG_PANEL, JUNGLE_DARK, JUNGLE_LIGHT, JUNGLE_MID,
    MOON, STAR, hex_to_rgb, resource_path,
)

# Final resolution comfortably covers the largest window; drawn at
# SUPERSAMPLE x this and downsampled with LANCZOS for smooth anti-aliased
# edges (no blocky/pixelated look).
BG_W, BG_H = 900, 1000
SUPERSAMPLE = 3
ASSETS_DIR = resource_path("assets")


def generate_background():
    ss = SUPERSAMPLE
    W, H = BG_W * ss, BG_H * ss
    img = Image.new("RGB", (W, H), hex_to_rgb(BG_DEEP))
    draw = ImageDraw.Draw(img)

    # Smooth night-sky gradient (many thin bands approximate a continuous one).
    top = hex_to_rgb(BG_DEEP)
    bottom = hex_to_rgb(JUNGLE_DARK)
    bands = 160
    for i in range(bands):
        y0 = int(H * i / bands)
        y1 = int(H * (i + 1) / bands) + 1
        t = i / (bands - 1)
        color = tuple(int(top[c] + (bottom[c] - top[c]) * t) for c in range(3))
        draw.rectangle([0, y0, W, y1], fill=color)

    # Moon with a soft crescent shadow.
    mx, my, r = W - int(0.16 * W), int(0.10 * H), int(0.045 * W)
    draw.ellipse([mx - r, my - r, mx + r, my + r], fill=hex_to_rgb(MOON))
    draw.ellipse(
        [mx - int(r * 0.5), my - int(r * 1.3), mx + int(r * 1.6), my + int(r * 0.6)],
        fill=hex_to_rgb(BG_DEEP),
    )

    # Seeded stars, avoiding the moon.
    rng = random.Random(1234)
    star_rgb = hex_to_rgb(STAR)
    placed = 0
    while placed < 130:
        x, y = rng.randrange(0, W), rng.randrange(0, H // 2)
        if abs(x - mx) < r * 2.5 and abs(y - my) < r * 2.5:
            continue
        rad = rng.choice([1, 1, 2, 2, 3]) * ss
        draw.ellipse([x - rad, y - rad, x + rad, y + rad], fill=star_rgb)
        placed += 1

    # Two parallax jungle-silhouette layers via a smooth seeded random walk.
    for seed, base_frac, amp_frac, color in (
        (7, 0.55, 0.05, JUNGLE_MID),
        (3, 0.72, 0.04, JUNGLE_DARK),
    ):
        rng2 = random.Random(seed)
        base_h, amp = H * base_frac, H * amp_frac
        step = max(1, W // 300)
        h = base_h
        points = [(0, H)]
        for x in range(0, W + step, step):
            h += rng2.uniform(-amp * 0.06, amp * 0.06)
            h = max(base_h - amp, min(base_h + amp, h))
            points.append((x, h))
        points.append((W, H))
        draw.polygon(points, fill=hex_to_rgb(color))

    # A few leaf-clump blobs on the front ridge.
    rng3 = random.Random(42)
    light = hex_to_rgb(JUNGLE_LIGHT)
    for _ in range(18):
        x = rng3.randrange(0, W)
        y = int(H * 0.72) + rng3.randrange(-int(0.02 * H), int(0.02 * H))
        rad = rng3.randrange(int(0.006 * W), int(0.013 * W))
        draw.ellipse([x - rad, y - rad, x + rad, y + rad], fill=light)

    img = img.resize((BG_W, BG_H), Image.LANCZOS)
    os.makedirs(ASSETS_DIR, exist_ok=True)
    path = os.path.join(ASSETS_DIR, "bg_jungle.png")
    img.save(path)
    return path


def _draw_eyes(draw, cx, cy, eye_w, eye_h, gap, color):
    """Two rounded-rect blocks side by side, echoing RoboEyes' default
    (neutral) eye proportions on the robot's own OLED face - ties the
    app's branding back to the physical device."""
    radius = max(1, eye_w // 5)
    lx0, ly0 = cx - gap // 2 - eye_w, cy - eye_h // 2
    rx0, ry0 = cx + gap // 2, cy - eye_h // 2
    draw.rounded_rectangle([lx0, ly0, lx0 + eye_w, ly0 + eye_h], radius=radius, fill=color)
    draw.rounded_rectangle([rx0, ry0, rx0 + eye_w, ry0 + eye_h], radius=radius, fill=color)


def generate_icon():
    """Builds one simple, chunky-shaped base icon at high resolution and
    lets Pillow's ICO encoder generate the smaller embedded sizes from it.

    Note: Pillow's ICO plugin does not actually honor append_images (a
    pre-resized-per-size list is silently ignored - only the base image
    ends up embedded) and won't upscale past the base image's own size.
    The base must therefore be >= the largest requested size, and the
    smaller sizes are Pillow's own internal resize of that base - fine
    here since the art is simple flat shapes (a bordered rounded rect +
    two eye blocks), which hold up at small sizes without needing
    hand-rolled NEAREST downsampling.
    """
    base = 256
    img = Image.new("RGBA", (base, base), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    pad = 16
    draw.rounded_rectangle(
        [pad, pad, base - pad, base - pad], radius=48,
        fill=hex_to_rgb(BG_PANEL) + (255,), outline=hex_to_rgb(ACCENT) + (255,), width=12,
    )
    _draw_eyes(draw, base // 2, base // 2 + 8, eye_w=64, eye_h=72, gap=24,
               color=hex_to_rgb(ACCENT) + (255,))

    sizes = [(16, 16), (32, 32), (48, 48), (64, 64), (256, 256)]
    os.makedirs(ASSETS_DIR, exist_ok=True)
    path = os.path.join(ASSETS_DIR, "icon.ico")
    img.save(path, format="ICO", sizes=sizes)
    return path


def generate_logo():
    ss = 2
    w, h = 480 * ss, 160 * ss
    img = Image.new("RGBA", (w, h), hex_to_rgb(BG_DEEP) + (255,))
    draw = ImageDraw.Draw(img)
    _draw_eyes(draw, 80 * ss, h // 2, eye_w=44 * ss, eye_h=52 * ss, gap=16 * ss,
               color=hex_to_rgb(ACCENT) + (255,))

    font = ImageFont.truetype(r"C:\Windows\Fonts\segoeuib.ttf", 52 * ss)
    text = "CHUZA"
    bbox = draw.textbbox((0, 0), text, font=font)
    th = bbox[3] - bbox[1]
    draw.text((160 * ss, h // 2 - th // 2 - bbox[1]), text, font=font, fill=hex_to_rgb(ACCENT) + (255,))

    img = img.resize((480, 160), Image.LANCZOS)
    os.makedirs(ASSETS_DIR, exist_ok=True)
    path = os.path.join(ASSETS_DIR, "logo.png")
    img.save(path)
    return path


if __name__ == "__main__":
    print("Generated:", generate_background())
    print("Generated:", generate_icon())
    print("Generated:", generate_logo())
