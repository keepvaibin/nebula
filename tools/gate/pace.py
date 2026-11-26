"""pace.py: the pacing measures of docs/plan/00-READ-FIRST.md 4.3 and the planner's bars (brief AD), from the game's [pace] lines.

  pace.py run --bin BIN --work WORK [--renderers d3d12,vulkan,d3d11] [--suite tests/pace/suite.json]
              [--device SERIAL [--device-lock PATH] [--apk APK --pak PAK]] [--lock D:/NebulaWork/plan/measure.lock]
              [--lock-wait MINUTES] [--paks ...] [--diagnostics NAMES] [--history FILE] [--again]
      Takes the measurement lock (waiting --lock-wait minutes for it), runs the suite through the harness
      (tools/lumacore/luma_test.py: real time, a visible window, no sound) on each renderer, or on an Android device,
      then digests the runs. With --apk and --pak the device's run installs the test app with the pak inside the device's
      lock and removes it after, as measure.py's tablet run does. --diagnostics: the game's --diagnostics for every run
      (`pace-fault`: the render thread sleeps 20 ms once every 2 s, the planted stutter the checks must refuse). --again: a
      renderer whose run has a FAIL row is played once more and the second run decides (the first stays in WORK/first).
  pace.py digest WORK [--history FILE]
      Reads every run under WORK (not WORK/first), writes WORK/pace.json and prints the table.
  pace.py keep WORK --history FILE --commit SHA
      Adds each run's figures of WORK/pace.json to FILE, the readings of main that the load row is judged against (the
      gate keeps them after main moved).

The game writes a [pace] line every ten seconds of steps (runtime/include/galaxy/pacing_recorder.h). A run's measured
windows are those written after the harness's "frame 0" line (the test in its stage) except the first after it, which
may have begun before; the others are its load windows (the boot, the title, the stage's load). Each measure gets its
value, its target and pass, FAIL, recorded, or not measured (and why). The bars of brief AD:
  - bar 1, "even pictures" (the PC, VSync on): no missed refresh of the game's and no doubled picture in the measured
    windows. A missed refresh whose present call had returned half a refresh before the refresh it was due at is the
    system's (pacing.h Summary::missed_system): reported beside, not refused.
  - bar 2, "jumps": no two pictures shown one after the other whose (display moment - content moment) differ by more than
    1 ms, except across a seam (a closed wipe, a new swap chain, another refresh period: pacing.h Picture::seam).
  - "loads": the game's missed refreshes and the jumps of the load windows. While LOADS is "main" (until G10.5 lands) a
    run is refused only when either count is above the most of main's last LOAD_READINGS readings of the same run in
    --history (no reading yet: recorded, refused nothing); with LOADS "none" (from G10.5 on) any miss or jump is refused.
One row reads every window from the start, the boot's loads included: each shows at least nine tenths of the pictures its
rate asks for (a lead held high or a stalled schedule shows fewer: R-009, 36 pictures in ten seconds hid before frame 0).
Exit status: 0 when nothing failed and every run gave windows, 1 otherwise; 3 when the measurement lock stayed held.

The gate's two checks (tools/gate/gate.py):
  pace/pc      every landing that builds the game: tests/pace/gate.json (the test match cut to about 30 s of play) on
               Direct3D 12 and Vulkan, --again, the loads judged against the gate's pace-history.tsv; a FAIL row refuses.
  pace/tablet  an extra check by name (docs/plan/native/step-checks.tsv): tests/pace/tablet.json (rate-60, rate-120) on the
               tablet, under the tablet's lock and the measurement lock. Until tests/pace/tablet-base.json is stored (G10.2,
               the baseline report), it records its rows and refuses nothing. From then on it refuses a landing whose late
               share, three-refresh share or unnamed share (G10.2's rows) is worse than the stored baseline's.
"""
import argparse
import datetime
import json
import math
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

TREE = pathlib.Path(__file__).resolve().parents[2]
STEP_MS = 1000.0 / 60.0
PACE = re.compile(r"\[pace\] (.*)$")
FRAME_ZERO = re.compile(r"harness: frame 0 is game frame")
# Display reports must cover this share of a run's pictures before its display measures are judged.
COVERAGE = 0.99
# Every window from the start shows at least this share of the pictures its rate asks for.
SHOWN = 0.9
# The load windows' rule: "main" (refused only above main's readings) until G10.5 lands; "none" (any miss or jump) from it on.
LOADS = "main"
LOAD_READINGS = 5
HISTORY_COLUMNS = ("time", "commit", "run", "load_missed", "load_jumps", "load_windows", "missed", "missed_system", "jumps")
# A first run kept beside the one that decides (--again): WORK/first/<renderer>; digest leaves it out.
FIRST = "first"
# The exit status when the measurement lock stayed held (the machine's, not the change's: the gate's kind env).
LOCK_HELD = 3


def windows_of(log_text, from_start=False):
    """The run's measured [pace] windows, each a dict of its numbers; with from_start every window, the boot's too."""
    after = False
    seen_after = 0
    out = []
    for line in log_text.splitlines():
        if FRAME_ZERO.search(line):
            after, seen_after = True, 0
            continue
        m = PACE.search(line)
        if not m or not (after or from_start):
            continue
        seen_after += 1
        if seen_after == 1 and not from_start:
            continue  # may have begun before frame 0
        out.append({k: float(v) for k, v in re.findall(r"([a-z_0-9]+)=([-0-9.]+)", m.group(1))})
    return out


def pictures_asked(window, test):
    """The pictures a window's rate asks for with VSync on: the refresh, or the test's rate on whole refreshes
    (144 on 240 Hz: every second refresh, 120); None where nothing fixes it (VSync off, an unlimited rate)."""
    if setting(test, "GALAXY_VSYNC_INTERVAL", "1") == "0" or window.get("refresh_ms", 0.0) <= 0:
        return None
    refresh = 1000.0 / window["refresh_ms"]
    try:
        rate = float(setting(test, "GALAXY_MAX_FPS", "0"))
    except ValueError:
        return None
    if rate <= 0 or rate >= refresh * 0.99:
        rate = refresh
    else:
        rate = refresh / math.ceil(refresh / rate - 0.01)  # 239.97 Hz over 120: 2
    return rate * window.get("seconds", 0.0)


def shown_from_start(windows, test):
    """The pictures-shown row: every window from the start against what its rate asks for."""
    asked = [(w, pictures_asked(w, test)) for w in windows]
    asked = [(w, a) for w, a in asked if a]
    if not asked:
        return None
    shares = [w.get("pictures", 0.0) / a for w, a in asked]
    worst = min(range(len(shares)), key=lambda i: shares[i])
    missed = sum(w.get("missed", 0.0) for w in windows)
    return ("pictures shown, from the start", "fewest %.0f%% (window %d of %d, %d pictures); %d missed in all" % (
        100 * shares[worst], worst + 1, len(windows), asked[worst][0].get("pictures", 0.0), missed),
        "every window %d%% of its rate" % (100 * SHOWN), "pass" if shares[worst] >= SHOWN else "FAIL")


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
        "doubled": s("doubled"), "missed_system": s("missed_system"),
        # Lines of a build before the jump count (G10.1) carry no jumps: None, not 0.
        "jumps": s("jumps") if all("jumps" in w for w in windows) else None, "jump_max_us": top("jump_max_us"),
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
    # A run folder's test.json holds the runner's record, the test under "test"; a test file holds it at the top.
    for a in test.get("settings", test.get("test", {}).get("settings", [])):
        k, _, v = a.partition("=")
        if k == key:
            return v
    return default


def load_figures(windows):
    """The load windows' (game's missed refreshes, jumps); jumps None where the lines carry none."""
    missed = sum(w.get("missed", 0.0) - w.get("missed_system", 0.0) for w in windows)
    jumps = sum(w.get("jumps", 0.0) for w in windows) if all("jumps" in w for w in windows) else None
    return missed, jumps


def load_row(run, history):
    """The loads row: the load windows' game misses and jumps, against main's readings (LOADS)."""
    loads = run.get("load_windows", [])
    if not loads:
        return None
    missed, jumps = load_figures(loads)
    value = "%d missed, %s jumps in %d windows" % (missed, "?" if jumps is None else "%d" % jumps, len(loads))
    if LOADS == "none":
        ok = missed == 0 and jumps == 0
        return ("loads", value, "no missed refresh, no jump", "pass" if ok else "FAIL")
    readings = history.get(run["run_key"], [])[-LOAD_READINGS:]
    if not readings:
        return ("loads", value, "no reading of main's yet: recorded", "recorded")
    most_missed = max(r["load_missed"] for r in readings)
    most_jumps = max(r["load_jumps"] for r in readings)
    ok = missed <= most_missed and (jumps is None or jumps <= most_jumps)
    return ("loads", value, "at most main's most of its last %d (%d missed, %d jumps)" % (len(readings), most_missed, most_jumps),
            "pass" if ok else "FAIL")


def judge(run, history=None):
    """The rows for one run: (measure, value, target, verdict)."""
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
        game = c["missed"] - c["missed_system"]
        rows.append(("even pictures", "%d missed (%d the system's), %d doubled, %d of %d on time" % (
            c["missed"], c["missed_system"], c["doubled"], c["on_time"], c["displayed"]),
                     "no missed refresh of the game's, none doubled", ("pass" if game == 0 and c["doubled"] == 0 else "FAIL")
                     if covered else uncovered))
    if c["jumps"] is None:
        rows.append(("jumps", "-", "no change over 1 ms between two pictures shown",
                     "not measured: the [pace] lines carry no jumps"))
    else:
        rows.append(("jumps", "%d, largest change %.2f ms" % (c["jumps"], c["jump_max_us"] / 1000.0),
                     "no change over 1 ms between two pictures shown", ("pass" if c["jumps"] == 0 else "FAIL") if covered
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
    shown = shown_from_start(run.get("all_windows", []), test)
    if shown is not None:
        rows.append(shown)
    loads = load_row(run, history or {})
    if loads is not None:
        rows.append(loads)
    if c["generated"] > 0:
        rows.append(("in-between picture's render work", "%.3f ms average, %.3f p99" % (
            c["generated_render_avg_us"] / 1000.0, c["generated_render_p99_us"] / 1000.0),
            "does not grow with the objects drawn", "compared across stages"))
    return rows


def read_history(path):
    """{run: [reading]} of a history file (HISTORY_COLUMNS), oldest first; {} where there is none."""
    out = {}
    if not path or not pathlib.Path(path).is_file():
        return out
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines()[1:]:
        cells = line.split("\t")
        if len(cells) != len(HISTORY_COLUMNS):
            continue
        row = dict(zip(HISTORY_COLUMNS, cells))
        try:
            reading = {"commit": row["commit"], "load_missed": float(row["load_missed"]),
                       "load_jumps": float(row["load_jumps"]) if row["load_jumps"] != "-" else 0.0}
        except ValueError:
            continue
        out.setdefault(row["run"], []).append(reading)
    return out


def read_runs(work, history=None):
    """Every run under WORK (not WORK/first), each with its figures and rows."""
    work = pathlib.Path(work)
    runs = []
    for log in sorted(work.rglob("run.log")):
        folder = log.parent
        if folder.relative_to(work).parts[0] == FIRST:
            continue
        test_file = folder / "test.json"
        test = json.loads(test_file.read_text(encoding="utf-8")) if test_file.is_file() else {}
        device = (folder / "exit-info.txt").is_file() or (folder / "diagnostics.txt").is_file()
        text = log.read_text(encoding="utf-8", errors="replace")
        measured, every = windows_of(text), windows_of(text, from_start=True)
        run = {"run": str(folder.relative_to(work)), "run_key": "%s/%s" % (folder.parent.name, folder.name), "test": test,
               "renderer": folder.name, "device": device, "figures": combine(measured), "all_windows": every,
               "load_windows": every[:len(every) - len(measured)]}
        missed, jumps = load_figures(run["load_windows"])
        run["loads"] = {"windows": len(run["load_windows"]), "missed": missed, "jumps": jumps}
        run["rows"] = judge(run, history)
        runs.append(run)
    return runs


def failed_rows(run):
    return [r for r in run["rows"] if r[3] == "FAIL"]


def digest(work, history_path=None):
    work = pathlib.Path(work)
    runs = read_runs(work, read_history(history_path))
    failed = not runs
    for run in runs:
        print("%s (%s)" % (run["run"], ", ".join(run["test"].get("settings", [])) or "default settings"))
        for measure, value, target, verdict in run["rows"]:
            print("  %-34s %-46s %-50s %s" % (measure, value, target, verdict))
            failed |= verdict == "FAIL"
    first = work / FIRST
    if first.is_dir():
        for run in read_runs(first, read_history(history_path)):
            bad = failed_rows(run)
            if bad:
                print("played again: the first run %s failed: %s" % (run["run"], "; ".join("%s %s" % (r[0], r[1]) for r in bad)))
    (work / "pace.json").write_text(json.dumps([{k: v for k, v in r.items() if k not in ("test", "all_windows", "load_windows")}
                                                for r in runs], indent=1), encoding="utf-8")
    return 1 if failed else 0


def keep(work, history_path, commit):
    """Each run's figures of WORK/pace.json added to the history (HISTORY_COLUMNS)."""
    runs = json.loads((pathlib.Path(work) / "pace.json").read_text(encoding="utf-8"))
    path = pathlib.Path(history_path)
    new = not path.is_file()
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    with open(path, "a", encoding="utf-8", newline="\n") as f:
        if new:
            f.write("\t".join(HISTORY_COLUMNS) + "\n")
        for run in runs:
            c, loads = run.get("figures") or {}, run.get("loads") or {}
            if not c or not loads.get("windows"):
                continue
            cells = (now, commit[:9], run["run_key"], "%d" % loads["missed"],
                     "-" if loads.get("jumps") is None else "%d" % loads["jumps"], "%d" % loads["windows"],
                     "%d" % c.get("missed", 0), "%d" % c.get("missed_system", 0),
                     "-" if c.get("jumps") is None else "%d" % c["jumps"])
            f.write("\t".join(cells) + "\n")
    print("pace.py: the readings of %s kept in %s" % (commit[:9], path))
    return 0


def take_lock(path, who, wait_minutes=0.0):
    lock = pathlib.Path(path)
    until = time.time() + 60.0 * wait_minutes
    while True:
        try:
            lock.mkdir()
            break
        except FileExistsError:
            if time.time() < until:
                time.sleep(10)
                continue
            holder = lock / "holder.txt"
            print("pace.py: the measurement lock %s is held: %s" % (
                lock, holder.read_text(encoding="utf-8", errors="replace").strip() if holder.exists() else "unknown"), file=sys.stderr)
            sys.exit(LOCK_HELD)
    (lock / "holder.txt").write_text("%s %s\n" % (who, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")),
                                     encoding="utf-8")


def play(args, renderer, work):
    """One harness run of the suite on a renderer (or the device), into work/<renderer>: the harness's exit code."""
    command = [sys.executable, "-X", "utf8", "-I", str(TREE / "tools/lumacore/luma_test.py"), "run", args.suite,
               "--bin", args.bin, "--work", str(work / renderer), "--who", "pace", "--renderer", renderer]
    if args.diagnostics:
        command += ["--diagnostics", args.diagnostics]
    env = dict(os.environ)
    if args.paks:
        env["LUMA_TEST_PAKS"] = args.paks
    if args.device and args.apk:
        # The test app installed with the pak inside the device's lock and removed after (measure.py's tablet run).
        sys.path.insert(0, str(TREE / "tools" / "measure"))
        import measure
        tablet = argparse.Namespace(device=args.device, device_lock=args.device_lock, device_wait=args.device_wait,
                                    commit="pace", apk=args.apk, pak=args.pak)
        code, why = measure.on_tablet(tablet, command, env)
        if code is None:
            print("pace.py: the device's run did not start: %s" % why, flush=True)
            return 3
        return code
    if args.device:
        command += ["--device", args.device] + (["--device-lock", args.device_lock] if args.device_lock else [])
    return subprocess.run(command, env=env, check=False).returncode


def run(args):
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    take_lock(args.lock, "pace.py %s" % work, args.lock_wait)
    codes = {}
    history = read_history(args.history)
    try:
        renderers = ["vulkan"] if args.device else args.renderers.split(",")
        for renderer in renderers:
            codes[renderer] = play(args, renderer, work)
            if args.again and any(failed_rows(r) for r in read_runs(work / renderer, history)):
                (work / FIRST).mkdir(exist_ok=True)
                shutil.rmtree(work / FIRST / renderer, ignore_errors=True)
                shutil.move(str(work / renderer), str(work / FIRST / renderer))
                print("pace.py: %s had a FAIL row: played once more" % renderer, flush=True)
                codes[renderer] = play(args, renderer, work)
    finally:
        shutil.rmtree(args.lock, ignore_errors=True)
        (work / "pace-run.json").write_text(json.dumps({"harness_exit": codes}, indent=1), encoding="utf-8")
    return digest(work, args.history)


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
    r.add_argument("--device-wait", type=float, default=20.0, help="minutes to wait for a held device lock (with --apk)")
    r.add_argument("--apk", default="", help="with --device: the test app to install for the run, with --pak")
    r.add_argument("--pak", default="")
    r.add_argument("--lock", default="D:/NebulaWork/plan/measure.lock")
    r.add_argument("--lock-wait", type=float, default=0.0, help="minutes to wait for a held measurement lock")
    r.add_argument("--paks", default=os.environ.get("LUMA_TEST_PAKS", ""))
    r.add_argument("--diagnostics", default="", help="the game's --diagnostics for every run (pace-fault: the planted stutter)")
    r.add_argument("--history", default="", help="main's readings the load row is judged against (pace.py keep writes them)")
    r.add_argument("--again", action="store_true", help="a renderer with a FAIL row is played once more; the second decides")
    d = sub.add_parser("digest")
    d.add_argument("work")
    d.add_argument("--history", default="")
    k = sub.add_parser("keep")
    k.add_argument("work")
    k.add_argument("--history", required=True)
    k.add_argument("--commit", required=True)
    args = p.parse_args()
    if args.command == "run":
        if args.apk and not (args.device and args.pak):
            p.error("--apk needs --device and --pak")
        sys.exit(run(args))
    if args.command == "keep":
        sys.exit(keep(args.work, args.history, args.commit))
    sys.exit(digest(args.work, args.history))


if __name__ == "__main__":
    main()
