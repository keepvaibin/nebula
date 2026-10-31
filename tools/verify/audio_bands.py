"""audio_bands.py RUN0 RUN1 [SECONDS] [START]: two runs' sound (audio.wav, 16-bit PCM, any rates) compared by spectrum:
the level of each third-octave band from 50 Hz to 16 kHz over SECONDS (default 60) from START (default 0), averaged
over quarter-second Hann windows, and the difference of RUN1 from RUN0 per band. Prints the bands and one summary line:
the largest and the mean absolute band difference (dB) over the bands within 40 dB of the loudest. Runs of one build
differ by their timing; compare against such a pair before calling a difference."""
import sys

import numpy as np


def samples(path):
    with open(path, "rb") as f:
        data = f.read()
    rate = int.from_bytes(data[24:28], "little")
    channels = int.from_bytes(data[22:24], "little")
    at = data.find(b"data", 12)
    pcm = np.frombuffer(data[at + 8:at + 8 + (len(data) - at - 8) // 2 * 2], dtype="<i2").astype(np.float64)
    pcm = pcm[: len(pcm) // channels * channels].reshape(-1, channels).mean(axis=1) / 32768.0
    return rate, pcm


def band_levels(path, seconds, start):
    rate, pcm = samples(path)
    pcm = pcm[int(start * rate):int((start + seconds) * rate)]
    size = rate // 4  # 4 Hz bins at any rate
    window = np.hanning(size)
    power = np.zeros(size // 2 + 1)
    frames = 0
    for at in range(0, len(pcm) - size, size // 2):
        spectrum = np.fft.rfft(pcm[at:at + size] * window)
        power += np.abs(spectrum) ** 2
        frames += 1
    power /= max(frames, 1)
    freqs = np.fft.rfftfreq(size, 1.0 / rate)
    centers = [1000.0 * 2.0 ** (k / 3.0) for k in range(-13, 13)]
    levels = []
    for c in centers:
        lo, hi = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        sel = (freqs >= lo) & (freqs < hi)
        # Mean square of the band (Parseval over the window), the same at any sample rate.
        band = 2.0 * power[sel].sum() / (size * (window ** 2).sum()) if sel.any() else 0.0
        levels.append(10.0 * np.log10(band + 1e-20))
    return centers, levels


def main():
    run0, run1 = sys.argv[1], sys.argv[2]
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 60.0
    start = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
    centers, a = band_levels(run0 + "/audio.wav", seconds, start)
    _, b = band_levels(run1 + "/audio.wav", seconds, start)
    loudest = max(a)
    diffs = []
    for c, x, y in zip(centers, a, b):
        mark = ""
        if x > loudest - 40.0:
            diffs.append(y - x)
        else:
            mark = " (quiet)"
        print("%7.0f Hz %7.1f %7.1f %+6.2f%s" % (c, x, y, y - x, mark))
    worst = max(abs(d) for d in diffs) if diffs else 0.0
    mean = sum(abs(d) for d in diffs) / len(diffs) if diffs else 0.0
    print("bands: largest difference %.2f dB, mean %.2f dB over %d bands" % (worst, mean, len(diffs)))


main()
