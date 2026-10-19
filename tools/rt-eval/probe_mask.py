"""probe_mask.py <present.ppm> <r,g,b> <out.png>: the pixels of one row-probe colour (within 6 levels) in magenta
over the dimmed picture; prints their share and bounding box (960 x 540 coordinates)."""
import sys

import numpy as np
from PIL import Image

im = Image.open(sys.argv[1]).convert("RGB")
a = np.asarray(im.resize((960, int(im.height * 960 / im.width))), dtype=np.int32)
c = np.array([int(v) for v in sys.argv[2].split(",")])
m = np.abs(a - c).max(axis=2) <= 6
ys, xs = np.nonzero(m)
print(f"share {m.mean():.4f}", (xs.min(), ys.min(), xs.max(), ys.max()) if len(xs) else "none")
out = (a * 0.35).astype(np.uint8)
out[m] = (255, 0, 255)
Image.fromarray(out).save(sys.argv[3])
