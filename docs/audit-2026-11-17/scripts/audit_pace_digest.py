"""audit_pace_digest.py RUN_FOLDER [SKIP_SECONDS]: what one audit_pace.sh run measured, from its [interp-stats] lines
(run.log) and its frame timeline (timeline.bin, runtime/include/galaxy/frame_timeline.h). Reads only.

Figures (the first SKIP_SECONDS, default 30, are left out: boot, the title, the stage's load):
  steps        the game step's rate and jitter (StepWaitEnd to StepWaitEnd)
  wait         how long a finished frame list waits before its first in-between picture's moment
               (the list's SubmitEnd to the first ContentTime of its sequence)
  pictures     content against display: each picture's game time (list frame - 1 + t) against the display refresh
               its present was issued for (the first vblank of the DWM grid after PresentEnd)
  delay        from the start of the step that made a list (its input is read then) to the display refresh on which
               that list is shown whole (content reaches the list's own time)
  no-interp    with no in-between pictures: step start to the refresh after the frame's present
"""
import pathlib
import re
import statistics
import struct
import sys

E = {"SubmitBegin": 1, "SubmitEnd": 2, "PresentBegin": 14, "PresentEnd": 15, "DisplayStats": 16, "VBlank": 24,
     "GameFrame": 26, "WalkBegin": 29, "WalkEnd": 30, "StepWaitBegin": 31, "StepWaitEnd": 32, "ContentTime": 33}


def load(path):
    with open(path, "rb") as f:
        header = f.read(64)
        if header[:8] != b"NBTL0001":
            sys.exit("not a Nebula timeline file")
        _version, freq, count, _written, _pid = struct.unpack_from("<5Q", header, 8)
        data = f.read(count * 32)
    records = [(q, e, a, b) for q, a, b, _t, e, v in struct.iter_unpack("<QQQIHH", data) if v]
    records.sort(key=lambda r: r[0])
    return freq, records


def q(values, p):
    v = sorted(values)
    return v[min(len(v) - 1, int(p * (len(v) - 1) + 0.5))]


def line(name, values, unit="ms"):
    if not values:
        return "%-10s n=0" % name
    return "%-10s n=%d middle=%.3f p90=%.3f p99=%.3f lowest=%.3f highest=%.3f %s" % (
        name, len(values), statistics.median(values), q(values, 0.9), q(values, 0.99), min(values), max(values), unit)


def stats_lines(log, skip_lines):
    keys = ("generated-per-frame", "sequences", "presented-hz", "late-slots", "superseded", "started", "real-only",
            "wall-ms-per-generated", "gpu-ms-per-generated", "game-fps", "cadence-drops", "capped-sequences",
            "measured-presents-hz", "history-resets", "dropped", "failed")
    rows = []
    for text in log.read_text(encoding="utf8", errors="replace").splitlines():
        if not text.startswith("[interp-stats]"):
            continue
        row = {}
        for k in keys:
            m = re.search(r"(?:^| )" + re.escape(k) + r"=([-\d.]+)", text)
            if m:
                row[k] = float(m.group(1))
        m = re.search(r"vblank-steps=(\d+)/(\d+)/(\d+)/(\d+)", text)
        if m:
            row["vb"] = [int(x) for x in m.groups()]
        m = re.search(r"slot-to-vblank-ms=([-\d.]+)/([-\d.]+)", text)
        if m:
            row["s2v"] = (float(m.group(1)), float(m.group(2)))
        m = re.search(r"step-delay-ms\(now/max\)=([-\d.]+)/([-\d.]+)", text)
        if m:
            row["delay"] = float(m.group(2))
        m = re.search(r"motion\(camera-cuts/game-cuts/teleports/flips\)=(\d+)/(\d+)/(\d+)/(\d+)", text)
        if m:
            row["motion"] = [int(x) for x in m.groups()]
        rows.append(row)
    return rows[skip_lines:]


def main():
    run = pathlib.Path(sys.argv[1])
    skip = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    print("== %s" % run.name)
    rows = stats_lines(run / "out" / "run.log", int(skip / 2))
    stat_lead = None
    cpu = [(float(m.group(1)), float(m.group(2))) for m in re.finditer(
        r"Nebula Render ([\d.]+) \(\d+ sw\); Nebula Game ([\d.]+)", (run / "out" / "run.log").read_text(encoding="utf8", errors="replace"))]
    cpu = cpu[int(skip):]
    if cpu:
        print("thread CPU, middle: render thread %.1f ms a second, game thread %.1f ms a second" % (
            statistics.median(c[0] for c in cpu), statistics.median(c[1] for c in cpu)))
    if rows:
        def mid(k):
            v = [r[k] for r in rows if k in r]
            return statistics.median(v) if v else float("nan")

        def total(k):
            return sum(r.get(k, 0.0) for r in rows)
        vb = [sum(r["vb"][i] for r in rows if "vb" in r) for i in range(4)]
        s2v = [r["s2v"] for r in rows if "s2v" in r]
        motion = [sum(r["motion"][i] for r in rows if "motion" in r) for i in range(4)]
        print("statistics (%d windows of 2 s): in-between per step %.0f; shown %.2f a second (measured %.2f); game %.4f steps a second"
              % (len(rows), mid("generated-per-frame"), mid("presented-hz"), mid("measured-presents-hz"), mid("game-fps")))
        print("  sequences started %.0f, ended on their real picture %.0f, replaced first %.0f; real-only %.0f; late slots %.0f; "
              "dropped %.0f; failed %.0f; history resets %.0f"
              % (total("started"), total("sequences"), total("superseded"), total("real-only"), total("late-slots"), total("dropped"),
                 total("failed"), total("history-resets")))
        print("  presents by refreshes since the one before (0 / 1 / 2 / 3+): %d / %d / %d / %d" % tuple(vb))
        if s2v:
            print("  picture's moment to the refresh after its present: %.3f to %.3f ms" % (min(a for a, _ in s2v), max(b for _, b in s2v)))
            stat_lead = statistics.median((a + b) / 2.0 for a, b in s2v)
        print("  per in-between picture: wall %.3f ms, GPU %.3f ms; step delay at most %.3f ms; cadence drops %.0f; capped %.0f"
              % (mid("wall-ms-per-generated"), mid("gpu-ms-per-generated"), max([r.get("delay", 0.0) for r in rows] or [0.0]),
                 total("cadence-drops"), total("capped-sequences")))
        print("  cuts by threshold %d, cuts the game stated %d, teleports %d, flips %d" % tuple(motion))
    path = run / "out" / "timeline.bin"
    if not path.is_file():
        print("no timeline")
        return
    freq, records = load(path)
    if not records:
        print("empty timeline")
        return
    ms = 1000.0 / freq
    start = records[0][0] + int(skip * freq)
    records = [r for r in records if r[0] >= start]
    by = {}
    for r in records:
        by.setdefault(r[1], []).append(r)
    counts = {name: len(by.get(code, [])) for name, code in E.items()}
    print("timeline: " + " ".join("%s=%d" % kv for kv in counts.items()))

    steps = [r[0] for r in by.get(E["StepWaitEnd"], [])]
    gaps = [(b - a) * ms for a, b in zip(steps, steps[1:])]
    if gaps:
        print(line("steps", gaps) + "; %.4f steps a second" % (1000.0 / (sum(gaps) / len(gaps))))

    submits = [r[0] for r in by.get(E["SubmitEnd"], [])]
    presents = [r[0] for r in by.get(E["PresentEnd"], [])]
    vblanks = by.get(E["VBlank"], [])

    def before(times, t):
        lo, hi = 0, len(times)
        while lo < hi:
            m = (lo + hi) // 2
            if times[m] <= t:
                lo = m + 1
            else:
                hi = m
        return times[lo - 1] if lo else None

    def after(times, t):
        lo, hi = 0, len(times)
        while lo < hi:
            m = (lo + hi) // 2
            if times[m] < t:
                lo = m + 1
            else:
                hi = m
        return times[lo] if lo < len(times) else None

    vb_times = [r[0] for r in vblanks]

    def refresh_after(t):
        """The first display refresh at or after t, on the newest DWM reading at or before t (else the first)."""
        if not vblanks:
            return None, None
        lo, hi = 0, len(vb_times)
        while lo < hi:
            m = (lo + hi) // 2
            if vb_times[m] <= t:
                lo = m + 1
            else:
                hi = m
        base, _e, period, _b = vblanks[max(lo - 1, 0)]
        if period == 0:
            return None, None
        k = -(-(t - base) // period)
        return base + k * period, period

    if vblanks:
        periods = [r[2] for r in vblanks if r[2]]
        print("display: %.4f Hz by the DWM readings (%d readings)" % (freq / statistics.median(periods), len(periods)))

    content = by.get(E["ContentTime"], [])
    if content:
        # wait: a list's hand-over to its sequence's first moment.
        first = {}
        for qpc, _e, a, _b in content:
            first.setdefault(a, qpc)
        # Step F: frame_begun writes GameFrame(a=F), then the list with game_frame F is handed over (host_main.cpp
        # svc_frame_begun, svc_submit_frame). So list F's hand-over is the first SubmitEnd after GameFrame(a=F), and
        # its step began at the StepWaitEnd before it.
        frame_at = {r[2]: r[0] for r in by.get(E["GameFrame"], [])}
        waits = []
        for a, qpc in first.items():
            g = frame_at.get(a)
            s = after(submits, g) if g is not None else None
            if s is not None and 0.0 <= (qpc - s) * ms < 80.0:
                waits.append((qpc - s) * ms)
        print(line("wait", waits))
        # pictures: content steps against display refreshes.
        shown = []
        for qpc, _e, a, b in content:
            p = after(presents, qpc)
            if p is None or (p - qpc) * ms > 50.0:
                continue
            d, period = refresh_after(p)
            if d is None:
                continue
            shown.append((qpc, a - 1.0 + b / 1e6, d, p))
        if len(shown) > 10:
            step_ms = sum(gaps) / len(gaps) if gaps else 1000.0 / 60.0
            errors, display_gaps, same, lead = [], [], 0, []
            for (q0, c0, d0, p0), (q1, c1, d1, p1) in zip(shown, shown[1:]):
                if d1 == d0:
                    same += 1
                    continue
                if abs(c1 - c0) > 1.5:
                    continue
                errors.append(((d1 - d0) * ms) - (c1 - c0) * step_ms)
                display_gaps.append((d1 - d0) * ms)
            for qpc, _c, d, p in shown:
                lead.append((d - qpc) * ms)
            print(line("pictures", [abs(e) for e in errors]) + " (animation error, absolute)")
            print("  two pictures for one refresh: %d of %d; display gap middle %.3f ms, highest %.3f ms" % (
                same, len(shown) - 1, statistics.median(display_gaps), max(display_gaps)))
            print(line("moment>disp", lead))
            # delay: step start to the refresh on which the list is whole.
            delays = []
            # The game's own statistic (the refresh after the present call returned) where it has one: a present that
            # returns just after a refresh is shown at the next, which the grid estimate above can place one early.
            mean_lead = stat_lead if stat_lead is not None else statistics.median(lead)
            for (q0, c0, d0, p0), (q1, c1, d1, p1) in zip(shown, shown[1:]):
                whole = int(c1) if c1 >= int(c1) > c0 - 1e-9 and int(c1) > int(c0 - 1e-9) else None
                if whole is None or c1 == c0:
                    continue
                cross = q0 + (q1 - q0) * (whole - c0) / (c1 - c0)
                g = frame_at.get(whole)
                if g is None:
                    continue
                step = before(steps, g)
                if step is None:
                    continue
                value = (cross - step) * ms + mean_lead
                if 0.0 < value < 150.0:
                    delays.append(value)
            print(line("delay", delays) + " (step start to the list whole on the display, lead %.2f ms)" % mean_lead)
    else:
        # no in-between pictures: each list's step start to the refresh after its present.
        delays = []
        for s in submits:
            step = before(steps, s)
            p = after(presents, s)
            if step is None or p is None:
                continue
            d, _period = refresh_after(p)
            if d is None:
                d = p
            value = (d - step) * ms
            if 0.0 < value < 150.0:
                delays.append(value)
        print(line("no-interp", delays))
    stats = by.get(E["DisplayStats"], [])
    if len(stats) > 2:
        pc = [(r[2] >> 32, r[2] & 0xFFFFFFFF) for r in stats]
        dp = pc[-1][0] - pc[0][0]
        dr = pc[-1][1] - pc[0][1]
        print("DXGI statistics: %d presents over %d refreshes (%.3f refreshes a present)" % (dp, dr, dr / dp if dp else 0.0))


main()
