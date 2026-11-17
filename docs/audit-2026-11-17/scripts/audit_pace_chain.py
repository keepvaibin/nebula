"""audit_pace_chain.py RUN_FOLDER...: per run, from the frame timeline after 30 s: how many steps were long or short, and
the chain of one frame with no in-between pictures (step start -> list handed over -> present call -> present returned),
and for every run the time from a picture's moment (or its walk's start) to its present call returning."""
import pathlib, statistics, struct, sys

def load(path):
    with open(path, "rb") as f:
        h = f.read(64)
        _v, freq, count, _w, _p = struct.unpack_from("<5Q", h, 8)
        data = f.read(count * 32)
    r = [(q, e, a, b) for q, a, b, _t, e, v in struct.iter_unpack("<QQQIHH", data) if v]
    r.sort()
    return freq, r

def q(v, p):
    v = sorted(v); return v[min(len(v) - 1, int(p * (len(v) - 1) + 0.5))]

def s(name, v):
    return "%-22s n=%d middle=%.2f p99=%.2f lowest=%.2f highest=%.2f ms" % (name, len(v), statistics.median(v), q(v, 0.99), min(v), max(v)) if v else name + " n=0"

def after(times, t):
    lo, hi = 0, len(times)
    while lo < hi:
        m = (lo + hi) // 2
        if times[m] < t: lo = m + 1
        else: hi = m
    return times[lo] if lo < len(times) else None

for folder in sys.argv[1:]:
    run = pathlib.Path(folder)
    p = run / "out" / "timeline.bin"
    if not p.is_file():
        continue
    freq, rec = load(p)
    ms = 1000.0 / freq
    start = rec[0][0] + 30 * freq
    rec = [r for r in rec if r[0] >= start]
    by = {}
    for r in rec:
        by.setdefault(r[1], []).append(r[0])
    steps = by.get(32, [])
    gaps = [(b - a) * ms for a, b in zip(steps, steps[1:])]
    print("== %s: steps %d; over 18.0 ms: %d; over 20.0 ms: %d; under 15.3 ms: %d" % (
        run.name, len(gaps), sum(g > 18.0 for g in gaps), sum(g > 20.0 for g in gaps), sum(g < 15.3 for g in gaps)))
    sub_end, pb, pe = by.get(2, []), by.get(14, []), by.get(15, [])
    content = by.get(33, [])
    if not content and pb:
        a1, a2, a3 = [], [], []
        for st in steps:
            se = after(sub_end, st); b = after(pb, se) if se else None; e = after(pe, b) if b else None
            if e and (e - st) * ms < 60:
                a1.append((se - st) * ms); a2.append((b - se) * ms); a3.append((e - b) * ms)
        print("  " + s("step start > handed", a1)); print("  " + s("handed > present call", a2)); print("  " + s("present call > returns", a3))
    if content and pe:
        lag = []
        for c in content:
            e = after(pe, c)
            if e and (e - c) * ms < 60: lag.append((e - c) * ms)
        print("  " + s("moment > present back", lag) + "; spread p1..p99 %.2f ms" % (q(lag, 0.99) - q(lag, 0.01)))

# Second pass: for every run with in-between pictures, from the content records alone: the time from the start of the
# step that made a list to the picture moment at which that list is shown whole (no display lead added), and how long
# the finished list waited for its first picture's moment.
print()
for folder in sys.argv[1:]:
    run = pathlib.Path(folder)
    p = run / "out" / "timeline.bin"
    if not p.is_file():
        continue
    freq, rec = load(p)
    ms = 1000.0 / freq
    start = rec[0][0] + 30 * freq
    rec = [r for r in rec if r[0] >= start]
    steps = [r[0] for r in rec if r[1] == 32]
    frame_at = {r[2]: r[0] for r in rec if r[1] == 26}
    content = [(r[0], r[2] - 1.0 + r[3] / 1e6) for r in rec if r[1] == 33]
    if not content:
        continue
    def before(times, t):
        lo, hi = 0, len(times)
        while lo < hi:
            m = (lo + hi) // 2
            if times[m] <= t: lo = m + 1
            else: hi = m
        return times[lo - 1] if lo else None
    core = []
    for (q0, c0), (q1, c1) in zip(content, content[1:]):
        whole = int(c1)
        if not (c1 >= whole > c0) or c1 == c0 or c1 - c0 > 1.5:
            continue
        g = frame_at.get(whole)
        if g is None:
            continue
        st = before(steps, g)
        if st is None:
            continue
        v = ((q0 + (q1 - q0) * (whole - c0) / (c1 - c0)) - st) * ms
        if 0 < v < 150:
            core.append(v)
    print("== %s: %s" % (run.name, s("step start > list whole at its moment", core)))
