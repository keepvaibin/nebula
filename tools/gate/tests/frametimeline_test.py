"""frametimeline_test.py: tools/gate/frametimeline.py (G10.2) on hand-made query results whose rows are counted by hand, the trace
processor called through a fake program; the planted faults run through the real parser and pace.py's rows and refused; the
baseline made, stored, read back and read by the gate's check pace/tablet. Nothing here opens the trace processor's folder or a
device.

The query results (tests/pace/frametimeline/<case>/: frames.csv, expected.csv and clocks.csv as the trace processor prints them,
sf-latency.txt and sf-jank.txt as the capture writes them) are 12 frames at 60 Hz (a refresh of 16666667 ns), the game's steps a
refresh apart from 1000000000 ns (CLOCK_MONOTONIC) and the trace's clock 1 s ahead of it (its clock snapshot). Each frame starts
at its vsync, is ready 8.2 ms after it, asks to be shown two refreshes after it (its desired present half a refresh earlier) and
is shown two refreshes after it, except:
  right    none
  sleep    frame 6 ready 28.2 ms after its vsync and shown three refreshes after it: App Deadline Missed (the 20 ms sleep)
  mode     frames 5 and 6 shown three refreshes after their vsync: ModeChange in progress (the app's and the display's rows)
  depth    every frame asks three refreshes and is shown three after its vsync, none late (the raised depth)
  unknown  frame 2 late with SurfaceFlinger CPU Deadline Missed and App Deadline Missed (the display's row: the SurfaceFlinger
           one only); frame 4 late with Unknown Jank; frame 8 on time with Buffer Stuffing; frame 9 late with Display HAL
"""
import json
import os
import pathlib
import shutil
import struct
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
import frametimeline as ft  # noqa: E402
import pace  # noqa: E402
import gate  # noqa: E402

TREE = HERE.parents[1]
FIXTURES = TREE / "tests" / "pace" / "frametimeline"
P = 16666667
STEPS = [1000000000 + k * P for k in range(12)]
FAKE = '''import pathlib, sys
args = sys.argv[1:]
sql = pathlib.Path(args[args.index("-q") + 1]).read_text(encoding="utf-8")
trace = pathlib.Path(args[-1])
if trace.read_text(encoding="utf-8") == "broken":
    sys.stderr.write("the trace could not be parsed")
    sys.exit(1)
name = ("frames.csv" if "actual_frame_timeline_slice" in sql else "expected.csv" if "expected_frame_timeline_slice" in sql
        else "clocks.csv")
sys.stdout.write((trace.parent / name).read_text(encoding="utf-8"))
'''

failures = []
results = []


def check(ok, what):
    if not ok:
        failures.append(what)


def write_timeline(path, starts, walks=()):
    """A game timeline (frame_timeline.h): StepWaitEnd at each step's start, WalkBegin b=1 at each in-between picture's."""
    events = [(q, ft.STEP_WAIT_END, 0) for q in starts] + [(q, ft.WALK_BEGIN, 1) for q in walks]
    with open(path, "wb") as f:
        f.write(b"NBTL0001" + struct.pack("<5Q", 1, 1000000000, len(events), len(events), 1) + b"\0" * 16)
        for q, e, b in sorted(events):
            f.write(struct.pack("<QQQIHH", q, 0, b, 1, e, 1))


def run_folder(root, case, trace="trace"):
    """A device run's folder holding a case's capture, its trace file (the fake program serves the case's tables for it) and the
    game's timeline."""
    folder = pathlib.Path(root) / case
    shutil.copytree(FIXTURES / case, folder)
    if trace is not None:
        (folder / ft.TRACE_FILE).write_text(trace, encoding="utf-8")
    write_timeline(folder / "timeline.bin", STEPS)
    return folder


def tables_of(folder, fake):
    return ft.run_processor([sys.executable, "-X", "utf8", "-I", str(fake)], folder / ft.TRACE_FILE, ft.QUERIES)


def report(folder, tables, method="auto", start="step"):
    text = lambda n: (folder / n).read_text(encoding="utf-8")
    return ft.report_of(tables, text("sf-latency.txt"), text("sf-jank.txt"), ft.game_starts(folder / "timeline.bin"), method, start)


def causes(r):
    return {c: n for c, n in r["causes"].items() if n}


def main():
    os.environ.pop(ft.PROCESSOR_ENV, None)  # the test never reaches a real trace processor
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        fake = root / "fake_trace_processor.py"
        fake.write_text(FAKE, encoding="utf-8")

        # The one function that runs the trace processor, through the fake program: three tables by name, read as CSV.
        right = run_folder(root, "right")
        tables = tables_of(right, fake)
        check(sorted(tables) == ["clocks", "expected", "frames"] and len(tables["frames"]["rows"]) == 24
              and tables["frames"]["columns"][0] == "ts" and tables["frames"]["rows"][1][5] is None,
              "run_processor: the fake's three tables, 24 rows of frames, a null read as None: %s" % sorted(tables))
        bad = root / "bad"
        shutil.copytree(FIXTURES / "right", bad)
        (bad / ft.TRACE_FILE).write_text("broken", encoding="utf-8")
        failed = tables_of(bad, fake)
        check(all("error" in t and "could not be parsed" in t["error"] for t in failed.values()),
              "a trace the processor cannot read: each query an error: %s" % failed)
        check(ft.records(failed["frames"]) == [] and ft.trace_frames(failed)[0] == [], "a failed table holds no rows")

        # The right case, by FrameTimeline's app rows: 12 frames, each two refreshes from its vsync and from its step, no miss.
        r = report(right, tables, "auto", "vsync")
        check(r["method"] == "frametimeline" and r["app_rows"] == 12 and r["layer"].startswith("SurfaceView[dev.nebula.galaxy.test/"),
              "auto with app rows: the frametimeline method, the game's layer: %s %s %s" % (r["method"], r["app_rows"], r["layer"]))
        check(r["refreshes"] == {"1 or less": 0, "2": 12, "3": 0, "4 or more": 0} and r["misses"] == 0 and causes(r) == {}
              and r["refresh_ms"] == 16.6667 and r["gpu_composition"] == 0 and r["gpu_frames"] == 12,
              "right, vsync start: 12 at two refreshes, no miss, 16.6667 ms, none composed by the GPU: %s" % r)
        s = report(right, tables, "frametimeline", "step")
        check(s["refreshes"]["2"] == 12 and s["refresh_frames"] == 12,
              "right, step start (the game's moments moved by the clock snapshot): 12 at two refreshes: %s" % s["refreshes"])
        check(not ft.unknown_fails(r) and r["unnamed_share"] == 0.0 and r["late_share"] == 0.0, "right: no unnamed miss")
        right_report = r

        # Planted fault (a), the 20 ms render sleep: App Deadline Missed, no SurfaceFlinger row; three refreshes from its vsync.
        sleep = run_folder(root, "sleep")
        a = report(sleep, tables_of(sleep, fake), "frametimeline", "vsync")
        check(causes(a) == {"App Deadline Missed": 1} and a["misses"] == 1 and a["refreshes"]["3"] == 1,
              "the sleep: one App Deadline Missed and nothing of SurfaceFlinger's, its frame three refreshes: %s %s" % (causes(a), a["refreshes"]))
        # Planted fault (b), a 120 to 60 Hz mode change: ModeChange in progress.
        mode = run_folder(root, "mode")
        b = report(mode, tables_of(mode, fake), "frametimeline", "vsync")
        check(causes(b) == {"ModeChange in progress": 2} and b["misses"] == 2, "the mode change: two ModeChange in progress: %s" % causes(b))
        # Planted fault (c), the raised depth: every frame three refreshes in row 1, none late (it was shown when it asked).
        depth = run_folder(root, "depth")
        c = report(depth, tables_of(depth, fake), "frametimeline", "vsync")
        check(c["refreshes"]["3"] == 12 and c["misses"] == 0 and c["three_refresh_share"] == 1.0,
              "the raised depth: 12 at three refreshes, no miss: %s" % c["refreshes"])
        c_step = report(depth, tables_of(depth, fake), "frametimeline", "step")
        check(c_step["refreshes"]["3"] == 12, "the raised depth from the step moments too: %s" % c_step["refreshes"])
        # Every miss in exactly one row: several flags go to the first of PRECEDENCE; one no row names is Unknown, above 0.05%: FAIL.
        unknown = run_folder(root, "unknown")
        u = report(unknown, tables_of(unknown, fake), "frametimeline", "vsync")
        check(causes(u) == {"App Deadline Missed": 1, "Display HAL": 1, "Unknown": 1} and u["misses"] == 3 and u["buffer_stuffing"] == 1,
              "the unknown case: App Deadline Missed over SurfaceFlinger CPU, Display HAL, one Unknown, Buffer Stuffing no miss: %s %d %d" % (
                  causes(u), u["misses"], u["buffer_stuffing"]))
        check(ft.unknown_fails(u) and abs(u["unnamed_share"] - 1 / 12) < 1e-12, "one Unknown of 12: above 0.05%, FAIL")
        for x in (a, b, c, u):
            check(sum(x["causes"].values()) == x["misses"], "every miss lands in exactly one row: %s" % x["causes"])
        check(ft.cause_of({"Buffer Stuffing"}, False) == (False, None, True) and ft.cause_of(set(), True) == (True, "Unknown", False)
              and ft.cause_of({"ModeChange in progress", "App Deadline Missed"}, True)[1] == "ModeChange in progress",
              "cause_of: stuffing alone no miss; a late present with no flag Unknown; a mode change first")

        # The faults against the right case's baseline, through pace.py's rows: the right case passes, each fault is refused.
        base = ft.baseline([right_report, right_report, right_report])
        for name, x in (("right", right_report), ("fault a, the 20 ms sleep", a), ("fault b, the mode change", b),
                        ("fault c, the raised depth", c)):
            verdict, text = ft.judge(x, base)
            want = "pass" if name == "right" else "FAIL"
            check(verdict == want, "%s against the right baseline: %s (%s)" % (name, verdict, text))
            results.append("%s: %s, %s; causes %s; refreshes %s" % (name, verdict, text, causes(x) or "none",
                                                                     {k: v for k, v in x["refreshes"].items() if v}))

        # The fallback (the decision rule): no app rows of the game's layer (FrameTimeline does not cover SurfaceViews), so rows 1
        # and 2 come from --latency and SurfaceFlinger's display-frame jank types, and the note says so.
        def without_app(t):
            t = json.loads(json.dumps(t))
            names = t["frames"]["columns"]
            t["frames"]["rows"] = [row for row in t["frames"]["rows"] if "surfaceflinger" in str(row[names.index("process_name")])]
            return t
        lr = report(right, without_app(tables), "auto", "step")
        check(lr["method"] == "latency" and lr["app_rows"] == 0 and lr["frames"] == 12 and lr["refreshes"]["2"] == 12 and lr["misses"] == 0
              and any("ASurfaceTransaction_setFrameTimeline" in n for n in lr["notes"]),
              "no app rows: the latency method, its 12 frames (the first read's left out, the pending one too), the note: %s" % lr)
        lm = report(mode, without_app(tables_of(mode, fake)), "auto", "step")
        check(causes(lm) == {"ModeChange in progress": 2}, "latency: the mode change named by the display's rows: %s" % causes(lm))
        la = report(sleep, without_app(tables_of(sleep, fake)), "auto", "step")
        check(causes(la) == {"App Deadline Missed": 1}, "latency: the sleep's frame ready after its time, App Deadline Missed: %s" % causes(la))
        lu = report(unknown, without_app(tables_of(unknown, fake)), "auto", "step")
        check(causes(lu) == {"SurfaceFlinger CPU Deadline Missed": 1, "Display HAL": 1, "Unknown": 1},
              "latency: the display's flags, Unknown where it names none: %s" % causes(lu))
        ld = report(depth, without_app(tables_of(depth, fake)), "auto", "step")
        check(ld["refreshes"]["3"] == 12 and ld["misses"] == 0, "latency: the raised depth, three refreshes, none late")
        check(report(right, without_app(tables), "auto", "vsync")["refresh_frames"] == 0,
              "latency with the vsync start: row 1 not measured (it needs app rows)")

        # read(): no trace processor (none given, none in the environment): the trace is not read, the latency method, and
        # frametimeline.json beside the run. A processor named that is not there: the same, with its note. No capture: None.
        no_tp = root / "no-tp"
        shutil.copytree(FIXTURES / "mode", no_tp)
        (no_tp / ft.TRACE_FILE).write_text("trace", encoding="utf-8")
        write_timeline(no_tp / "timeline.bin", STEPS)
        got = ft.read(no_tp)
        check(got["method"] == "latency" and any("no trace processor" in n for n in got["notes"]) and (no_tp / ft.REPORT).is_file()
              and causes(got) == {"Unknown": 2},
              "read with no processor: latency, its note, the file; the mode change with no display rows is Unknown: %s" % causes(got))
        gone = ft.read(no_tp, processor=str(root / "nowhere" / "trace_processor_shell.exe"))
        check(any("is not there" in n for n in gone["notes"]), "a processor that is not there: a note")
        empty = root / "empty"
        empty.mkdir()
        check(ft.read(empty) is None, "a run with no capture: no report")
        check(ft.processor_path("") == "" and ft.processor_path("x") == "x", "the processor's path: the option, else the environment")
        os.environ[ft.PROCESSOR_ENV] = "from-env"
        check(ft.processor_path("") == "from-env", "the environment's name")
        os.environ.pop(ft.PROCESSOR_ENV)

        # The layer from SurfaceFlinger's list, and the capture's config.
        listing = ("Background for SurfaceView[dev.nebula.galaxy.test/dev.nebula.galaxy.GameActivity]#5\n"
                   "SurfaceView[dev.nebula.galaxy.test/dev.nebula.galaxy.GameActivity]#6\n"
                   "SurfaceView[dev.nebula.galaxy.test/dev.nebula.galaxy.GameActivity](BLAST)#7\nStatusBar#9\n")
        check(ft.pick_layer(listing, "dev.nebula.galaxy.test") == "SurfaceView[dev.nebula.galaxy.test/dev.nebula.galaxy.GameActivity](BLAST)#7",
              "the game's BLAST SurfaceView layer, never its background")
        config = ft.trace_config(90000, ["dev.nebula.galaxy.test"])
        check('name: "android.surfaceflinger.frametimeline" target_buffer: 0' in config and 'ftrace_events: "sched/sched_switch"' in config
              and 'atrace_apps: "dev.nebula.galaxy.test"' in config and "duration_ms: 90000" in config, "the trace's config")

        # The capture on a scripted device: frame 0 starts it (the layer, Perfetto in the background, a --latency read), polls read
        # --latency again, the end stops Perfetto and pulls the trace; capture.json says what was done.
        class Device:
            def __init__(self):
                self.calls = []

            def shell(self, command, timeout=600):
                self.calls.append(command)
                if command.startswith("dumpsys SurfaceFlinger --list"):
                    return 0, listing
                if command.startswith("dumpsys SurfaceFlinger --latency"):
                    return 0, "%d\n1\t2\t3\n" % P
                if command.startswith("kill -0"):
                    return 1, ""
                return 0, ""

            def shell_input(self, command, text, timeout=600):
                self.calls.append(command)
                self.config = text
                return 0, "4242\n"

            def log_find(self, pid, text):
                return False

            def adb(self, *args, timeout=600, stdin=None):
                self.calls.append(" ".join(args))
                pathlib.Path(args[2]).write_text("trace", encoding="utf-8")
                return type("R", (), {"returncode": 0, "stderr": b""})()
        now = [100.0]
        dev = Device()
        cap_folder = root / "cap"
        cap_folder.mkdir()
        cap = ft.Capture(dev, cap_folder, "dev.nebula.galaxy.test", 420, clock=lambda: now[0])
        cap.poll("4321", "[display] refresh-hz=120\n")
        check(not cap.started, "no frame 0 yet: nothing started")
        cap.tick()
        check(not cap.started and cap.reads == 0 and not dev.calls, "a tick before frame 0 does nothing: %s" % dev.calls)
        cap.poll("4321", "extension: harness: frame 0 is game frame 900\n")
        now[0] += 0.5
        cap.tick()
        check(cap.reads == 1, "a tick half a second after a read: not due yet (%d reads)" % cap.reads)
        now[0] += 0.5
        cap.tick()
        check(cap.reads == 2, "a tick a second after a read at 120 Hz: the next --latency read, between the 2 s looks (%d)" % cap.reads)
        now[0] += 0.5
        cap.poll("4321", "")
        cap.finish(sleep=lambda s: None)
        said = json.loads((cap_folder / "capture.json").read_text(encoding="utf-8"))
        check(said["perfetto"] == "pid 4242" and said["trace"] == "pulled" and said["latency_reads"] == 3 and not said["errors"]
              and (cap_folder / ft.TRACE_FILE).is_file() and "kill -TERM 4242" in dev.calls and "duration_ms: 480000" in dev.config,
              "the capture: Perfetto started and stopped, three --latency reads (1 s apart at 120 Hz), the trace pulled: %s %s" % (said, dev.calls))
        check(any(c.startswith("dumpsys SurfaceFlinger --latency 'SurfaceView[") for c in dev.calls), "the layer's name quoted for the shell")
        off = root / "cap-off"
        off.mkdir()
        never = ft.Capture(Device(), off, "dev.nebula.galaxy.test", 420)
        never.finish()
        check(json.loads((off / "capture.json").read_text(encoding="utf-8"))["started"] is False, "a run with no frame 0: not started")

        # The baseline from synthetic frametimeline.json of three runs at each rate, stored and read back.
        def synthetic(rate, frames, late, three, unknown):
            r = {"format": 1, "method": "latency", "start": "step", "refresh_ms": round(1000.0 / rate, 4), "frames": frames,
                 "refresh_frames": frames, "refreshes": {"1 or less": 0, "2": frames - three, "3": three, "4 or more": 0},
                 "causes": dict({cause: 0 for cause in ft.CAUSES}, **{"App Deadline Missed": late - unknown, "Unknown": unknown}),
                 "misses": late, "buffer_stuffing": 0, "gpu_composition": 0, "gpu_frames": frames, "latched_unsignaled": None}
            r.update(ft.shares(r))
            return r
        runs = root / "base-runs"
        made = []
        for i, (rate, late) in enumerate([(60, 0), (60, 2), (60, 1), (120, 4), (120, 6), (120, 5)]):
            folder = runs / ("run-%d" % i)
            folder.mkdir(parents=True)
            (folder / ft.REPORT).write_text(json.dumps(synthetic(rate, 4800 * rate // 60, late, 0, 0)), encoding="utf-8")
            made.append(str(folder))
        stored = root / "tablet-base.json"
        check(ft.main(["baseline"] + made + ["--out", str(stored)]) == 0, "frametimeline.py baseline writes the file")
        back = json.loads(stored.read_text(encoding="utf-8"))
        check(back["method"] == "latency" and back["start"] == "step" and sorted(back["rates"]) == ["120", "60"]
              and back["rates"]["60"]["runs"] == 3 and back["rates"]["60"]["misses"] == 3 and back["rates"]["60"]["frames"] == 14400
              and abs(back["rates"]["60"]["most"]["late_share"] - 2 / 4800) < 1e-12 and back["rates"]["120"]["causes"]["App Deadline Missed"] == 15,
              "the baseline by rate: three runs each, frames and causes summed, the most late share of one run: %s" % back["rates"]["60"])
        mixed = dict(synthetic(60, 100, 0, 0, 0), method="frametimeline")
        try:
            ft.baseline([synthetic(60, 100, 0, 0, 0), mixed])
            check(False, "reports of two methods: refused")
        except ValueError:
            pass
        check(ft.judge(synthetic(60, 4800, 2, 0, 0), back)[0] == "pass", "the stored figures: pass")
        check(ft.judge(synthetic(60, 4800, 3, 0, 0), back)[0] == "FAIL", "a worse late share: refused")
        check(ft.judge(synthetic(120, 9600, 6, 1, 0), back)[0] == "FAIL", "a worse three-refresh share: refused")
        check(ft.judge(synthetic(60, 4800, 1, 0, 1), back)[0] == "FAIL", "a worse unnamed share: refused")
        check(ft.judge(synthetic(144, 11520, 0, 0, 0), back)[0] == "recorded", "a rate the baseline has no figure for: recorded")
        check(ft.judge(dict(synthetic(60, 4800, 0, 0, 0), method="frametimeline"), back)[0].startswith("not measured"),
              "a run read by another method: not measured")

        # The gate's check pace/tablet on pace.py's real digest of device runs: their captures read (no trace processor: the latency
        # method), the rows, and with a stored baseline only the row against it refusing.
        line = ("[pace] seconds=9.983333 steps=600 steps_per_second=60.0000 pictures=600 displayed=600 generated=0 refresh_ms=16.667 "
                "on_time=599 missed=0 doubled=0 missed_system=0 jumps=0 jump_max_us=100.0 animation_within_1ms=1.0 animation_p99_us=80.0 "
                "delay_avg_us=18000.0 delay_p99_us=20000.0 render_avg_us=3000.0 render_p99_us=4000.0 step_work_avg_us=3000.0 "
                "step_work_p99_us=4000.0 step_work_max_us=5000.0")
        timing = ("[present-timing] 600 frames: 598 shown at their requested slot, 2 late, 0 without one (unmatched); 600 at the "
                  "target interval; actual-hz 60.00, interval-max 33.33 ms, duplicate-times 0; late(available/delayed)=1/1 margin 5.00 "
                  "ms (driver refresh 33.33, display 16.67 ms); pipeline 33.33 ms (a late frame missed its slot by 16.67 at most)")

        def device_work(name, case):
            work = root / name
            folder = work / "runs" / "pace" / "pace-rate-60" / "vulkan"
            folder.parent.mkdir(parents=True)
            shutil.copytree(FIXTURES / case, folder)
            write_timeline(folder / "timeline.bin", STEPS)
            (folder / "run.log").write_text("\n".join([line, "extension: harness: frame 0 is game frame 900", line, timing, line,
                                                       timing, line, timing]) + "\n", encoding="utf-8")
            (folder / "exit-info.txt").write_text("exit 0\n", encoding="utf-8")
            (folder / "test.json").write_text(json.dumps({"test": {"settings": ["GALAXY_MAX_FPS=60", "GALAXY_VSYNC_INTERVAL=1"]}}),
                                              encoding="utf-8")
            (work / "pace-run.json").write_text(json.dumps({"harness_exit": {"vulkan": 0}, "not_quiet": {}}), encoding="utf-8")
            return work

        plain = device_work("plain", "right")
        code = pace.digest(plain, None, pace.reading_of())
        rows = {m: (v, verdict) for m, v, _t, verdict in json.loads((plain / "pace.json").read_text(encoding="utf-8"))[0]["rows"]}
        check(rows.get("frame start to scan-out", ("", ""))[1] == "recorded" and "2: 100.00%" in rows["frame start to scan-out"][0]
              and rows["miss: Unknown"][1] == "pass" and rows["misses by the game's lines"][0] == "4 late of 1200: 2 available late, 2 delayed late"
              and pace.BASELINE_ROW not in rows,
              "pace.py's rows for a device run: refreshes, causes, the game's split beside them; no baseline row: %s" % rows)
        check(gate.pace_tablet_reading(plain, code, stored=False)[0], "no stored file: pace/tablet refuses nothing")
        # The stored baseline: three runs of the right case as pace.py read them (latency, step): no miss, none at three refreshes.
        reports = [json.loads((plain / "runs/pace/pace-rate-60/vulkan" / ft.REPORT).read_text(encoding="utf-8"))] * 3
        stored.write_text(json.dumps(ft.baseline(reports)), encoding="utf-8")
        reading = pace.reading_of(baseline=str(stored))
        for name, case, want in (("same", "right", True), ("late", "sleep", False), ("mode change", "mode", False),
                                 ("deep", "depth", False), ("unnamed", "unknown", False)):
            work = device_work("stored-" + name, case)
            code = pace.digest(work, None, reading)
            passed, detail, _t, kind = gate.pace_tablet_reading(work, code, stored=True)
            check(passed == want and (want or kind == "fail"),
                  "pace/tablet with a stored baseline, %s: %s (%s, %s)" % (name, "pass" if passed else "refused", kind, detail))
            results.append("pace/tablet against the stored baseline, %s: %s" % (name, detail))
        # Misses that are FAIL rows of their own (the Unknown row) refuse nothing once a baseline is stored: only the row against it.
        unnamed = root / "stored-unnamed"
        said = json.loads((unnamed / "pace.json").read_text(encoding="utf-8"))[0]["rows"]
        check(any(m == "miss: Unknown" and v == "FAIL" for m, _x, _t, v in said) and any(m == pace.BASELINE_ROW and v == "FAIL" for m, _x, _t, v in said),
              "the unknown case: its Unknown row FAIL and the baseline row FAIL")
        # A run the baseline could not judge (another method) is the tool's, never a pass.
        other = dict(json.loads(stored.read_text(encoding="utf-8")), method="frametimeline")
        stored.write_text(json.dumps(other), encoding="utf-8")
        work = device_work("stored-other", "right")
        code = pace.digest(work, None, dict(pace.reading_of(baseline=str(stored)), method="latency"))
        passed, detail, _t, kind = gate.pace_tablet_reading(work, code, stored=True)
        check(not passed and kind == "tool", "a run read another way than the baseline: tool (%s, %s)" % (kind, detail))
        # The command the gate runs: the trace processor always named, the baseline only when it is stored.
        g = type("G", (), {})()
        g.android_bin, g.pak, g.paks_text = root / "bin", "game.pak", lambda: ""
        g.args = type("A", (), {"tablet": "T", "tablet_lock": "L", "tablet_wait": 20.0, "pak": "", "work": str(root), "pace_fault": False})()
        saved = gate.PACE_TABLET_BASE
        try:
            gate.PACE_TABLET_BASE = root / "nowhere.json"
            cmd = gate.pace_tablet_command(g, root / "w", root / "t.apk")
            check("--baseline" not in cmd and cmd[cmd.index("--trace-processor") + 1] == gate.PACE_TRACE_PROCESSOR, "no stored file: no --baseline")
            gate.PACE_TABLET_BASE = stored
            cmd = gate.pace_tablet_command(g, root / "w", root / "t.apk")
            args = pace.parser().parse_args(cmd[5:])
            check(args.baseline == str(stored) and args.trace_processor == gate.PACE_TRACE_PROCESSOR, "a stored file: --baseline it")
        finally:
            gate.PACE_TABLET_BASE = saved

    for r in results:
        print("  " + r)
    if failures:
        print("frametimeline_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("frametimeline_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
