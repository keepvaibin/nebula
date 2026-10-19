"""triptych.py <out.png> <width> <label=path.ppm> ... [--crop x0,y0,x1,y1]: pictures side by side (one row), each
scaled to <width>, a caption bar above each; with --crop (960x540 coordinates) a second row of the same crop of each,
scaled up to the same width. Prints each picture's mean luminance (0-255) and, from the second picture on, its mean
absolute difference to the first."""
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

out, width = sys.argv[1], int(sys.argv[2])
args = sys.argv[3:]
crop = None
if "--crop" in args:
    i = args.index("--crop")
    crop = tuple(int(v) for v in args[i + 1].split(","))
    args = args[:i] + args[i + 2:]
items = [a.split("=", 1) for a in args]
try:
    font = ImageFont.truetype("segoeui.ttf", 22)
except OSError:
    font = ImageFont.load_default()
bar = 34
pictures = []
for label, path in items:
    im = Image.open(path).convert("RGB")
    pictures.append((label, im))
h = int(pictures[0][1].height * width / pictures[0][1].width)
rows = 2 if crop else 1
crop_h = 0
if crop:
    cw, ch = crop[2] - crop[0], crop[3] - crop[1]
    crop_h = int(width * ch / cw)
sheet = Image.new("RGB", (len(pictures) * width + (len(pictures) - 1) * 6, bar + h + (crop_h + 6 if crop else 0)),
                  (24, 26, 30))
draw = ImageDraw.Draw(sheet)
first = None
for k, (label, im) in enumerate(pictures):
    x = k * (width + 6)
    scaled = im.resize((width, h), Image.LANCZOS)
    sheet.paste(scaled, (x, bar))
    draw.text((x + 8, 6), label, fill=(235, 238, 242), font=font)
    if crop:
        s = im.width / 960.0
        part = im.crop(tuple(int(v * s) for v in crop)).resize((width, crop_h), Image.LANCZOS)
        sheet.paste(part, (x, bar + h + 6))
    a = np.asarray(im.resize((960, int(im.height * 960 / im.width))), dtype=np.float32)
    lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    line = f"{label}: mean {lum.mean():.1f}"
    if first is not None:
        line += f"  |diff| to {pictures[0][0]} {np.abs(lum - first).mean():.1f}"
    else:
        first = lum
    print(line)
sheet.save(out)
print(out, sheet.size)
