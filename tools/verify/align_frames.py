"""align_frames.py RUN_A RUN_B [FIRST LAST [MAXSHIFT]]: picture test independent of timing (FIRST/LAST "-": all).

Both runs snapshot every present in [FIRST, LAST] (GALAXY_PRESENT_SNAPSHOT_EVERY=1 _FIRST _LAST). Finds the present
offset k for which B's frame n equals A's frame n+k; prints, per offset, how many frames are pixel-identical, and for
the best offset the largest difference left. A build that draws the same pictures is identical at one offset for every
frame, whatever its frame pacing (an optimisation, a shader compile, a load moved the presents). Exit 0 when one offset
makes every compared frame identical."""
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
    same = total = 0
    worst = 0
    for n, pb in sorted(fb.items()):
        pa = fa.get(n + k)
        if pa is None:
            continue
        total += 1
        d = int(np.abs(load(pa) - load(pb)).max())
        same += d == 0
        worst = max(worst, d)
    if total:
        print(f"offset {k:+d}: {same}/{total} identical, max difference {worst}")
        if best is None or same > best[1] or (same == best[1] and worst < best[3]):
            best = (k, same, total, worst)
if best is None:
    print("no overlapping frames")
    sys.exit(2)
k, same, total, worst = best
print(f"best offset {k:+d}: {same}/{total} identical" + ("" if same == total else f", max difference {worst}"))
sys.exit(0 if same == total and total >= 8 else 1)
