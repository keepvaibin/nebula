"""flickdiff.py <a_map.npy> <b_map.npy> <base.png> <out.png>: where b flickers more (red) or less (blue) than a,
over a dimmed base picture; prints the mean of each and block totals of the change (8 x 6 grid)."""
import sys

import numpy as np
from PIL import Image

a, b = np.load(sys.argv[1]), np.load(sys.argv[2])
d = b - a
print(f"a {a.mean():.3f}  b {b.mean():.3f}  change {d.mean():+.3f}")
h, w = d.shape
gy, gx = 6, 8
for y in range(gy):
    row = []
    for x in range(gx):
        blk = d[y * h // gy:(y + 1) * h // gy, x * w // gx:(x + 1) * w // gx]
        row.append(f"{blk.mean():+6.2f}")
    print(" ".join(row))
base = np.asarray(Image.open(sys.argv[3]).convert("RGB").resize((w, h)), dtype=np.float32)
up = np.clip(d / 6.0, 0, 1)[..., None]
down = np.clip(-d / 6.0, 0, 1)[..., None]
out = base * 0.3 * (1 - up) * (1 - down) + up * np.array([255.0, 40.0, 0.0]) + down * np.array([0.0, 120.0, 255.0])
Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(sys.argv[4])
