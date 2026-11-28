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
GROSS_SHIPPED = dict(getattr(pace, "BASELINE_GROSS", {}))
REFUSES_SHIPPED = dict(getattr(pace, "BASELINE_REFUSES", {}))  # what pace.py ships with; the test sets it for its own blocks and puts it back


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


def shown_cases():
    """G10.4m: what the screen showed, counted by hand from the present times (pair_verdict, shown_events, classify_late, episodes)."""
    P = 8333333
    frame = lambda present, desired: {"present": present, "desired": desired, "ready": present - 1}
    ms2 = 2_000_000
    frames = [frame(0, -P // 2),                      # 0
              frame(P, P // 2),                       # 1 on time: one picture a refresh as asked
              frame(2 * P, 3 * P // 2),               # 2 on time
              frame(5 * P, 7 * P // 2),               # 3 the picture before held 3 refreshes: repeated; shown 1.5 refreshes after its time: late
              frame(6 * P, 11 * P // 2),              # 4 the game's moment moved 2 refreshes, the screen's 1: skipped
              frame(7 * P, 13 * P // 2 - ms2),        # 5 (shown - asked) changed by -2 ms, held 1 refresh: shifted
              frame(8 * P, 15 * P // 2 - ms2),        # 6 on time again
              frame(20 * P, 39 * P // 2)]             # 7 a gap of 12 refreshes: a seam
    verdicts = [ft.pair_verdict(a, b, P) for a, b in zip(frames, frames[1:])]
    check(verdicts == ["on time", "on time", "repeated", "skipped", "shifted", "on time", None], "pair verdicts: %s" % verdicts)
    shown = ft.shown_events(frames, P)
    check((shown["pairs"], shown["seams"], shown["repeated"], shown["skipped"], shown["shifted"], shown["on_time"], shown["jumps"],
           shown["events"]) == (6, 1, 1, 1, 1, 3, 3, 3), "shown events counted by hand: %s" % shown)
    late = ft.classify_late(frames, P)
    check(len(late) == 1 and late[0]["index"] == 3 and late[0]["verdict"] == "repeated" and late[0]["held"] == 3.0 and late[0]["asked"] == 2.0,
          "one late frame (3), repeated, held 3 asked 2: %s" % late)
    check(ft.episodes(frames, P) == [{"frames": 1, "opened": "repeated", "closed": "skipped"}], "one episode, opened by a repeat, closed by a skip")
    # A frame shown a refresh after its asked time at every refresh (a steady offset) is late in the label and on time on the screen.
    steady = [frame(k * P, k * P - P // 2 - P) for k in range(6)]
    kinds = [r["verdict"] for r in ft.classify_late(steady, P)]
    check(kinds == ["seam"] + ["on time"] * 5 and ft.shown_events(steady, P)["events"] == 0,
          "a steady offset: 6 late labels (the first has no pair before it), 5 on time, no event: %s" % kinds)
    # The bound of a row from the baseline's runs: the median plus the range (G10.4m: 60 Hz clean runs 4.1, 3.9, 3.4, 4.5, 4.1 a minute).
    check(abs(ft.bound_of([4.1, 3.9, 3.4, 4.5, 4.1]) - (4.1 + 1.1)) < 1e-9 and ft.bound_of([5.0, 5.0]) == 5.0,
          "the bound of a row: the median 4.1 plus the range 1.1")
    base = {"method": "latency", "start": "step", "rates": {"60": {"bounds": {"events_per_minute": 5.2, "jumps_per_minute": 0.0,
                                                                             "late_share": 0.0}}}}
    run = {"method": "latency", "start": "step", "refresh_ms": 16.6667, "late_share": 0.0, "shown": {"events_per_minute": 4.4,
                                                                                                     "jumps_per_minute": 0.0}}
    check(ft.judge(run, base)[0] == "pass" and ft.rows_beyond(run, base) == [], "a clean run at 60 Hz: no row beyond its bound")
    sleepy = dict(run, late_share=0.002, shown={"events_per_minute": 25.4, "jumps_per_minute": 6.2})
    check([row for row, _f, _b in ft.rows_beyond(sleepy, base)] == ["late_share", "events_per_minute", "jumps_per_minute"]
          and ft.judge(sleepy, base)[0] == "FAIL", "the 20 ms sleep's run (events 25.4, jumps 6.2 a minute): three rows beyond their bounds")
    check(ft.judge(dict(run, refresh_ms=8.3333), base)[0] == "recorded", "a rate the baseline has no figure for: recorded")
    results.append("what the screen showed: %d pairs, %d repeated, %d skipped, %d shifted, %d jumps, 1 seam" % (
        shown["pairs"], shown["repeated"], shown["skipped"], shown["shifted"], shown["jumps"]))


def ab_cases():
    """G10.4m: the A/B rule (a win only by more than the larger range), the power rule (a rise only by more than the baseline's range),
    the choice among variants (a tie never costs power; the baseline stays when none wins), counted by hand."""
    base = [10, 12, 14]                                  # median 12, range 4
    check(ft.ab_wins(base, [4, 5, 6]), "median 5 against 12: gain 7 over the range 4: a win")
    check(not ft.ab_wins(base, [9, 10, 11]), "median 10 against 12: gain 2 inside the range 4: not a win")
    check(not ft.ab_wins(base, [8, 8, 8]), "gain exactly the range 4: not a win (more than)")
    check(not ft.ab_wins([10, 10, 10], [2, 6, 10]), "gain 4 inside the variant's own range 8: not a win")
    check(not ft.ab_wins(base, [12, 12, 12]) and not ft.ab_wins(base, [12, 13, 14]), "a tie or a worse variant: the baseline stays")
    check(ft.ab_wins([5, 6, 7], [20, 21, 22], lower_is_better=False), "a row where more is better: median 21 against 6")
    try:
        ft.ab_wins([1], [2, 3])
        check(False, "one baseline run: refused")
    except ValueError:
        pass
    heat = [1.0, 1.1, 1.2]                               # median 1.1, range 0.2
    check(ft.power_raises(heat, [1.3, 1.35, 1.4]), "median 1.35 over 1.1: a rise of 0.25 over the range 0.2")
    check(not ft.power_raises(heat, [1.25, 1.3, 1.35]), "median 1.3: a rise of exactly 0.2: allowed")
    check(not ft.power_raises(heat, [0.5, 0.6, 0.7]), "a fall is never a rise")
    baseline = {"row": base, "power": {"gpu": [300, 310, 320], "cpu": [1000, 1010, 1020]}}
    variants = {"hint-share-80": {"row": [4, 5, 6], "power": {"gpu": [320, 325, 330], "cpu": [1000, 1020, 1030]}},
                "hint-share-90": {"row": [9, 10, 11], "power": {"gpu": [300, 305, 310], "cpu": [1000, 1005, 1010]}},
                "hint-share-67": {"row": [3, 4, 5], "power": {"gpu": [360, 370, 380], "cpu": [1000, 1010, 1020]}}}
    choice, detail = ft.ab_choice(baseline, variants, ("gpu", "cpu"))
    check(choice == "hint-share-80" and detail["hint-share-90"]["wins"] is False and detail["hint-share-67"]["wins"]
          and detail["hint-share-67"]["raises"] == ["gpu"], "80 wins inside the power bound, 90 is no win, 67 wins and raises the GPU clock: %s" % (detail,))
    check(ft.ab_choice(baseline, {"hint-share-90": variants["hint-share-90"], "hint-share-67": variants["hint-share-67"]}, ("gpu", "cpu"))[0] is None,
          "no candidate (one no win, one a rise): the baseline stays")
    tie = dict(variants)
    tie["hint-share-85"] = {"row": [4, 5, 6], "power": {"gpu": [300, 305, 310], "cpu": [1000, 1005, 1010]}}
    check(ft.ab_choice(baseline, tie, ("gpu", "cpu"))[0] == "hint-share-85", "two variants tie on the row: the one with less power is kept")
    results.append("A/B rule and power rule: wins only beyond the larger range, a rise only beyond the baseline's range, chosen %s" % choice)


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
        # Android 16 lists `RequestedLayerState{<name>#<id> parentId=...}`: the bare name, as the older listings give it.
        game = "SurfaceView[dev.nebula.galaxy.test/dev.nebula.galaxy.GameActivity](BLAST)#7"
        listing16 = ("RequestedLayerState{Background for SurfaceView[dev.nebula.galaxy.test/dev.nebula.galaxy.GameActivity]#5 parentId=3 "
                     "relativeParentId=none isRelativeOf=false z=-1}\n"
                     "RequestedLayerState{SurfaceView[dev.nebula.galaxy.test/dev.nebula.galaxy.GameActivity]#6 parentId=3 z=-2}\n"
                     "RequestedLayerState{%s parentId=3 relativeParentId=none z=-2}\n"
                     "RequestedLayerState{StatusBar#9 parentId=1 z=0}\n" % game)
        check(ft.pick_layer(listing16, "dev.nebula.galaxy.test") == game,
              "an Android 16 listing: the bare BLAST layer name, no wrapper: %r" % ft.pick_layer(listing16, "dev.nebula.galaxy.test"))
        check(ft.pick_layer("RequestedLayerState{%s}\n" % game, "dev.nebula.galaxy.test") == game, "a wrapped line with no parentId: the name")
        check(ft.pick_layer(listing16.splitlines()[0] + "\n" + listing16.splitlines()[3], "dev.nebula.galaxy.test") == "",
              "an Android 16 listing with only the Background layer and others: no layer found, not a wrong name")
        check(ft.pick_layer("RequestedLayerState{SurfaceView[com.other/x](BLAST)#7 parentId=3 z=0}\n", "dev.nebula.galaxy.test") == "",
              "an Android 16 layer of another package: no layer found")
        check(ft.pick_layer(listing, "dev.nebula.galaxy.test") == ft.pick_layer(listing16, "dev.nebula.galaxy.test"),
              "the old and the new listing format give the same name")
        # The tablet's own listing (Android 16, G10.2m): a hash before the name, "@0", parentId and the rest of the line cut off.
        real = ("RequestedLayerState{Background for d4c3c SurfaceView[dev.nebula.galaxy.test/dev.nebula.galaxy.GameActivity]@0#158352 "
                "parentId=158350 relativeParentId=158346 z=-2147483648}\n"
                "RequestedLayerState{d4c3c SurfaceView[dev.nebula.galaxy.test/dev.nebula.galaxy.GameActivity]@0(BLAST)#158351 "
                "parentId=158350}\n"
                "RequestedLayerState{d4c3c SurfaceView[dev.nebula.galaxy.test/dev.nebula.galaxy.GameActivity]@0#158350 "
                "parentId=158349 relativeParentId=158346 z=-2}\n")
        check(ft.pick_layer(real, "dev.nebula.galaxy.test") == "d4c3c SurfaceView[dev.nebula.galaxy.test/dev.nebula.galaxy.GameActivity]@0(BLAST)#158351",
              "the tablet's own listing: the name --latency answers for: %r" % ft.pick_layer(real, "dev.nebula.galaxy.test"))
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
                if command.startswith("[ -d /proc/"):
                    return 1, ""
                if command.startswith("stat -c %s"):
                    return 0, "5\n"
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

        # Perfetto still writing when the shell user's `kill -0` says it is gone (G10.2m, Android 16): the trace grows for 6 reads and
        # its process lives for 9; the trace is pulled whole, never before the process has ended and the size has stood still.
        class Slow(Device):
            GROWS, LIVES, WHOLE = 6, 9, 6000

            def __init__(self, lives=None):
                super().__init__()
                self.looks, self.lives, self.pulled = 0, lives if lives is not None else self.LIVES, None

            def size(self):
                return self.WHOLE * min(self.looks, self.GROWS) // self.GROWS

            def shell(self, command, timeout=600):
                if command.startswith("stat -c %s"):
                    self.calls.append(command)
                    self.looks += 1
                    return 0, "%d\n" % self.size()
                if command.startswith("[ -d /proc/"):
                    self.calls.append(command)
                    return (0 if self.looks < self.lives else 1), ""
                return super().shell(command, timeout)

            def adb(self, *args, timeout=600, stdin=None):
                self.pulled = self.size()
                self.calls.append(" ".join(args))
                pathlib.Path(args[2]).write_bytes(b"x" * self.pulled)
                return type("R", (), {"returncode": 0, "stderr": b""})()

        def finished(device, name):
            folder = root / name
            folder.mkdir()
            cap = ft.Capture(device, folder, "dev.nebula.galaxy.test", 420, clock=lambda: 1.0)
            cap.started, cap.pid = True, "4242"
            cap.finish(sleep=lambda s: None)
            return json.loads((folder / "capture.json").read_text(encoding="utf-8"))
        slow = Slow()
        said = finished(slow, "cap-slow")
        check(slow.pulled == Slow.WHOLE and said["trace"] == "pulled" and not said["errors"] and slow.looks >= Slow.LIVES,
              "perfetto outliving the first check: the trace pulled whole (%s of %d bytes), after %d looks: %s" % (
                  slow.pulled, Slow.WHOLE, slow.looks, said))
        stuck = Slow(lives=10 ** 6)
        said = finished(stuck, "cap-stuck")
        check(any("still writing" in e for e in said["errors"]) and stuck.looks == ft.SETTLE_SECONDS * 2 + 1,
              "perfetto that never ends: the wait is bounded (%d looks) and the capture says so: %s" % (stuck.looks, said["errors"]))
        short = Slow()
        short.adb = lambda *args, **kw: (pathlib.Path(args[2]).write_bytes(b"x" * 5),
                                         type("R", (), {"returncode": 0, "stderr": b""})())[1]
        said = finished(short, "cap-short")
        check(any("pulled" in e and "bytes of" in e for e in said["errors"]),
              "a pulled file of another size than the device's: an error in capture.json: %s" % said["errors"])

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
        # The bound of a row is the median of the runs plus their range (G10.4m): the late counts 0, 2, 1 of 4800 give 1 + 2 = 3.
        check(ft.judge(synthetic(60, 4800, 3, 0, 0), back)[0] == "pass" and back["rates"]["60"]["bounds"]["late_share"] == 3 / 4800,
              "a late share at the median plus the range: pass")
        check(ft.judge(synthetic(60, 4800, 4, 0, 0), back)[0] == "FAIL", "a worse late share: refused")
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

        # The game's report of what the platform offers (G10.3), as the tablet writes it at 60 Hz.
        offers = ["[platform] hint-features: not supported (the system has no APerformanceHint_isFeatureSupported, API 36)",
                  "[platform] frame-timeline: 60.0 Hz: expected present less frame time 2.00 refreshes (2.00 to 2.00 over 32 "
                  "callbacks; preferred timeline 1 of 3)",
                  "[platform] game-mode: standard; sustained performance: not supported",
                  "[platform] thermal-thresholds: not supported (the call answered 38 with 0 thresholds)",
                  "[platform] vulkan-images: surface minimum 4, maximum 64; asked 5, made 5 (PRESENT_MODE_FIFO)"]

        def device_work(name, case, platform=offers):
            work = root / name
            folder = work / "runs" / "pace" / "pace-rate-60" / "vulkan"
            folder.parent.mkdir(parents=True)
            shutil.copytree(FIXTURES / case, folder)
            write_timeline(folder / "timeline.bin", STEPS)
            (folder / "run.log").write_text("\n".join(platform + [line, "extension: harness: frame 0 is game frame 900", line, timing,
                                                                  line, timing, line, timing]) + "\n", encoding="utf-8")
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
        # The switch of M-011, by rate and row since G10.4m: a row beyond its bound refuses only where pace.BASELINE_REFUSES names it. This
        # first block judges with every row refusing, the block after it with the shipped rows.
        every_row = tuple(ft.ROWS)
        pace.BASELINE_REFUSES = {"60": every_row, "120": every_row}
        for name, case, want in (("same", "right", True), ("late", "sleep", False), ("mode change", "mode", False),
                                 ("deep", "depth", False), ("unnamed", "unknown", False)):
            work = device_work("stored-" + name, case)
            code = pace.digest(work, None, reading)
            passed, detail, _t, kind = gate.pace_tablet_reading(work, code, stored=True)
            check(passed == want and (want or kind == "fail"),
                  "pace/tablet with a stored baseline, %s: %s (%s, %s)" % (name, "pass" if passed else "refused", kind, detail))
            results.append("pace/tablet against the stored baseline, %s: %s" % (name, detail))
        # G10.3's planted fault: the same right run with a line of what the platform offers missing is refused (kind fail) by the row
        # of what the platform offers, the baseline row passing.
        work = device_work("stored-noplatform", "right", [o for o in offers if "vulkan-images" not in o])
        code = pace.digest(work, None, reading)
        passed, detail, _t, kind = gate.pace_tablet_reading(work, code, stored=True)
        said = {m: v for m, _x, _t, v in json.loads((work / "pace.json").read_text(encoding="utf-8"))[0]["rows"]}
        check(not passed and kind == "fail" and said.get(pace.PLATFORM_ROW) == "FAIL" and said.get(pace.BASELINE_ROW) == "pass",
              "pace/tablet with a line of the platform's offer missing: refused (%s, %s)" % (kind, detail))
        results.append("pace/tablet, the platform's vulkan-images line missing: %s" % detail)
        # Misses that are FAIL rows of their own (the Unknown row) refuse nothing once a baseline is stored: only the row against it.
        unnamed = root / "stored-unnamed"
        said = json.loads((unnamed / "pace.json").read_text(encoding="utf-8"))[0]["rows"]
        check(any(m == "miss: Unknown" and v == "FAIL" for m, _x, _t, v in said) and any(m == pace.BASELINE_ROW and v == "FAIL" for m, _x, _t, v in said),
              "the unknown case: its Unknown row FAIL and the baseline row FAIL")
        pace.BASELINE_REFUSES = REFUSES_SHIPPED
        # As shipped (G10.4m): at 120 Hz the mean refreshes and the jumps refuse, at 60 Hz no row does (the clean runs spread wider than
        # the plant's effect there); a row beyond its bound at 60 Hz is said, as a report, and refuses nothing.
        check(set(REFUSES_SHIPPED) == {"60", "120"} and all(row in ft.ROWS for rows in REFUSES_SHIPPED.values() for row in rows)
              and REFUSES_SHIPPED["60"] == () and REFUSES_SHIPPED["120"] == ("refreshes_mean",)
              and pace.BASELINE_GROSS == {"120": {"jumps_per_minute": 284.0}, "60": {"jumps_per_minute": 56.0}},
              "the shipped rows are rows of the baseline, by rate: %s" % (REFUSES_SHIPPED,))
        pace.BASELINE_GROSS = {}  # these 12-frame cases make thousands of jumps a minute: the gross guard has its own cases below
        for name, case, says in (("same", "right", ""), ("late", "sleep", "a report: late_share"), ("mode change", "mode", "a report: late_share"),
                                 ("deep", "depth", "a report: three_refresh_share"), ("unnamed", "unknown", "a report: late_share")):
            work = device_work("report-" + name, case)
            code = pace.digest(work, None, reading)
            passed, detail, _t, kind = gate.pace_tablet_reading(work, code, stored=True)
            row = {m: (v, verdict) for m, v, _x, verdict in json.loads((work / "pace.json").read_text(encoding="utf-8"))[0]["rows"]}[pace.BASELINE_ROW]
            check(passed and says in row[0] and row[1] == ("pass" if name == "same" else "recorded"),
                  "pace/tablet with a stored baseline, as shipped (60 Hz: a report), %s: %s (%s, %s; row %s)" % (name, "pass" if passed else "refused", kind, detail, row))
            results.append("pace/tablet against the stored baseline as shipped at 60 Hz, %s: %s" % (name, row[0]))
        # At 120 Hz the same case is refused where its rows are among the shipped ones: the same run as the 120 Hz's, a jump beyond the bound.
        pace.BASELINE_REFUSES = {"60": (), "120": ("jumps_per_minute",)}
        hot = dict(json.loads((plain / "runs/pace/pace-rate-60/vulkan" / ft.REPORT).read_text(encoding="utf-8")), refresh_ms=8.3333)
        cold = ft.baseline([dict(hot, shown={"events_per_minute": 300.0, "jumps_per_minute": 90.0}),
                            dict(hot, shown={"events_per_minute": 330.0, "jumps_per_minute": 100.0}),
                            dict(hot, shown={"events_per_minute": 360.0, "jumps_per_minute": 110.0})])
        check(cold["rates"]["120"]["bounds"]["jumps_per_minute"] == 120.0, "the 120 Hz bound of the jumps: the median 100 plus the range 20")
        rows = pace.frametimeline_rows({"frametimeline": dict(hot, shown={"pairs": 9, "repeated": 1, "skipped": 0, "shifted": 0,
                                                                         "events_per_minute": 340.0, "jumps_per_minute": 190.0}),
                                        "present_timing": None}, cold, "base.json")
        said = {m: (v, verdict) for m, v, _t, verdict in rows}
        check(said[pace.BASELINE_ROW][1] == "FAIL" and "refusing: jumps_per_minute" in said[pace.BASELINE_ROW][0],
              "at 120 Hz a run with 190 jumps a minute against the bound 120 is refused by that row: %s" % (said[pace.BASELINE_ROW],))
        rows = pace.frametimeline_rows({"frametimeline": dict(hot, shown={"pairs": 9, "repeated": 1, "skipped": 0, "shifted": 0,
                                                                         "events_per_minute": 900.0, "jumps_per_minute": 110.0}),
                                        "present_timing": None}, cold, "base.json")
        said = {m: (v, verdict) for m, v, _t, verdict in rows}
        check(said[pace.BASELINE_ROW][1] == "recorded" and "a report: events_per_minute" in said[pace.BASELINE_ROW][0],
              "at 120 Hz the events beyond their bound are a report where the row is not shipped: %s" % (said[pace.BASELINE_ROW],))
        # As shipped the jumps a minute are a report until G10.5 and refuse only a gross break (twice the highest clean run on record, 2 x 142):
        # 190 a minute beyond the bound 120 is said; 300 refuses.
        pace.BASELINE_REFUSES = REFUSES_SHIPPED
        pace.BASELINE_GROSS = GROSS_SHIPPED
        for jumps, want, says in ((190.0, "recorded", "a report: jumps_per_minute"), (300.0, "FAIL", "refusing: jumps_per_minute")):
            rows = pace.frametimeline_rows({"frametimeline": dict(hot, shown={"pairs": 9, "repeated": 1, "skipped": 0, "shifted": 0,
                                                                             "events_per_minute": 340.0, "jumps_per_minute": jumps}),
                                            "present_timing": None}, cold, "base.json")
            said = {m: (v, verdict) for m, v, _t, verdict in rows}
            check(said[pace.BASELINE_ROW][1] == want and says in said[pace.BASELINE_ROW][0],
                  "as shipped at 120 Hz, %s jumps a minute: %s: %s" % (jumps, want, said[pace.BASELINE_ROW]))
        pace.BASELINE_GROSS = GROSS_SHIPPED
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

    shown_cases()
    ab_cases()

    for r in results:
        print("  " + r)
    if failures:
        print("frametimeline_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("frametimeline_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
