"""flickppm.py <snap dir> <out prefix> [scale_w]: temporal flicker of consecutive present snapshots.

Per pixel, flicker = |f(t) - (f(t-1) + f(t+1)) / 2| on luminance: the part of a change that reverses within one
frame (steady motion cancels). Writes <prefix>_map.png (mean flicker, colour-mapped over the first frame) and
prints the mean, the 99th percentile and the share of pixels above 4 levels."""
import os
import re
import sys

import numpy as np
from PIL import Image

src, prefix = sys.argv[1], sys.argv[2]
w = int(sys.argv[3]) if len(sys.argv) > 3 else 960
files = sorted((f for f in os.listdir(src) if re.fullmatch(r"present_\d+\.ppm", f)), key=lambda f: int(f[8:-4]))
frames = []
for f in files:
    im = Image.open(os.path.join(src, f)).convert("RGB")
    h = int(im.height * w / im.width)
    a = np.asarray(im.resize((w, h), Image.BILINEAR), dtype=np.float32)
    frames.append(0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2])
f = np.stack(frames)
flick = np.abs(f[1:-1] - 0.5 * (f[:-2] + f[2:]))
m = flick.mean(axis=0)
print(f"frames {len(files)}: mean {m.mean():.3f}  p99 {np.percentile(m, 99):.2f}  share>4 {(m > 4).mean():.4f}  max {m.max():.1f}")
base = np.asarray(Image.open(os.path.join(src, files[0])).convert("RGB").resize((m.shape[1], m.shape[0])), dtype=np.float32)
heat = np.clip(m / 8.0, 0, 1)[..., None]
out = base * 0.35 * (1 - heat) + heat * np.array([255.0, 40.0, 0.0])
Image.fromarray(out.astype(np.uint8)).save(prefix + "_map.png")
np.save(prefix + "_map.npy", m)
