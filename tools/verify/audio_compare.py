"""audio_compare.py RUN0 RUN1 [SECONDS]: two runs' sound (audio.wav, 16-bit PCM) compared by loudness: each run's level
in 50 ms windows (dB), the second shifted by the lag (within 3 s) that best matches the first over SECONDS (default 60),
then the correlation of the two envelopes, their mean level difference over windows where either is audible, and the
share of silent windows in each (below -60 dB). Run-to-run timing moves sound by a few windows, so identical builds
correlate at about 0.9 or more; a missing music stream or a silent run shows as a drop and a silent share."""
import math
import struct
import sys
import wave


def envelope(path, window_s=0.05):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit(path + ": not 16-bit PCM")
        channels, rate = w.getnchannels(), w.getframerate()
    # A run that is stopped leaves the header's sizes unwritten: the samples are the data chunk to the file's end.
    with open(path, "rb") as f:
        data = f.read()
    at = data.find(b"data", 12)
    if at < 0:
        raise SystemExit(path + ": no data chunk")
    frames = data[at + 8:]
    count = len(frames) // 2
    samples = struct.unpack("<%dh" % count, frames[: count * 2])
    step = max(1, int(rate * window_s)) * channels
    levels = []
    for start in range(0, len(samples) - step + 1, step):
        chunk = samples[start:start + step]
        power = sum(s * s for s in chunk) / len(chunk)
        levels.append(10.0 * math.log10(power / (32768.0 * 32768.0)) if power > 0 else -120.0)
    return levels


def correlation(a, b):
    n = min(len(a), len(b))
    if n < 2:
        return 0.0
    a, b = a[:n], b[:n]
    ma, mb = sum(a) / n, sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    if va == 0 or vb == 0:
        return 0.0
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(va * vb)


def main():
    run0, run1 = sys.argv[1], sys.argv[2]
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 60.0
    e0 = envelope(run0 + "/audio.wav")
    e1 = envelope(run1 + "/audio.wav")
    span = min(int(seconds / 0.05), len(e0), len(e1))
    best_lag, best = 0, -2.0
    for lag in range(-60, 61):
        a = e0[max(0, -lag):span]
        b = e1[max(0, lag):span + lag] if lag >= 0 else e1[:span + lag]
        c = correlation(a, b)
        if c > best:
            best_lag, best = lag, c
    a = e0[max(0, -best_lag):span]
    b = e1[max(0, best_lag):span + best_lag] if best_lag >= 0 else e1[:span + best_lag]
    audible = [(x, y) for x, y in zip(a, b) if x > -60.0 or y > -60.0]
    diff = sum(abs(x - y) for x, y in audible) / len(audible) if audible else 0.0
    silent0 = sum(1 for x in e0[:span] if x <= -60.0) / max(1, span)
    silent1 = sum(1 for y in e1[:span] if y <= -60.0) / max(1, span)
    print("audio: lag %+d windows, correlation %.3f, mean level difference %.1f dB, silent %.0f%% / %.0f%%"
          % (best_lag, best, diff, 100 * silent0, 100 * silent1))


main()
