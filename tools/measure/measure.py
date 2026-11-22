"""measure.py: the play route measure (docs/plan 07-BASE-FIRST stage 1 item 1, N-1043): one heavy stage in real time,
three minutes from the stage's first frame, on each renderer, with one line per run in the measure history.

  measure.py run --bin BIN --work WORK --commit SHA [--runs d3d12,vulkan,d3d11,d3d12-4cores,tablet]
                 [--device SERIAL --android-bin BIN --apk APK --pak PAK [--device-lock PATH]] [--history FILE]
                 [--pending FILE] [--paks ...] [--quiet-minutes 30]
                 [--gate-lock D:/NebulaWork/plan/gate.lock --board FILE --who NAME] [--lock measure.lock]
      Takes the gate lock (through its queue, so no gate, long run part or gate build runs beside it; none when the
      caller holds it) and the measurement lock (not when --runs names only the tablet: the tablet's measure needs no
      quiet PC, and in a landing it runs beside the PC's lanes), then measures each run named (real time, a visible window, no sound):
      it first waits for a quiet machine (quiet.py: no compiler, build tool or other copy of the game, looking every
      30 seconds for up to 30 minutes), reads the graphics card's use by other programs before and after, watches the
      processors' use by other programs over the run, runs tests/measure/route through the harness, and compares each
      judged figure with the median of the last ten trusted runs of the same key (band.py: computer, card, run kind,
      renderer, scene, picture size, rate). A figure outside its band on the bad side is measured again once (only the
      runs that were worse); worse again is WORSE. Each run's line is added to the history (band.py names its columns),
      or to FILE when --pending is given (the lines are compared with the history as usual; `keep` adds them later).
      A run that was not quiet is printed `not trusted` with its plain reason, compared with nothing and not written:
      a build or another game running; the card used by other programs for more than 10 % (the desktop's compositor,
      dwm.exe, left out); other programs using more than 10 % of all processors' time over the run; the window
      minimized, covered, active or the pointer moving over it (the runner's window-watch.txt: P-328).
      Each judged figure is also compared with the median of the anchor's lines of its key (`anchor`): a figure past its
      band on the bad side says "slower than at the start of the native work by N%". That never changes the exit code.
      Writes WORK/measure-verdict.json (each figure's value, median, band, verdict and mark), WORK/result.json (for a
      tool: for each run kind its state ok, better, no-baseline, worse, not-trusted or tool, the reason, and each
      figure's value, median, band, judged, verdict and anchor comparison) and ends with a line "measure: ...".
      Exit: 0 ran (ok, better, no baseline yet), 1 WORSE (twice), 2 could not run or could not read its own results,
      3 not trusted.
      gate.py starts it after every move of main (detached, on a copy of the moved commit's build) and in the long run.
  measure.py keep FILE [--history FILE]
      Adds the lines of a --pending FILE to the history (a line already there is not added twice). The gate calls it
      when the landing that measured has moved main: the history holds main's commits only.
  measure.py anchor --commit SHA [--history FILE]
      Marks that commit's trusted lines as the anchor, the start of the native work (one anchor: an earlier mark is
      cleared). A run's figures are compared with it when it has five lines or more for the key.
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

The figures, from the stage's first frame (the harness's "frame 0") to the run's end. Judged: cpu_work_ms, cpu_game_ms,
cpu_render_ms, gap_p99_ms, late_per_min, render_ms, gpu_ms, memory_mb. Shown, never judged (one slow frame in three minutes is
not a change in the game; step_ms and step_p99_ms hold the waits of a step as well as its work, and cpu_ms the processor time
of the whole process, which holds waiting too): gap_max_ms, cpu_ms, step_ms, step_p99_ms, step_max_ms, steps_over_18,
pictures_per_step and every cpu_<name>_ms of a frame thread other than game and render.
  rate            the pictures a second asked for; refresh_hz the display's refresh as the renderer reported it
  gap_max_ms      the longest time between two presents
  gap_p99_ms      the time between two presents that 99 of 100 stay under
  late_per_min    presents that missed their refresh (the [pace] lines' missed), per minute
  cpu_work_ms     the processor time the frame threads spent WORKING, per game step, all together: the median over the
                  [pace] windows of cpu_work_us_per_step. Each frame thread (the game thread, the render thread) reads its own
                  processor clock at its marks of work and of waiting (runtime/include/galaxy/perf_hint.h; on Windows
                  QueryThreadCycleTime as time by the time stamp counter's rate, elsewhere CLOCK_THREAD_CPUTIME_ID) and the
                  span from a mark of work to the next mark of waiting is added up, so a timer's spin and a present that
                  blocks are not in it. cpu_game_ms and cpu_render_ms are the game thread's and the render thread's
                  (cpu_game_us_per_step, cpu_render_us_per_step). A game or a machine that gives none: not measured (written -,
                  never 0)
  cpu_ms          the processor time the whole process used per game step (cpu_us_per_step; platform_time.h process_cpu_stamp).
                  Shown, never judged: it holds waiting (the spin of PreciseWait, which grows with how late the timer wakes on a
                  busy PC, and the time a present holds the threads), so one build read 1.3 ms and 3.4 ms a step
  step_ms         the median of a game step's time on the clock (step start to the step clock's wait: waits included)
  step_p99_ms     the 99th percentile of the same
  step_max_ms     the longest game step's work
  steps_over_18   game steps whose work took over 18 ms
  render_ms       the render thread's work per picture (a list's walk), median
  gpu_ms          the GPU's time per frame, the median of the frames' times (a frame's command lists added when it has
                  more than one): an average would take in every moment another program held the graphics card. A
                  renderer that records no GPU times in the timeline gives `not measured` (written -, never 0), compared
                  with nothing
  memory_mb       the most memory the process held (the harness's process.memory facts)
On the tablet the timeline has no game-frame and no step-wait events (the Android game does not write them): the stage's
first frame is found by the render thread's content-time events, and the step figures are the [pace] lines' (step_ms the
median of the windows' average step work, step_p99_ms the median of their 99th percentile).
Every figure is better lower (band.py FIGURES names the bad side of each); each is compared on its own, with the band's
floor (band.py FLOORS). A history line written before cpu_ms existed has that cell empty (never 0); it is no baseline for it.

THE PAIR (for a PC that is in use: the owner works on it, his own jobs use the processors and the graphics card)
  measure.py pair --base BIN --cand BIN --runs d3d12[,d3d11,vulkan] --work WORK --pending FILE [--calibration FILE]
                  [--base-pak PAK --cand-pak PAK --base-commit SHA --cand-commit SHA --form hidden|visible]
                  [--name measure/step/pc] [--gate-lock ... --board FILE --who NAME] [--lock measure.lock]
  measure.py pair-calibrate --bin BIN --runs d3d12[,d3d11,vulkan] --pairs 5 --work WORK [--calibration FILE]
  measure.py pair-accept --commit SHA --renderer R --figure F --reason TEXT [--calibration FILE]
  measure.py pair-probe --bin BIN --renderer R --work WORK [--form hidden|visible]
      The two builds run side by side, in the same minutes, on the same processors, each as the single measure runs one
      (the harness, its own work folder, saves and app data, the same stage, size and rate, no sound), and only the ratio
      of a figure is judged: whatever the PC is doing reaches both alike. Nothing is compared with another day, so the
      trust rules of the single measure do not apply: the pair waits for no quiet minutes, reads no window watch and no
      load of other programs, and the windows are hidden (the harness's hidden form, FORM below). It takes the measurement
      lock; a build or a game of someone else's beside it is allowed.
      A round starts both builds at once and waits for both (each in its own run folder, WORK/RENDERER/rN-aK/base and
      cand). Both are held to one set of logical processors by the process affinity of the runner the tool starts (the
      game inherits it): from the machine's own description (GetLogicalProcessorInformationEx), the processors of the
      highest efficiency class and, of those, the ones that share the largest level-3 cache (several of that size: the
      first); every processor: nothing is set. The set is printed. The measured span is the stage's first 5400 game
      steps after its frame 0 (90 seconds), cut by the game frame numbers of the timeline, so both sides are measured over
      the same moments of the game; the route (tests/measure/route) is cut at game step 6200 after frame 0.
      The ratio of a judged figure that both sides have: r = (candidate + f) / (base + f), f the figure's floor (band.py
      FLOORS). Its band comes from the calibration file (pair-bands.tsv beside the history; key: computer, graphics card,
      renderer, scene, size, rate): r above 1 + band says worse, under 1 - band better, else ok; no line for the key:
      no-baseline ("not calibrated on this PC"; it passes). A renderer with a figure worse in round 1 gets round 2 with
      the order of starting swapped, and, worse again, round 3 (the order of round 1); worse in all three is WORSE (exit 1),
      anything else ok. A round that did not run (a side ended early, the stage was not reached, a figure file is missing)
      is made once more; twice is `tool` (exit 2, with the runner's last lines). A figure that is WORSE with a line for the
      candidate's commit in pair-accepted.tsv (pair-accept writes one; the owner's word) is `accepted` and passes.
      Writes the --pending file (one row for each renderer and figure: check, renderer, figure, state, the line in words;
      the states are ok, better, no-baseline, worse, accepted and tool), WORK/result.json (the shape of the single measure's:
      for each renderer its state, reason and figures, a figure's value the candidate's and its median the base's) and,
      beside the calibration file, one line for every figure of every round in pairs.tsv. Exit: 0 ran and nothing worse,
      1 worse, 2 tool.
      pair-calibrate runs rounds of one build against itself (5, the order of starting alternating) and keeps, for each
      judged figure, band = max(least, 2 x the largest |r - 1|) of the five ratios (least: cpu_work_ms, cpu_game_ms and cpu_render_ms 0.03, render_ms 0.05,
      gpu_ms 0.05, memory_mb 0.02, gap_p99_ms 0.25, late_per_min 0.50) in the calibration file, replacing the key's older
      lines. pair-probe runs one build alone and lists its figures (the choice of the form: a hidden run of a build that
      gives every judged figure makes hidden the form).
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
RESULT_FILE = "result.json"
EXIT_RAN, EXIT_WORSE, EXIT_TOOL, EXIT_NOT_TRUSTED = 0, 1, 2, 3
RUNS ={"d3d12": ("d3d12", 0), "vulkan": ("vulkan", 0), "d3d11": ("d3d11", 0), "d3d12-4cores": ("d3d12", 4),
        "tablet": ("vulkan", 0)}
TAB = "\t"
FRAME_ZERO = re.compile(r"harness: frame 0 is game frame (\d+)")
PACE = re.compile(r"\[pace\] (.*)$")
# frame_timeline.h's events.
GPU_BEGIN, GPU_END, PRESENT_END, GAME_FRAME, WALK_BEGIN, WALK_END, WAIT_BEGIN, WAIT_END = 7, 8, 15, 26, 29, 30, 31, 32
CONTENT_TIME = 33  # render: a picture of an interpolation sequence; a = its list's game frame (every platform)


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


def pace_windows(log_text, span_steps=None):
    """The [pace] windows written after frame 0, the first after it left out (it may have begun before). With span_steps, only
    as many windows as hold that many steps together (a window of 600 steps is ten seconds): the pair's span."""
    out, after, seen, steps = [], False, 0, 0
    for line in log_text.splitlines():
        if FRAME_ZERO.search(line):
            after, seen, steps = True, 0, 0
            continue
        m = PACE.search(line)
        if m and after:
            seen += 1
            if seen > 1:
                window = {k: float(v) for k, v in re.findall(r"([a-z_0-9]+)=([-0-9.]+)", m.group(1))}
                if span_steps is not None and steps + window.get("steps", 0.0) > span_steps:
                    break
                steps += window.get("steps", 0.0)
                out.append(window)
    return out


def timeline_figures(path, first_frame, span_steps=None):
    """The timeline's figures from game frame first_frame on: present gaps, steps, walks and GPU times (ms). With span_steps,
    up to the game frame first_frame + span_steps (the pair measures both builds over the same frames of the game); a
    timeline that does not reach it gives no figures."""
    freq, records = load_timeline(path)
    ms = 1000.0 / freq
    # The stage's first frame in the timeline: the game's frame marker. The game writes it on Windows only (host_main.cpp);
    # on Android the timeline has none, and the first picture of an interpolation sequence of that frame or a later one
    # (the render thread writes it on every platform) marks the moment, a few milliseconds late at most.
    marker = GAME_FRAME if any(e == GAME_FRAME for _q, e, _a, _b in records) else CONTENT_TIME
    start = next((q for q, e, a, _b in records if e == marker and a >= first_frame), None)
    if start is None:
        return {}
    records = [r for r in records if r[0] >= start]
    if span_steps is not None:
        end = next((q for q, e, a, _b in records if e == marker and a >= first_frame + span_steps), None)
        if end is None:
            return {}
        records = [r for r in records if r[0] < end]
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
    # A frame's GPU time: its spans (one a fence) added when a frame has more than one.
    gpu_open, gpu_frames = {}, {}
    for q, e, a, _b in records:
        if e == GPU_BEGIN:
            gpu_open.setdefault(a, []).append(q)
        elif e == GPU_END and gpu_open.get(a):
            began = gpu_open[a].pop(0)
            if q > began:
                gpu_frames[a] = gpu_frames.get(a, 0.0) + (q - began) * ms
    gpu = list(gpu_frames.values())
    f = {}
    if gaps:
        f["gap_max_ms"], f["gap_p99_ms"] = max(gaps), quantile(gaps, 0.99)
    if steps:
        f["step_max_ms"], f["steps_over_18"] = max(steps), sum(s > 18.0 for s in steps)
        f["step_ms"], f["step_p99_ms"] = statistics.median(steps), quantile(steps, 0.99)
    if walks:
        f["render_ms"] = statistics.median(walks)
    if gpu and sum(gpu) > 0:  # no GPU times, or all zero: not measured, never 0
        # The median of the frames' times, not the sum over the presents: an average takes in every moment another program
        # held the graphics card.
        f["gpu_ms"] = statistics.median(gpu)
    return f


def figures(run_folder, span_steps=None):
    """One run's figures: the timeline's, the [pace] lines' and the memory facts'. Missing ones are left out. With span_steps
    (the pair), the first that many game steps after frame 0: the timeline up to that frame, the [pace] windows that hold that
    many steps, the memory facts of those seconds (60 steps a second)."""
    run_folder = pathlib.Path(run_folder)
    log = (run_folder / "run.log").read_text(encoding="utf-8", errors="replace")
    zero = FRAME_ZERO.search(log)
    f = {}
    windows = pace_windows(log, span_steps)
    if windows:
        seconds = sum(w.get("seconds", 0.0) for w in windows)
        f["late_per_min"] = 60.0 * sum(w.get("missed", 0.0) for w in windows) / seconds if seconds else 0.0
        refresh = statistics.median([w["refresh_ms"] for w in windows if w.get("refresh_ms", 0.0) > 0] or [0.0])
        if refresh:
            f["refresh_hz"] = 1000.0 / refresh
        # Without a timeline (a device that wrote none): the [pace] lines' own step and render figures.
        f["step_max_ms"] = max(w.get("step_work_max_us", 0.0) for w in windows) / 1000.0
        f["render_ms"] = statistics.median(w.get("render_avg_us", 0.0) for w in windows) / 1000.0
        # The Android game writes no step-wait events (host_main.cpp: Windows only), so there the step figures are the
        # windows' own: the median of the ten-second windows' average step work and of their 99th percentile.
        for name, field in (("step_ms", "step_work_avg_us"), ("step_p99_ms", "step_work_p99_us")):
            if all(field in w for w in windows):
                f[name] = statistics.median(w[field] for w in windows) / 1000.0
        # The processor time of the whole process per step (all threads, the game's own count: runtime/src/pacing_recorder.cpp),
        # the median of the windows that have it, and of the frame threads' work (cpu_work_us_per_step and cpu_<name>_us_per_step:
        # judged are all together, game and render; any other thread's is shown). A game that does not write a field, or a machine
        # that cannot tell: not there.
        cpu = [w["cpu_us_per_step"] for w in windows if "cpu_us_per_step" in w]
        if cpu:
            f["cpu_ms"] = statistics.median(cpu) / 1000.0
        for key in sorted({k for w in windows for k in w}):
            m = re.fullmatch(r"cpu_([a-z0-9_]+)_us_per_step", key)
            if m and m.group(1) != "us":
                values = [w[key] for w in windows if key in w]
                f["cpu_%s_ms" % m.group(1)] = statistics.median(values) / 1000.0
        steps_total = sum(w.get("steps", 0.0) for w in windows)
        if steps_total > 0:
            f["pictures_per_step"] = sum(w.get("pictures", 0.0) for w in windows) / steps_total
    timeline = run_folder / "timeline.bin"
    if zero and timeline.is_file():
        f.update(timeline_figures(timeline, int(zero.group(1)), span_steps))
    memory = []
    facts = run_folder / "facts.jsonl"
    if facts.is_file():
        for line in facts.read_text(encoding="utf-8").splitlines():
            try:
                fact = json.loads(line)
            except ValueError:
                continue
            if fact.get("key") == "process.memory" and isinstance(fact.get("value"), dict):
                seconds = fact["value"].get("seconds")
                if span_steps is not None and isinstance(seconds, (int, float)) and seconds > span_steps / 60.0:
                    continue
                memory.append(fact["value"].get("bytes", 0))
    if memory:
        f["memory_mb"] = max(memory) / (1024.0 * 1024.0)
    return f


def scene_of(run_folder):
    """The scene a run measured: the stage (name:scenario) or the stop of the test's description (test.json, which the
    harness copies into the run's folder; the route's own when the folder has none), else the test's name."""
    test = None
    # The tablet's run folder holds no test.json: the route's own description is the same test.
    for path in (pathlib.Path(run_folder) / "test.json", ROUTE / "test.json"):
        try:
            test = json.loads(path.read_text(encoding="utf-8"))
            break
        except (OSError, ValueError):
            continue
    if test is None:
        return "unknown"
    if isinstance(test.get("test"), dict):  # the harness's copy wraps the description: {"runner": ..., "test": {...}}
        test = test["test"]
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
            "size": context.get("size", ""), "marks": band.format_marks(marks or {}),
            "step_ms": num("step_ms", "%.3f"), "step_p99_ms": num("step_p99_ms", "%.3f"), "anchor": "",
            "cpu_ms": num("cpu_ms", "%.3f"), "cpu_work_ms": num("cpu_work_ms", "%.3f"),
            "cpu_game_ms": num("cpu_game_ms", "%.3f"), "cpu_render_ms": num("cpu_render_ms", "%.3f")}


def history_line(f, commit, device, renderer, rate, run_folder, now=None, context=None, marks=None):
    row = history_row(f, commit, device, renderer, rate, run_folder, now, context, marks)
    return TAB.join(row[c] for c in COLUMNS)


def add_history(path, line):
    """A history line (a dict, or the text of history_line) added to the file."""
    band.add_row(path, line if isinstance(line, dict) else band.row_of(line.split(TAB)))


def read_history(path):
    return band.read_history(path)


# What the harness's window watch (window-watch.txt: one line "KIND first at S s, N of the quarter seconds" for each way
# the desktop reached the window) says, in plain words. A kind not named here is written as it stands.
WINDOW_REASONS = {"minimized": "the window was minimized", "active": "the window had the keyboard focus",
                  "pointer": "the pointer moved over the window", "covered": "the window was covered by other windows"}


def window_disturbed(run_folder):
    """How the player's desktop reached a visible run's window (the harness's window-watch.txt, P-328), as plain
    reasons, or None: a run whose window was minimized, covered, active or under the pointer is not compared. (The
    watch itself is the runner's, tools/lumacore/luma_test.py: this is the reading side.)"""
    try:
        text = (pathlib.Path(run_folder) / "window-watch.txt").read_text(encoding="utf-8").strip()
    except OSError:
        return None
    reasons = []
    for line in text.splitlines():
        kind, _, rest = line.strip().partition(" ")
        if not kind:
            continue
        reasons.append("%s, %s" % (WINDOW_REASONS[kind], rest) if kind in WINDOW_REASONS else line.strip())
    return "; ".join(reasons) or None


def run_folder_of(work, renderer):
    found = sorted(pathlib.Path(work).rglob("run.log"), key=lambda p: p.stat().st_mtime)
    found = [p.parent for p in found if p.parent.name == renderer]
    return found[-1] if found else None


def measure_wait_module():
    sys.path.insert(0, str(TREE / "tools" / "gate"))
    try:
        import measure_wait
    finally:
        sys.path.remove(str(TREE / "tools" / "gate"))
    return measure_wait


def take_lock(path, who, alive=None):
    """The measurement lock: a folder made at once or not at all, its holder.txt ending "pid N" (this process), which builds and
    test runs started by hand read (tools/gate/measure_wait.py). A lock whose holder's process has ended is taken over."""
    lock = pathlib.Path(path)
    waiter = measure_wait_module()
    for _ in range(2):
        try:
            lock.mkdir()
            break
        except FileExistsError:
            held = waiter.holder_of(lock, alive or waiter.process_alive)
            if held is not None:
                raise SystemExit("measure.py: the measurement lock %s is held: %s" % (lock, held))
            try:
                old = (lock / "holder.txt").read_text(encoding="utf-8", errors="replace").strip()
            except OSError:
                old = ""
            print("measure.py: the measurement lock %s was held by pid %s, which has ended: taken over (%s)" % (
                lock, waiter.lockholder.parse(old)[0], old), flush=True)
            shutil.rmtree(lock, ignore_errors=True)  # its holder has ended
    else:
        raise SystemExit("measure.py: the measurement lock %s could not be taken" % lock)
    (lock / "holder.txt").write_text("%s %s %s\n" % (who, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                                                     waiter.lockholder.named()), encoding="utf-8")


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
        floor = band.FLOORS.get(name, 0.0)
        verdict = band.judge(value, band.trusted_values(earlier_rows, key, name), band.FIGURES[name], floor)
        judged = name in band.JUDGED
        entry = {"figure": name, "value": value, "median": verdict["median"], "band": verdict["band"], "judged": judged,
                 "verdict": verdict["verdict"] if judged or value is None else "not judged",
                 "earlier": verdict.get("earlier", 0), "anchor": None}
        if judged:
            entry["anchor"] = band.anchor_judge(value, band.anchor_values(earlier_rows, key, name), band.FIGURES[name],
                                                floor)
        out.append(entry)
    return out


def say_number(v):
    return "-" if v is None else ("%.6g" % v)


def anchor_say(verdict):
    """The anchor comparison of one figure in words, or "" when there is nothing to say."""
    a = verdict.get("anchor")
    if not a or a["verdict"] != "slower":
        return ""
    if a["percent"] is None:
        return "slower than at the start of the native work (%s against %s)" % (say_number(verdict["value"]),
                                                                              say_number(a["median"]))
    return "slower than at the start of the native work by %d%%" % round(a["percent"])


def anchor_state(verdicts):
    """The anchor comparison of one run: (`ok` or `not enough lines`, lines). The lines are those of the figure with
    the most of them."""
    lines = max([v["anchor"]["lines"] for v in verdicts if v.get("anchor")] or [0])
    return ("ok" if lines >= band.ANCHOR_LEAST_LINES else "not enough lines"), lines


def print_run(label, device_name, renderer, code, verdicts):
    print("%s (%s %s, harness exit %d):" % (label, device_name, renderer, code))
    for v in verdicts:
        what = v["verdict"]
        if v["median"] is not None:
            what += " (median %s, band %s)" % (say_number(v["median"]), say_number(v["band"]))
        elif v["verdict"] == "no baseline yet":
            what += " (%d trusted earlier)" % v["earlier"]
        print("  %-14s %-10s %s" % (v["figure"], say_number(v["value"]), what))
    state, lines = anchor_state(verdicts)
    if state != "ok":
        print("  anchor: not enough lines (%d of %d)" % (lines, band.ANCHOR_LEAST_LINES))
    for v in verdicts:
        if anchor_say(v):
            print("  %s: %s" % (v["figure"], anchor_say(v)))


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
    uses, watch, cpu = [], None, (None, "")
    if not tablet:
        quiet_now, why = machine.wait_quiet()
        if not quiet_now:
            out.update(status="not trusted", why="the machine was not quiet: %s" % why)
            return out
        uses.append(machine.card_use())
        watch = machine.cpu_watch()
        watch.begin()
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
            cpu = watch.end()  # the watch's thread always ends
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
        # The player used the desktop over this run's window, or other windows covered it: its figures are not the game's
        # alone (P-328).
        out.update(status="not trusted", why=disturbed, say=disturbed)
        return out
    for percent, detail in uses:
        if percent is not None and percent > quiet.CARD_LIMIT:
            out.update(status="not trusted", why="card in use by other programs: %s" % detail,
                       say="card in use by other programs")
            return out
    if cpu[0] is not None and cpu[0] > quiet.CPU_LIMIT:
        out.update(status="not trusted", why="other programs used the processors: %s" % cpu[1],
                   say="other programs used the processors")
        return out
    out["card"] = [detail for _percent, detail in uses]
    if watch is not None and cpu[0] is None:
        out["card"].append(cpu[1])
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


def line_target(args):
    """Where a run's history lines go: the pending file when there is one (a landing's measure: the gate keeps the lines
    when the landing has moved main), else the history."""
    return getattr(args, "pending", "") or args.history


def write_row(args, result, marks, now=None):
    """The history line of a measured run, with its marks (only the figures measured have one)."""
    row = dict(result["row"])
    row["marks"] = band.format_marks({k: v for k, v in marks.items() if result["figures"].get(k) is not None})
    if now is not None:
        row["time"] = now.strftime("%Y-%m-%d %H:%M")
    band.add_row(line_target(args), row)
    return row


def entries_of(result, marks):
    """The verdict file's lines for one measure: for each figure the key, value, median, band, verdict and mark."""
    out = []
    for v in result["verdicts"]:
        out.append({"key": dict(zip(band.KEY_COLUMNS, band.key_of(result["row"])), figure=v["figure"]),
                    "value": v["value"], "median": v["median"], "band": v["band"], "verdict": v["verdict"],
                    "mark": marks.get(v["figure"], "-"), "attempt": result["attempt"], "run": str(result["folder"])})
    return out


def state_of(verdicts):
    """The state of a measured run from its judged figures: worse, better, ok (compared, none outside its band) or
    no-baseline (nothing could be compared yet)."""
    judged = [v for v in verdicts if v["judged"]]
    if any(v["verdict"] == "worse" for v in judged):
        return "worse"
    if any(v["verdict"] == "better" for v in judged):
        return "better"
    if any(v["verdict"] == "holds" for v in judged):
        return "ok"
    return "no-baseline" if any(v["verdict"] == "no baseline yet" for v in judged) else "ok"


def figures_of(verdicts):
    """result.json's figures of one run: {figure: value, median, band, judged, verdict and, for a judged figure, the anchor's
    comparison (verdict, median, band, lines, percent and what it says)}."""
    out = {}
    for v in verdicts:
        anchor = None
        if v.get("anchor"):
            anchor = dict(v["anchor"], says=anchor_say(v))
        out[v["figure"]] = {"value": v["value"], "median": v["median"], "band": v["band"], "judged": v["judged"],
                            "verdict": v["verdict"], "anchor": anchor}
    return out


def describe(v):
    return "%s %s (median %s, band %s)" % (v["figure"], say_number(v["value"]), say_number(v["median"]),
                                           say_number(v["band"]))


def outcome_of(result, state=None, reason=""):
    """result.json's entry for one run kind from its last measure."""
    verdicts = result["verdicts"]
    state = state or state_of(verdicts)
    anchor_kind, lines = anchor_state(verdicts)
    if not reason:
        if state == "worse":
            reason = "worse: " + "; ".join(describe(v) for v in verdicts if v["verdict"] == "worse")
        elif state == "better":
            reason = "better: " + ", ".join(v["figure"] for v in verdicts if v["verdict"] == "better")
        elif state == "no-baseline":
            reason = "fewer than %d trusted earlier runs of this key" % band.LEAST_RUNS
    return {"state": state, "reason": reason, "device": result["device_name"], "renderer": result["renderer"],
            "figures": figures_of(verdicts), "anchor": {"state": anchor_kind, "lines": lines}}


def failed_outcome(state, reason, result=None):
    out = {"state": state, "reason": reason, "figures": {}, "anchor": None}
    if result:
        out.update(device=result.get("device_name", ""), renderer=result.get("renderer", ""))
    return out


def write_result(work, args, code, outcomes):
    """WORK/result.json, for a tool to read: the commit, the exit code, and for each run kind its state (ok, better,
    no-baseline, worse, not-trusted, tool; left-out for a run not asked for), the plain reason where there is one and
    each figure's value, median, band, whether it is judged, its verdict and its anchor comparison. Returns False when
    the file could not be written."""
    names = {0: "ok", 1: "WORSE", 2: "tool fault", 3: "not trusted"}
    result = {"commit": str(getattr(args, "commit", ""))[:9], "history": str(getattr(args, "history", "")),
              "pending": getattr(args, "pending", "") or None, "exit": code, "verdict": names[code], "runs": outcomes}
    try:
        work.mkdir(parents=True, exist_ok=True)
        (work / RESULT_FILE).write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8")
    except OSError as e:
        print("measure.py: %s could not be written: %s" % (RESULT_FILE, e))
        return False
    return True


def measure_runs(args, work, runner=subprocess.run, device=None, gate=None, machine=None, now=None):
    """Each run named, in turn, then once more each run that had a figure worse than its band. (exit code, one text a
    run). Writes WORK/measure-verdict.json and WORK/result.json."""
    machine = machine or quiet.Machine(minutes=getattr(args, "quiet_minutes", quiet.WAIT_MINUTES))
    said, entries, kept, outcomes = [], [], [], {}
    tool_fault = untrusted = worse_twice = False
    pending = []  # (first measure, its worse figures)
    for name in args.runs.split(","):
        result = measure_once(args, work, name, 1, runner, device, gate, machine, band.read_history(args.history))
        status = result["status"]
        if status == "left out":
            said.append("tablet left out (%s)" % result["why"])
            outcomes[name] = failed_outcome("left-out", result["why"])
        elif status == "no run":
            said.append("%s no run (%s)" % (name, result["why"]))
            kept.append({"run": name, "status": "no run", "reason": result["why"]})
            outcomes[name] = failed_outcome("tool", result["why"], result)
            tool_fault = True
        elif status == "not trusted":
            print("%s (%s %s): not trusted, %s" % (name, result["device_name"], result["renderer"], result["why"]))
            said.append("%s not trusted (%s)" % (name, result.get("say", result["why"])))
            kept.append({"run": name, "status": "not trusted", "reason": result["why"]})
            outcomes[name] = failed_outcome("not-trusted", result["why"], result)
            untrusted = True
        else:
            print_run(name, result["device_name"], result["renderer"], result["code"], result["verdicts"])
            for line in result["card"]:
                if line.endswith("not read"):
                    print("  " + line)
            worse = [v["figure"] for v in result["verdicts"] if v["verdict"] == "worse"]
            if worse:
                pending.append((result, worse))
                continue
            marks = {v["figure"]: "trusted" for v in result["verdicts"] if v["value"] is not None}
            write_row(args, result, marks, now)
            entries += entries_of(result, marks)
            outcomes[name] = outcome_of(result)
            better = [v["figure"] for v in result["verdicts"] if v["verdict"] == "better"]
            said.append("%s %s" % (name, ("better: " + ", ".join(better)) if better else "holds"))
    for first, worse in pending:
        name = first["name"]
        print("%s: worse in %s; measuring again once" % (name, ", ".join(worse)))
        second = measure_once(args, work, name, 2, runner, device, gate, machine, band.read_history(args.history))
        if second["status"] == "no run":
            said.append("%s no run on the second measure (%s)" % (name, second["why"]))
            kept.append({"run": name, "status": "no run", "reason": second["why"]})
            outcomes[name] = failed_outcome("tool", "the second measure: " + second["why"], second)
            tool_fault = True
            continue
        if second["status"] != "measured":
            print("%s (%s %s), second measure: not trusted, %s" % (name, second["device_name"], second["renderer"],
                                                                  second["why"]))
            said.append("%s not trusted on the second measure (%s); the first, worse in %s, is not written" % (
                name, second.get("say", second["why"]), ", ".join(worse)))
            kept.append({"run": name, "status": "not trusted", "reason": second["why"], "first_worse": worse})
            outcomes[name] = failed_outcome("not-trusted", "the second measure: %s; the first, worse in %s, is not "
                                            "written" % (second["why"], ", ".join(worse)), second)
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
            outcomes[name] = outcome_of(second, "worse", "worse twice: " + "; ".join(
                describe(v) for v in second["verdicts"] if v["figure"] in still))
        else:
            said.append("%s ok (worse in %s once, fine when measured again)" % (name, ", ".join(worse)))
            outcomes[name] = outcome_of(second, "ok", "worse in %s once, fine when measured again" % ", ".join(worse))
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
    if not write_result(work, args, code, outcomes):
        code = EXIT_TOOL
    return code, said


def tablet_only(args):
    """Whether `--runs` names the tablet and nothing else."""
    names = [n for n in getattr(args, "runs", "").split(",") if n]
    return bool(names) and all(n == "tablet" for n in names)


def stopped_result(args, work, why):
    """result.json for a measure that did not start or did not end: every run named is a tool fault with the reason."""
    names = [n for n in getattr(args, "runs", "").split(",") if n]
    write_result(work, args, EXIT_TOOL, {n: failed_outcome("tool", why) for n in names})


def run(args):
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    # The harness this measure starts (and the builds it makes) never wait for the measure's own lock.
    os.environ["NEBULA_NO_MEASURE_WAIT"] = "1"
    holder = "measure.py: the play route measure of %s pid %d" % (args.commit[:9], os.getpid())
    gate = gate_module() if args.gate_lock else None
    if gate is not None:
        why = gate.take_gate_lock(args.gate_lock, holder, args.wait, args.board, args.who)
        if why is not None:
            print("measure: no measure: %s" % why)
            stopped_result(args, work, why)
            return EXIT_TOOL
    summary, code = "ended early", EXIT_TOOL
    # The measurement lock is for the PC's runs: an invocation of the tablet alone takes none (in a landing it runs beside the
    # PC's lanes, and it must not hold up a PC measure or a build started by hand).
    locked = not tablet_only(args)
    try:
        if locked:
            try:
                take_lock(args.lock, "measure.py %s %s" % (args.commit[:9], work))
            except SystemExit as e:
                print("measure: no measure: %s" % e)
                stopped_result(args, work, str(e))
                return EXIT_TOOL
        try:
            if getattr(args, "pending", ""):
                # This landing's lines only: a file left by an earlier measure at the same path is not kept again.
                pathlib.Path(args.pending).parent.mkdir(parents=True, exist_ok=True)
                pathlib.Path(args.pending).write_text(TAB.join(COLUMNS) + "\n", encoding="utf-8", newline="\n")
            code, said = measure_runs(args, work)
            summary = "; ".join(said)
        except Exception as e:  # the exit code 1 means WORSE: a fault of the tool is 2
            import traceback
            traceback.print_exc()
            summary, code = "the measure stopped: %s: %s" % (type(e).__name__, e), EXIT_TOOL
            stopped_result(args, work, summary)
        finally:
            if locked:
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


def anchor_command(args):
    try:
        keys, lines = band.set_anchor(args.history, args.commit)
    except (OSError, ValueError) as e:
        print("measure: anchor: %s" % e)
        return EXIT_TOOL
    print("measure: anchor: the start of the native work is %s: %d trusted line(s) for %d key(s)" % (args.commit, lines,
                                                                                                    keys))
    return EXIT_RAN


def keep_command(args):
    if not pathlib.Path(args.pending_file).is_file():
        print("measure: keep: %s does not exist" % args.pending_file)
        return EXIT_TOOL
    try:
        added, already = band.keep(args.pending_file, args.history)
    except (OSError, ValueError) as e:
        print("measure: keep: %s" % e)
        return EXIT_TOOL
    print("measure: kept %d line(s) of %s in %s (%d already there)" % (added, args.pending_file, args.history, already))
    return EXIT_RAN


# ---- The pair (measure.py pair, pair-calibrate, pair-accept, pair-probe) ----
SPAN_STEPS = 5400          # the measured span: the stage's first 5400 game steps after its frame 0 (90 seconds)
ROUTE_END = 6200           # the pair's route ends at this game step after frame 0: the span, the [pace] window left out
                           # at the start (up to 600 steps) and the window that is written after the span's last one
HIDDEN, VISIBLE = "hidden", "visible"
PAIR_FORM = HIDDEN         # the form of both sides (measure.py pair-probe chooses: see the head text)
PAIR_RATE = "120"          # the rate of the route (its GALAXY_MAX_FPS)
PAIR_VSYNC_INTERVAL = "0"  # a pair's pictures wait for nothing of the display
CALIBRATION = HISTORY.parent / "pair-bands.tsv"
PAIRS_FILE, ACCEPTED_FILE = "pairs.tsv", "pair-accepted.tsv"
PAIR_CHECK = "measure/step/pc"
PENDING_COLUMNS = ("check", "renderer", "figure", "state", "line")
# What a side's figures hold when its timeline reached the span, on every PC renderer (the timeline's own: steps_over_18 comes
# from the step waits, render_ms from the walks; Vulkan writes no present events, so it has no gap figures and Direct3D 11
# and Vulkan no GPU times). A side without these did not reach the stage or lost its files.
REQUIRED = ("steps_over_18", "render_ms")
TAIL_LINES = 8
RELATION_CORE, RELATION_CACHE, RELATION_ALL = 0, 2, 0xFFFF


def parse_processor_information(buffer):
    """(cores, caches) from the bytes GetLogicalProcessorInformationEx(RelationAll) returns: a core {mask, group,
    efficiency} for each group mask of each core (the higher the efficiency class, the faster and the hungrier the core), a
    cache {level, size, type, mask, group} for each group mask of each cache. Records of other relations are skipped."""
    cores, caches, offset = [], [], 0
    while offset + 8 <= len(buffer):
        relationship, size = struct.unpack_from("<II", buffer, offset)
        if size < 8 or offset + size > len(buffer):
            break
        body = offset + 8
        if relationship == RELATION_CORE and size >= 40:
            efficiency = buffer[body + 1]
            for i in range(max(1, struct.unpack_from("<H", buffer, body + 22)[0])):
                mask, group = struct.unpack_from("<QH", buffer, body + 24 + 16 * i)
                cores.append({"mask": mask, "group": group, "efficiency": efficiency})
        elif relationship == RELATION_CACHE and size >= 48:
            level = buffer[body]
            cache_size, kind = struct.unpack_from("<II", buffer, body + 4)
            for i in range(max(1, struct.unpack_from("<H", buffer, body + 30)[0])):
                mask, group = struct.unpack_from("<QH", buffer, body + 32 + 16 * i)
                caches.append({"level": level, "size": cache_size, "type": kind, "mask": mask, "group": group})
        offset += size
    return cores, caches


def machine_processor_tables():
    """(cores, caches) of this machine as the system describes it, or None (not Windows, or the system did not answer)."""
    if os.name != "nt":
        return None
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    size = ctypes.c_ulong(0)
    k32.GetLogicalProcessorInformationEx(RELATION_ALL, None, ctypes.byref(size))
    if not size.value:
        return None
    buffer = ctypes.create_string_buffer(size.value)
    if not k32.GetLogicalProcessorInformationEx(RELATION_ALL, buffer, ctypes.byref(size)):
        return None
    return parse_processor_information(buffer.raw[:size.value])


def count_bits(mask):
    return bin(mask).count("1")


def choose_processors(cores, caches):
    """(mask or None, text): the set of logical processors both sides of a pair are held to: those of the highest
    efficiency class and, of those, the ones that share the largest level-3 cache (several of that size: the first). None
    when that is every processor (nothing is set). Only the first processor group is used: a mask holds one group's."""
    if not cores:
        return None, "the system gave no processor table: nothing is set"
    groups = sorted({c["group"] for c in cores})
    group = groups[0]
    mine = [c for c in cores if c["group"] == group]
    every = 0
    for c in mine:
        every |= c["mask"]
    top = max(c["efficiency"] for c in mine)
    top_mask = 0
    for c in mine:
        if c["efficiency"] == top:
            top_mask |= c["mask"]
    chosen, why = top_mask, "efficiency class %d" % top
    level3 = [c for c in caches if c["level"] == 3 and c["group"] == group and c["type"] in (0, 2) and c["mask"] & top_mask]
    if level3:
        best = max(level3, key=lambda c: (c["size"], -((c["mask"] & -c["mask"]).bit_length())))
        chosen = best["mask"] & top_mask
        why += ", sharing a level-3 cache of %d MB" % (best["size"] // (1 << 20))
    if chosen == every and len(groups) == 1:
        return None, "every logical processor (%d): nothing is set" % count_bits(every)
    return chosen, "logical processors %#x (%d of %d), %s" % (chosen, count_bits(chosen), count_bits(every), why)


def set_processor_mask(mask):
    """Windows: this process's processor mask set to `mask` (its children inherit it); the old mask, to put back."""
    if os.name != "nt" or not mask:
        return None
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.GetCurrentProcess.restype = ctypes.c_void_p
    process = ctypes.c_void_p(k32.GetCurrentProcess())
    mine, system = ctypes.c_size_t(), ctypes.c_size_t()
    k32.GetProcessAffinityMask(process, ctypes.byref(mine), ctypes.byref(system))
    if not k32.SetProcessAffinityMask(process, ctypes.c_size_t(mask)):
        raise SystemExit("measure.py: the processor mask %#x was refused" % mask)
    return mine.value


class Started:
    """A game's runner started: wait() gives its exit code."""
    def __init__(self, process, log):
        self.process, self.log = process, log

    def wait(self):
        try:
            return self.process.wait()
        finally:
            self.log.close()


def start_process(command, env, log_path):
    log = open(log_path, "wb")
    try:
        return Started(subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT), log)
    except OSError:
        log.close()
        raise


def pair_route(work, form):
    """WORK/route/test.json: the route of the single measure cut at ROUTE_END game steps after frame 0 and in the form given
    (hidden: the harness starts the game with its window hidden; visible: shown, taking no focus, as the single measure's
    is), with the pictures presented without waiting for the display (GALAXY_VSYNC_INTERVAL=0: how long a present of a hidden
    window holds the threads is Windows' choice, and it differed between the two sides of a pair) and the frame cap the route
    has (GALAXY_MAX_FPS). Both sides of a pair run this one test. Returns the folder."""
    test = json.loads((ROUTE / "test.json").read_text(encoding="utf-8"))
    steps = [s for s in test["steps"] if s.get("at", 0) < ROUTE_END and not s.get("end")]
    steps.append({"at": ROUTE_END, "end": True})
    test.update(name="measure-pair", steps=steps, visible=(form == VISIBLE))
    test["settings"] = [("GALAXY_VSYNC_INTERVAL=%s" % PAIR_VSYNC_INTERVAL) if s.startswith("GALAXY_VSYNC_INTERVAL=") else s
                        for s in test["settings"]]
    test["about"] = ("The play route cut at game step %d after the stage's first frame, for tools/measure/measure.py pair "
                     "(the file the tool writes beside the runs from tests/measure/route/test.json)." % ROUTE_END)
    folder = pathlib.Path(work) / "route"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "test.json").write_text(json.dumps(test, indent=1) + "\n", encoding="utf-8", newline="\n")
    return folder


def tail_of(path, lines=TAIL_LINES):
    try:
        text = pathlib.Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""
    return " | ".join(l.strip() for l in text.splitlines()[-lines:] if l.strip())


class Pair:
    """What a pair's rounds share: the two builds, the work folder, how a game is started and what the files are."""

    def __init__(self, args, starter=start_process, now=None):
        self.args = args
        self.work = pathlib.Path(args.work)
        self.starter = starter
        self.now = now or datetime.datetime.now
        self.bins = {"base": getattr(args, "base", None) or getattr(args, "bin", ""),
                     "cand": getattr(args, "cand", None) or getattr(args, "bin", "")}
        self.paks = {"base": getattr(args, "base_pak", "") or "", "cand": getattr(args, "cand_pak", "") or ""}
        self.commits = {"base": getattr(args, "base_commit", "unknown") or "unknown",
                        "cand": getattr(args, "cand_commit", getattr(args, "commit", "unknown")) or "unknown"}
        self.calibration = pathlib.Path(getattr(args, "calibration", "") or CALIBRATION)
        self.route = pair_route(self.work, getattr(args, "form", PAIR_FORM))
        self.seconds = []   # how long each round took, from its start to its figures

    def command(self, side, renderer, folder, lock):
        command = [sys.executable, "-X", "utf8", "-I", str(TREE / "tools/lumacore/luma_test.py"), "run", str(self.route),
                   "--bin", str(self.bins[side]), "--work", str(folder), "--who", "measure pair", "--renderer", renderer,
                   "--commit", self.commits[side], "--lock", str(lock)]
        if self.paks[side]:
            command += ["--pak", self.paks[side]]
        return command

    def round(self, renderer, number, attempt, first):
        """One round for one renderer: both builds started at once (`first` starts first), waited for. {status `ran`: figures
        {side: figures}, context, folders; status `no run`: why}."""
        began = self.now()
        tag = "r%d-a%d" % (number, attempt)
        order = [first, "cand" if first == "base" else "base"]
        env = dict(os.environ)
        started, folders = {}, {}
        for side in order:
            folders[side] = self.work / renderer / tag / side
            folders[side].mkdir(parents=True, exist_ok=True)
            try:
                started[side] = self.starter(self.command(side, renderer, folders[side], self.work / ("harness-%s-%s-%s.lock" % (
                    renderer, tag, side))), env, folders[side] / "runner.log")
            except OSError as e:
                for other in started.values():
                    other.wait()
                return {"status": "no run", "why": "the %s side could not be started: %s" % (side, e)}
        codes = {side: started[side].wait() for side in order}
        found, out = {}, {"status": "ran", "figures": {}, "folders": {}, "first": first}
        for side in ("base", "cand"):
            run_folder = run_folder_of(folders[side], renderer)
            tail = tail_of(folders[side] / "runner.log")
            if codes[side] != 0 or run_folder is None:
                return {"status": "no run", "why": "the %s side ended with exit %s%s" % (
                    side, codes[side], (": " + tail) if tail else "")}
            try:
                figures_found = figures(run_folder, SPAN_STEPS)
            except (OSError, ValueError, struct.error) as e:
                return {"status": "no run", "why": "the %s side's results could not be read: %s: %s" % (
                    side, type(e).__name__, e)}
            lacking = [n for n in REQUIRED if n not in figures_found]
            if lacking:
                return {"status": "no run", "why": "the %s side did not give %s over its first %d steps (the stage was not "
                        "reached, or a file is missing): %s" % (side, ", ".join(lacking), SPAN_STEPS, tail)}
            out["figures"][side] = figures_found
            out["folders"][side] = run_folder
            found[side] = run_folder
        out["context"] = run_context(found["base"], socket.gethostname())
        self.seconds.append((self.now() - began).total_seconds())
        return out

    def made(self, renderer, number, first):
        """A round; one that did not run is made once more, and a second failure is the answer (status `no run`)."""
        result = None
        for attempt in (1, 2):
            result = self.round(renderer, number, attempt, first)
            if result["status"] == "ran":
                return result
            print("measure.py pair: %s round %d, try %d did not run: %s" % (renderer, number, attempt, result["why"]), flush=True)
        return result

    def log_round(self, renderer, number, result, ratios, words):
        for figure in sorted(ratios):
            band.add_cells(self.calibration.parent / PAIRS_FILE, band.ROUNDS_COLUMNS, {
                "time": self.now().strftime("%Y-%m-%d %H:%M:%S"), "base_commit": self.commits["base"],
                "cand_commit": self.commits["cand"], "renderer": renderer, "round": number, "figure": figure,
                "base": "%.4f" % result["figures"]["base"][figure], "cand": "%.4f" % result["figures"]["cand"][figure],
                "ratio": "%.4f" % ratios[figure], "word": words[figure]})


def round_ratios(result):
    """{figure: ratio} for every judged figure that both sides gave."""
    base, cand = result["figures"]["base"], result["figures"]["cand"]
    return {f: band.pair_ratio(base[f], cand[f], f) for f in band.JUDGED if f in base and f in cand}


def percent(ratio):
    return "%+.1f%%" % ((ratio - 1.0) * 100.0)


def number_word(n):
    return {1: "one", 2: "two", 3: "three"}.get(n, str(n))


def figure_line(check, renderer, figure, entry):
    """The plain line of one figure: for example `measure/step/pc d3d12 cpu_ms worse: base 3.210, candidate 3.540 (+10.3%,
    +9.8%, +10.9% in three rounds; the band is 4.0%)`."""
    shown = ", ".join(percent(r) for r in entry["ratios"])
    if len(entry["ratios"]) > 1:
        shown += " in %s rounds" % number_word(len(entry["ratios"]))
    if entry["band"] is None:
        tail = "not calibrated on this PC"
    else:
        tail = "the band is %.1f%%" % (entry["band"] * 100.0)
    if entry["verdict"] == "accepted":
        tail += "; accepted: " + entry["reason"]
    return "%s %s %s %s: base %.3f, candidate %.3f (%s; %s)" % (check, renderer, figure, entry["verdict"], entry["base"],
                                                              entry["cand"], shown, tail)


def settle_figures(rounds, calibration, accepted_file, cand_commit, renderer):
    """{figure: entry} from a renderer's rounds (each a round's result): the words of round 1 stand, except that a figure that
    was worse in round 1 is `worse` only when it was worse in every round that was made (three), else ok. entry: verdict, base,
    cand (the medians over the rounds), ratios (each round's), band (round 1's key's), reason."""
    out = {}
    per_round = []
    for result in rounds:
        ratios = round_ratios(result)
        bands = band.pair_bands(calibration, band.pair_key(result["context"], renderer, PAIR_RATE))
        per_round.append((ratios, {f: band.pair_word(r, bands.get(f)) for f, r in ratios.items()}, bands))
    for figure in per_round[0][0]:
        words = [w[figure] for _r, w, _b in per_round if figure in w]
        ratios = [r[figure] for r, _w, _b in per_round if figure in r]
        first = words[0]
        if first == "worse":
            verdict = "worse" if len(words) == band.PAIR_ROUNDS and all(w == "worse" for w in words) else "ok"
        else:
            verdict = first
        entry = {"verdict": verdict, "ratios": ratios, "band": per_round[0][2].get(figure), "reason": "",
                 "base": statistics.median(r["figures"]["base"][figure] for r in rounds if figure in r["figures"]["base"]),
                 "cand": statistics.median(r["figures"]["cand"][figure] for r in rounds if figure in r["figures"]["cand"])}
        if verdict == "worse":
            reason = band.accepted_reason(accepted_file, cand_commit, renderer, figure)
            if reason is not None:
                entry["verdict"], entry["reason"] = "accepted", reason
        out[figure] = entry
    return out


def renderer_state(figures_settled):
    """worse, accepted, better, ok or no-baseline, from the figures' verdicts."""
    words = {e["verdict"] for e in figures_settled.values()}
    for state in ("worse", "accepted", "better", "ok"):
        if state in words:
            return state
    return "no-baseline"


def pair_renderer(pair, renderer, check):
    """One renderer's pair: round 1 (the base starts first), round 2 with the order swapped for figures worse in round 1,
    round 3 (the order of round 1) for figures worse in both. {state, reason, figures, lines, rounds}."""
    rounds, still, first = [], None, "base"
    accepted = pair.calibration.parent / ACCEPTED_FILE
    for number in range(1, band.PAIR_ROUNDS + 1):
        result = pair.made(renderer, number, first)
        if result["status"] != "ran":
            why = result["why"]
            return {"state": "tool", "reason": why, "figures": {}, "rounds": len(rounds),
                    "lines": {"-": "%s %s tool: %s" % (check, renderer, why)}}
        rounds.append(result)
        ratios = round_ratios(result)
        bands = band.pair_bands(pair.calibration, band.pair_key(result["context"], renderer, PAIR_RATE))
        words = {f: band.pair_word(r, bands.get(f)) for f, r in ratios.items()}
        pair.log_round(renderer, number, result, ratios, words)
        print("measure.py pair: %s round %d (%s first, %.0f s): %s" % (
            renderer, number, first, pair.seconds[-1], ", ".join("%s %s %s" % (f, percent(ratios[f]), words[f])
                                                                   for f in sorted(ratios))), flush=True)
        worse = {f for f, w in words.items() if w == "worse"}
        still = worse if still is None else still & worse
        if not still:
            break
        first = "cand" if first == "base" else "base"
    settled = settle_figures(rounds, pair.calibration, accepted, pair.commits["cand"], renderer)
    state = renderer_state(settled)
    lines = {f: figure_line(check, renderer, f, settled[f]) for f in band.JUDGED if f in settled}
    reason = "; ".join(lines[f] for f in lines if settled[f]["verdict"] in ("worse", "accepted"))
    per_step = [r["figures"]["base"]["pictures_per_step"] for r in rounds if "pictures_per_step" in r["figures"]["base"]]
    return {"state": state, "reason": reason, "figures": settled, "rounds": len(rounds), "lines": lines,
            "pictures_per_step": statistics.median(per_step) if per_step else None}


def pair_result(args, code, outcomes, processors, form, commits):
    """WORK/result.json: the single measure's shape (each run kind's state, reason, figures: a figure's `value` is the
    candidate's, its `median` the base's), with the pair's own ratios, bands and rounds."""
    runs = {}
    for renderer, o in outcomes.items():
        runs[renderer] = {"state": o["state"], "reason": o["reason"], "device": "pc", "renderer": renderer, "anchor": None,
                          "rounds": o["rounds"], "pictures_per_step": o.get("pictures_per_step"),
                          "figures": {f: {"value": e["cand"], "median": e["base"], "band": e["band"], "judged": True,
                                          "verdict": e["verdict"], "anchor": None, "ratios": e["ratios"]}
                                      for f, e in o["figures"].items()}}
    names = {EXIT_RAN: "ok", EXIT_WORSE: "WORSE", EXIT_TOOL: "tool fault"}
    return {"commit": commits["cand"][:9], "base": commits["base"][:9], "pending": args.pending or None, "exit": code,
            "verdict": names[code], "processors": processors, "form": form,
            "settings": {"GALAXY_MAX_FPS": PAIR_RATE, "GALAXY_VSYNC_INTERVAL": PAIR_VSYNC_INTERVAL}, "runs": runs}


def pair_code(outcomes):
    states = [o["state"] for o in outcomes.values()]
    return EXIT_TOOL if "tool" in states else EXIT_WORSE if "worse" in states else EXIT_RAN


def pair_body(args, starter=start_process, tables=None, now=None):
    """The pair's rounds for every renderer named, the files, and the exit code: (code, summary)."""
    work = pathlib.Path(args.work)
    mask, processors = choose_processors(*(tables if tables is not None else machine_processor_tables() or ([], [])))
    print("measure.py pair: " + processors, flush=True)
    old = set_processor_mask(mask)
    try:
        pair = Pair(args, starter, now)
        outcomes = {}
        for renderer in [r for r in args.runs.split(",") if r]:
            outcomes[renderer] = pair_renderer(pair, renderer, args.name)
            for line in outcomes[renderer]["lines"].values():
                print(line, flush=True)
    finally:
        restore_mask(old)
    code = pair_code(outcomes)
    rows = []
    for renderer, o in outcomes.items():
        if o["state"] == "tool":
            rows.append({"check": args.name, "renderer": renderer, "figure": "-", "state": "tool", "line": o["lines"]["-"]})
        for figure, line in o["lines"].items():
            if figure != "-":
                rows.append({"check": args.name, "renderer": renderer, "figure": figure,
                             "state": o["figures"][figure]["verdict"], "line": line})
    if args.pending:
        band.cells_file(args.pending, PENDING_COLUMNS, rows)
    work.mkdir(parents=True, exist_ok=True)
    result = pair_result(args, code, outcomes, processors, getattr(args, "form", PAIR_FORM), pair.commits)
    (work / RESULT_FILE).write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8", newline="\n")
    return code, "; ".join("%s %s" % (r, o["state"]) for r, o in outcomes.items())


def calibrate_body(args, starter=start_process, tables=None, now=None):
    """Rounds of one build against itself for every renderer named; the band of each judged figure into the calibration file."""
    mask, processors = choose_processors(*(tables if tables is not None else machine_processor_tables() or ([], [])))
    print("measure.py pair-calibrate: " + processors, flush=True)
    old = set_processor_mask(mask)
    said, code = [], EXIT_RAN
    try:
        pair = Pair(args, starter, now)
        for renderer in [r for r in args.runs.split(",") if r]:
            rounds = []
            for number in range(1, args.pairs + 1):
                result = pair.made(renderer, number, "base" if number % 2 else "cand")
                if result["status"] != "ran":
                    print("measure.py pair-calibrate: %s: %s" % (renderer, result["why"]))
                    said.append("%s tool (%s)" % (renderer, result["why"]))
                    code = EXIT_TOOL
                    break
                rounds.append(result)
            else:
                ratios = {}
                for result in rounds:
                    for figure, r in round_ratios(result).items():
                        ratios.setdefault(figure, []).append(r)
                bands = {f: (band.pair_band(rs, f), rs) for f, rs in ratios.items() if len(rs) == args.pairs}
                key = band.pair_key(rounds[0]["context"], renderer, PAIR_RATE)
                band.write_pair_bands(pair.calibration, key, bands, pair.commits["cand"], pair.now().strftime("%Y-%m-%d %H:%M"))
                print("measure.py pair-calibrate: %s, key %s" % (renderer, " / ".join(key)))
                for figure in sorted(bands):
                    print("  %-13s band %5.1f%%  ratios %s" % (figure, bands[figure][0] * 100.0, " ".join(
                        "%.4f" % r for r in bands[figure][1])))
                said.append("%s calibrated (%d figures)" % (renderer, len(bands)))
    finally:
        restore_mask(old)
    return code, "; ".join(said)


def probe_body(args, starter=start_process, tables=None, now=None):
    """One build alone in the form given: its figures over the pair's span, and which judged figures are missing."""
    mask, processors = choose_processors(*(tables if tables is not None else machine_processor_tables() or ([], [])))
    print("measure.py pair-probe: " + processors, flush=True)
    old = set_processor_mask(mask)
    try:
        pair = Pair(args, starter, now)
        folder = pair.work / args.renderer / "probe"
        folder.mkdir(parents=True, exist_ok=True)
        command = pair.command("base", args.renderer, folder, pair.work / "harness-probe.lock")
        code = starter(command, dict(os.environ), folder / "runner.log").wait()
    finally:
        restore_mask(old)
    run_folder = run_folder_of(folder, args.renderer)
    if code != 0 or run_folder is None:
        print("measure.py pair-probe: the run ended with exit %s: %s" % (code, tail_of(folder / "runner.log")))
        return EXIT_TOOL, "no run"
    found = figures(run_folder, SPAN_STEPS)
    for name in band.FIGURES:
        print("  %-14s %s" % (name, say_number(found.get(name))))
    missing = [n for n in band.JUDGED if n not in found]
    print("measure.py pair-probe: %s form, %s: %s" % (getattr(args, "form", PAIR_FORM), args.renderer, (
        "every judged figure is there" if not missing else "missing: " + ", ".join(missing))))
    return (EXIT_RAN if not missing else EXIT_TOOL), ("no figure missing" if not missing else "missing " + ", ".join(missing))


def pair_locked(args, label, body):
    """A pair command inside the locks: the gate lock when --gate-lock is given (through its queue, as the single measure
    takes it), then the measurement lock. No quiet minutes and no window watch: the ratio of two builds that ran side by side
    does not depend on what else the PC was doing."""
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    os.environ["NEBULA_NO_MEASURE_WAIT"] = "1"
    holder = "measure.py: the %s pid %d" % (label, os.getpid())
    gate = gate_module() if args.gate_lock else None
    if gate is not None:
        why = gate.take_gate_lock(args.gate_lock, holder, args.wait, args.board, args.who)
        if why is not None:
            print("measure: no %s: %s" % (label, why))
            return EXIT_TOOL
    summary, code = "ended early", EXIT_TOOL
    try:
        try:
            take_lock(args.lock, "measure.py %s %s" % (label, work))
        except SystemExit as e:
            print("measure: no %s: %s" % (label, e))
            return EXIT_TOOL
        try:
            code, summary = body(args)
        except Exception as e:  # the exit code 1 means WORSE: a fault of the tool is 2
            import traceback
            traceback.print_exc()
            summary, code = "the %s stopped: %s: %s" % (label, type(e).__name__, e), EXIT_TOOL
        finally:
            shutil.rmtree(args.lock, ignore_errors=True)
    finally:
        if gate is not None:
            gate.free_gate_lock(args.gate_lock, holder, args.board, args.who, "%s: %s" % (label, summary))
    print("measure: " + summary)
    return code


def pair_accept_command(args):
    path = pathlib.Path(args.calibration or CALIBRATION).parent / ACCEPTED_FILE
    if args.figure not in band.JUDGED:
        print("measure: pair-accept: %s is not a judged figure (%s)" % (args.figure, ", ".join(band.JUDGED)))
        return EXIT_TOOL
    if not args.reason.strip():
        print("measure: pair-accept: give the reason")
        return EXIT_TOOL
    band.add_cells(path, band.ACCEPTED_COLUMNS, {"commit": args.commit, "renderer": args.renderer, "figure": args.figure,
                                                 "reason": args.reason.replace(TAB, " ").replace("\n", " "),
                                                 "date": datetime.date.today().isoformat()})
    print("measure: accepted: %s %s %s in %s" % (args.commit, args.renderer, args.figure, path))
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
    r.add_argument("--pending", default="", help="write this run's trusted lines to FILE, not to the history (a landing's "
                   "measure: `keep` adds them when the landing has moved main)")
    k = sub.add_parser("keep", help="add the lines of a --pending file to the history")
    k.add_argument("pending_file", metavar="FILE")
    k.add_argument("--history", default=str(HISTORY))
    n = sub.add_parser("anchor", help="mark a commit's trusted lines as the start of the native work")
    n.add_argument("--commit", required=True)
    n.add_argument("--history", default=str(HISTORY))
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

    def locks(parser):
        parser.add_argument("--lock", default="D:/NebulaWork/plan/measure.lock")
        parser.add_argument("--gate-lock", default="")
        parser.add_argument("--board", default="")
        parser.add_argument("--who", default="Agent 1")
        parser.add_argument("--wait", type=float, default=240.0, help="minutes to wait for the gate lock")
        parser.add_argument("--form", choices=(HIDDEN, VISIBLE), default=PAIR_FORM,
                            help="the window of both sides: hidden, or shown without the focus (the single measure's)")
    pr = sub.add_parser("pair", help="two builds side by side in the same minutes: only the ratio of a figure is judged")
    pr.add_argument("--base", required=True, help="the folder of a built game: the base")
    pr.add_argument("--cand", required=True, help="the folder of a built game: the candidate")
    pr.add_argument("--runs", default="d3d12")
    pr.add_argument("--work", required=True)
    pr.add_argument("--pending", default="", help="write one row for each renderer and figure to FILE")
    pr.add_argument("--calibration", default="", help="the bands (default: pair-bands.tsv beside the history)")
    pr.add_argument("--base-pak", default="", help="the game.pak of the base (default: the one its folder's pak.path names)")
    pr.add_argument("--cand-pak", default="", help="the game.pak of the candidate (default: its folder's pak.path's)")
    pr.add_argument("--base-commit", default="unknown")
    pr.add_argument("--cand-commit", default="unknown")
    pr.add_argument("--name", default=PAIR_CHECK, help="the check's name in the lines")
    locks(pr)
    pc = sub.add_parser("pair-calibrate", help="rounds of one build against itself: the band of each figure")
    pc.add_argument("--bin", required=True)
    pc.add_argument("--runs", default="d3d12")
    pc.add_argument("--pairs", type=int, default=5)
    pc.add_argument("--work", required=True)
    pc.add_argument("--calibration", default="")
    pc.add_argument("--base-pak", default="")
    pc.add_argument("--cand-pak", default="")
    pc.add_argument("--commit", default="unknown")
    locks(pc)
    pa = sub.add_parser("pair-accept", help="a slowdown that was looked at and accepted (the owner's word)")
    pa.add_argument("--commit", required=True, help="the candidate's commit")
    pa.add_argument("--renderer", required=True)
    pa.add_argument("--figure", required=True)
    pa.add_argument("--reason", required=True)
    pa.add_argument("--calibration", default="")
    pp = sub.add_parser("pair-probe", help="one build alone: its figures over the pair's span")
    pp.add_argument("--bin", required=True)
    pp.add_argument("--renderer", default="d3d12")
    pp.add_argument("--work", required=True)
    pp.add_argument("--base-pak", default="")
    pp.add_argument("--commit", default="unknown")
    locks(pp)
    args = p.parse_args(argv)
    if args.command == "pair":
        return pair_locked(args, "pair", pair_body)
    if args.command == "pair-calibrate":
        return pair_locked(args, "pair-calibrate", calibrate_body)
    if args.command == "pair-probe":
        return pair_locked(args, "pair-probe", probe_body)
    if args.command == "pair-accept":
        return pair_accept_command(args)
    if args.command == "run":
        return run(args)
    if args.command == "accept":
        return accept_command(args)
    if args.command == "keep":
        return keep_command(args)
    if args.command == "anchor":
        return anchor_command(args)
    f = figures(args.run_folder)
    print(json.dumps(f, indent=1))
    if args.history:
        add_history(args.history, history_line(f, args.commit, args.device, pathlib.Path(args.run_folder).name, "120",
                                               args.run_folder))
    return 0


if __name__ == "__main__":
    sys.exit(main())
