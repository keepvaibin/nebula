"""wavdiff.py A.wav B.wav...: first differing stereo frame of each B against A (seconds at the file's rate), differing
frame count, largest sample difference; "identical" when sample-exact over the shorter length."""
import array
import sys
import wave


def load(path):
    with wave.open(path, "rb") as w:
        rate = w.getframerate()
    data = open(path, "rb").read()
    at = data.find(b"data", 12)
    samples = array.array("h")
    body = data[at + 8:]
    samples.frombytes(body[: len(body) // 2 * 2])
    return rate, samples


rate, a = load(sys.argv[1])
for path in sys.argv[2:]:
    _, b = load(path)
    n = min(len(a), len(b))
    if a[:n] == b[:n]:
        print(f"{path}: identical ({n // 2} frames)")
        continue
    first = next(i for i in range(n) if a[i] != b[i])
    count = sum(1 for i in range(n) if a[i] != b[i])
    peak = max(abs(a[i] - b[i]) for i in range(first, n))
    print(f"{path}: first {first // 2} frames = {first / 2 / rate:.4f} s, {count} samples differ of {n}, max {peak}")
