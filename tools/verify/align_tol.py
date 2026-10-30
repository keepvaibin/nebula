"""align_tol.py RUN_A RUN_B [FIRST LAST [MAXSHIFT]]: picture difference independent of timing, for changes that are
meant to move pixels by a step or two (float shading in place of the 8-bit lattice).

Like align_frames.py, finds the present offset k matching B's frame n to A's frame n+k, here the one with the lowest
mean difference. For that offset prints the largest per-channel difference, the mean, and per frame the share of
pixels whose largest channel difference exceeds 2 and 8 (worst frame and 99th percentile of frames)."""
import os
import sys

import numpy as np
from PIL import Image

a_dir, b_dir = sys.argv[1], sys.argv[2]
first = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] != "-" else None
last = int(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[4] != "-" else None
max_shift = int(sys.argv[5]) if len(sys.argv) > 5 else 8


def frames(d):
    out = {}
    for f in os.listdir(d):
        if f.startswith("present_") and f.endswith(".ppm") and ".xfb" not in f:
            n = int(f[len("present_"):-len(".ppm")])
            if (first is None or n >= first) and (last is None or n <= last):
                out[n] = os.path.join(d, f)
    return out


fa, fb = frames(a_dir), frames(b_dir)
cache = {}


def load(path):
    if path not in cache:
        cache[path] = np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)
    return cache[path]


best = None
for k in range(-max_shift, max_shift + 1):
    pairs = [(fa[n + k], pb) for n, pb in sorted(fb.items()) if n + k in fa]
    if not pairs:
        continue
    means = [float(np.abs(load(pa) - load(pb)).mean()) for pa, pb in pairs]
    m = sum(means) / len(means)
    if best is None or m < best[1]:
        best = (k, m, pairs)
if best is None:
    print("no overlapping frames")
    sys.exit(2)
k, m, pairs = best
worst = 0
over2, over8 = [], []
for pa, pb in pairs:
    d = np.abs(load(pa) - load(pb)).max(axis=2)
    worst = max(worst, int(d.max()))
    over2.append(float((d > 2).mean()))
    over8.append(float((d > 8).mean()))
o2, o8 = np.array(over2), np.array(over8)
print(f"best offset {k:+d}: {len(pairs)} frames, mean {m:.4f}, max {worst}, "
      f">2: worst {o2.max() * 100:.3f}% p99 {np.percentile(o2, 99) * 100:.3f}%, "
      f">8: worst {o8.max() * 100:.3f}% p99 {np.percentile(o8, 99) * 100:.3f}%")
