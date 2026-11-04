"""motion_check.py DIR OUT.png: are the motion vectors exact? Each pair of frames from GALAXY_TEST_MOTION_DUMP: the second frame warped
back by its motion onto the first. A pixel is wrong where the warp misses, leaving the picture still misses too, and none of its
neighbours in the frame before match. The band around a moving edge is left out: what it uncovers has no place in the frame
before. A model drawn for the first time (flying into view) has no place there either and cannot be told apart, so one frame can
reach 2-3% with nothing wrong: the mean over a run is the number to watch (0.25% at 120 Hz). Prints one line per pair, marks the
worst pairs' wrong pixels red; exit 1 when a pair's share passes 3%.
"""
import glob
import re
import sys

import numpy as np
from PIL import Image

directory, out = sys.argv[1], sys.argv[2]
LIMIT = 0.03
MISS = 60


def load(name, frame):
    path = glob.glob("%s/%s_%d_*.bin" % (directory, name, frame))
    width, height = [int(v) for v in re.findall(r"_(\d+)x(\d+)\.bin$", path[0])[0]]
    return path[0], width, height


def spread(mask, radius):
    """`mask` grown by `radius` pixels, one direction at a time."""
    grown = mask.copy()
    for axis in (0, 1):
        step = grown.copy()
        for r in range(1, radius + 1):
            step |= np.roll(grown, r, axis=axis) | np.roll(grown, -r, axis=axis)
        grown = step
    return grown


frames = sorted(int(re.findall(r"motion_(\d+)_", p)[0]) for p in glob.glob(directory + "/motion_*.bin"))
results = []
for f in frames:
    if f - 1 not in frames:
        continue
    p, w, h = load("color", f)
    cur = np.fromfile(p, np.uint8).reshape(h, w, 4)[:, :, :3].astype(np.int32)
    prev = np.fromfile(load("color", f - 1)[0], np.uint8).reshape(h, w, 4)[:, :, :3].astype(np.int32)
    mv = np.fromfile(load("motion", f)[0], np.float16).reshape(h, w, 2).astype(np.float32)
    ys, xs = np.mgrid[0:h, 0:w]
    px = np.clip(np.rint(xs + mv[:, :, 0]).astype(int), 0, w - 1)
    py = np.clip(np.rint(ys + mv[:, :, 1]).astype(int), 0, h - 1)
    warped = np.abs(prev[py, px] - cur).sum(axis=2)
    still = np.abs(prev - cur).sum(axis=2)
    best = warped.copy()
    for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        best = np.minimum(best, np.abs(prev[np.clip(py + dy, 0, h - 1), np.clip(px + dx, 0, w - 1)] - cur).sum(axis=2))
    # A step in the motion: the band as wide as the larger move beside it.
    magnitude = np.hypot(mv[:, :, 0], mv[:, :, 1])
    step_x = np.abs(np.diff(magnitude, axis=1, prepend=magnitude[:, :1]))
    step_y = np.abs(np.diff(magnitude, axis=0, prepend=magnitude[:1, :]))
    edges = (step_x > 1.0) | (step_y > 1.0)
    band = spread(edges, int(min(32, np.ceil(magnitude.max())) + 1))
    bad = (best > MISS) & (still > MISS) & ~band
    results.append((bad.mean(), f, cur, bad))
    print("frame %6d bad %.4f |mv| max %6.1f band %.3f warped %6.2f still %6.2f" % (f, bad.mean(), magnitude.max(), band.mean(), warped.mean(), still.mean()))
results.sort(key=lambda r: -r[0])
W, H = 640, 360
sheet = Image.new("RGB", (3 * (W + 3), 3 * (H + 3)), (255, 0, 255))
for i, (share, f, cur, bad) in enumerate(results[:9]):
    view = cur.astype(np.uint8).copy()
    view[bad] = (255, 0, 0)
    sheet.paste(Image.fromarray(view).resize((W, H)), ((i % 3) * (W + 3), (i // 3) * (H + 3)))
sheet.save(out)
worst = results[0][0] if results else 1.0
print("pairs %d worst bad share %.4f" % (len(results), worst))
sys.exit(1 if worst > LIMIT else 0)
