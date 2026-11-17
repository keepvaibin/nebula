"""audit_tablet_figures.py LOGCAT TOUR_RUNS_DIR [OUT.md]: per tour stop on an Android device, from the game's own log lines:
game steps a second while the stage plays (the harness runs fixed steps as fast as the device goes, two pictures a step),
and the Vulkan renderer's GPU time a picture, its longest picture, and scene passes and copies a picture (the last window the
renderer logged in that stop). Only the seconds inside the 600 played frames count: the stop's facts.jsonl gives the game
frame at which play began. Reads only."""
import json
import pathlib
import re
import statistics
import sys

line_re = re.compile(r"^\d\d-\d\d (\d\d):(\d\d):(\d\d)\.(\d+) \S+\s+\(\s*(\d+)\): (.*)$")
stop_re = re.compile(r"/harness/(\d{3}-[A-Za-z0-9]+-\d+)-\d{6}/")
host_re = re.compile(r"\[\s*([\d.]+)\] host: frame (\d+), .*renderer frames (\d+)")
gpu_re = re.compile(r"\[vk-gpu\] frame ([\d.]+) ms average, ([\d.]+) ms longest; ([\d.]+) scene target passes and ([\d.]+) scene "
                    r"target copies a frame; scene target (\d+x\d+)")


def play_begin(stop):
    """The game frame at which the stop's 600 played frames began, from its facts."""
    f = pathlib.Path(sys.argv[2]) / str(stop) / "vulkan" / "facts.jsonl"
    if not f.is_file():
        return None
    for line in f.read_text(encoding="utf8", errors="replace").splitlines():
        try:
            fact = json.loads(line)
        except ValueError:
            continue
        if fact.get("key") == "harness.clock":
            return int(fact["value"]["game_frame"])
    return None


def main():
    runs = {}
    order = []
    for raw in open(sys.argv[1], encoding="utf8", errors="replace"):
        m = line_re.match(raw.rstrip())
        if not m:
            continue
        pid, text = m.group(5), m.group(6)
        r = runs.setdefault(pid, {"stop": None, "host": [], "gpu": []})
        s = stop_re.search(text)
        if s and r["stop"] is None:
            r["stop"] = s.group(1)
            order.append(pid)
        h = host_re.search(text)
        if h:
            r["host"].append((float(h.group(1)), int(h.group(2)), int(h.group(3))))
        g = gpu_re.search(text)
        if g:
            r["gpu"].append((float(g.group(1)), float(g.group(2)), float(g.group(3)), float(g.group(4)), g.group(5)))
    rows = []
    for pid in order:
        r = runs[pid]
        host = r["host"]
        rates = []
        begin = play_begin(r["stop"])
        if len(host) >= 3 and begin is not None:
            for a, b in zip(host, host[1:]):
                if a[1] >= begin and b[1] <= begin + 600 and b[0] > a[0]:
                    rates.append((b[1] - a[1]) / (b[0] - a[0]))
        gpu = r["gpu"][-1] if r["gpu"] else None
        rows.append((r["stop"], rates, gpu))
    out = []
    out.append("| Stop | Steps a second, lowest | Steps a second, middle | GPU ms a picture | Longest picture, ms | Passes | Copies |")
    out.append("|---|---|---|---|---|---|---|")
    lows, mids, gpus, longs, passes = [], [], [], [], []
    for stop, rates, gpu in rows:
        lo = min(rates) if rates else None
        mid = statistics.median(rates) if rates else None
        if lo is not None:
            lows.append((lo, stop))
            mids.append(mid)
        if gpu:
            gpus.append((gpu[0], stop))
            longs.append((gpu[1], stop))
            passes.append(gpu[2])
        out.append("| %s | %s | %s | %s | %s | %s | %s |" % (
            stop, "%.0f" % lo if lo is not None else "", "%.0f" % mid if mid is not None else "",
            "%.2f" % gpu[0] if gpu else "", "%.2f" % gpu[1] if gpu else "", "%.1f" % gpu[2] if gpu else "",
            "%.1f" % gpu[3] if gpu else ""))
    head = []
    head.append("Stops with figures: %d (steps), %d (GPU)." % (len(lows), len(gpus)))
    if lows:
        lows.sort()
        head.append("Steps a second, lowest second of any stop: %.0f (%s); five slowest stops: %s." % (
            lows[0][0], lows[0][1], ", ".join("%s %.0f" % (s, v) for v, s in lows[:5])))
        head.append("Steps a second, middle value over all stops: %.0f; range of the stops' middle values %.0f to %.0f." % (
            statistics.median(mids), min(mids), max(mids)))
        head.append("Stops whose lowest second fell under 60 steps: %d; under 120: %d." % (
            sum(1 for v, _ in lows if v < 60), sum(1 for v, _ in lows if v < 120)))
    if gpus:
        gpus.sort(reverse=True)
        longs.sort(reverse=True)
        head.append("GPU ms a picture: middle %.2f, highest %.2f (%s); five highest: %s." % (
            statistics.median([v for v, _ in gpus]), gpus[0][0], gpus[0][1], ", ".join("%s %.2f" % (s, v) for v, s in gpus[:5])))
        head.append("Longest single picture on the GPU: %.2f ms (%s); stops with a picture over 16.7 ms: %d of %d." % (
            longs[0][0], longs[0][1], sum(1 for v, _ in longs if v > 16.7), len(longs)))
        head.append("Scene passes a picture: %.1f to %.1f, middle %.1f." % (min(passes), max(passes), statistics.median(passes)))
    text = "\n".join(head) + "\n\n" + "\n".join(out) + "\n"
    if len(sys.argv) > 3:
        open(sys.argv[3], "w", encoding="utf8").write(text)
    print("\n".join(head))


main()
