"""compare.py OLD NEW: the material equivalence harness's outputs (harness.cpp), key by key: identical keys, keys whose
targets differ, differing bytes and the largest byte difference (colour targets), with the first differing keys."""
import struct
import sys

KEY_SIZE = 736


def records(path):
    data = open(path, "rb").read()
    at = 0
    while at < len(data):
        tag = data[at:at + 1]
        at += 1
        if tag in (b"X", b"Y"):
            yield None, tag, None
            continue
        n, = struct.unpack_from("<I", data, at)
        key = data[at + 4:at + 4 + KEY_SIZE]
        size, = struct.unpack_from("<I", data, at + 4 + KEY_SIZE)
        body = data[at + 8 + KEY_SIZE:at + 8 + KEY_SIZE + size]
        at += 8 + KEY_SIZE + size
        yield n, key, body


old = list(records(sys.argv[1]))
new = list(records(sys.argv[2]))
same = differ = skipped = 0
bytes_differ = 0
worst = 0
first = []
for (n, ka, a), (_, kb, b) in zip(old, new):
    if a is None or b is None:
        skipped += 1
        continue
    if ka != kb:
        print("key mismatch at", n)
        sys.exit(2)
    if a == b:
        same += 1
        continue
    differ += 1
    colour = a[:64 * 64 * 4]
    d = [abs(x - y) for x, y in zip(colour, b[:64 * 64 * 4]) if x != y]
    bytes_differ += sum(1 for x, y in zip(a, b) if x != y)
    worst = max(worst, max(d) if d else 0)
    if len(first) < 12:
        first.append((n, len(d), max(d) if d else 0, ka[0], ka[11]))
print(f"keys {len(old)}: identical {same}, differing {differ}, not drawn {skipped}; differing bytes {bytes_differ}, "
      f"largest colour difference {worst}")
for n, count, m, stages, reserved in first:
    print(f"  key {n}: {count} colour bytes differ (max {m}), stages {stages}, backend bits 0x{reserved:02x}")
sys.exit(0 if differ == 0 else 1)
