"""shiftseq.py <snap dir> <first> <count> [x0,y0,x1,y1 in 960x540]: sub-pixel shift (output pixels) of each present
against the first, by phase correlation over a region (default the lower middle), to tell camera motion from
shading change."""
import os
import sys

import numpy as np
from PIL import Image

src, first, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
box = [int(v) for v in (sys.argv[4] if len(sys.argv) > 4 else "200,280,760,520").split(",")]


def load(i):
    im = Image.open(os.path.join(src, f"present_{i}.ppm")).convert("L")
    k = im.width / 960.0
    a = np.asarray(im.crop(tuple(int(v * k) for v in box)), dtype=np.float64)
    return a - a.mean()


ref = load(first)
win = np.outer(np.hanning(ref.shape[0]), np.hanning(ref.shape[1]))
F0 = np.fft.fft2(ref * win)
out = []
for i in range(first + 1, first + count):
    F1 = np.fft.fft2(load(i) * win)
    r = F0 * np.conj(F1)
    c = np.fft.ifft2(r / (np.abs(r) + 1e-9)).real
    y, x = np.unravel_index(np.argmax(c), c.shape)
    h, w = c.shape

    def at(oy, ox):
        return c[(y + oy) % h, (x + ox) % w]

    # Sub-pixel peak: a parabola through the peak and its two neighbours on each axis.
    dy = (at(1, 0) - at(-1, 0)) / (2 * (2 * c[y, x] - at(1, 0) - at(-1, 0)) + 1e-12)
    dx = (at(0, 1) - at(0, -1)) / (2 * (2 * c[y, x] - at(0, 1) - at(0, -1)) + 1e-12)
    yy = (y if y < h // 2 else y - h) + dy
    xx = (x if x < w // 2 else x - w) + dx
    out.append(f"{i}:{xx:+.2f},{yy:+.2f}")
print(" ".join(out))
