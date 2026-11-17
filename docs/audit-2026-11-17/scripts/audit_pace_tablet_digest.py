"""audit_pace_tablet_digest.py RUN_FOLDER...: what each tablet run logged once it was in the stage, from logcat.txt:
the pacing statistics ([interp-stats], every 2 s), Android's own presentation record ([present-timing], every 10 s),
GPU time ([vk-gpu]) and the step's CPU work ([pacing]). Lines before the mod's "to the stage" line plus 12 s are left out."""
import pathlib
import re
import statistics
import sys


def clock(text):
    m = re.match(r"\d\d-\d\d (\d\d):(\d\d):(\d\d)\.(\d+)", text)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)) + int(m.group(4)) / 1000.0 if m else None


def num(text, key):
    m = re.search(r"(?:^| )" + re.escape(key) + r"=([-\d.]+)", text)
    return float(m.group(1)) if m else None


for folder in sys.argv[1:]:
    run = pathlib.Path(folder)
    lines = (run / "logcat.txt").read_text(encoding="utf8", errors="replace").splitlines()
    start = None
    for text in lines:
        if "pace: to the stage" in text:
            start = clock(text)
    print("== %s" % run.name)
    if start is None:
        print("  the stage was never asked for")
        continue
    kept = [t for t in lines if (clock(t) or 0.0) >= start + 12.0]
    stats = [t for t in kept if "[interp-stats]" in t]
    if stats:
        hz = [num(t, "presented-hz") for t in stats]
        vb = [[int(x) for x in re.search(r"vblank-steps=(\d+)/(\d+)/(\d+)/(\d+)", t).groups()] for t in stats]
        tot = [sum(v[i] for v in vb) for i in range(4)]
        gen = [num(t, "generated-per-frame") for t in stats]
        cap = [num(t, "gpu-generated-cap") for t in stats]
        s2v = [re.search(r"slot-to-vblank-ms=([-\d.]+)/([-\d.]+)", t) for t in stats]
        print("  in-between per step: %s; pictures a second: middle %.1f, lowest %.1f, highest %.1f (%d windows)" % (
            "/".join(sorted({"%.0f" % g for g in gen})), statistics.median(hz), min(hz), max(hz), len(stats)))
        allp = sum(tot)
        print("  presents by refreshes since the one before (0 / 1 / 2 / 3+): %d / %d / %d / %d = %.1f%% exactly one refresh apart" % (
            tot[0], tot[1], tot[2], tot[3], 100.0 * tot[1] / allp if allp else 0.0))
        print("  GPU cap on in-between pictures seen: %s; sequences ended on their real picture %d of %d; late moments %d; cadence drops %d" % (
            "/".join(sorted({"%.0f" % c for c in cap if c is not None})), sum(num(t, "sequences") for t in stats),
            sum(num(t, "started") for t in stats), sum(num(t, "late-slots") for t in stats), sum(num(t, "cadence-drops") for t in stats)))
        print("  per in-between picture: wall %.2f ms, GPU %.2f ms; step delay at most %.2f ms; moment to refresh %.1f to %.1f ms" % (
            statistics.median(num(t, "wall-ms-per-generated") for t in stats), statistics.median(num(t, "gpu-ms-per-generated") for t in stats),
            max(float(re.search(r"step-delay-ms\(now/max\)=[-\d.]+/([-\d.]+)", t).group(1)) for t in stats),
            min(float(m.group(1)) for m in s2v if m), max(float(m.group(2)) for m in s2v if m)))
    else:
        print("  no in-between pictures (no [interp-stats] lines)")
    for t in kept:
        m = re.search(r"\[present-timing\] (\d+) frames: (\d+) shown at their requested slot, (\d+) late.*?; (\d+) at the target interval; actual-hz ([\d.]+), interval-max ([\d.]+) ms.*?margin ([\d.]+) ms.*?pipeline ([\d.]+) ms", t)
        if m:
            print("  Android: %s presents, %s at the slot asked, %s late, %s at the target interval; %.1f a second; longest gap %s ms; ready %s ms early; depth %s ms" % (
                m.group(1), m.group(2), m.group(3), m.group(4), float(m.group(5)), m.group(6), m.group(7), m.group(8)))
    gpu = [re.search(r"\[vk-gpu\] frame ([\d.]+) ms average, ([\d.]+) ms longest; ([\d.]+) scene target passes", t) for t in kept]
    gpu = [m for m in gpu if m]
    if gpu:
        print("  GPU a picture: %.2f to %.2f ms average, longest %.2f ms; scene openings %.1f" % (
            min(float(m.group(1)) for m in gpu), max(float(m.group(1)) for m in gpu), max(float(m.group(2)) for m in gpu),
            statistics.median(float(m.group(3)) for m in gpu)))
    pace = [re.search(r"\[pacing\] (\d+) frames, (\d+) on the display's vsync \(([\d.]+) Hz\); frame work ([\d.]+) ms average, ([\d.]+) ms longest", t) for t in kept]
    pace = [m for m in pace if m]
    if pace:
        print("  steps on the display's vsync: %d of %d (%s Hz); CPU work a step %.2f to %.2f ms average, longest %.2f ms" % (
            sum(int(m.group(2)) for m in pace), sum(int(m.group(1)) for m in pace), pace[-1].group(3),
            min(float(m.group(4)) for m in pace), max(float(m.group(4)) for m in pace), max(float(m.group(5)) for m in pace)))
    res = [t for t in kept if "[dynres]" in t or "internal resolution" in t]
    if res:
        print("  resolution lines: %d; last: %s" % (len(res), res[-1].split("): ", 1)[-1][:160]))
    heat = run / "heat.txt"
    if heat.is_file():
        h = heat.read_text(encoding="utf8", errors="replace")
        t = re.search(r"temperature: (\d+)", h)
        s = re.search(r"Thermal Status: (\d+)", h)
        print("  battery %.1f C, thermal status %s at the end" % (int(t.group(1)) / 10.0 if t else float("nan"), s.group(1) if s else "?"))
