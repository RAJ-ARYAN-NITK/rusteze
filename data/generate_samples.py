#!/usr/bin/env python3
import os
import math
import random
from PIL import Image, ImageDraw, ImageFilter

os.makedirs("data/test/images", exist_ok=True)
os.makedirs("data/test/masks", exist_ok=True)

W, H = 640, 480

def save_sample(name, img, mask):
    img.save(f"data/test/images/{name}.jpg", quality=92)
    mask.save(f"data/test/masks/{name}.png")
    print(f"Generated {name}.jpg and {name}.png")

# 1. Clean Pipe (0% rust, Grade 10)
img1 = Image.new("RGB", (W, H))
mask1 = Image.new("L", (W, H), 0)
pixels1 = img1.load()
for y in range(H):
    for x in range(W):
        # brushed steel cylinder shading
        v = int(140 + 40 * math.sin(x * 0.015) + random.randint(-4, 4))
        v = max(0, min(255, v))
        pixels1[x, y] = (int(v * 0.95), int(v * 0.96), int(v * 0.98))
save_sample("clean_pipe", img1, mask1)

# 2. Pitted Plate (~2-4% rust, Grade 5)
img2 = Image.new("RGB", (W, H))
mask2 = Image.new("L", (W, H), 0)
pixels2 = img2.load()
mask_px2 = mask2.load()
for y in range(H):
    for x in range(W):
        v = 150 + random.randint(-8, 8)
        pixels2[x, y] = (v, v, v)

random.seed(42)
for _ in range(50):
    cx = random.randint(40, W - 40)
    cy = random.randint(40, H - 40)
    rad = random.randint(4, 12)
    for dy in range(-rad, rad + 1):
        for dx in range(-rad, rad + 1):
            if dx*dx + dy*dy <= rad*rad:
                px, py = cx + dx, cy + dy
                if 0 <= px < W and 0 <= py < H:
                    n = random.randint(-15, 15)
                    # Rust RGB (reddish/orange brown)
                    r = max(0, min(255, 175 + n))
                    g = max(0, min(255, 75 + n))
                    b = max(0, min(255, 25 + n))
                    pixels2[px, py] = (r, g, b)
                    mask_px2[px, py] = 255
save_sample("pitted_plate", img2, mask2)

# 3. Severe Flange (~15-20% rust, Grade 3)
img3 = Image.new("RGB", (W, H))
mask3 = Image.new("L", (W, H), 0)
pixels3 = img3.load()
mask_px3 = mask3.load()
for y in range(H):
    for x in range(W):
        pixels3[x, y] = (135, 135, 135)

random.seed(101)
for y in range(H):
    for x in range(W):
        dist = math.hypot((x - 320) / 1.7, (y - 240) / 1.0)
        if dist < 120 + random.randint(-18, 18):
            n = random.randint(-25, 25)
            r = max(0, min(255, 185 + n))
            g = max(0, min(255, 80 + n))
            b = max(0, min(255, 28 + n))
            pixels3[x, y] = (r, g, b)
            mask_px3[x, y] = 255
save_sample("severe_flange", img3, mask3)

# 4. Structural Beam (~30-40% rust, Grade 1)
img4 = Image.new("RGB", (W, H))
mask4 = Image.new("L", (W, H), 0)
pixels4 = img4.load()
mask_px4 = mask4.load()
for y in range(H):
    for x in range(W):
        is_flange = (y < 110 or y > H - 110 or abs(x - W//2) < 45)
        if is_flange and (random.random() < 0.75):
            n = random.randint(-30, 30)
            r = max(0, min(255, 180 + n))
            g = max(0, min(255, 75 + n))
            b = max(0, min(255, 25 + n))
            pixels4[x, y] = (r, g, b)
            mask_px4[x, y] = 255
        else:
            v = 120 + random.randint(-6, 6)
            pixels4[x, y] = (v, v, v)
save_sample("structural_beam", img4, mask4)
print("All sample images generated successfully.")
