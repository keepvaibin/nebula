"""Reads wgc_capture recordings and finds one-frame flashes in them.

    frames.py <capture.bin> flashes [limit]      frames that step away from both neighbours (8x8 tiles), and frames
                                                  that turn back against a ramp
    frames.py <capture.bin> png <first> <last> <out dir>   those frames as PNG
    frames.py <capture.bin> times                 frame count, rate and the largest gaps between frames
"""
import os
import struct
import sys

import numpy as np


def frames(path):
    """Yields (index, time in seconds, RGB array) for every frame of a recording."""
    with open(path, 'rb') as f:
        index = 0
        while True:
            head = f.read(16)
            if len(head) < 16:
                return
            time, w, h = struct.unpack('<QII', head)
            data = f.read(w * h * 3)
            if len(data) < w * h * 3:
                return
            yield index, time / 1e7, np.frombuffer(data, np.uint8).reshape(h, w, 3)
            index += 1


def tiles(rgb):
    g = rgb.astype(np.float32).mean(axis=2)
    h, w = g.shape
    th, tw = h // 16, w // 16
    return g[:th * 16, :tw * 16].reshape(16, th, 16, tw).mean(axis=(1, 3))


def flashes(path, limit):
    prev = cur = None
    hits = []
    steps = []
    for i, t, rgb in frames(path):
        nt = tiles(rgb)
        if prev is not None:
            a = cur - prev
            b = nt - cur
            across = np.abs(nt - prev)
            spike = (np.abs(a) > limit) & (np.abs(b) > limit) & (a * b < 0) & (across < 0.5 * np.minimum(np.abs(a), np.abs(b)))
            if spike.any():
                y, x = np.unravel_index(np.argmax(np.where(spike, np.abs(a), 0)), spike.shape)
                hits.append((i - 1, int(spike.sum()), round(float(a[y, x]), 1), round(float(b[y, x]), 1), (int(x), int(y))))
        prev, cur = cur, nt
        steps.append((i, t))
    print(path, len(steps), 'frames;', len(hits), 'one-frame tile flashes over', limit, '(frame, tiles, in, out, tile x,y)')
    for h in hits[:80]:
        print(' ', h)


def times(path):
    ts = [t for _, t, _ in frames(path)]
    if len(ts) < 2:
        print('frames', len(ts))
        return
    d = np.diff(ts)
    print('frames', len(ts), 'over %.2f s: %.1f fps; median gap %.2f ms, largest' % (ts[-1] - ts[0], (len(ts) - 1) / (ts[-1] - ts[0]), np.median(d) * 1e3),
          sorted((round(x * 1e3, 2) for x in d), reverse=True)[:10])


def png(path, first, last, out):
    from PIL import Image
    os.makedirs(out, exist_ok=True)
    for i, t, rgb in frames(path):
        if first <= i <= last:
            Image.fromarray(rgb).save(os.path.join(out, 'f%06d.png' % i))
        if i > last:
            break


if __name__ == '__main__':
    what = sys.argv[2]
    if what == 'flashes':
        flashes(sys.argv[1], float(sys.argv[3]) if len(sys.argv) > 3 else 12.0)
    elif what == 'times':
        times(sys.argv[1])
    elif what == 'png':
        png(sys.argv[1], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5])
