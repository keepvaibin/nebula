"""region_ab.py DIR_A DIR_B FIRST LAST x0,x1,y0,y1 [...]: two runs' present snapshots compared only inside the given
boxes (1280x720 pixels), at the frame offset (-30..30) that matches them best. Prints the largest channel difference
and the most pixels over 8 in any frame. For screens whose background is not deterministic (the Mii picker)."""
import os
import sys


def load(path):
    data = open(path, "rb").read()
    parts = data.split(maxsplit=4)
    return int(parts[1]), parts[4]


def box_diff(a, b, w, box):
    x0, x1, y0, y1 = box
    worst = 0
    over = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = (y * w + x) * 3
            d = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
            worst = max(worst, d)
            over += d > 8
    return worst, over


dir_a, dir_b, first, last = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
boxes = [tuple(int(v) for v in arg.split(",")) for arg in sys.argv[5:]]
best = None
shift = int(os.environ.get("REGION_SHIFT", "0"))
for offset in range(-shift - 30, -shift + 31):
    total, worst, over, frames = 0, 0, 0, 0
    for f in range(first, last + 1):
        try:
            w, a = load(f"{dir_a}/present_{f}.ppm")
            _, b = load(f"{dir_b}/present_{f + offset}.ppm")
        except OSError:
            continue
        frames += 1
        for box in boxes:
            m, n = box_diff(a, b, w, box)
            total += n
            worst = max(worst, m)
            over = max(over, n)
    # the most frames' overlap counts: at least half the window, scored by the mean per frame
    if frames * 2 >= last - first + 1 and (best is None or (total / frames, worst) < (best[1], best[2])):
        best = (offset, total / frames, worst, over, frames)
offset, total, worst, over, frames = best
print(f"icons: offset {offset:+d}, {frames} frames, max difference {worst}, at most {over} pixels over 8")
