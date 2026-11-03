"""matchframes.py OLD_RUN NEW_RUN: each present of a new run sought among an old run's by its bytes, whatever its frame number. For a change that moves where a stop starts; misses are measured against the aligned old picture."""
import hashlib, os, re, sys
def table(D):
    out = []
    for f in os.listdir(D):
        if f.startswith('present_') and f.endswith('.ppm') and 'xfb' not in f and 'display' not in f:
            out.append((int(re.search(r'(\d+)', f).group(1)), hashlib.md5(open(os.path.join(D, f), 'rb').read()).hexdigest()))
    return sorted(out)
a = table(sys.argv[1])
b = table(sys.argv[2])
seen = {h: n for n, h in a}
print('pictures old', len(a), 'new', len(b), 'distinct old', len(seen))
hit = [(n, seen[h]) for n, h in b if h in seen]
miss = [n for n, h in b if h not in seen]
print('new found among old', len(hit), 'not found', len(miss))
shifts = {}
for n, o in hit:
    shifts[o - n] = shifts.get(o - n, 0) + 1
print('shifts', sorted(shifts.items(), key=lambda kv: -kv[1])[:16])
runs = []
for n in miss:
    if runs and n == runs[-1][1] + 1:
        runs[-1][1] = n
    else:
        runs.append([n, n])
print('not found, runs', runs[:30])
import numpy as np
from PIL import Image
mapping = sorted(hit)
order = [m[0] for m in mapping]
import bisect
worst = []
names_a = {int(re.search(r'(\d+)', f).group(1)): f for f in os.listdir(sys.argv[1]) if f.startswith('present_') and f.endswith('.ppm') and 'xfb' not in f}
names_b = {int(re.search(r'(\d+)', f).group(1)): f for f in os.listdir(sys.argv[2]) if f.startswith('present_') and f.endswith('.ppm') and 'xfb' not in f}
for n in miss:
    i = bisect.bisect_left(order, n)
    shifts_near = {mapping[k][1] - mapping[k][0] for k in (i - 1, i) if 0 <= k < len(mapping)}
    best = None
    for s in shifts_near:
        if n + s not in names_a:
            continue
        try:
            x = np.asarray(Image.open(os.path.join(sys.argv[1], names_a[n + s])).convert('RGB')).astype(int)
            y = np.asarray(Image.open(os.path.join(sys.argv[2], names_b[n])).convert('RGB')).astype(int)
        except OSError:
            continue
        d = np.abs(x - y).max(axis=2)
        v = (int((d > 8).sum()), n, n + s, int(d.max()))
        if best is None or v < best:
            best = v
    worst.append(best if best is not None else (-1, n, 0, 0))
worst.sort(reverse=True)
print('misses against the aligned old frame (px over 8, new, old, max)', worst[:24])
print('misses', len(worst), 'no aligned old picture', sum(1 for w in worst if w[0] < 0), '0 px over 8', sum(1 for w in worst if w[0] == 0), '1-999 px', sum(1 for w in worst if 0 < w[0] < 1000), '1000 and more', sum(1 for w in worst if w[0] >= 1000))
for d in sys.argv[1:3]:
    log = open(os.path.join(d, 'run.log'), errors='replace').read()
    print('crash', log.count('crash: '), [s[:90] for s in log.split('\n') if '[loss]' in s][-1:])
