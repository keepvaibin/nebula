"""measure.py: the play route measure (docs/plan 07-BASE-FIRST stage 1 item 1, N-1043): one heavy stage in real time,
three minutes from the stage's first frame, on each renderer, with one line per run in the measure history.

  measure.py run --bin BIN --work WORK --commit SHA [--runs d3d12,vulkan,d3d11,d3d12-4cores,tablet]
                 [--device SERIAL --android-bin BIN [--device-lock PATH]] [--history FILE] [--paks ...]
                 [--gate-lock D:/NebulaWork/plan/gate.lock --board FILE --who NAME] [--lock measure.lock]
      Takes the gate lock (through its queue, so no gate, long run part or gate build runs beside it; none when the
      caller holds it) and the measurement lock, runs tests/measure/route through the harness (real time, a visible
      window, no sound) once per run named, adds each run's line to the history and compares it with the last ten
      lines of the same device and renderer. Exit 1 when a figure is above all of them or a run gave no line.
      gate.py starts it after every move of main (detached, on a copy of the moved commit's build) and in the long run.
  measure.py digest RUN_FOLDER [--commit SHA --device NAME --history FILE]
      One run's figures (and its history line when --history is given).

A run named d3d12-4cores holds the game to four logical processors (the harness's processor mask, which the game
inherits). The tablet's run sets what 120 Hz needs (power saving off, Motion smoothness adaptive) for the run and
puts both back after it; no other setting of the tablet is touched.

The figures, from the stage's first frame (the harness's "frame 0") to the run's end:
  rate            the pictures a second asked for; refresh_hz the display's refresh as the renderer reported it
  gap_max_ms      the longest time between two presents
  gap_p99_ms      the time between two presents that 99 of 100 stay under
  late_per_min    presents that missed their refresh (the [pace] lines' missed), per minute
  step_max_ms     the longest game step's work (step start to the step clock's wait)
  steps_over_18   game steps whose work took over 18 ms
  render_ms       the render thread's work per picture (a list's walk), median
  gpu_ms          the GPU's time per picture: its command lists' time over the presents (a renderer that records
                  no GPU times in the timeline: -)
  memory_mb       the most memory the process held (the harness's process.memory facts)
Every figure is better lower; each is compared on its own.
"""
import argparse
import ctypes
import datetime
import json
import os
import pathlib
import re
import shutil
import statistics
import struct
import subprocess
import sys

TREE = pathlib.Path(__file__).resolve().parents[2]
ROUTE = TREE / "tests" / "measure" / "route"
HISTORY = pathlib.Path("D:/NebulaWork/build/gate-runs/measure-history.tsv")
COLUMNS = ("commit", "time", "device", "renderer", "rate", "refresh_hz", "gap_max_ms", "gap_p99_ms", "late_per_min",
           "step_max_ms", "steps_over_18", "render_ms", "gpu_ms", "memory_mb", "run")
COMPARED = ("gap_max_ms", "gap_p99_ms", "late_per_min", "step_max_ms", "steps_over_18", "render_ms", "gpu_ms",
            "memory_mb")
SPREAD_RUNS = 10
LEAST_RUNS = 3
RUNS = {"d3d12": ("d3d12", 0), "vulkan": ("vulkan", 0), "d3d11": ("d3d11", 0), "d3d12-4cores": ("d3d12", 4),
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
    if gpu and presents:
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


def history_line(f, commit, device, renderer, rate, run_folder, now=None):
    def num(key, form="%.2f"):
        return form % f[key] if key in f else "-"
    values = {"commit": commit, "time": (now or datetime.datetime.now()).strftime("%Y-%m-%d %H:%M"), "device": device,
              "renderer": renderer, "rate": rate, "refresh_hz": num("refresh_hz", "%.1f"), "gap_max_ms": num("gap_max_ms"),
              "gap_p99_ms": num("gap_p99_ms"), "late_per_min": num("late_per_min"), "step_max_ms": num("step_max_ms"),
              "steps_over_18": num("steps_over_18", "%d"), "render_ms": num("render_ms", "%.3f"),
              "gpu_ms": num("gpu_ms", "%.3f"), "memory_mb": num("memory_mb", "%.0f"), "run": str(run_folder)}
    return TAB.join(values[c] for c in COLUMNS)


def add_history(path, line):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    new = not path.is_file()
    with open(path, "a", encoding="utf-8", newline="\n") as out:
        if new:
            out.write(TAB.join(COLUMNS) + "\n")
        out.write(line + "\n")


def read_history(path):
    path = pathlib.Path(path)
    if not path.is_file():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines()[1:]:
        cells = line.split(TAB)
        if len(cells) == len(COLUMNS):
            rows.append(dict(zip(COLUMNS, cells)))
    return rows


def compare(line, earlier):
    """[(figure, value, spread, verdict)] of a history line against the last SPREAD_RUNS earlier lines of its device
    and renderer: a value above every one of theirs is "worse"."""
    row = dict(zip(COLUMNS, line.split(TAB)))
    same = [r for r in earlier if r["device"] == row["device"] and r["renderer"] == row["renderer"]][-SPREAD_RUNS:]
    out = []
    for name in COMPARED:
        if row[name] == "-":
            out.append((name, "-", "", "not measured"))
            continue
        value = float(row[name])
        values = [float(r[name]) for r in same if r[name] != "-"]
        if len(values) < LEAST_RUNS:
            out.append((name, value, "%d earlier" % len(values), "no spread"))
            continue
        lo, hi = min(values), max(values)
        out.append((name, value, "%.6g to %.6g over %d" % (lo, hi, len(values)), "worse" if value > hi else "holds"))
    return out


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


def measure_runs(args, work, runner=subprocess.run):
    """Each run named, in turn: (failed, one text a run)."""
    failed, said = False, []
    for name in args.runs.split(","):
        renderer, cores = RUNS[name]
        folder = work / name
        tablet = None
        if name == "tablet":
            if not args.device or not args.android_bin:
                said.append("tablet left out (no --device and --android-bin)")
                continue
            sys.path.insert(0, str(TREE / "tools" / "lumacore"))
            import luma_test_android
            tablet = Tablet(luma_test_android.Device(args.device))
        command = [sys.executable, "-X", "utf8", "-I", str(TREE / "tools/lumacore/luma_test.py"), "run", str(ROUTE),
                   "--bin", args.android_bin if tablet else args.bin, "--work", str(folder), "--who", "measure",
                   "--renderer", renderer, "--commit", args.commit,
                   # The harness refuses a run without a run lock file; each run has its own beside its folder.
                   "--lock", str(work / ("harness-%s.lock" % name))]
        if tablet:
            command += ["--device", args.device] + (["--device-lock", args.device_lock] if args.device_lock else [])
        env = dict(os.environ)
        if args.paks:
            env["LUMA_TEST_PAKS"] = args.paks
        old = processor_mask(cores)
        try:
            if tablet:
                print("measure.py: the tablet for the run: %s" % tablet.set_for_run(), flush=True)
            code = runner(command, env=env, check=False).returncode
        finally:
            restore_mask(old)
            if tablet:
                print("measure.py: the tablet put back: %s" % tablet.put_back(), flush=True)
        found = run_folder_of(folder, renderer)
        if found is None:
            said.append("%s no run (harness exit %d)" % (name, code))
            failed = True
            continue
        device = "tablet" if tablet else ("pc-4cores" if cores else "pc")
        line = history_line(figures(found), args.commit[:9], device, renderer, "120", found)
        earlier = read_history(args.history)
        add_history(args.history, line)
        verdicts = compare(line, earlier)
        print("%s (%s %s, harness exit %d):" % (name, device, renderer, code))
        for figure, value, spread, verdict in verdicts:
            print("  %-14s %-10s %-30s %s" % (figure, value if isinstance(value, str) else "%.6g" % value, spread,
                                              verdict))
        worse = [v[0] for v in verdicts if v[3] == "worse"]
        said.append("%s %s" % (name, ("worse: " + ", ".join(worse)) if worse else "holds"))
        failed |= code != 0 or bool(worse)
    return failed, said


def run(args):
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    holder = "measure.py: the play route measure of %s pid %d" % (args.commit[:9], os.getpid())
    gate = gate_module() if args.gate_lock else None
    if gate is not None:
        why = gate.take_gate_lock(args.gate_lock, holder, args.wait, args.board, args.who)
        if why is not None:
            print("measure: no measure: %s" % why)
            return 1
    summary = "ended early"
    try:
        take_lock(args.lock, "measure.py %s %s" % (args.commit[:9], work))
        try:
            failed, said = measure_runs(args, work)
        finally:
            shutil.rmtree(args.lock, ignore_errors=True)
        summary = "; ".join(said)
    finally:
        if gate is not None:
            gate.free_gate_lock(args.gate_lock, holder, args.board, args.who, "measure of %s: %s" % (args.commit[:9],
                                                                                                     summary))
    print("measure: " + summary)
    return 1 if failed else 0


def main(argv=None):
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
    r.add_argument("--history", default=str(HISTORY))
    r.add_argument("--lock", default="D:/NebulaWork/plan/measure.lock")
    r.add_argument("--gate-lock", default="")
    r.add_argument("--board", default="")
    r.add_argument("--who", default="Agent 1")
    r.add_argument("--wait", type=float, default=240.0, help="minutes to wait for the gate lock")
    r.add_argument("--paks", default=os.environ.get("LUMA_TEST_PAKS", ""))
    d = sub.add_parser("digest")
    d.add_argument("run_folder")
    d.add_argument("--commit", default="-")
    d.add_argument("--device", default="pc")
    d.add_argument("--history", default="")
    args = p.parse_args(argv)
    if args.command == "run":
        return run(args)
    f = figures(args.run_folder)
    print(json.dumps(f, indent=1))
    if args.history:
        add_history(args.history, history_line(f, args.commit, args.device, pathlib.Path(args.run_folder).name, "120",
                                               args.run_folder))
    return 0


if __name__ == "__main__":
    sys.exit(main())
