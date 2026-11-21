"""measure.py: the play route measure (docs/plan 07-BASE-FIRST stage 1 item 1, N-1043): one heavy stage in real time,
three minutes from the stage's first frame, on each renderer, with one line per run in the measure history.

  measure.py run --bin BIN --work WORK --commit SHA [--runs d3d12,vulkan,d3d11,d3d12-4cores,tablet]
                 [--device SERIAL --android-bin BIN --apk APK --pak PAK [--device-lock PATH]] [--history FILE]
                 [--paks ...] [--quiet-minutes 30]
                 [--gate-lock D:/NebulaWork/plan/gate.lock --board FILE --who NAME] [--lock measure.lock]
      Takes the gate lock (through its queue, so no gate, long run part or gate build runs beside it; none when the
      caller holds it) and the measurement lock, then measures each run named (real time, a visible window, no sound):
      it first waits for a quiet machine (quiet.py: no compiler, build tool or other copy of the game, looking every
      30 seconds for up to 30 minutes), reads the graphics card's use by other programs before and after, runs
      tests/measure/route through the harness, and compares each figure with the median of the last ten trusted runs
      of the same key (band.py: computer, card, run kind, renderer, scene, picture size, rate). A figure outside its
      band on the bad side is measured again once (only the runs that were worse); worse again is WORSE. Each run's
      line is added to the history (band.py names its columns). A run that was not quiet (a build or another game
      running, the card used by other programs for more than 10 %, the window reached: P-328) is printed
      `not trusted` with its reason, compared with nothing and not written.
      Writes WORK/measure-verdict.json (each figure's value, median, band, verdict and mark) and ends with a line
      "measure: ...". Exit: 0 ran (ok, better, no baseline yet), 1 WORSE (twice), 2 could not run or could not read
      its own results, 3 not trusted.
      gate.py starts it after every move of main (detached, on a copy of the moved commit's build) and in the long run.
  measure.py accept --commit SHA [--renderer R] [--scene S] [--history FILE]
      Restarts the baseline from SHA for those keys (all when none is given): the lines before it stop counting, and
      its `worse` figures from it on become trusted. For a slowdown that was looked at and accepted.
  measure.py digest RUN_FOLDER [--commit SHA --device NAME --history FILE]
      One run's figures (and its history line when --history is given).

A run named d3d12-4cores holds the game to four logical processors (the harness's processor mask, which the game
inherits). The tablet's run stands alone (P-321): inside the device's lock (waited for up to --device-wait minutes)
the test app APK is installed with PAK (luma_test_android.py prepare), the route runs, and the test app is removed,
also when the run fails; the player's game is never touched. It sets what 120 Hz needs (power saving off, Motion
smoothness adaptive) for the run and puts both back after it; no other setting of the tablet is touched. The
quiet-machine checks are for the PC's runs: the tablet's figures do not depend on the PC's load.

The figures, from the stage's first frame (the harness's "frame 0") to the run's end:
  rate            the pictures a second asked for; refresh_hz the display's refresh as the renderer reported it
  gap_max_ms      the longest time between two presents
  gap_p99_ms      the time between two presents that 99 of 100 stay under
  late_per_min    presents that missed their refresh (the [pace] lines' missed), per minute
  step_max_ms     the longest game step's work (step start to the step clock's wait)
  steps_over_18   game steps whose work took over 18 ms
  render_ms       the render thread's work per picture (a list's walk), median
  gpu_ms          the GPU's time per picture: its command lists' time over the presents. A renderer that records no
                  GPU times in the timeline gives `not measured` (written -, never 0), compared with nothing
  memory_mb       the most memory the process held (the harness's process.memory facts)
Every figure is better lower (band.py FIGURES names the bad side of each); each is compared on its own.
"""
import argparse
import ctypes
import datetime
import json
import os
import pathlib
import re
import shutil
import socket
import statistics
import struct
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import band  # noqa: E402
import quiet  # noqa: E402

TREE = HERE.parents[1]
ROUTE = TREE / "tests" / "measure" / "route"
HISTORY = pathlib.Path("D:/NebulaWork/build/gate-runs/measure-history.tsv")
COLUMNS = band.COLUMNS
COMPARED = tuple(band.FIGURES)
VERDICT_FILE = "measure-verdict.json"
EXIT_RAN, EXIT_WORSE, EXIT_TOOL, EXIT_NOT_TRUSTED = 0, 1, 2, 3
RUNS ={"d3d12": ("d3d12", 0), "vulkan": ("vulkan", 0), "d3d11": ("d3d11", 0), "d3d12-4cores": ("d3d12", 4),
        "tablet": ("vulkan", 0)}
TAB = "\t"
FRAME_ZERO = re.compile(r"harness: frame 0 is game frame (\d+)")
PACE = re.compile(r"\[pace\] (.*)$")
# frame_timeline.h's events.
GPU_BEGIN, GPU_END, PRESENT_END, GAME_FRAME, WALK_BEGIN, WALK_END, WAIT_BEGIN, WAIT_END = 7, 8, 15, 26, 29, 30, 31, 32


def load_timeline(path):
    """(ticks a second, [(qpc, event, a, b)] in time order)."""
    with open(path, "rb") as f:
        header = f.read(64)
        if header[:8] != b"NBTL0001":
            raise ValueError("%s is not a Nebula timeline" % path)
        _version, freq, count, _written, _pid = struct.unpack_from("<5Q", header, 8)
        data = f.read(count * 32)
    records = [(q, e, a, b) for q, a, b, _t, e, v in struct.iter_unpack("<QQQIHH", data) if v]
    records.sort(key=lambda r: r[0])
    return freq, records


def quantile(values, p):
    v = sorted(values)
    return v[min(len(v) - 1, int(p * (len(v) - 1) + 0.5))]


def pace_windows(log_text):
    """The [pace] windows written after frame 0, the first after it left out (it may have begun before)."""
    out, after, seen = [], False, 0
    for line in log_text.splitlines():
        if FRAME_ZERO.search(line):
            after, seen = True, 0
            continue
        m = PACE.search(line)
        if m and after:
            seen += 1
            if seen > 1:
                out.append({k: float(v) for k, v in re.findall(r"([a-z_0-9]+)=([-0-9.]+)", m.group(1))})
    return out


def timeline_figures(path, first_frame):
    """The timeline's figures from game frame first_frame on: present gaps, steps, walks and GPU times (ms)."""
    freq, records = load_timeline(path)
    ms = 1000.0 / freq
    start = next((q for q, e, a, _b in records if e == GAME_FRAME and a >= first_frame), None)
    if start is None:
        return {}
    records = [r for r in records if r[0] >= start]
    presents = [q for q, e, _a, _b in records if e == PRESENT_END]
    gaps = [(b - a) * ms for a, b in zip(presents, presents[1:])]
    # A step's work: from its wait's end (the step starts) to the next wait's start.
    steps, began = [], None
    walks, walk = [], None
    for q, e, _a, _b in records:
        if e == WAIT_END:
            began = q
        elif e == WAIT_BEGIN and began is not None:
            steps.append((q - began) * ms)
            began = None
        elif e == WALK_BEGIN:
            walk = q
        elif e == WALK_END and walk is not None:
            walks.append((q - walk) * ms)
            walk = None
    gpu_begin = {a: q for q, e, a, _b in records if e == GPU_BEGIN}
    gpu = [(q - gpu_begin[a]) * ms for q, e, a, _b in records if e == GPU_END and a in gpu_begin and q > gpu_begin[a]]
    f = {}
    if gaps:
        f["gap_max_ms"], f["gap_p99_ms"] = max(gaps), quantile(gaps, 0.99)
    if steps:
        f["step_max_ms"], f["steps_over_18"] = max(steps), sum(s > 18.0 for s in steps)
    if walks:
        f["render_ms"] = statistics.median(walks)
    if gpu and presents and sum(gpu) > 0:  # no GPU times, or all zero: not measured, never 0
        f["gpu_ms"] = sum(gpu) / len(presents)
    return f


def figures(run_folder):
    """One run's figures: the timeline's, the [pace] lines' and the memory facts'. Missing ones are left out."""
    run_folder = pathlib.Path(run_folder)
    log = (run_folder / "run.log").read_text(encoding="utf-8", errors="replace")
    zero = FRAME_ZERO.search(log)
    f = {}
    windows = pace_windows(log)
    if windows:
        seconds = sum(w.get("seconds", 0.0) for w in windows)
        f["late_per_min"] = 60.0 * sum(w.get("missed", 0.0) for w in windows) / seconds if seconds else 0.0
        refresh = statistics.median([w["refresh_ms"] for w in windows if w.get("refresh_ms", 0.0) > 0] or [0.0])
        if refresh:
            f["refresh_hz"] = 1000.0 / refresh
        # Without a timeline (a device that wrote none): the [pace] lines' own step and render figures.
        f["step_max_ms"] = max(w.get("step_work_max_us", 0.0) for w in windows) / 1000.0
        f["render_ms"] = statistics.median(w.get("render_avg_us", 0.0) for w in windows) / 1000.0
    timeline = run_folder / "timeline.bin"
    if zero and timeline.is_file():
        f.update(timeline_figures(timeline, int(zero.group(1))))
    memory = []
    facts = run_folder / "facts.jsonl"
    if facts.is_file():
        for line in facts.read_text(encoding="utf-8").splitlines():
            try:
                fact = json.loads(line)
            except ValueError:
                continue
            if fact.get("key") == "process.memory" and isinstance(fact.get("value"), dict):
                memory.append(fact["value"].get("bytes", 0))
    if memory:
        f["memory_mb"] = max(memory) / (1024.0 * 1024.0)
    return f


def scene_of(run_folder):
    """The scene a run measured: the stage (name:scenario) or the stop of the test's description (test.json, which the
    harness copies into the run's folder), else the test's name."""
    try:
        test = json.loads((pathlib.Path(run_folder) / "test.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return "unknown"
    stage = test.get("stage")
    if isinstance(stage, dict) and stage.get("name"):
        return "%s:%s" % (stage["name"], stage.get("scenario", 1))
    return str(test.get("stop") or test.get("name") or "unknown")


def run_context(run_folder, computer):
    """The parts of a run's key that come from its folder: the graphics card's name and the picture size (the last
    `[Renderer] internal resolution A -> B` line: B) from run.log, and the scene."""
    try:
        log = (pathlib.Path(run_folder) / "run.log").read_text(encoding="utf-8", errors="replace")
    except OSError:
        log = ""
    adapter = re.search(r'\[gpu\] adapter="([^"]*)"', log)
    sizes = re.findall(r"\[Renderer\] internal resolution \d+x\d+ -> (\d+x\d+)", log)
    return {"computer": computer, "gpu": adapter.group(1) if adapter else "unknown",
            "scene": scene_of(run_folder), "size": sizes[-1] if sizes else "unknown"}


def history_row(f, commit, device, renderer, rate, run_folder, now=None, context=None, marks=None):
    """One run's history line as a dict (band.COLUMNS). A figure not measured is -. `marks`: {figure: mark}."""
    def num(key, form="%.2f"):
        return form % f[key] if key in f else "-"
    context = context or {}
    return {"commit": commit, "time": (now or datetime.datetime.now()).strftime("%Y-%m-%d %H:%M"), "device": device,
            "renderer": renderer, "rate": rate, "refresh_hz": num("refresh_hz", "%.1f"), "gap_max_ms": num("gap_max_ms"),
            "gap_p99_ms": num("gap_p99_ms"), "late_per_min": num("late_per_min"), "step_max_ms": num("step_max_ms"),
            "steps_over_18": num("steps_over_18", "%d"), "render_ms": num("render_ms", "%.3f"),
            "gpu_ms": num("gpu_ms", "%.3f"), "memory_mb": num("memory_mb", "%.0f"), "run": str(run_folder),
            "computer": context.get("computer", ""), "gpu": context.get("gpu", ""), "scene": context.get("scene", ""),
            "size": context.get("size", ""), "marks": band.format_marks(marks or {})}


def history_line(f, commit, device, renderer, rate, run_folder, now=None, context=None, marks=None):
    row = history_row(f, commit, device, renderer, rate, run_folder, now, context, marks)
    return TAB.join(row[c] for c in COLUMNS)


def add_history(path, line):
    """A history line (a dict, or the text of history_line) added to the file."""
    band.add_row(path, line if isinstance(line, dict) else band.row_of(line.split(TAB)))


def read_history(path):
    return band.read_history(path)


def window_disturbed(run_folder):
    """How the player's desktop reached a visible run's window (the harness's window-watch.txt, P-328), or None: a run
    whose window was minimized, active or under the pointer is not compared."""
    try:
        text = (pathlib.Path(run_folder) / "window-watch.txt").read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return "; ".join(text.splitlines()) or None


def run_folder_of(work, renderer):
    found = sorted(pathlib.Path(work).rglob("run.log"), key=lambda p: p.stat().st_mtime)
    found = [p.parent for p in found if p.parent.name == renderer]
    return found[-1] if found else None


def take_lock(path, who):
    lock = pathlib.Path(path)
    try:
        lock.mkdir()
    except FileExistsError:
        holder = lock / "holder.txt"
        raise SystemExit("measure.py: the measurement lock %s is held: %s" % (
            lock, holder.read_text(encoding="utf-8", errors="replace").strip() if holder.exists() else "unknown"))
    (lock / "holder.txt").write_text("%s %s\n" % (who, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")),
                                     encoding="utf-8")


def processor_mask(cores):
    """Windows: this process's processor mask set to its first `cores` processors (its children inherit it); the
    old mask, to put back. None elsewhere."""
    if os.name != "nt" or not cores:
        return None
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.GetCurrentProcess.restype = ctypes.c_void_p
    process = ctypes.c_void_p(k32.GetCurrentProcess())
    mine, system = ctypes.c_size_t(), ctypes.c_size_t()
    k32.GetProcessAffinityMask(process, ctypes.byref(mine), ctypes.byref(system))
    want, have = 0, 0
    for bit in range(64):
        if system.value >> bit & 1 and have < cores:
            want, have = want | 1 << bit, have + 1
    if not k32.SetProcessAffinityMask(process, ctypes.c_size_t(want)):
        raise SystemExit("measure.py: the processor mask %#x was refused" % want)
    return mine.value


def restore_mask(old):
    if os.name == "nt" and old:
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.GetCurrentProcess.restype = ctypes.c_void_p
        k32.SetProcessAffinityMask(ctypes.c_void_p(k32.GetCurrentProcess()), ctypes.c_size_t(old))


class Tablet:
    """The tablet's two settings a 120 Hz run needs, read first and put back after (owner, 2026-11-18 10:24):
    power saving off, and Motion smoothness adaptive (Samsung's refresh_rate_mode 1, up to 120 Hz)."""
    SETTINGS = (("global", "low_power", "0"), ("secure", "refresh_rate_mode", "1"))

    def __init__(self, device):
        self.device = device
        self.before = {}

    def get(self, space, name):
        return self.device.shell("settings get %s %s" % (space, name))[1].strip()

    def now(self):
        return "; ".join("%s %s" % (name, self.get(space, name)) for space, name, _value in self.SETTINGS)

    def set_for_run(self):
        for space, name, value in self.SETTINGS:
            self.before[(space, name)] = self.get(space, name)
            self.device.shell("settings put %s %s %s" % (space, name, value))
        return self.now()

    def put_back(self):
        for (space, name), value in self.before.items():
            if value in ("", "null"):
                self.device.shell("settings delete %s %s" % (space, name))
            else:
                self.device.shell("settings put %s %s %s" % (space, name, value))
        return self.now()


def gate_module():
    sys.path.insert(0, str(TREE / "tools" / "gate"))
    import gate
    return gate


def on_tablet(args, command, env, runner=subprocess.run, device=None, gate=None):
    """The tablet's run, standing alone (P-321): inside the device's lock, the test app installed with the pak
    (luma_test_android.py prepare), the route run, then the test app removed, also when the run or the install fails.
    (the harness's exit code, None), or (None, why it did not run)."""
    if device is None:
        sys.path.insert(0, str(TREE / "tools" / "lumacore"))
        import luma_test_android
        device = luma_test_android.Device(args.device)
    if not device.answers():
        return None, "%s does not answer adb" % args.device
    if args.device_lock:
        held = (gate or gate_module()).take_device_lock_wait(
            args.device_lock, "measure.py %s pid %d" % (args.commit[:9], os.getpid()), args.device_wait)
        if held is not None:
            return None, "the tablet's lock stayed held %g minutes: %s" % (args.device_wait, held)
    # This run holds the lock: the steps below take none of their own.
    env = {k: v for k, v in env.items() if k != "LUMA_TEST_DEVICE_LOCK"}
    android = [sys.executable, "-X", "utf8", "-I", str(TREE / "tools/lumacore/luma_test_android.py")]
    try:
        prepared = runner(android + ["prepare", "--device", args.device, "--apk", args.apk, "--pak", args.pak, "--who",
                                     "measure"], env=env, check=False).returncode
        if prepared != 0:
            return None, "the test app was not prepared (exit %d)" % prepared
        tablet = Tablet(device)
        print("measure.py: the tablet for the run: %s" % tablet.set_for_run(), flush=True)
        try:
            return runner(command + ["--device", args.device], env=env, check=False).returncode, None
        finally:
            print("measure.py: the tablet put back: %s" % tablet.put_back(), flush=True)
    finally:
        removed = runner(android + ["remove", "--device", args.device], env=env, check=False).returncode
        if removed != 0:
            print("measure.py: the test app may still be on %s (remove exit %d)" % (args.device, removed), flush=True)
        if args.device_lock:
            shutil.rmtree(args.device_lock, ignore_errors=True)


def judge_figures(row, figures_found, earlier_rows):
    """[{figure, value, median, band, verdict}] for every compared figure of one run (history row `row`, its figures):
    against the median of the last ten trusted lines of the same key (band.py)."""
    key = band.key_of(row)
    out = []
    for name in COMPARED:
        value = figures_found.get(name)
        verdict = band.judge(value, band.trusted_values(earlier_rows, key, name), band.FIGURES[name])
        out.append({"figure": name, "value": value, "median": verdict["median"], "band": verdict["band"],
                    "verdict": verdict["verdict"], "earlier": verdict.get("earlier", 0)})
    return out


def say_number(v):
    return "-" if v is None else ("%.6g" % v)


def print_run(label, device_name, renderer, code, verdicts):
    print("%s (%s %s, harness exit %d):" % (label, device_name, renderer, code))
    for v in verdicts:
        what = v["verdict"]
        if v["median"] is not None:
            what += " (median %s, band %s)" % (say_number(v["median"]), say_number(v["band"]))
        elif v["verdict"] == "no baseline yet":
            what += " (%d trusted earlier)" % v["earlier"]
        print("  %-14s %-10s %s" % (v["figure"], say_number(v["value"]), what))


def measure_once(args, work, name, attempt, runner, device, gate, machine, history_rows):
    """One measure of the run `name`. A dict: status `measured` (figures, context, row, verdicts), `not trusted` or
    `no run` (why), or `left out`. A PC run first waits for a quiet machine and reads the card's use by other programs
    before and after; the window's watch (P-328) is read for every visible PC run."""
    renderer, cores = RUNS[name]
    folder = work / (name if attempt == 1 else "%s-again" % name)
    tablet = name == "tablet"
    out = {"name": name, "attempt": attempt, "renderer": renderer,
           "device_name": "tablet" if tablet else ("pc-4cores" if cores else "pc")}
    if tablet and not (args.device and args.android_bin and args.apk and args.pak):
        out.update(status="left out", why="no --device, --android-bin, --apk and --pak")
        return out
    uses = []
    if not tablet:
        quiet_now, why = machine.wait_quiet()
        if not quiet_now:
            out.update(status="not trusted", why="the machine was not quiet: %s" % why)
            return out
        uses.append(machine.card_use())
    command = [sys.executable, "-X", "utf8", "-I", str(TREE / "tools/lumacore/luma_test.py"), "run", str(ROUTE),
               "--bin", args.android_bin if tablet else args.bin, "--work", str(folder), "--who", "measure",
               "--renderer", renderer, "--commit", args.commit,
               # The harness refuses a run without a run lock file; each run has its own beside its folder.
               "--lock", str(work / ("harness-%s%s.lock" % (name, "" if attempt == 1 else "-again")))]
    env = dict(os.environ)
    if args.paks:
        env["LUMA_TEST_PAKS"] = args.paks
    why = None
    if tablet:
        code, why = on_tablet(args, command, env, runner, device, gate)
    else:
        old = processor_mask(cores)
        try:
            code = runner(command, env=env, check=False).returncode
        finally:
            restore_mask(old)
        uses.append(machine.card_use())
    found = None if why else run_folder_of(folder, renderer)
    if found is None:
        out.update(status="no run", why=why or "harness exit %d" % code)
        return out
    out.update(code=code, folder=found)
    if code != 0:
        out.update(status="no run", why="the run failed, harness exit %d" % code)
        return out
    disturbed = None if tablet else window_disturbed(found)
    if disturbed:
        # The player used the desktop over this run's window: its figures are not the game's alone (P-328).
        out.update(status="not trusted", why="window: %s" % disturbed, say="window: %s" % disturbed)
        return out
    for percent, detail in uses:
        if percent is not None and percent > quiet.CARD_LIMIT:
            out.update(status="not trusted", why="card in use by other programs: %s" % detail,
                       say="card in use by other programs")
            return out
    out["card"] = [detail for _percent, detail in uses]
    try:
        found_figures = figures(found)
    except (OSError, ValueError, struct.error) as e:
        out.update(status="no run", why="its results could not be read: %s: %s" % (type(e).__name__, e))
        return out
    context = run_context(found, args.device if tablet else socket.gethostname())
    out["figures"] = found_figures
    out["context"] = context
    out["row"] = history_row(found_figures, args.commit[:9], out["device_name"], renderer, "120", found, context=context)
    out["verdicts"] = judge_figures(out["row"], found_figures, history_rows)
    out["status"] = "measured"
    return out


def write_row(args, result, marks, now=None):
    """The history line of a measured run, with its marks (only the figures measured have one)."""
    row = dict(result["row"])
    row["marks"] = band.format_marks({k: v for k, v in marks.items() if result["figures"].get(k) is not None})
    if now is not None:
        row["time"] = now.strftime("%Y-%m-%d %H:%M")
    band.add_row(args.history, row)
    return row


def entries_of(result, marks):
    """The verdict file's lines for one measure: for each figure the key, value, median, band, verdict and mark."""
    out = []
    for v in result["verdicts"]:
        out.append({"key": dict(zip(band.KEY_COLUMNS, band.key_of(result["row"])), figure=v["figure"]),
                    "value": v["value"], "median": v["median"], "band": v["band"], "verdict": v["verdict"],
                    "mark": marks.get(v["figure"], "-"), "attempt": result["attempt"], "run": str(result["folder"])})
    return out


def measure_runs(args, work, runner=subprocess.run, device=None, gate=None, machine=None, now=None):
    """Each run named, in turn, then once more each run that had a figure worse than its band. (exit code, one text a
    run). Writes WORK/measure-verdict.json."""
    machine = machine or quiet.Machine(minutes=getattr(args, "quiet_minutes", quiet.WAIT_MINUTES))
    said, entries, kept = [], [], []
    tool_fault = untrusted = worse_twice = False
    pending = []  # (first measure, its worse figures)
    for name in args.runs.split(","):
        result = measure_once(args, work, name, 1, runner, device, gate, machine, band.read_history(args.history))
        status = result["status"]
        if status == "left out":
            said.append("tablet left out (%s)" % result["why"])
        elif status == "no run":
            said.append("%s no run (%s)" % (name, result["why"]))
            kept.append({"run": name, "status": "no run", "reason": result["why"]})
            tool_fault = True
        elif status == "not trusted":
            print("%s (%s %s): not trusted, %s" % (name, result["device_name"], result["renderer"], result["why"]))
            said.append("%s not trusted (%s)" % (name, result.get("say", result["why"])))
            kept.append({"run": name, "status": "not trusted", "reason": result["why"]})
            untrusted = True
        else:
            print_run(name, result["device_name"], result["renderer"], result["code"], result["verdicts"])
            for line in result["card"]:
                if line == "card use not read":
                    print("  card use not read")
            worse = [v["figure"] for v in result["verdicts"] if v["verdict"] == "worse"]
            if worse:
                pending.append((result, worse))
                continue
            marks = {v["figure"]: "trusted" for v in result["verdicts"] if v["value"] is not None}
            write_row(args, result, marks, now)
            entries += entries_of(result, marks)
            better = [v["figure"] for v in result["verdicts"] if v["verdict"] == "better"]
            said.append("%s %s" % (name, ("better: " + ", ".join(better)) if better else "holds"))
    for first, worse in pending:
        name = first["name"]
        print("%s: worse in %s; measuring again once" % (name, ", ".join(worse)))
        second = measure_once(args, work, name, 2, runner, device, gate, machine, band.read_history(args.history))
        if second["status"] == "no run":
            said.append("%s no run on the second measure (%s)" % (name, second["why"]))
            kept.append({"run": name, "status": "no run", "reason": second["why"]})
            tool_fault = True
            continue
        if second["status"] != "measured":
            print("%s (%s %s), second measure: not trusted, %s" % (name, second["device_name"], second["renderer"],
                                                                  second["why"]))
            said.append("%s not trusted on the second measure (%s); the first, worse in %s, is not written" % (
                name, second.get("say", second["why"]), ", ".join(worse)))
            kept.append({"run": name, "status": "not trusted", "reason": second["why"], "first_worse": worse})
            untrusted = True
            continue
        print_run(name + " again", second["device_name"], second["renderer"], second["code"], second["verdicts"])
        again = {v["figure"]: v["verdict"] for v in second["verdicts"]}
        first_marks = {v["figure"]: "trusted" for v in first["verdicts"] if v["value"] is not None}
        second_marks = {}
        still = []
        for figure in worse:
            if again[figure] == "worse":
                first_marks[figure] = second_marks[figure] = "worse"
                still.append(figure)
            else:
                first_marks[figure], second_marks[figure] = "outlier", "trusted"
        write_row(args, first, first_marks, now)
        write_row(args, second, second_marks, now)
        entries += entries_of(first, first_marks) + entries_of(second, {**{k: "-" for k in again}, **second_marks})
        if still:
            worse_twice = True
            said.append("%s WORSE: %s" % (name, ", ".join(still)))
        else:
            said.append("%s ok (worse in %s once, fine when measured again)" % (name, ", ".join(worse)))
    code = (EXIT_TOOL if tool_fault else EXIT_WORSE if worse_twice else EXIT_NOT_TRUSTED if untrusted else EXIT_RAN)
    verdict = {"commit": args.commit[:9], "history": str(args.history), "exit": code,
               "verdict": {EXIT_TOOL: "tool fault", EXIT_WORSE: "WORSE", EXIT_NOT_TRUSTED: "not trusted",
                           EXIT_RAN: "ok"}[code],
               "figures": entries, "runs_without_figures": kept}
    try:
        work.mkdir(parents=True, exist_ok=True)
        (work / VERDICT_FILE).write_text(json.dumps(verdict, indent=1) + "\n", encoding="utf-8")
    except OSError as e:
        print("measure.py: %s could not be written: %s" % (VERDICT_FILE, e))
        code = EXIT_TOOL
    return code, said


def run(args):
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    holder = "measure.py: the play route measure of %s pid %d" % (args.commit[:9], os.getpid())
    gate = gate_module() if args.gate_lock else None
    if gate is not None:
        why = gate.take_gate_lock(args.gate_lock, holder, args.wait, args.board, args.who)
        if why is not None:
            print("measure: no measure: %s" % why)
            return EXIT_TOOL
    summary, code = "ended early", EXIT_TOOL
    try:
        try:
            take_lock(args.lock, "measure.py %s %s" % (args.commit[:9], work))
        except SystemExit as e:
            print("measure: no measure: %s" % e)
            return EXIT_TOOL
        try:
            code, said = measure_runs(args, work)
            summary = "; ".join(said)
        except Exception as e:  # the exit code 1 means WORSE: a fault of the tool is 2
            import traceback
            traceback.print_exc()
            summary, code = "the measure stopped: %s: %s" % (type(e).__name__, e), EXIT_TOOL
        finally:
            shutil.rmtree(args.lock, ignore_errors=True)
    finally:
        if gate is not None:
            gate.free_gate_lock(args.gate_lock, holder, args.board, args.who, "measure of %s: %s" % (args.commit[:9],
                                                                                                     summary))
    print("measure: " + summary)
    return code


def accept_command(args):
    try:
        keys, lines = band.accept(args.history, args.commit, args.renderer, args.scene)
    except (OSError, ValueError) as e:
        print("measure: accept: %s" % e)
        return EXIT_TOOL
    print("measure: accepted from %s: the baseline restarts for %d key(s), %d history line(s) changed" % (
        args.commit, keys, lines))
    return EXIT_RAN


def main(argv=None):
    # A self-test that starts this tool as a child process runs it under the live-state guard (tools/gate/selftest_guard.py).
    import pathlib as _pathlib
    import sys as _sys
    _gate = str(_pathlib.Path(__file__).resolve().parents[1] / "gate")
    _sys.path.insert(0, _gate)
    try:
        import selftest_guard
    finally:
        _sys.path.remove(_gate)
    selftest_guard.install_if_asked()
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run")
    r.add_argument("--bin", required=True)
    r.add_argument("--work", required=True)
    r.add_argument("--commit", required=True)
    r.add_argument("--runs", default="d3d12,vulkan,d3d11,d3d12-4cores,tablet")
    r.add_argument("--device", default="")
    r.add_argument("--device-lock", default="")
    r.add_argument("--android-bin", default="")
    r.add_argument("--apk", default="", help="the test app the tablet's run installs (build_apk.ps1 -Debuggable -Harness)")
    r.add_argument("--pak", default="", help="the game.pak the tablet's run copies into the test app")
    r.add_argument("--device-wait", type=float, default=120.0, help="minutes to wait for the device's lock")
    r.add_argument("--history", default=str(HISTORY))
    r.add_argument("--lock", default="D:/NebulaWork/plan/measure.lock")
    r.add_argument("--gate-lock", default="")
    r.add_argument("--board", default="")
    r.add_argument("--who", default="Agent 1")
    r.add_argument("--wait", type=float, default=240.0, help="minutes to wait for the gate lock")
    r.add_argument("--paks", default=os.environ.get("LUMA_TEST_PAKS", ""))
    r.add_argument("--quiet-minutes", type=float, default=quiet.WAIT_MINUTES,
                   help="minutes to wait for a machine with no build and no other game (30 by the design)")
    a = sub.add_parser("accept")
    a.add_argument("--commit", required=True, help="the commit whose figures are the new baseline")
    a.add_argument("--renderer", default="", help="only the lines of this renderer (or run kind: pc-4cores, tablet)")
    a.add_argument("--scene", default="", help="only the lines of this scene")
    a.add_argument("--history", default=str(HISTORY))
    d = sub.add_parser("digest")
    d.add_argument("run_folder")
    d.add_argument("--commit", default="-")
    d.add_argument("--device", default="pc")
    d.add_argument("--history", default="")
    args = p.parse_args(argv)
    if args.command == "run":
        return run(args)
    if args.command == "accept":
        return accept_command(args)
    f = figures(args.run_folder)
    print(json.dumps(f, indent=1))
    if args.history:
        add_history(args.history, history_line(f, args.commit, args.device, pathlib.Path(args.run_folder).name, "120",
                                               args.run_folder))
    return 0


if __name__ == "__main__":
    sys.exit(main())
