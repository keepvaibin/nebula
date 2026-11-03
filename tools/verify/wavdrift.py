"""wavdrift.py A.wav B.wav: how far B has drifted from A: for blocks along the file, the lag (samples at 48 kHz) that lines B up best with A, by cross-correlation of the music's envelope-free signal; then the slope in percent."""
import sys

import numpy as np


def load(path):
    d = open(path, "rb").read()
    at = d.find(b"data") + 8
    x = np.frombuffer(d[at:at + ((len(d) - at) & ~3)], dtype=np.int16).reshape(-1, 2).astype(np.float64)
    return x.mean(axis=1)


a, b = load(sys.argv[1]), load(sys.argv[2])
n = min(len(a), len(b))
win = 1 << 16
pts = []
for start in range(48000 * 2, n - win * 2, 48000 * 2):
    x = a[start:start + win]
    y = b[start:start + win]
    if np.abs(x).max() < 200:
        continue
    fx = np.fft.rfft(x * np.hanning(win), win * 2)
    fy = np.fft.rfft(y * np.hanning(win), win * 2)
    c = np.fft.irfft(fx * np.conj(fy))
    k = int(np.argmax(c))
    lag = k if k < win else k - win * 2
    score = c[k] / (np.linalg.norm(x) * np.linalg.norm(y) + 1e-9)
    pts.append((start / 48000.0, lag, score))
    print(f"{start / 48000.0:6.1f} s  lag {lag:6d}  correlation {score:.3f}")
good = [(t, l) for t, l, s in pts if s > 0.3]
if len(good) >= 2:
    t = np.array([p[0] for p in good])
    l = np.array([p[1] for p in good], dtype=float)
    slope = np.polyfit(t, l, 1)[0]
    print(f"slope {slope:.2f} samples a second = {slope / 48000.0 * 100.0:.4f} %")
