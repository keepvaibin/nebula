"""wavshift.py A.wav B.wav [FROM_S]: B against A, block by block (default blocks of 840 stereo frames, one audio frame):
for each span of blocks the shift (in frames, multiples of 120, up to +-8 audio frames) under which B equals A. Prints
each change of shift with its time and the first index unmatched at no shift: shows whether the runs differ by one
start offset or by moving parts."""
import array
import sys


def load(path):
    data = open(path, "rb").read()
    at = data.find(b"data", 12)
    s = array.array("h")
    body = data[at + 8:]
    s.frombytes(body[: len(body) // 2 * 2])
    return s


a = load(sys.argv[1])
b = load(sys.argv[2])
start = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
N = 840
frames = min(len(a), len(b)) // 2
shifts = [0] + [m * 120 * sign for m in range(1, 57) for sign in (1, -1)]
last = None
i = int(start * 48000) // N
out = 0
while i * N + N < frames:
    A = a[i * N * 2 : (i * N + N) * 2]
    found = None
    order = ([last] if last is not None else []) + shifts
    for s in order:
        j = i * N + s
        if j < 0 or j + N > frames:
            continue
        if b[j * 2 : (j + N) * 2] == A:
            found = s
            break
    if found != last:
        print(f"{i * N / 48000:8.4f} s block {i}: shift {found}")
        last = found
        out += 1
        if out > 60:
            print("...")
            break
    i += 1
print("end")
