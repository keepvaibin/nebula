"""measure_test.py: tools/measure/measure.py without the game (N-1043): the figures, the history's lines, the runs with
a fake harness, the tablet's run. The band, the run-again and accept are band_test.py's; the quiet machine is
quiet_test.py's.

python -X utf8 -I tools/measure/tests/measure_test.py
"""
import argparse
import json
import os
import pathlib
import struct
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import measure as m  # noqa: E402

failures = []
checks = [0]


def check(ok, what):
    checks[0] += 1
    if not ok:
        failures.append(what)


def timeline(path, records, freq=1000):
    """A timeline file of (ms, event, a, b) records at `freq` ticks a second (frame_timeline.h's layout)."""
    with open(path, "wb") as f:
        f.write(b"NBTL0001" + struct.pack("<5Q", 1, freq, len(records), len(records), 1) + b"\0" * 16)
        for q, e, a, b in records:
            f.write(struct.pack("<QQQIHH", q, a, b, 0, e, 1))


work = pathlib.Path(tempfile.mkdtemp())

# The timeline: frame 10 is the stage's first; what comes before it is left out.
records = [(0, m.GAME_FRAME, 9, 0), (1, m.PRESENT_END, 0, 0), (2, m.WAIT_BEGIN, 0, 0)]  # before frame 10
records += [(100, m.GAME_FRAME, 10, 0), (100, m.WAIT_END, 0, 0), (105, m.WAIT_BEGIN, 0, 0),  # a 5 ms step
            (106, m.WALK_BEGIN, 1, 0), (108, m.WALK_END, 1, 0), (110, m.PRESENT_END, 1, 0),
            (110, m.GPU_BEGIN, 7, 0), (113, m.GPU_END, 7, 0),
            (117, m.WAIT_END, 0, 0), (137, m.WAIT_BEGIN, 0, 0),  # a 20 ms step
            (138, m.WALK_BEGIN, 2, 0), (142, m.WALK_END, 2, 0), (140, m.PRESENT_END, 2, 0),
            (141, m.GPU_BEGIN, 8, 0), (142, m.GPU_END, 8, 0),
            (150, m.PRESENT_END, 3, 0)]
timeline(work / "timeline.bin", records)
f = m.timeline_figures(work / "timeline.bin", 10)
check(abs(f.get("gap_max_ms", 0) - 30.0) < 1e-6, "the longest present gap from frame 10 on: %s" % f)
check(f.get("steps_over_18") == 1 and abs(f.get("step_max_ms", 0) - 20.0) < 1e-6, "steps over 18 ms: %s" % f)
check(abs(f.get("render_ms", 0) - 3.0) < 1e-6, "the walks' median: %s" % f)
check(abs(f.get("gpu_ms", 0) - 2.0) < 1e-6, "the GPU's time: the median of the frames' (3 and 1 ms): %s" % f)
check(m.timeline_figures(work / "timeline.bin", 99) == {}, "no frame 99: no figures")
# A renderer that writes no GPU events, or only empty ones: gpu_ms is not there (never 0).
no_gpu = [r for r in records if r[1] not in (m.GPU_BEGIN, m.GPU_END)]
timeline(work / "nogpu.bin", no_gpu)
check("gpu_ms" not in m.timeline_figures(work / "nogpu.bin", 10), "no GPU events: gpu_ms is left out, not 0")
zero_gpu = no_gpu + [(110, m.GPU_BEGIN, 7, 0), (110, m.GPU_END, 7, 0)]
timeline(work / "zerogpu.bin", zero_gpu)
check("gpu_ms" not in m.timeline_figures(work / "zerogpu.bin", 10), "GPU events of no length: gpu_ms is left out, not 0")

# A run folder: [pace] lines after frame 0 (the first left out), memory facts, the timeline.
run = work / "runs" / "measure-route" / "d3d12"
run.mkdir(parents=True)
pace = "[pace] seconds=10 steps=600 refresh_ms=8.333 missed=%d step_work_max_us=%d render_avg_us=%d\n"
(run / "run.log").write_text(pace % (50, 1, 1) + "harness: frame 0 is game frame 10\n" + pace % (40, 1, 1)
                             + pace % (2, 7000, 900) + pace % (1, 6000, 1100), encoding="utf-8")
(run / "facts.jsonl").write_text("\n".join(json.dumps({"frame": i, "key": "process.memory",
                                                       "value": {"seconds": i, "bytes": b}})
                                           for i, b in enumerate((100 << 20, 300 << 20, 200 << 20))) + "\n",
                                 encoding="utf-8")
timeline(run / "timeline.bin", records)
f = m.figures(run)
check(abs(f.get("late_per_min", 0) - 9.0) < 1e-6, "missed per minute from the windows after the first: %s" % f)
check(abs(f.get("refresh_hz", 0) - 120.0) < 0.01, "the refresh: %s" % f)
check(abs(f.get("memory_mb", 0) - 300.0) < 1e-6, "the most memory held: %s" % f)
check(abs(f.get("step_max_ms", 0) - 20.0) < 1e-6, "the timeline's figures win over the [pace] lines': %s" % f)
(run / "timeline.bin").unlink()
f = m.figures(run)
check(abs(f.get("step_max_ms", 0) - 7.0) < 1e-6 and abs(f.get("render_ms", 0) - 1.0) < 1e-6 and "gap_max_ms" not in f,
      "with no timeline, the [pace] lines' step and render figures: %s" % f)

# The history's lines (the comparison itself is band_test.py's): a figure not measured is -, the key's parts and the
# marks are the last columns, an old line (15 columns) still reads and never counts for a baseline.
history = work / "history.tsv"
ctx = {"computer": "PC1", "gpu": "Card A", "scene": "Stage:1", "size": "3840x2160"}
line = m.history_line({"gap_max_ms": 10.0, "memory_mb": 300.0}, "abc", "pc", "d3d12", "120", run, context=ctx,
                      marks={"gap_max_ms": "trusted"})
cells = line.split("\t")
check(cells[m.COLUMNS.index("gpu_ms")] == "-", "a figure not measured is -")
check(cells[m.COLUMNS.index("computer")] == "PC1" and cells[m.COLUMNS.index("marks")] == "gap_max_ms=trusted",
      "the key's parts and the marks are written: %s" % cells)
old = "\t".join(["old", "2026-11-19 10:00", "pc", "d3d12", "120", "240.0", "10.00", "9.00", "1.00", "5.00", "0",
                "0.100", "-", "300", "R"])
history.write_text("\t".join(m.COLUMNS[:15]) + "\n" + old + "\n", encoding="utf-8")  # a file of the old kind
m.add_history(history, line)
rows = m.read_history(history)
check(len(rows) == 2 and rows[0]["commit"] == "old" and rows[0]["marks"] == "" and rows[1]["computer"] == "PC1",
      "an old file keeps its line and takes the new header: %s" % rows)
check(history.read_text(encoding="utf-8").splitlines()[0] == "\t".join(m.COLUMNS), "the header is the new one")
check(m.band.trusted_values(rows, m.band.key_of(rows[0]), "gap_max_ms") == [], "an old line is never a baseline")
check(m.scene_of(work / "nothing") == "HoneyBeeKingdomGalaxy:1", "no test.json in the folder: the route's own")
(run / "test.json").write_text(json.dumps({"runner": "1.0", "test": {"name": "measure-route", "stage": {
    "name": "OtherGalaxy", "scenario": 2}}}), encoding="utf-8")
check(m.scene_of(run) == "OtherGalaxy:2", "the harness's copy of the test wraps it: %s" % m.scene_of(run))
(run / "test.json").write_text(json.dumps({"name": "x", "stop": "Stop9"}), encoding="utf-8")
check(m.scene_of(run) == "Stop9", "a stop instead of a stage: %s" % m.scene_of(run))
(run / "test.json").write_text(json.dumps({"name": "measure-route", "stage": {"name": "HoneyBeeKingdomGalaxy",
                                                                           "scenario": 1}}), encoding="utf-8")
log_before = (run / "run.log").read_text(encoding="utf-8")
(run / "run.log").write_text('[gpu] adapter="Card A" vram-mb=1\n[Renderer] internal resolution 1280x720 -> 3840x2160\n'
                             '[Renderer] internal resolution 1280x720 -> 2800x1752\n', encoding="utf-8")
check(m.run_context(run, "PC1") == {"computer": "PC1", "gpu": "Card A", "scene": "HoneyBeeKingdomGalaxy:1",
                                    "size": "2800x1752"}, "the key's parts from the run folder: %s" % m.run_context(run, "PC1"))
(run / "run.log").write_text(log_before, encoding="utf-8")


class QuietMachine:
    """A machine that is quiet and whose card nobody else uses."""
    def wait_quiet(self):
        return True, ""

    def card_use(self):
        return 0.0, "other programs use the card 0.0%"

    def cpu_watch(self):
        return type("Watch", (), {"begin": lambda self: None,
                                  "end": lambda self: (0.0, "other programs use the processors 0.0%")})()


machine = QuietMachine()


# The tablet's two settings: set for the run, put back after, a setting that had no value deleted again.
class FakeDevice:
    def __init__(self):
        self.values = {"global low_power": "1", "secure refresh_rate_mode": "null"}
        self.commands = []

    def shell(self, command):
        self.commands.append(command)
        words = command.split()
        key = " ".join(words[2:4])
        if words[1] == "get":
            return 0, self.values.get(key, "null") + "\n"
        if words[1] == "put":
            self.values[key] = words[4]
        elif words[1] == "delete":
            self.values.pop(key, None)
        return 0, ""


device = FakeDevice()
tablet = m.Tablet(device)
said = tablet.set_for_run()
check(device.values == {"global low_power": "0", "secure refresh_rate_mode": "1"}, "set for the run: %s" % said)
tablet.put_back()
check(device.values == {"global low_power": "1"}, "put back, the unset one deleted: %s" % device.values)
check(all(c.split()[2] in ("global", "secure") and c.split()[3] in ("low_power", "refresh_rate_mode")
          for c in device.commands), "no other setting is touched: %s" % device.commands)

# The runs: a fake harness writes a run folder; the line lands in the history; a missing run is a tool fault (exit 2).
args = argparse.Namespace(runs="d3d12,tablet", bin="B", android_bin="", apk="", pak="", device="", device_lock="",
                          paks="", commit="0123456789ab", history=str(work / "runs-history.tsv"))


commands = []


def runner(command, env=None, check=False):
    commands.append(command)
    folder = pathlib.Path(command[command.index("--work") + 1]) / "runs" / "measure-route" / "d3d12"
    folder.mkdir(parents=True)
    (folder / "run.log").write_text("harness: frame 0 is game frame 10\n" + pace % (0, 5000, 800) * 3, encoding="utf-8")

    class Done:
        returncode = 0
    return Done()


failed, said = m.measure_runs(args, work / "measure", runner, machine=machine)
check(commands and all("--lock" in c for c in commands),
      "the harness gets a run lock (luma_test.py run refuses a run without one): %s" % commands)
lines = m.read_history(args.history)
check(failed == 0 and len(lines) == 1 and lines[0]["commit"] == "012345678" and lines[0]["device"] == "pc",
      "one line for the d3d12 run: %s %s" % (said, lines))
check(any(s.startswith("tablet left out") for s in said), "no device: the tablet is left out, said: %s" % said)
args.runs = "vulkan"
failed, said = m.measure_runs(args, work / "measure2", lambda *a, **k: type("D", (), {"returncode": 1})(),
                              machine=machine)
check(failed == 2 and said == ["vulkan no run (harness exit 1)"], "a run with no folder is a tool fault, exit 2: %s" % said)
check(m.processor_mask(0) is None, "no cores asked: the mask is left alone")


def disturbed_runner(command, env=None, check=False):
    folder = pathlib.Path(command[command.index("--work") + 1]) / "runs" / "measure-route" / "d3d12"
    folder.mkdir(parents=True)
    (folder / "run.log").write_text("harness: frame 0 is game frame 10\n" + pace % (0, 5000, 800) * 3, encoding="utf-8")
    (folder / "window-watch.txt").write_text("active first at 12.0 s, 40 of the quarter seconds\n", encoding="utf-8")
    return type("D", (), {"returncode": 0})()


# P-328: a run whose window the player's desktop reached is not compared and adds no history line.
args.runs = "d3d12"
before = len(m.read_history(args.history))
failed, said = m.measure_runs(args, work / "measure3", disturbed_runner, machine=machine)
check(failed == 3 and said == ["d3d12 not trusted (the window had the keyboard focus, first at 12.0 s, 40 of the quarter seconds)"] and
      len(m.read_history(args.history)) == before, "a reached window: not trusted, no line: %s" % said)
check(m.window_disturbed(work / "measure") is None, "no watch file: nothing reached")


# The tablet's run stands alone (P-321): inside the device's lock, the test app prepared before the run and removed
# after it, also when the run or the install fails; the steps take no lock of their own.
class FakeTablet(FakeDevice):
    def __init__(self, answers=True):
        super().__init__()
        self.answering = answers

    def answers(self):
        return self.answering


class FakeGate:
    def __init__(self, held=None):
        self.held, self.taken = held, []

    def take_device_lock_wait(self, path, who, wait_minutes):
        self.taken.append((path, wait_minutes))
        if self.held is None:
            pathlib.Path(path).mkdir()
        return self.held


def tablet_runs(prepare_code=0, run_code=0, held=None, answers=True):
    """measure_runs' tablet run with a fake adb, harness and lock: (failed, said, steps, gate, lock)."""
    steps = []

    def fake(command, env=None, check=False):
        step = "prepare" if "prepare" in command else "remove" if "remove" in command else "run"
        steps.append((step, "LUMA_TEST_DEVICE_LOCK" in (env or {}), command))
        if step == "run":
            folder = pathlib.Path(command[command.index("--work") + 1]) / "runs" / "measure-route" / "vulkan"
            folder.mkdir(parents=True)
            (folder / "run.log").write_text("harness: frame 0 is game frame 10\n" + pace % (0, 5000, 800) * 3,
                                            encoding="utf-8")
        return type("D", (), {"returncode": {"prepare": prepare_code, "run": run_code}.get(step, 0)})()

    lock = pathlib.Path(tempfile.mkdtemp()) / "tablet.lock"
    targs = argparse.Namespace(runs="tablet", bin="B", android_bin="AB", apk="T.apk", pak="G.pak", device="SERIAL",
                               device_lock=str(lock), device_wait=5.0, paks="", commit="0123456789ab",
                               history=str(pathlib.Path(tempfile.mkdtemp()) / "h.tsv"))
    gate = FakeGate(held)
    os.environ["LUMA_TEST_DEVICE_LOCK"] = "elsewhere.lock"
    try:
        failed, said = m.measure_runs(targs, pathlib.Path(tempfile.mkdtemp()), fake, FakeTablet(answers), gate, machine)
    finally:
        del os.environ["LUMA_TEST_DEVICE_LOCK"]
    return failed, said, steps, gate, lock


failed, said, steps, gate, lock = tablet_runs()
names = [s[0] for s in steps]
check(names == ["prepare", "run", "remove"] and failed == 0, "prepare, the run, remove: %s %s" % (names, said))
prepare = steps[0][2] if steps else []
check("--apk" in prepare and prepare[prepare.index("--apk") + 1] == "T.apk" and prepare[prepare.index("--pak") + 1] ==
      "G.pak", "the test app installed with the pak: %s" % prepare)
check(gate.taken == [(str(lock), 5.0)] and not lock.exists(), "the device's lock taken, then freed: %s" % gate.taken)
check(not any(s[1] for s in steps) and not any("--device-lock" in s[2] for s in steps),
      "the steps take no lock of their own while the run holds it: %s" % steps)
failed, said, steps, gate, lock = tablet_runs(run_code=1)
check([s[0] for s in steps] == ["prepare", "run", "remove"], "a failed run still removes the test app: %s" % steps)
failed, said, steps, gate, lock = tablet_runs(prepare_code=1)
check([s[0] for s in steps] == ["prepare", "remove"] and failed == 2 and said == ["tablet no run (the test app was not "
                                                                                  "prepared (exit 1))"],
      "a failed install: no run, the test app removed: %s %s" % ([s[0] for s in steps], said))
failed, said, steps, gate, lock = tablet_runs(held="gate 20261119 tablet")
check(steps == [] and failed == 2 and "lock stayed held 5 minutes: gate 20261119 tablet" in said[0],
      "a held tablet: nothing runs, the holder said: %s" % said)
failed, said, steps, gate, lock = tablet_runs(answers=False)
check(steps == [] and gate.taken == [] and said == ["tablet no run (SERIAL does not answer adb)"],
      "a tablet that does not answer: no lock, nothing runs: %s" % said)

if failures:
    print("measure_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("measure_test.py: pass (%d checks)" % checks[0])
