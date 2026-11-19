"""measure_test.py: tools/measure/measure.py without the game (N-1043).

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


def check(ok, what):
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
check(abs(f.get("gpu_ms", 0) - 4.0 / 3.0) < 1e-6, "the GPU's time over the presents: %s" % f)
check(m.timeline_figures(work / "timeline.bin", 99) == {}, "no frame 99: no figures")

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

# The history and the comparison.
history = work / "history.tsv"
line = m.history_line({"gap_max_ms": 10.0, "memory_mb": 300.0}, "abc", "pc", "d3d12", "120", run)
check(line.split("\t")[m.COLUMNS.index("gpu_ms")] == "-", "a figure not measured is -")
check(all(v[3] in ("no spread", "not measured") for v in m.compare(line, [])), "no spread with no earlier lines")
for gap in (9.0, 11.0, 10.0):
    m.add_history(history, m.history_line({"gap_max_ms": gap, "memory_mb": 300.0}, "old", "pc", "d3d12", "120", run))
m.add_history(history, m.history_line({"gap_max_ms": 99.0}, "old", "tablet", "vulkan", "120", run))
rows = m.read_history(history)
check(len(rows) == 4 and history.read_text(encoding="utf-8").startswith("commit\ttime"), "the history's lines: %d" % len(rows))
verdicts = {v[0]: v[3] for v in m.compare(line, rows)}
check(verdicts["gap_max_ms"] == "holds" and verdicts["memory_mb"] == "holds", "within the spread holds: %s" % verdicts)
worse = m.history_line({"gap_max_ms": 11.5, "memory_mb": 300.0}, "new", "pc", "d3d12", "120", run)
check({v[0]: v[3] for v in m.compare(worse, rows)}["gap_max_ms"] == "worse", "above every earlier line is worse")
other = m.history_line({"gap_max_ms": 11.5}, "new", "pc-4cores", "d3d12", "120", run)
check({v[0]: v[3] for v in m.compare(other, rows)}["gap_max_ms"] == "no spread", "another device has its own spread")


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

# The runs: a fake harness writes a run folder; the line lands in the history; a missing run fails.
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


failed, said = m.measure_runs(args, work / "measure", runner)
check(commands and all("--lock" in c for c in commands),
      "the harness gets a run lock (luma_test.py run refuses a run without one): %s" % commands)
lines = m.read_history(args.history)
check(not failed and len(lines) == 1 and lines[0]["commit"] == "012345678" and lines[0]["device"] == "pc",
      "one line for the d3d12 run: %s %s" % (said, lines))
check(any(s.startswith("tablet left out") for s in said), "no device: the tablet is left out, said: %s" % said)
args.runs = "vulkan"
failed, said = m.measure_runs(args, work / "measure2", lambda *a, **k: type("D", (), {"returncode": 1})())
check(failed and said == ["vulkan no run (harness exit 1)"], "a run with no folder fails: %s" % said)
check(m.processor_mask(0) is None, "no cores asked: the mask is left alone")

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
        failed, said = m.measure_runs(targs, pathlib.Path(tempfile.mkdtemp()), fake, FakeTablet(answers), gate)
    finally:
        del os.environ["LUMA_TEST_DEVICE_LOCK"]
    return failed, said, steps, gate, lock


failed, said, steps, gate, lock = tablet_runs()
names = [s[0] for s in steps]
check(names == ["prepare", "run", "remove"] and not failed, "prepare, the run, remove: %s %s" % (names, said))
prepare = steps[0][2] if steps else []
check("--apk" in prepare and prepare[prepare.index("--apk") + 1] == "T.apk" and prepare[prepare.index("--pak") + 1] ==
      "G.pak", "the test app installed with the pak: %s" % prepare)
check(gate.taken == [(str(lock), 5.0)] and not lock.exists(), "the device's lock taken, then freed: %s" % gate.taken)
check(not any(s[1] for s in steps) and not any("--device-lock" in s[2] for s in steps),
      "the steps take no lock of their own while the run holds it: %s" % steps)
failed, said, steps, gate, lock = tablet_runs(run_code=1)
check([s[0] for s in steps] == ["prepare", "run", "remove"], "a failed run still removes the test app: %s" % steps)
failed, said, steps, gate, lock = tablet_runs(prepare_code=1)
check([s[0] for s in steps] == ["prepare", "remove"] and failed and said == ["tablet no run (the test app was not "
                                                                             "prepared (exit 1))"],
      "a failed install: no run, the test app removed: %s %s" % ([s[0] for s in steps], said))
failed, said, steps, gate, lock = tablet_runs(held="gate 20261119 tablet")
check(steps == [] and failed and "lock stayed held 5 minutes: gate 20261119 tablet" in said[0],
      "a held tablet: nothing runs, the holder said: %s" % said)
failed, said, steps, gate, lock = tablet_runs(answers=False)
check(steps == [] and gate.taken == [] and said == ["tablet no run (SERIAL does not answer adb)"],
      "a tablet that does not answer: no lock, nothing runs: %s" % said)

if failures:
    print("measure_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("measure_test.py: pass")
