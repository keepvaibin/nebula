"""diffseq.py <snap dir> <first> <count> <x0,y0,x1,y1 in 960x540> <out.png>: per frame, the crop (top row) and its
second difference f(t) - (f(t-1) + f(t+1)) / 2 x8 (bottom row: red brighter, blue darker than its neighbours)."""
import os
import sys

import numpy as np
from PIL import Image

src, first, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
x0, y0, x1, y1 = (int(v) for v in sys.argv[4].split(","))


def load(i):
    im = Image.open(os.path.join(src, f"present_{i}.ppm")).convert("RGB")
    k = im.width / 960.0
    return np.asarray(im.crop((int(x0 * k), int(y0 * k), int(x1 * k), int(y1 * k))), dtype=np.float32)


frames = [load(first + i) for i in range(count + 2)]
tops, bottoms = [], []
for i in range(1, count + 1):
    f = frames[i]
    lum = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    d = lum(f) - 0.5 * (lum(frames[i - 1]) + lum(frames[i + 1]))
    vis = np.zeros_like(f)
    vis[..., 0] = np.clip(d * 8, 0, 255)
    vis[..., 2] = np.clip(-d * 8, 0, 255)
    vis += lum(f)[..., None] * 0.15
    tops.append(f)
    bottoms.append(vis)
row1 = np.concatenate(tops, axis=1)
row2 = np.concatenate(bottoms, axis=1)
Image.fromarray(np.clip(np.concatenate([row1, row2], axis=0), 0, 255).astype(np.uint8)).save(sys.argv[5])
