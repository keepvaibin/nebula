"""pace.py: the pacing measures of docs/plan/00-READ-FIRST.md 4.3, from the game's own [pace] lines.

  pace.py run --bin BIN --work WORK [--renderers d3d12,vulkan,d3d11] [--suite tests/pace/suite.json]
              [--device SERIAL [--device-lock PATH]] [--lock D:/NebulaWork/plan/measure.lock] [--paks ...]
      Takes the measurement lock, runs the suite through the harness (tools/lumacore/luma_test.py: real time, a
      visible window, no sound) on each renderer, or on an Android device, then digests the runs.
  pace.py digest WORK
      Reads every run under WORK, writes WORK/pace.json and prints the table.

The game writes a [pace] line every ten seconds of steps (runtime/include/galaxy/pacing_recorder.h). A run's measured
windows are those written after the harness's "frame 0" line (the test in its stage) except the first after it, which
may have begun before. Each measure of 4.3 gets its value, its target and pass, FAIL, or not measured (and why).
Exit status: 0 when nothing failed and every run gave windows, 1 otherwise.
"""
import argparse
import datetime
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

TREE = pathlib.Path(__file__).resolve().parents[2]
STEP_MS = 1000.0 / 60.0
PACE = re.compile(r"\[pace\] (.*)$")
FRAME_ZERO = re.compile(r"harness: frame 0 is game frame")
# Display reports must cover this share of a run's pictures before its display measures are judged.
COVERAGE = 0.99


def windows_of(log_text):
    """The run's measured [pace] windows, each a dict of its numbers."""
    after = False
    seen_after = 0
    out = []
    for line in log_text.splitlines():
        if FRAME_ZERO.search(line):
            after, seen_after = True, 0
            continue
        m = PACE.search(line)
        if not m or not after:
            continue
        seen_after += 1
        if seen_after == 1:
            continue  # may have begun before frame 0
        out.append({k: float(v) for k, v in re.findall(r"([a-z_0-9]+)=([-0-9.]+)", m.group(1))})
    return out


def combine(windows):
    """One run's windows as one: counts summed, shares and averages weighted, percentiles and longest the largest."""
    if not windows:
        return None
    s = lambda k: sum(w.get(k, 0.0) for w in windows)
    top = lambda k: max(w.get(k, 0.0) for w in windows)

    def weighted(k, weight):
        total = sum(weight(w) for w in windows)
        return sum(w.get(k, 0.0) * weight(w) for w in windows) / total if total > 0 else 0.0
    seconds = s("seconds")
    return {
        "windows": len(windows), "seconds": seconds, "steps": s("steps"),
        "steps_per_second": sum(w["steps"] - 1 for w in windows) / seconds if seconds > 0 else 0.0,
        "pictures": s("pictures"), "displayed": s("displayed"), "generated": s("generated"),
        "refresh_ms": windows[-1].get("refresh_ms", 0.0), "on_time": s("on_time"), "missed": s("missed"),
        "doubled": s("doubled"),
        "animation_within_1ms": weighted("animation_within_1ms", lambda w: w.get("displayed", 0.0)),
        "animation_p99_us": top("animation_p99_us"),
        "delay_avg_us": weighted("delay_avg_us", lambda w: w.get("displayed", 0.0)), "delay_p99_us": top("delay_p99_us"),
        "render_avg_us": weighted("render_avg_us", lambda w: w.get("pictures", 0.0) - w.get("generated", 0.0)),
        "render_p99_us": top("render_p99_us"),
        "generated_render_avg_us": weighted("generated_render_avg_us", lambda w: w.get("generated", 0.0)),
        "generated_render_p99_us": top("generated_render_p99_us"),
        "step_work_avg_us": weighted("step_work_avg_us", lambda w: w.get("steps", 0.0)),
        "step_work_p99_us": top("step_work_p99_us"), "step_work_max_us": top("step_work_max_us"),
    }


def setting(test, key, default=""):
    for a in test.get("settings", []):
        k, _, v = a.partition("=")
        if k == key:
            return v
    return default


def judge(run):
    """The 4.3 rows for one run: (measure, value, target, verdict)."""
    c, test, on_device = run["figures"], run["test"], run["device"]
    rows = []
    if c is None:
        return [("all", "no measured [pace] window", "", "FAIL")]
    vsync = setting(test, "GALAXY_VSYNC_INTERVAL", "1") != "0"
    refresh = c["refresh_ms"]
    covered = c["pictures"] > 0 and c["displayed"] >= COVERAGE * c["pictures"]
    uncovered = "not measured: the renderer reported %d of %d pictures shown" % (c["displayed"], c["pictures"])
    rate = c["steps_per_second"]
    rows.append(("game speed", "%.4f steps a second" % rate, "60.000 within 0.01%",
                 "pass" if abs(rate - 60.0) <= 0.006 else "FAIL"))
    if vsync and not on_device:
        rows.append(("even pictures", "%d missed, %d doubled, %d of %d on time" % (c["missed"], c["doubled"], c["on_time"],
                                                                                  c["displayed"]),
                     "no missed refresh", ("pass" if c["missed"] == 0 and c["doubled"] == 0 else "FAIL") if covered
                     else uncovered))
    rows.append(("animation error", "%.2f%% within 1 ms (p99 %.0f us)" % (100 * c["animation_within_1ms"],
                                                                       c["animation_p99_us"]),
                 "99 of 100 within 1 ms", ("pass" if c["animation_within_1ms"] >= 0.99 else "FAIL") if covered
                 else uncovered))
    work = c["step_work_p99_us"] / 1000.0
    delay = c["delay_p99_us"] / 1000.0
    if c["generated"] > 0:
        bound = STEP_MS + refresh + work
        target = "one step + one refresh + the step's work (%.2f ms)" % bound
    else:
        bound = work + refresh
        target = "the step's work + one refresh (%.2f ms)" % bound
    rows.append(("delay, in-between pictures %s" % ("on" if c["generated"] > 0 else "off"),
                 "p99 %.2f ms (average %.2f)" % (delay, c["delay_avg_us"] / 1000.0), target,
                 ("pass" if delay <= bound else "FAIL") if covered and refresh > 0 else uncovered))
    if not on_device:
        longest = c["step_work_max_us"] / 1000.0
        rows.append(("long step", "longest %.2f ms (p99 %.2f)" % (longest, work), "none over 18 ms",
                     "pass" if longest <= 18.0 else "FAIL"))
    else:
        hz = 1000.0 / refresh if refresh > 0 else 0.0
        if 110 <= hz <= 130:
            share = c["on_time"] / c["displayed"] if c["displayed"] else 0.0
            rows.append(("tablet at 120 Hz", "%.2f%% one refresh after the last" % (100 * share), "99 of 100",
                         ("pass" if share >= 0.99 else "FAIL") if covered else uncovered))
        elif 55 <= hz <= 65:
            ok = c["missed"] == 0 and c["doubled"] == 0 and delay <= 2 * refresh
            rows.append(("tablet at 60 Hz", "%d missed, %d doubled, delay p99 %.2f ms" % (c["missed"], c["doubled"], delay),
                         "every picture at its time; two refreshes from step to display", ("pass" if ok else "FAIL")
                         if covered else uncovered))
    if c["generated"] > 0:
        rows.append(("in-between picture's render work", "%.3f ms average, %.3f p99" % (
            c["generated_render_avg_us"] / 1000.0, c["generated_render_p99_us"] / 1000.0),
            "does not grow with the objects drawn", "compared across stages"))
    return rows


def digest(work):
    work = pathlib.Path(work)
    runs = []
    for log in sorted(work.rglob("run.log")):
        folder = log.parent
        test_file = folder / "test.json"
        test = json.loads(test_file.read_text(encoding="utf-8")) if test_file.is_file() else {}
        device = (folder / "exit-info.txt").is_file() or (folder / "diagnostics.txt").is_file()
        run = {"run": str(folder.relative_to(work)), "test": test, "renderer": folder.name, "device": device,
               "figures": combine(windows_of(log.read_text(encoding="utf-8", errors="replace")))}
        run["rows"] = judge(run)
        runs.append(run)
    failed = not runs
    for run in runs:
        print("%s (%s)" % (run["run"], ", ".join(run["test"].get("settings", [])) or "default settings"))
        for measure, value, target, verdict in run["rows"]:
            print("  %-34s %-46s %-50s %s" % (measure, value, target, verdict))
            failed |= verdict == "FAIL"
    (work / "pace.json").write_text(json.dumps([{k: v for k, v in r.items() if k != "test"} for r in runs], indent=1),
                                    encoding="utf-8")
    return 1 if failed else 0


def take_lock(path, who):
    lock = pathlib.Path(path)
    try:
        lock.mkdir()
    except FileExistsError:
        holder = lock / "holder.txt"
        sys.exit("pace.py: the measurement lock %s is held: %s" % (
            lock, holder.read_text(encoding="utf-8", errors="replace").strip() if holder.exists() else "unknown"))
    (lock / "holder.txt").write_text("%s %s\n" % (who, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")),
                                     encoding="utf-8")


def run(args):
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    take_lock(args.lock, "pace.py %s" % work)
    try:
        renderers = ["vulkan"] if args.device else args.renderers.split(",")
        for renderer in renderers:
            command = [sys.executable, "-X", "utf8", "-I", str(TREE / "tools/lumacore/luma_test.py"), "run", args.suite,
                       "--bin", args.bin, "--work", str(work / renderer), "--who", "pace", "--renderer", renderer]
            if args.device:
                command += ["--device", args.device] + (["--device-lock", args.device_lock] if args.device_lock else [])
            env = dict(os.environ)
            if args.paks:
                env["LUMA_TEST_PAKS"] = args.paks
            subprocess.run(command, env=env, check=False)
    finally:
        shutil.rmtree(args.lock, ignore_errors=True)
    return digest(work)


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run")
    r.add_argument("--bin", required=True)
    r.add_argument("--work", required=True)
    r.add_argument("--renderers", default="d3d12,vulkan,d3d11")
    r.add_argument("--suite", default=str(TREE / "tests/pace/suite.json"))
    r.add_argument("--device", default="")
    r.add_argument("--device-lock", default="")
    r.add_argument("--lock", default="D:/NebulaWork/plan/measure.lock")
    r.add_argument("--paks", default=os.environ.get("LUMA_TEST_PAKS", ""))
    d = sub.add_parser("digest")
    d.add_argument("work")
    args = p.parse_args()
    sys.exit(run(args) if args.command == "run" else digest(args.work))


if __name__ == "__main__":
    main()
