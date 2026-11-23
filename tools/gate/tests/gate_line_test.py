"""gate_line_test.py: the speed measure in a landing, the kind `worse` and `--also` by name (package B8; tools/gate/gate.py measure/step/pc,
measure/step/tablet, ALSO_PARTS, kinds.py, landing.py, scope.py), without the game, a build, a graphics card or a tablet.

    python -X utf8 -I tools/gate/tests/gate_line_test.py [--work FOLDER]

The gate's run_checks runs for real on a landing's plan. Every check but the measure is a fake that sleeps, records when it started and
ended and passes (or fails, when the case says); measure.py is a fake program that writes the result.json the case plants and exits as
planted, recording its own arguments and times. The folders under D:/NebulaWork are replaced by temporary ones.

Table B, each state of the measure (the tablet's check; the PC's check is the pair, Table P below):
  every_state_has_its_row          ok, better, no-baseline: pass, the figures named with value and median (no baseline yet said); worse: kind
                                   worse, the run fails, exit 1; not-trusted, tool, no result.json, an exit code and a state that disagree,
                                   the time limit: owed with the plain reason, one line in tool-faults.tsv; left-out: owed, no line
  a_row_fails_the_run_like_fail    the verdict and exit code of each: pass 0, worse fail 1, owed pass 0
  one_fault_line_for_a_check       two owed measures in one run: one line each, the check named as the test
Table P, the PC's check is the pair against the kept base (package B13, part B; a fake measure.py writes the result.json and the --pending table):
  pair_states                      every figure ok, better or accepted: pass; a figure worse: kind worse, the landing fails, the figures named;
                                   not calibrated: owed with the command that makes the bands; no base yet: owed, nothing started; the pair
                                   could not run on the candidate's side or on both, no result.json, an exit and a state that disagree, the time
                                   limit: owed and one line in tool-faults.tsv; the base's side could not and the candidate's can: owed, the
                                   base moves to the candidate unjudged, the line saying which renderer's side ended how; a base side that
                                   refused for a store version: owed, a TOOL line, the base stays (never moved for a store difference)
  pair_renderers                   d3d12 always; d3d11 and vulkan when the plan runs them
  base_follows_the_move            judged: the base becomes the candidate and the old folder goes; owed: it stays; rebase: it moves; a plan
                                   without the check leaves it
  anchor_part                      --also speed/anchor: all three renderers against the anchor; worse fails; not runnable: owed, replaced after
                                   the move with the last ratios in anchor-history.tsv; refused for a store version: owed, the anchor stays
Where the checks sit (B14 part 3: the PC's last checks do not wait for the tablet):
  pc_measure_starts_beside_the_tablets_lane   tour/tablet-pc, then the pair, one at a time, start when the PC's own lanes and the Android build
                                   are over and the tablet's lane is not; never beside a build or a PC game check; the parts --also asked for keep
                                   the pair last
  the_screen_size                  the tablet's size is read at the start of its lane (wm size: landscape), tour/tablet-pc plays at it; the tablet's
                                   tour reporting another size is kind tool with both sizes in one line, replays/tablet not compared; a size not
                                   known at the start is played after the tablet's tour; a tablet that is away owes the PC's half
  pc_measure_skipped_on_a_failure  a check that failed before it: no row and no measure; the parts --also asked for are not run either
  tablet_measure_in_its_lane       it starts after build/android passed and ends before tablet/prepare starts; a plan without the tablet's
                                   part has it too; a failed build/android gives none
  tablet_owed                      no --tablet, or a tablet that does not answer: owed, nothing started
  the_plan_decides                 an engine or tour group gets measure_pc, with the Android build measure_tablet; a plan without either, and
                                   the other levels, have neither check
  the_commands                     measure.py's arguments as the brief spells them, with no gate lock option
The history:
  keep_only_after_a_move           main moved: `measure.py keep` for each pending file that exists; not moved: never; a keep that fails leaves
                                   the landing a pass with one fault line and one printed line
The kind worse:
  worse_in_kinds                   ordered after fail, fails the run, never run again alone, never a flake
  worse_signature                  the same figures worse three times (other values) stop the branch with exit 3, other figures do not
--also:
  also_names_are_planned           each name of the table is planned (its checks selected); soak asks for 30 minutes, soak/long the long run's 120
  also_after_the_checks            they run after the needed checks and before the measure, one after another
  also_stress_is_a_fail            asked for by name, the stress lane is told its result is a fail; the nightly's is not
  also_refusals                    a name of the table whose part is not built is refused (exit 2, with the words), soak with soak/long, an unknown name, as before
Exit 0 when every case holds.
"""
import contextlib
import io
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import unittest.mock

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parents[1] / "plan"))
sys.path.insert(0, str(HERE))
import failures  # noqa: E402
import gate  # noqa: E402
import gate_github_test as gh  # noqa: E402
import kinds  # noqa: E402
import landing  # noqa: E402
import landing_test as lt  # noqa: E402
import scope  # noqa: E402
import speedbase  # noqa: E402

# The speed measure's tests need a plan that has the game's groups at the landing level, the Android build and no tablet part: the table
# of the real tree with `native/**` as it was before the group `pictures` (which takes every picture of the main level and the tablet).
TABLE = scope.parse(scope.TABLE.read_text(encoding="utf-8").replace(
    "native/**\trepo,build,unit,start,pictures,android,github", "native/**\trepo,build,unit,start,engine1,tour1,android,github"))

FAKE_MEASURE = '''
import json, os, pathlib, sys, time
args = sys.argv[1:]
plan = json.loads(pathlib.Path(os.environ["FAKE_MEASURE_PLAN"]).read_text(encoding="utf-8"))
record = pathlib.Path(os.environ["FAKE_MEASURE_RECORD"])


def note(entry, times=1):
    # one file for each process: appends of processes that run at once to one file interleave on Windows (T11-B)
    with open(str(record) + "." + str(os.getpid()), "a", encoding="utf-8") as f:
        for _ in range(times):
            f.write(json.dumps(entry) + "\\n")


opts = {a: args[i + 1] for i, a in enumerate(args) if a.startswith("--") and i + 1 < len(args)}
if args[0] == "keep":
    while plan.get("start_when") and not pathlib.Path(plan["start_when"]).exists():
        time.sleep(0.001)
    note({"keep": args[1], "history": opts.get("--history"), "time": time.time()}, plan.get("record_times", 1))
    print(plan.get("keep_says", "measure: kept 1 line(s)"))
    sys.exit(plan.get("keep_exit", 0))
if args[0] in ("pair", "pair-probe"):
    kind = args[0]
    renderers = opts["--runs"].split(",") if kind == "pair" else [opts["--renderer"]]
    p = plan.get(kind, {})
    start = time.time()
    time.sleep(p.get("seconds", 0.2))
    code = 0
    if kind == "pair-probe":
        if p.get("ok", True):
            print("  steps_over_18  3")
            print("  render_ms      1.5")
        else:
            print("measure.py pair-probe: the run ended with exit 1: nothing")
            code = 2
    else:
        states = {r: p.get("states", {}).get(r, "ok") for r in renderers}
        reasons = {r: p.get("reasons", {}).get(r, "the cand side ended with exit 1") for r in renderers}
        rows, figures = [], {}
        for r in renderers:
            figures[r] = {}
            if states[r] == "tool":
                rows.append([r, "-", "tool", "measure/step/pc %s tool: %s" % (r, reasons[r])])
                continue
            for fig in p.get("figures", ["cpu_work_ms", "render_ms"]):
                state = p.get("figure_states", {}).get("%s.%s" % (r, fig), states[r])
                rows.append([r, fig, state, "measure/step/pc %s %s %s: base 1.000, candidate 1.100 (+10.0%%)" % (r, fig, state)])
                figures[r][fig] = {"value": 1.1, "median": 1.0, "band": 0.05, "judged": True, "verdict": state, "anchor": None,
                                   "ratios": [1.1, 1.09, 1.11] if state == "worse" else [1.01]}
        code = 2 if "tool" in states.values() else 1 if "worse" in states.values() else 0
        code = p.get("exit", code)
        if opts.get("--pending"):
            lines = ["check\\trenderer\\tfigure\\tstate\\tline"] + ["\\t".join([opts.get("--name", "measure/step/pc")] + row) for row in rows]
            pathlib.Path(opts["--pending"]).write_text("\\n".join(lines) + "\\n", encoding="utf-8")
        if not p.get("no_result"):
            work = pathlib.Path(opts["--work"])
            work.mkdir(parents=True, exist_ok=True)
            body = {"commit": "fake", "exit": p.get("result_exit", code), "verdict": "x",
                    "runs": {r: {"state": states[r], "reason": reasons[r] if states[r] == "tool" else "", "figures": figures[r]}
                             for r in renderers}}
            (work / "result.json").write_text(json.dumps(body), encoding="utf-8")
    note({"run": kind, "args": args, "start": start, "end": time.time()})
    sys.exit(code)
run = opts["--runs"]
p = plan[run]
start = time.time()
time.sleep(p.get("seconds", 0.2))
if opts.get("--pending"):
    pathlib.Path(opts["--pending"]).write_text("commit\\ttime\\nfake\\tnow\\n", encoding="utf-8")
if not p.get("no_result"):
    work = pathlib.Path(opts["--work"])
    work.mkdir(parents=True, exist_ok=True)
    body = {"commit": "fake", "exit": p.get("result_exit", p["exit"]), "verdict": "x",
            "runs": {run: {"state": p["state"], "reason": p.get("reason", ""), "figures": p.get("figures", {})}}}
    (work / "result.json").write_text(json.dumps(body), encoding="utf-8")
note({"run": run, "args": args, "start": start, "end": time.time()})
sys.exit(p["exit"])
'''


def figure(value, median, band=0.2, verdict="holds", judged=True, says=""):
    return {"value": value, "median": median, "band": band, "judged": judged, "verdict": verdict,
            "anchor": {"verdict": "slower", "says": says} if says else None}


HOLDS = {"step_ms": figure(3.1, 3.0), "render_ms": figure(1.5, 1.4), "gap_max_ms": figure(9.0, 8.0, 1.0, "not judged", False)}
FRESH = {"step_ms": figure(3.1, None, None, "no baseline yet"), "render_ms": figure(1.5, None, None, "no baseline yet")}
WORSE = {"step_ms": figure(4.5, 3.0, 0.2, "worse"), "render_ms": figure(2.5, 1.4, 0.1, "worse"), "memory_mb": figure(400, 399, 32)}


class Gates:
    """The world a landing's run_checks runs in: every folder temporary, every check a recording fake, measure.py a fake program."""

    def __init__(self, test):
        self.test = test
        self.t = pathlib.Path(tempfile.mkdtemp())
        test.addCleanup(lambda: shutil.rmtree(self.t, ignore_errors=True))
        self.work, self.live, self.build = self.t / "work", self.t / "live", self.t / "build"
        self.times, self.fail, self.slow = [], set(), {}
        self.faults, self.soaks, self.stress_fails = [], [], []
        self.answers = True
        (self.build / "android").mkdir(parents=True)
        (self.build / "android" / "Nebula.apk").write_bytes(b"apk")
        self.script = self.t / "fake_measure.py"
        self.script.write_text(FAKE_MEASURE, encoding="utf-8")
        self.plan_file, self.record = self.t / "plan.json", self.t / "record.jsonl"
        self.measure({})
        self.speed = self.t / "speed-base"
        self.bins = self.t / "bins"
        (self.bins / "D3D12").mkdir(parents=True)
        (self.bins / "Nebula.exe").write_bytes(b"exe")
        self.pak = self.t / "game.pak"
        self.pak.write_bytes(b"pak")
        for kind, sha in (("base", "b" * 40), ("anchor", "a" * 40)):
            speedbase.make(kind, self.bins, sha, self.pak, self.speed, registry=self.t / "registry.tsv")

    def measure(self, plan):
        self.plan_file.write_text(json.dumps(plan), encoding="utf-8")

    def calls(self):
        files = sorted(self.record.parent.glob(self.record.name + ".*"))
        rows = [json.loads(l) for f in files for l in f.read_text(encoding="utf-8").splitlines() if l.strip()]
        return sorted(rows, key=lambda c: c.get("end", c.get("time", 0)))

    def runs(self):
        return [c for c in self.calls() if "run" in c]

    def span(self, name):
        return next(((a, b) for n, a, b in self.times if n == name), None)

    # --- the patched gate --------------------------------------------------------------------------------------------

    def __enter__(self):
        names = ("MEASURE", "FLAKES", "SET_ASIDE", "BASE_BINS", "MACHINE_JSON", "PICTURE_STORE", "machine_ready", "tablet_part", "fault_writer",
                 "android_module", "start_soak", "harness", "measure_limit", "pair_limit", "SPEED_ROOT")
        self.saved = {n: getattr(gate, n) for n in names}
        self.saved_check, self.saved_ensure = gate.Gate.check, gate.machine.ensure_adb_server
        self.env = {k: os.environ.get(k) for k in ("FAKE_MEASURE_PLAN", "FAKE_MEASURE_RECORD")}
        os.environ["FAKE_MEASURE_PLAN"], os.environ["FAKE_MEASURE_RECORD"] = str(self.plan_file), str(self.record)
        gate.MEASURE = self.script
        gate.SPEED_ROOT = self.speed
        gate.FLAKES, gate.SET_ASIDE = self.t / "flakes.tsv", self.t / "set-aside.tsv"
        gate.BASE_BINS, gate.MACHINE_JSON, gate.PICTURE_STORE = self.t / "base-bins", self.t / "machine.json", self.t / "pictures"
        gate.machine_ready = lambda *a, **k: True
        gate.fault_writer = lambda live: (lambda *fault: self.faults.append(fault))
        gate.machine.ensure_adb_server = lambda *a, **k: None
        rig = self

        class Device:
            adb_path = "adb"

            def __init__(self, serial):
                self.serial = serial

            def answers(self):
                return rig.answers

            def shell(self, command, timeout=600):
                return 0, "Physical size: 1752x2800" + chr(10)

        gate.android_module = lambda: type("Module", (), {"Device": Device})
        original = gate.Gate.check

        def check(g, name, fn, needs=()):
            if name.startswith(("measure/step", "speed/")):
                return original(g, name, fn, needs)

            def fake(log):
                begin = time.time()
                time.sleep(self.slow.get(name, 0.02))
                self.times.append((name, begin, time.time()))
                return name not in self.fail, "a fake: %s" % ("failed" if name in self.fail else "passed")
            return original(g, name, fake, needs)

        def tablet_part(g, stops, device=None, pc_tail=True):
            # What the real part registers (its needs) and, with the tablet away, owes; the PC's half is the tail's (tour/tablet-pc).
            for n in ("tour/tablet-pc", "replays/tablet"):
                g.wanted(n, ("build/windows",))
            if g.android_built and not rig.answers:
                for n in ("tablet/prepare", "android/tablet", "tour/tablet", "tablet/remove", "tour/tablet-pc", "replays/tablet"):
                    g.owe(n, "the tablet does not answer adb")
            elif g.android_built:
                for n in ("tablet/prepare", "android/tablet", "tour/tablet", "tablet/remove"):
                    g.check(n, None)
        gate.Gate.check, gate.tablet_part = check, tablet_part

        def start_soak(g, minutes=None):
            self.soaks.append(minutes)
            log = g.work / "soak-run.log"
            log.write_text("soak: fake\n", encoding="utf-8")
            begin = time.time()
            time.sleep(0.05)
            self.times.append(("soak-process", begin, time.time()))
            return type("Process", (), {"wait": lambda s: 0})(), log
        gate.start_soak = start_soak
        real_harness = self.saved["harness"]

        def harness(g, suite, renderer, label, *more, **kw):
            if kw.get("stress_lane"):
                self.stress_fails.append(kw.get("stress_fails"))
            return real_harness(g, suite, renderer, label, *more, **kw)
        gate.harness = harness
        return self

    def __exit__(self, *exc):
        for n, v in self.saved.items():
            setattr(gate, n, v)
        gate.Gate.check, gate.machine.ensure_adb_server = self.saved_check, self.saved_ensure
        for k, v in self.env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        gate.STOPPED.clear()
        gate.STOPPED_WHY[0] = ""

    # --- a landing's run ----------------------------------------------------------------------------------------------

    def argv(self, tablet="T", also=(), extra=()):
        argv = ["--main", "--move", "--scope", "x", "--work", str(self.work), "--live", str(self.live), "--board", "", "--status", "",
                "--build-root", str(self.build), "--paks", "", "--pak", "X", "--discs", str(self.t / "discs"), "--wait", "0",
                "--gate-lock", str(self.t / "gate.lock"), "--tablet-lock", str(self.t / "tablet.lock"), "--tablet-wait", "7"]
        if tablet:
            argv += ["--tablet", tablet]
        if also:
            argv += ["--also", ",".join(also)]
        return argv + list(extra)

    def plan(self, *paths):
        return scope.plan_for(list(paths), TABLE)

    def land(self, plan, also=(), tablet="T"):
        """The landing's selection and run, as main() does them: (the Gate, the args)."""
        args = gate.parser().parse_args(self.argv(tablet, also))
        args.mode = gate.landing_level(plan, also)
        selected = gate.landing_selection(args, plan, list(also), {}, {})
        with contextlib.redirect_stdout(io.StringIO()) as out:
            g = gate.run_checks(args, {}, {}, selected)
        g.printed = out.getvalue()
        return g, args

    def row(self, g, name):
        return next((r for r in g.results if r["check"] == name), None)


NATIVE = ("native/petari/src/Game/Foo.cpp",)          # repo, build, unit, start, engine1, tour1, android, github (+ the measures)
WITH_TABLET = NATIVE + ("platform/android/app/build.gradle",)
ENGINE_ONLY = ("tests/engine/x/test.json",)
PC, TABLET = gate.MEASURE_PC_CHECK, gate.MEASURE_TABLET_CHECK


class Table(unittest.TestCase):
    """Table B: what each state of the measure means."""

    def go(self, plan, *paths):
        with Gates(self) as w:
            self.w = w
            w.measure({**{"d3d12": {"exit": 0, "state": "ok", "figures": HOLDS}, "tablet": {"exit": 0, "state": "ok", "figures": HOLDS}},
                       **plan})
            g, _ = w.land(w.plan(*paths))
            return g, w

    CASES = [
        # state, exit, the figures, the row's state, a text the row's detail has, a line in tool-faults.tsv (its kind) or None,
        # the verdict and the exit code of the run
        ("ok", 0, HOLDS, "pass", "holds: step_ms 3.1 (median 3); render_ms 1.5 (median 1.4)", None, "pass", 0),
        ("better", 0, HOLDS, "pass", "better: step_ms 3.1 (median 3)", None, "pass", 0),
        ("no-baseline", 0, FRESH, "pass", "no baseline yet: step_ms 3.1 (no baseline yet); render_ms 1.5 (no baseline yet)", None, "pass", 0),
        ("worse", 1, WORSE, "worse", "worse: render_ms+step_ms: render_ms 2.5 (median 1.4, band 0.1); step_ms 4.5 (median 3, band 0.2)",
         None, "fail", 1),
        ("not-trusted", 3, {}, "owed", "the measure was not trusted: the machine was not quiet: a compiler ran", "ENV", "pass", 0),
        ("tool", 2, {}, "owed", "the measure could not run: the harness exit 1", "TOOL", "pass", 0),
        ("left-out", 0, {}, "owed", "the tablet was left out: no --device", None, "pass", 0),
    ]

    def one(self, check, run, case):
        state, code, figures, row_state, text, fault, verdict, exit_code = case
        if check == PC:
            return                                        # the PC's check is the pair (class Pair)
        reason = {"not-trusted": "the machine was not quiet: a compiler ran", "tool": "the harness exit 1",
                  "left-out": "no --device"}.get(state, "")
        g, w = self.go({run: {"exit": code, "state": state, "reason": reason, "figures": figures}}, *NATIVE)
        row = w.row(g, check)
        label = "%s %s" % (check, state)
        self.assertIsNotNone(row, label + ": " + g.printed)
        self.assertEqual(row["state"], row_state, label + ": " + row["detail"])
        self.assertIn(text, row["detail"], label)
        self.assertEqual((g.verdict, g.code), (verdict, exit_code), label)
        self.assertEqual([(f[2], f[3], f[4]) for f in w.faults], [(fault, check, check)] if fault else [], label)
        if fault:
            self.assertIn(row["detail"].split(": ", 1)[1][:30], w.faults[0][5], label + ": the reason is the line's text")
        return g, row

    def test_every_state_has_its_row(self):
        for case in self.CASES:
            self.one(TABLET, "tablet", case)

    def test_worse_row_is_a_kind_line_and_fails_the_run(self):
        g, row = self.one(TABLET, "tablet", self.CASES[3])
        lines = [l for l in g.lines if l.startswith("KIND worse")]
        self.assertEqual(len(lines), 1, g.lines)
        parsed = kinds.parse_kind_line(lines[0])
        self.assertEqual(parsed[:3], ("worse", TABLET, "-"))
        self.assertEqual(kinds.worse_names(parsed[3]), "render_ms+step_ms")
        self.assertEqual([r["kind"] for r in row["records"]], ["worse"])
        self.assertEqual(landing.failures_of_lines(g.lines), [(TABLET, "worse", parsed[3])])

    def test_the_states_that_disagree_are_owed(self):
        # An exit code that is not the state's, a state measure.py does not have, no result.json, a result.json that is not one.
        cases = {"state ok, exit 1": {"exit": 1, "state": "ok", "figures": HOLDS},
                 "state worse, exit 0": {"exit": 0, "state": "worse", "figures": WORSE},
                 "result.json says exit 3, the program exited 0": {"exit": 0, "result_exit": 3, "state": "ok", "figures": HOLDS},
                 "an unknown state": {"exit": 0, "state": "great", "figures": HOLDS},
                 "no result.json": {"exit": 0, "state": "ok", "no_result": True},
                 "a worse state with no worse figure": {"exit": 1, "state": "worse", "figures": HOLDS}}
        for why, plan in cases.items():
            g, w = self.go({"tablet": plan}, *NATIVE)
            row = w.row(g, TABLET)
            self.assertEqual(row["state"], "owed", why)
            self.assertTrue(row["detail"].startswith("the measure could not run: "), "%s: %s" % (why, row["detail"]))
            self.assertEqual((g.verdict, len(w.faults)), ("pass", 1), why)

    def test_the_time_limit_is_owed(self):
        with Gates(self) as w:
            w.measure({"pair": {"seconds": 30}})
            gate.pair_limit = lambda renderers: 1
            g, _ = w.land(w.plan(*ENGINE_ONLY))
            row = w.row(g, PC)
            self.assertEqual((row["state"], row["detail"]), ("owed", "the measure could not run: its time limit"))
            self.assertEqual(len(w.faults), 1)
            self.assertEqual(g.verdict, "pass")

    def test_the_limits_follow_the_long_levels_rule(self):
        # The tablet: a route of three minutes measured at most twice with two minutes for each start and end, its lock's wait and five
        # minutes for the install and removal. The pair: three rounds of every renderer asked for and one repeat, a round at 150 s, and 120 s.
        self.assertEqual(gate.measure_limit(30), (10 + 30 + 5) * 60)
        self.assertEqual(gate.pair_limit(["d3d12"]), 120 + 4 * 150)
        self.assertEqual(gate.pair_limit(["d3d12", "d3d11", "vulkan"]), 120 + 10 * 150)

    def test_one_fault_line_for_a_check(self):
        with Gates(self) as w:
            g = gate.Gate(gate.parser().parse_args(w.argv()))
            gate.measure_fault(g, PC, "TOOL", "first")
            gate.measure_fault(g, PC, "TOOL", "second")
            gate.measure_fault(g, TABLET, "ENV", "third")
            self.assertEqual([(f[3], f[4], f[5]) for f in w.faults], [(PC, PC, "first"), (TABLET, TABLET, "third")])


class Pair(unittest.TestCase):
    """Table P: the PC's check is the pair against the kept base; the base and the anchor follow the move."""

    def go(self, pair=None, probe=None, plan=ENGINE_ONLY, also=(), setup=None, then=None):
        """A landing's checks on `plan` with the planted pair; `then(g, w)` runs inside the world (its folders, its fault writer), where
        what the move does with the kept builds is tried: the real speed-base folder is never reached."""
        with Gates(self) as w:
            self.w = w
            w.measure({"pair": pair or {}, **({"pair-probe": probe} if probe else {}), "tablet": {"exit": 0, "state": "ok", "figures": HOLDS}})
            if setup:
                setup(w)
            g, args = w.land(w.plan(*plan), also)
            if then:
                then(g, w)
            return g, w

    def test_pass(self):
        g, w = self.go()
        row = w.row(g, PC)
        self.assertEqual(row["state"], "pass", row["detail"])
        self.assertIn("holds against the base " + "b" * 9, row["detail"])
        self.assertEqual(g.speed, {PC: "judged"})
        self.assertEqual((g.verdict, g.code, w.faults), ("pass", 0, []))
        for state in ("better", "accepted"):
            g, w = self.go({"states": {"d3d12": state}})
            self.assertEqual((w.row(g, PC)["state"], g.speed), ("pass", {PC: "judged"}), state)

    def test_worse_fails_the_landing_with_the_figures_named(self):
        g, w = self.go({"states": {"d3d12": "worse"}, "figure_states": {"d3d12.render_ms": "ok"}})
        row = w.row(g, PC)
        self.assertEqual(row["state"], "worse", row["detail"])
        self.assertTrue(row["detail"].startswith("worse: d3d12.cpu_work_ms: measure/step/pc d3d12 cpu_work_ms worse: base 1.000"), row["detail"])
        self.assertEqual((g.verdict, g.code), ("fail", 1))
        parsed = kinds.parse_kind_line([l for l in g.lines if l.startswith("KIND worse")][0])
        self.assertEqual((parsed[:3], kinds.worse_names(parsed[3])), (("worse", PC, "-"), "d3d12.cpu_work_ms"))
        self.assertEqual(g.speed[PC], "owed")

    def test_not_calibrated_is_owed_with_the_command_that_makes_the_bands(self):
        g, w = self.go({"states": {"d3d12": "no-baseline"}})
        row = w.row(g, PC)
        self.assertEqual(row["state"], "owed", row["detail"])
        self.assertIn("not calibrated on this PC: d3d12", row["detail"])
        self.assertIn("measure.py pair-calibrate --bin %s --runs d3d12" % g.bin, row["detail"])
        self.assertEqual((g.verdict, w.faults, g.speed), ("pass", [], {PC: "owed"}), "owed, no fault line: it is no fault of a tool")

    def test_no_base_yet_is_owed_and_nothing_starts(self):
        g, w = self.go(setup=lambda w: (w.speed / "current.txt").unlink())
        row = w.row(g, PC)
        self.assertEqual(row["state"], "owed")
        self.assertIn("there is no base yet", row["detail"])
        self.assertIn("gate.py speed-base make --bin", row["detail"])
        self.assertEqual([c["run"] for c in w.runs()], [])
        self.assertEqual(g.speed, {PC: "owed"})

    def test_a_broken_base_is_owed_and_said(self):
        g, w = self.go(setup=lambda w: shutil.rmtree(next(p for p in w.speed.iterdir() if p.is_dir() and p.name.startswith("base-"))))
        row = w.row(g, PC)
        self.assertEqual(row["state"], "owed")
        self.assertIn("the base cannot be read", row["detail"])

    def test_the_candidates_side_that_cannot_run_is_a_tool_fault(self):
        g, w = self.go({"states": {"d3d12": "tool"}, "reasons": {"d3d12": "the cand side ended with exit 1: boom"}})
        row = w.row(g, PC)
        self.assertEqual(row["state"], "owed", row["detail"])
        self.assertIn("the measure could not run: d3d12: the cand side ended with exit 1: boom", row["detail"])
        self.assertEqual([(f[2], f[3]) for f in w.faults], [("TOOL", PC)])
        self.assertEqual([c["run"] for c in w.runs()], ["pair"], "no probe: the candidate is the one at fault")
        self.assertEqual(g.speed, {PC: "owed"})

    def test_both_sides_that_cannot_run_is_a_tool_fault(self):
        g, w = self.go({"states": {"d3d12": "tool"}, "reasons": {"d3d12": "the base side ended with exit 1"}}, probe={"ok": False})
        row = w.row(g, PC)
        self.assertEqual(row["state"], "owed", row["detail"])
        self.assertIn("the measure could not run", row["detail"])
        self.assertEqual([(f[2], f[3]) for f in w.faults], [("TOOL", PC)])
        self.assertIn("probe", [c["run"].replace("pair-", "") for c in w.runs()], "the candidate was asked for alone")
        self.assertEqual(g.speed, {PC: "owed"})

    def test_the_base_that_cannot_run_the_route_moves_to_the_candidate_unjudged(self):
        g, w = self.go({"states": {"d3d12": "tool"}, "reasons": {"d3d12": "the base side ended with exit 1: old route"}}, probe={"ok": True})
        row = w.row(g, PC)
        self.assertEqual(row["state"], "owed", row["detail"])
        self.assertEqual(row["detail"], "the base %s cannot run this route any more (d3d12: the base side ended with exit 1: old route): "
                                        "the base moves to %s unjudged" % ("b" * 9, g.commit[:9]))
        self.assertEqual((g.verdict, w.faults, g.speed), ("pass", [], {PC: "rebase"}))
        probe = [c for c in w.calls() if c.get("run") == "pair-probe"]
        self.assertEqual(len(probe), 1)
        self.assertEqual(probe[0]["args"][probe[0]["args"].index("--renderer") + 1], "d3d12")
        self.assertEqual(probe[0]["args"][probe[0]["args"].index("--bin") + 1], g.bin, "the candidate's build alone")

    def test_a_base_side_that_refuses_for_a_store_version_is_never_moved_unjudged(self):
        # The runner's words for data of another store version: the side is run with its build's own data, so this is no start that
        # failed; the kept build stays, the candidate is not asked alone, and the tool's fault is written.
        why = "the base side ended with exit 1: measure-pair d3d12 harness-error the game x.pak is of store version 37; this tree's is 39"
        g, w = self.go({"states": {"d3d12": "tool"}, "reasons": {"d3d12": why}}, probe={"ok": True})
        row = w.row(g, PC)
        self.assertEqual(row["state"], "owed", row["detail"])
        self.assertIn("the measure could not run", row["detail"])
        self.assertIn("of store version 37", row["detail"])
        self.assertEqual((g.verdict, [(f[2], f[3]) for f in w.faults], g.speed), ("pass", [("TOOL", PC)], {PC: "owed"}))
        self.assertEqual([c["run"] for c in w.runs()], ["pair"], "the candidate was not asked alone")
        said = []
        g, w = self.go({"states": {"d3d12": "tool"}, "reasons": {"d3d12": why}}, probe={"ok": True}, then=lambda g, w: said.append(self.keep(g, w)))
        self.assertEqual(speedbase.read("base", w.speed).commit, "b" * 40, "the base stays")
        self.assertIn("speed base stays bbbbbbbbb", said[0])

    def test_a_pair_that_wrote_no_result_is_owed(self):
        g, w = self.go({"no_result": True})
        row = w.row(g, PC)
        self.assertEqual(row["state"], "owed")
        self.assertIn("no readable result.json for d3d12", row["detail"])
        self.assertEqual([(f[2], f[3]) for f in w.faults], [("TOOL", PC)])

    def test_the_renderers_follow_the_plan(self):
        class G:
            def __init__(self, selected):
                self.selected = selected
        every = ["d3d12", "d3d11", "vulkan"]
        self.assertEqual(gate.pair_renderers(G(None)), every, "a full run")
        self.assertEqual(gate.pair_renderers(G(set())), ["d3d12"], "d3d12 always")
        self.assertEqual(gate.pair_renderers(G({"tour/d3d11", "engine/d3d12"})), ["d3d12", "d3d11"])
        self.assertEqual(gate.pair_renderers(G({"engine/vulkan"})), ["d3d12", "vulkan"])
        self.assertEqual(gate.pair_renderers(G({"start/vulkan", "pictures/tour/d3d11"})), ["d3d12"], "a start or a picture is no play")
        # engine1 and tour1 run d3d12 only; engine3 and the group pictures the three
        for paths, want in ((NATIVE, "d3d12"), (ENGINE_ONLY, "d3d12,d3d11,vulkan"), (("tests/replays/x.tsv",), "d3d12,d3d11,vulkan")):
            g, w = self.go(plan=paths)
            runs = [c for c in w.runs() if c["run"] == "pair"][0]["args"]
            self.assertEqual(runs[runs.index("--runs") + 1], want, paths)

    # --- what the move does with the kept builds

    def keep(self, g, w, pak=None, landed="c" * 40):
        """gate.keep_speed for the run's Gate with the candidate's build in w.bins: what it printed."""
        g.args.pak = str(w.pak if pak is None else pak)
        g.bin = str(w.bins)
        (w.bins / "marker.txt").write_text("the candidate's", encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()) as out:
            gate.keep_speed(g, landed, g.args)
        return out.getvalue()

    def moved(self, pair=None, probe=None, plan=ENGINE_ONLY, also=()):
        said = []
        g, w = self.go(pair, probe, plan, also, then=lambda g, w: said.append(self.keep(g, w)))
        return g, w, said[0]

    def history(self, w, name, columns):
        return speedbase.read_rows(w.speed / name, columns)

    def test_a_judged_pair_makes_the_candidate_the_base(self):
        g, w, said = self.moved()
        base = speedbase.read("base", w.speed)
        self.assertEqual(base.commit, "c" * 40)
        self.assertTrue((base.bins / "marker.txt").is_file(), "the candidate's files")
        self.assertEqual([p.name for p in w.speed.iterdir() if p.is_dir() and p.name.startswith("base-")], [base.folder.name], "the old base's folder is removed")
        rows = self.history(w, speedbase.BASE_HISTORY, speedbase.BASE_COLUMNS)
        self.assertEqual([(r["event"], r["commit"], r["previous"]) for r in rows], [("moved", "c" * 9, "b" * 9)])
        self.assertIn("speed base moved to ccccccccc (was bbbbbbbbb)", said)
        self.assertEqual(speedbase.read("anchor", w.speed).commit, "a" * 40, "a landing that judged never replaces the anchor")

    def test_a_pair_that_did_not_judge_leaves_the_base(self):
        for pair in ({"states": {"d3d12": "no-baseline"}}, {"states": {"d3d12": "tool"}, "reasons": {"d3d12": "the cand side ended with exit 1"}}):
            g, w, said = self.moved(pair)
            self.assertEqual(speedbase.read("base", w.speed).commit, "b" * 40, pair)
            rows = self.history(w, speedbase.BASE_HISTORY, speedbase.BASE_COLUMNS)
            self.assertEqual([(r["event"], r["commit"]) for r in rows], [("kept", "b" * 9)], "the next pair covers both changes")
            self.assertIn("speed base stays bbbbbbbbb", said)

    def test_a_base_that_could_not_run_is_replaced_and_the_history_says_rebased(self):
        g, w, said = self.moved({"states": {"d3d12": "tool"}, "reasons": {"d3d12": "the base side ended with exit 1"}}, probe={"ok": True})
        self.assertEqual(speedbase.read("base", w.speed).commit, "c" * 40)
        rows = self.history(w, speedbase.BASE_HISTORY, speedbase.BASE_COLUMNS)
        self.assertEqual([(r["event"], r["commit"], r["previous"]) for r in rows], [("rebased", "c" * 9, "b" * 9)])
        self.assertIn("rebased to ccccccccc, unjudged", said)

    def test_a_plan_without_the_check_does_not_touch_the_base(self):
        g, w, said = self.moved(plan=("docs/plan/x.md",))
        self.assertEqual(g.speed, {})
        self.assertEqual(speedbase.read("base", w.speed).commit, "b" * 40)
        self.assertFalse((w.speed / speedbase.BASE_HISTORY).exists(), "no row")
        self.assertEqual(said, "")

    def test_a_failed_keep_is_a_line_and_a_fault_not_a_failure(self):
        said = []
        g, w = self.go(then=lambda g, w: said.append(self.keep(g, w, pak=w.t / "no-such.pak")))
        self.assertIn("the speed base was not kept up (KeptError", said[0])
        self.assertEqual([(f[2], f[3]) for f in w.faults], [("TOOL", "speed/keep")])
        self.assertEqual(speedbase.read("base", w.speed).commit, "b" * 40)

    # --- the anchor

    def test_the_anchor_part_runs_the_three_renderers_against_the_anchor(self):
        g, w = self.go(also=["speed/anchor"])
        row = w.row(g, "speed/anchor")
        self.assertEqual(row["state"], "pass", row["detail"])
        self.assertIn("holds against the anchor " + "a" * 9, row["detail"])
        calls = [c["args"] for c in w.runs() if c["run"] == "pair"]
        self.assertEqual(len(calls), 2, "the landing's pair and the anchor's")
        mine = [a for a in calls if a[a.index("--name") + 1] == "speed/anchor"][0]
        anchor = speedbase.read("anchor", w.speed)
        self.assertEqual((mine[mine.index("--runs") + 1], mine[mine.index("--base") + 1], mine[mine.index("--base-commit") + 1]),
                         ("d3d12,d3d11,vulkan", str(anchor.bins), "a" * 40))
        self.assertEqual(mine[mine.index("--work") + 1], str(g.work / "speed-anchor"))
        order = [r["check"] for r in g.results]
        self.assertLess(order.index("speed/anchor"), order.index(PC), "before the landing's own pair")
        self.assertEqual(g.speed, {"speed/anchor": "judged", PC: "judged"})

    def test_a_worse_anchor_fails_with_kind_worse(self):
        with Gates(self) as w:
            w.measure({"pair": {"states": {"vulkan": "worse"}}})
            g, _ = w.land(w.plan(*ENGINE_ONLY), ["speed/anchor"])
            row = w.row(g, "speed/anchor")
            self.assertEqual(row["state"], "worse", row["detail"])
            self.assertIn("vulkan.cpu_work_ms", row["detail"])
            self.assertEqual((g.verdict, g.code), ("fail", 1))
            self.assertIsNone(w.row(g, PC), "a landing that failed does not run the pair")

    def test_an_anchor_is_not_moved_for_a_store_version(self):
        why = "the base side ended with exit 1: measure-pair d3d12 harness-error the game x.pak is of store version 37; this tree's is 40"
        said = []
        with Gates(self) as w:
            w.measure({"pair": {"states": {r: "tool" for r in ("d3d12", "d3d11", "vulkan")}, "reasons": {r: why for r in ("d3d12", "d3d11", "vulkan")}},
                       "pair-probe": {"ok": True}})
            g, args = w.land(w.plan(*ENGINE_ONLY), ["speed/anchor"])
            row = w.row(g, "speed/anchor")
            self.assertEqual(row["state"], "owed", row["detail"])
            self.assertNotIn("moves to", row["detail"])
            self.assertNotEqual(g.speed.get("speed/anchor"), "rebase")
            self.assertIn(("TOOL", "speed/anchor"), [(f[2], f[3]) for f in w.faults])
            g.args.pak, g.bin = str(w.pak), str(w.bins)
            with contextlib.redirect_stdout(io.StringIO()):
                gate.keep_speed(g, "d" * 40, g.args)
            self.assertEqual(speedbase.read("anchor", w.speed).commit, "a" * 40, "the anchor stays")

    def test_no_anchor_yet_is_owed(self):
        g, w = self.go(also=["speed/anchor"], setup=lambda w: (w.speed / "anchor.txt").unlink())
        row = w.row(g, "speed/anchor")
        self.assertEqual(row["state"], "owed")
        self.assertIn("there is no anchor yet", row["detail"])
        self.assertIn("speed-base anchor --bin", row["detail"])
        self.assertEqual(w.row(g, PC)["state"], "pass")

    def test_the_judged_anchor_keeps_its_ratios_and_an_anchor_that_cannot_run_is_replaced(self):
        g, w, said = self.moved(also=["speed/anchor"])
        last = json.loads((w.speed / speedbase.ANCHOR_LAST).read_text(encoding="utf-8"))
        self.assertEqual((last["anchor"], last["judged at"]), ("a" * 40, "c" * 40))
        self.assertEqual(last["ratios"]["d3d12"], {"cpu_work_ms": 1.01, "render_ms": 1.01})
        self.assertEqual(set(last["ratios"]), {"d3d12", "d3d11", "vulkan"})
        self.assertEqual(speedbase.read("anchor", w.speed).commit, "a" * 40)
        self.assertIn("speed anchor aaaaaaaaa: the ratios of ccccccccc are kept", said)
        # the anchor can no longer run the route: owed, replaced by the candidate after the move, the history keeping the last ratios
        with Gates(self) as w2:
            speedbase.note_anchor_ratios({"d3d12": {"cpu_work_ms": 1.05}}, "a" * 40, "9" * 40, w2.speed)
            w2.measure({"pair": {"states": {"d3d12": "tool", "d3d11": "tool", "vulkan": "tool"},
                                 "reasons": {r: "the base side ended with exit 1" for r in ("d3d12", "d3d11", "vulkan")}}, "pair-probe": {"ok": True}})
            g2, args = w2.land(w2.plan(*ENGINE_ONLY), ["speed/anchor"])
            row = w2.row(g2, "speed/anchor")
            self.assertEqual((row["state"], g2.speed["speed/anchor"]), ("owed", "rebase"), row["detail"])
            self.assertEqual(row["detail"], "the anchor %s cannot run this route any more (d3d11: the base side ended with exit 1; d3d12: the base "
                                            "side ended with exit 1; vulkan: the base side ended with exit 1): the anchor moves to %s unjudged" % (
                                                "a" * 9, g2.commit[:9]))
            g2.args.pak, g2.bin = str(w2.pak), str(w2.bins)
            with contextlib.redirect_stdout(io.StringIO()):
                gate.keep_speed(g2, "d" * 40, g2.args)
            self.assertEqual(speedbase.read("anchor", w2.speed).commit, "d" * 40)
            rows = speedbase.read_rows(w2.speed / speedbase.ANCHOR_HISTORY, speedbase.ANCHOR_COLUMNS)
            self.assertEqual((rows[-1]["commit"], rows[-1]["previous"], rows[-1]["judged at"], rows[-1]["ratios"]),
                             ("d" * 9, "a" * 9, "9" * 40, "d3d12.cpu_work_ms=1.0500"))
            self.assertFalse((w2.speed / speedbase.ANCHOR_LAST).exists())


class Screen(unittest.TestCase):
    """The tablet's screen size, and the lanes' order (B14 part 3)."""

    def world(self, g, size="2800x1752"):
        run = g.work / "runs-tour-tablet-vulkan" / "001" / "vulkan"
        run.mkdir(parents=True)
        if size:
            (run / "run.log").write_text("[vulkan] swapchain %s transform=1 images=5\n" % size, encoding="utf-8")

    def gate_of(self, w):
        args = gate.parser().parse_args(w.argv())
        args.mode = gate.LAND_LEVEL
        g = gate.Gate(args)
        g.selected = None
        return g

    def test_the_size_is_read_from_wm_size_and_is_landscape(self):
        class Device:
            def __init__(self, out, code=0):
                self.out, self.code = out, code

            def shell(self, command, timeout=600):
                assert command == "wm size"
                return self.code, self.out
        self.assertEqual(gate.tablet_screen_of(Device("Physical size: 1752x2800\n")), (2800, 1752))
        self.assertEqual(gate.tablet_screen_of(Device("Physical size: 2800x1752\n")), (2800, 1752))
        self.assertEqual(gate.tablet_screen_of(Device("Physical size: 1752x2800\nOverride size: 1080x1728\n")), (1728, 1080), "an override is what the game sees")
        self.assertIsNone(gate.tablet_screen_of(Device("error\n")))
        self.assertIsNone(gate.tablet_screen_of(Device("Physical size: 1x2\n", code=1)))

    def test_the_lane_reads_the_size_first_and_the_tour_plays_at_it(self):
        with Gates(self) as w:
            args = gate.parser().parse_args(w.argv())
            g = gate.run_checks(args, {}, {}, gate.landing_selection(args, w.plan(*WITH_TABLET), [], {}, {}))
            self.assertEqual(g.tablet_screen, (2800, 1752))
            self.assertEqual(g.tablet_pc_screen, (2800, 1752), "tour/tablet-pc played at it")
            tour = json.loads((g.work / "tour-tablet-pc" / "suite.json").read_text(encoding="utf-8"))
            first = json.loads((g.work / "tour-tablet-pc" / tour["tests"][0] / "test.json").read_text(encoding="utf-8"))
            self.assertEqual(first.get("window"), [2800, 1752])

    def test_another_size_from_the_tablets_tour_is_kind_tool_with_both_sizes(self):
        with Gates(self) as w:
            g = self.gate_of(w)
            self.world(g, "2560x1600")
            g.tablet_pc_screen = (2800, 1752)
            gate.tablet_after(g, ["BattleShipGalaxy 1"])
            rows = {r["check"]: r for r in g.results}
            self.assertEqual(rows["tablet/screen"]["state"], "tool")
            self.assertEqual(rows["tablet/screen"]["detail"],
                             "the tablet's tour reports a screen of 2560x1600 and the PC's run of its stops used 2800x1752")
            self.assertEqual(rows["replays/tablet"]["state"], "tool")
            self.assertIn("not compared: the tablet's tour reports a screen of 2560x1600", rows["replays/tablet"]["detail"])
            self.assertNotIn("differ", rows["tablet/screen"]["detail"] + rows["replays/tablet"]["detail"], "never a difference of hashes")

    def test_the_same_size_is_compared(self):
        with Gates(self) as w:
            g = self.gate_of(w)
            self.world(g, "2800x1752")
            g.tablet_pc_screen = (2800, 1752)
            gate.tablet_after(g, ["BattleShipGalaxy 1"])
            self.assertEqual([r["check"] for r in g.results], ["replays/tablet"])
            self.assertEqual(g.results[0]["state"], "pass")

    def test_a_size_not_known_at_the_start_is_played_after_the_tablets_tour(self):
        with Gates(self) as w:
            g = self.gate_of(w)
            self.world(g, "2800x1752")
            gate.tablet_after(g, ["BattleShipGalaxy 1"])
            self.assertEqual([r["check"] for r in g.results], ["tour/tablet-pc", "replays/tablet"])
            self.assertEqual(g.tablet_pc_screen, (2800, 1752))
            # no size in the tablet's runs at all: both are said as not run, as before
            g2 = self.gate_of(w)
            shutil.rmtree(g.work / "runs-tour-tablet-vulkan")
            self.world(g, "")
            gate.tablet_after(g2, ["BattleShipGalaxy 1"])
            self.assertEqual([(r["check"], r["state"]) for r in g2.results], [("tour/tablet-pc", "fail"), ("replays/tablet", "fail")])

    def test_lanes_with_a_tail(self):
        with Gates(self) as w:
            g = self.gate_of(w)
            g.args.serial = False
            seen = []

            def lane(name, seconds, copies=1, tablet=False, part=""):
                def run():
                    seen.append(("start", name, time.time()))
                    if name == "tablet":
                        time.sleep(0.1)
                        g.android_over.set()      # its build is over
                    time.sleep(seconds)
                    g.rows().append({"check": name, "state": "pass", "passed": True})
                    seen.append(("end", name, time.time()))
                run.__name__, run.copies, run.part = name, copies, part
                return run
            tablet = lane("tablet", 1.2)
            tablet.part = "tablet"

            def tail():
                seen.append(("start", "tail", time.time()))
                g.rows().append({"check": "tail", "state": "pass", "passed": True})
                seen.append(("end", "tail", time.time()))
            g.lanes_with_tail([lane("a", 0.3), lane("b", 0.6), lane("self", 1.0, copies=0, part="self")], [tablet], tail, wait_for_cpu_lane=False)
            at = {(k, n): t for k, n, t in seen}
            self.assertGreaterEqual(at[("start", "tail")], at[("end", "b")], "after the PC's lanes")
            self.assertLess(at[("start", "tail")], at[("end", "self")], "not waiting for the lane that starts no game")
            self.assertLess(at[("start", "tail")], at[("end", "tablet")], "beside the tablet's lane")
            self.assertEqual([r["check"] for r in g.results], ["a", "b", "tail", "self", "tablet"], "the PC's first, the tail, then the others")
            # waiting for every lane
            seen.clear()
            g.results.clear()
            g.android_over.clear()
            g.lanes_with_tail([lane("a", 0.3), lane("self", 1.0, copies=0, part="self")], [lane("tablet", 0.2)], tail, wait_for_cpu_lane=True)
            at = {(k, n): t for k, n, t in seen}
            self.assertGreaterEqual(at[("start", "tail")], at[("end", "self")])


    def test_a_tail_that_breaks_is_a_tool_row_and_the_tablets_lane_is_still_joined(self):
        with Gates(self) as w:
            g = self.gate_of(w)
            g.args.serial = False
            done = []

            def tablet():
                g.android_over.set()
                time.sleep(0.4)
                done.append("tablet")
            tablet.part = "tablet"

            def other():
                done.append("pc")

            def tail():
                raise RuntimeError("the pair broke")
            g.lanes_with_tail([other], [tablet], tail)
            self.assertEqual(sorted(done), ["pc", "tablet"], "the tablet's lane ended before the call did")
            self.assertEqual([(r["check"], r["state"]) for r in g.results], [("lane/tail", "tool")])
            self.assertIn("RuntimeError: the pair broke", g.results[0]["detail"])


class Where(unittest.TestCase):
    """Where the two checks sit."""

    def run_plan(self, paths, setup=None, tablet="T", also=(), slow=None):
        with Gates(self) as w:
            self.w = w
            w.measure({"pair": {"seconds": 0.3}, "tablet": {"exit": 0, "state": "ok", "figures": HOLDS, "seconds": 0.4}})
            w.slow = dict(slow or {})
            if setup:
                setup(w)
            g, args = w.land(w.plan(*paths), also, tablet)
            return g, w, args

    TABLETS = ("build/android", "android/tablet", "tour/tablet", "tablet/prepare", "tablet/remove")

    def test_pc_measure_starts_beside_the_tablets_lane(self):
        slow = {"tour/d3d12": 0.8, "build/android": 0.2, "tablet/prepare": 2.5, "replays/d3d12": 0.2}
        g, w, _ = self.run_plan(WITH_TABLET, slow=slow)
        pc = [c for c in w.runs() if c["run"] == "pair"]
        self.assertEqual(len(pc), 1, w.runs())
        start = pc[0]["start"]
        pcs = [(n, b, e) for n, b, e in w.times
               if n not in self.TABLETS and n != "tour/tablet-pc" and not n.startswith(("replays/", "pictures/"))]
        self.assertGreaterEqual(start, max(e for n, b, e in pcs), "after every check of the PC's own lanes: %s" % pcs)
        self.assertGreaterEqual(start, w.span("build/android")[1], "never beside a build")
        self.assertLess(start, w.span("tablet/prepare")[1], "while the tablet's lane is still going")
        pc_tour = w.span("tour/tablet-pc")
        self.assertIsNotNone(pc_tour, [n for n, b, e in w.times])
        self.assertGreaterEqual(pc_tour[0], max(e for n, b, e in pcs), "tour/tablet-pc after the PC's lanes too")
        self.assertLessEqual(pc_tour[1], start, "one at a time: tour/tablet-pc, then the pair")
        self.assertEqual(w.row(g, PC)["state"], "pass")
        order = [r["check"] for r in g.results]
        self.assertLess(order.index("tour/tablet-pc"), order.index(PC))
        self.assertLess(order.index(PC), order.index("tablet/prepare"), "the rows: the PC's, its tail, the tablet's")

    def test_the_pair_does_not_wait_for_the_self_tests(self):
        # B14 part 3's proof: the pair beside the tools' self-tests stays inside its bands, so it does not wait for them.
        slow = {"harness/self": 2.5, "description/self": 0.1, "tour/d3d12": 0.2, "tablet/prepare": 3.0}
        g, w, _ = self.run_plan(WITH_TABLET + ("tools/gate/x.py",), slow=slow)
        pc = [c for c in w.runs() if c["run"] == "pair"][0]
        self.assertLess(pc["start"], w.span("harness/self")[1], "the self-tests go on beside the pair")
        order = [r["check"] for r in g.results]
        self.assertLess(order.index(PC), order.index("harness/self"), "the self-tests' rows come after the PC's lanes and the tail")
        self.assertEqual(w.row(g, "harness/self")["state"], "pass")
        self.assertEqual(w.row(g, "description/self")["state"], "pass")

    def test_the_self_tests_are_a_lane_of_their_own_and_rust_stays_in_the_cpu_lane(self):
        with Gates(self) as w:
            args = gate.parser().parse_args(w.argv())
            args.mode = gate.LAND_LEVEL
            g = gate.Gate(args)
            seen = []
            g.check = lambda name, fn, needs=(): seen.append(name)
            gate.cpu_lane(g, ("RMGE01",), with_self=False)()
            self.assertEqual([n for n in seen if n in ("rust", "harness/self", "description/self")], ["rust"], seen)
            seen.clear()
            gate.cpu_lane(g, ("RMGE01",))()
            self.assertEqual([n for n in seen if n in ("rust", "harness/self", "description/self")], ["rust", "harness/self", "description/self"])
            seen.clear()
            lane = gate.self_lane(g)
            lane()
            self.assertEqual((seen, lane.part, lane.copies), (["harness/self", "description/self"], "self", 0))
            self.assertTrue(gate.PAIR_WAITS_FOR_SELF is False)

    def test_the_pair_waits_for_the_android_build(self):
        slow = {"tour/d3d12": 0.1, "build/android": 1.5, "tablet/prepare": 0.2}
        g, w, _ = self.run_plan(WITH_TABLET, slow=slow)
        pc = [c for c in w.runs() if c["run"] == "pair"][0]
        self.assertGreaterEqual(pc["start"], w.span("build/android")[1], "the build is a build: the pair never runs beside it")
        self.assertGreaterEqual(w.span("tour/tablet-pc")[0], w.span("build/android")[1])

    def test_the_parts_asked_for_by_name_keep_the_pair_last(self):
        g, w, _ = self.run_plan(WITH_TABLET, also=("soak",), slow={"tablet/prepare": 0.3})
        pc = [c for c in w.runs() if c["run"] == "pair"][0]
        self.assertGreaterEqual(pc["start"], w.span("soak-process")[1], "after the soak asked for")
        self.assertGreaterEqual(pc["start"], max(e for n, b, e in w.times if n != "soak-process" and not n.startswith("tour/tablet-pc")), "last")
        self.assertIsNotNone(w.span("tour/tablet-pc"), "tour/tablet-pc moves all the same")

    def test_a_tablet_that_is_away_owes_the_pcs_half_and_the_pair_still_runs(self):
        g, w, _ = self.run_plan(WITH_TABLET, setup=lambda w: setattr(w, "answers", False))
        self.assertIsNone(w.span("tour/tablet-pc"), "no size: the PC's half is not played")
        self.assertEqual(w.row(g, "tour/tablet-pc")["state"], "owed")
        self.assertEqual(w.row(g, PC)["state"], "pass")

    def test_pc_measure_is_skipped_when_a_check_failed(self):
        g, w, _ = self.run_plan(NATIVE, setup=lambda w: w.fail.add("tour/d3d12"))
        self.assertIsNone(w.row(g, PC), "no row")
        self.assertEqual([c["run"] for c in w.runs()], ["tablet"], "the PC's measure was not started; the tablet's ran in its lane before")
        self.assertEqual(g.verdict, "fail")

    def test_pc_measure_is_skipped_after_a_failed_android_build_and_so_is_the_tablet(self):
        g, w, _ = self.run_plan(WITH_TABLET, setup=lambda w: w.fail.add("build/android"))
        self.assertIsNone(w.row(g, TABLET), "no tablet measure when build/android failed")
        self.assertIsNone(w.row(g, PC), "the pair is not started before the Android build is known to have passed")
        self.assertEqual(w.runs(), [])

    def test_tablet_measure_is_between_the_build_and_the_tablets_part(self):
        slow = {"build/android": 0.3, "tablet/prepare": 0.2}
        g, w, _ = self.run_plan(WITH_TABLET, slow=slow)
        tablet = [c for c in w.runs() if c["run"] == "tablet"][0]
        build, prepare = w.span("build/android"), w.span("tablet/prepare")
        self.assertGreaterEqual(tablet["start"], build[1], "after build/android ended")
        self.assertLessEqual(tablet["end"], prepare[0], "before tablet/prepare started")
        self.assertEqual(w.row(g, TABLET)["state"], "pass")

    def test_a_plan_without_the_tablets_part_has_the_tablet_measure(self):
        g, w, _ = self.run_plan(NATIVE)
        self.assertEqual(w.row(g, TABLET)["state"], "pass")
        self.assertFalse([r for r in g.results if r["check"].startswith("tablet/")], "no tablet part in this plan")
        self.assertEqual({c["run"] for c in w.runs()}, {"pair", "tablet"})

    def test_no_tablet_named_is_owed_and_nothing_starts(self):
        g, w, _ = self.run_plan(NATIVE, tablet="")
        row = w.row(g, TABLET)
        self.assertEqual(row["state"], "owed")
        self.assertIn("no tablet is named", row["detail"])
        self.assertEqual([c["run"] for c in w.runs()], ["pair"])
        self.assertEqual(g.verdict, "pass")
        self.assertEqual(w.faults, [], "a tablet that is away is no tool fault")

    def test_a_tablet_that_does_not_answer_is_owed(self):
        g, w, _ = self.run_plan(NATIVE, setup=lambda w: setattr(w, "answers", False))
        row = w.row(g, TABLET)
        self.assertEqual(row["state"], "owed")
        self.assertIn("does not answer adb", row["detail"])
        self.assertEqual([c["run"] for c in w.runs()], ["pair"])
        self.assertEqual(g.verdict, "pass")

    def test_the_plan_decides_what_is_measured(self):
        cases = [(NATIVE, True, True), (ENGINE_ONLY, True, False), (("tests/replays/x.tsv",), True, False),
                 (("platform/android/app/build.gradle",), False, False), (("tools/gate/x.py",), False, False),
                 (("docs/plan/x.md",), False, False)]
        for paths, pc, tablet in cases:
            plan = scope.plan_for(list(paths), TABLE)
            self.assertEqual(("measure_pc" in plan.groups, "measure_tablet" in plan.groups), (pc, tablet), paths)
            g, w, _ = self.run_plan(paths)
            self.assertEqual((w.row(g, PC) is not None, w.row(g, TABLET) is not None), (pc, tablet), (paths, g.printed))
            self.assertEqual({c["run"] for c in w.runs()}, ({"pair"} if pc else set()) | ({"tablet"} if tablet else set()), paths)
        # An importer change can move a picture (the group `pictures`), so its plan has both measures; the run of it is not played here (the
        # collision proof of its group has no disc image in this fake world).
        importer = scope.plan_for(["crates/nebula-import/src/lib.rs"], TABLE)
        self.assertEqual(("measure_pc" in importer.groups, "measure_tablet" in importer.groups), (True, True))
        full = scope.groups_from_text("full")[0]
        self.assertIn("measure_pc", full)
        self.assertIn("measure_tablet", full)

    def test_the_groups_are_printed_in_the_plan(self):
        plan = scope.plan_for(list(NATIVE), TABLE)
        self.assertIn("measure_pc", plan.text())
        self.assertIn("measure_tablet", plan.text())
        said = "\n".join(scope.describe(plan))
        self.assertIn("speed measure of the play route on Direct3D 12", said)
        self.assertIn("on the tablet", said)
        groups, full = scope.groups_from_text(plan.text())
        self.assertEqual((groups, full), (plan.groups, False), "the text a landing passes to the gate names them again")

    def test_the_other_levels_have_neither_check(self):
        with Gates(self) as w:
            for level in ("quick", "long", "final"):
                args = gate.parser().parse_args(["--" + level, "--tablet", "T", "--work", str(w.work)])
                needs = gate.run_checks(args, {}, {}, dry=True).needs
                self.assertFalse({PC, TABLET} & set(needs), level)
            args = gate.parser().parse_args(["--main", "--tablet", "T", "--work", str(w.work)])
            self.assertTrue({PC, TABLET} <= set(gate.run_checks(args, {}, {}, dry=True).needs))

    def test_the_commands(self):
        g, w, args = self.run_plan(NATIVE)
        by = {c["run"]: c["args"] for c in w.runs()}
        pc, tablet = by["pair"], by["tablet"]
        base = speedbase.read("base", w.speed)
        for a, want in ((pc, [("--runs", "d3d12"), ("--pending", str(g.work / "measure-pc.tsv")), ("--work", str(g.work / "measure-pc")),
                              ("--base", str(base.bins)), ("--cand", g.bin), ("--base-pak", str(base.pak)), ("--cand-pak", "X"),
                              ("--base-commit", "b" * 40), ("--cand-commit", g.commit), ("--name", PC)]),
                        (tablet, [("--runs", "tablet"), ("--device", "T"), ("--device-lock", str(w.t / "tablet.lock")),
                                  ("--device-wait", "7"), ("--android-bin", g.android_bin), ("--apk", str(w.build / "android" / "Nebula.apk")),
                                  ("--pak", "X"), ("--pending", str(g.work / "measure-tablet.tsv")),
                                  ("--work", str(g.work / "measure-tablet"))])):
            for option, value in want:
                self.assertEqual(a[a.index(option) + 1], value, "%s of %s" % (option, a[:4]))
        self.assertNotIn("--gate-lock", pc, "the landing holds the gate lock")
        self.assertNotIn("--gate-lock", tablet)
        self.assertNotIn("--device", pc)
        self.assertNotIn("--quiet-minutes", pc)
        self.assertNotIn("--quiet-minutes", tablet)
        self.assertEqual(gate.MEASURE_PC_RUN, "d3d12")
        self.assertEqual(gate.MEASURE_TABLET_RUN, "tablet")


class Keep(unittest.TestCase):
    """The history: the pending files are kept only after main has moved."""

    def drive(self, move_why=None, keep_exit=0, pending=("measure-pc.tsv", "measure-tablet.tsv"), absent=()):
        world = gh.World(None)
        world.faults = []
        with Gates(self) as w:
            w.measure({"keep_exit": keep_exit, "keep_says": "measure: kept 3 line(s)" if keep_exit == 0 else "measure: keep: broke"})

            def checks(g, world):
                for name in pending:
                    if name not in absent:
                        (g.work / name).write_text("commit\n", encoding="utf-8")
                    g.measure_pending.append(g.work / name)
            code, lines, folder = gh.drive(self, world, "repo", checks=checks, allow_github=False, move_why=move_why)
            return code, lines, world, w.calls()

    def test_main_moved_keeps_each_pending_file_that_exists(self):
        code, lines, world, calls = self.drive(absent=("measure-tablet.tsv",))
        self.assertEqual((code, lines[-1]), (0, "VERDICT pass"), lines)
        kept = [c for c in calls if "keep" in c]
        self.assertEqual([pathlib.Path(c["keep"]).name for c in kept], ["measure-pc.tsv"], "the file that is not there is not kept")
        self.assertTrue(kept[0]["history"].endswith("measure-history.tsv"))
        self.assertIn("gate.py: measure: kept 3 line(s)", lines)
        self.assertEqual(world.faults, [])

    def test_main_not_moved_keeps_nothing(self):
        code, lines, world, calls = self.drive(move_why="main moved during the gate (now 1234): rebase on it and gate again")
        self.assertEqual(code, 2, lines)
        self.assertEqual([c for c in calls if "keep" in c], [], "the files stay in the run folder")
        self.assertFalse([l for l in lines if "kept" in l])

    def test_a_keep_that_fails_changes_nothing_about_the_landing(self):
        code, lines, world, calls = self.drive(keep_exit=2)
        self.assertEqual((code, lines[-1]), (0, "VERDICT pass"), lines)
        self.assertIn("MOVED " + gh.SHA, lines)
        said = [l for l in lines if "were not kept in the history" in l]
        self.assertEqual(len(said), 2, "one printed line for each file: %s" % lines)
        self.assertEqual([(f[2], f[3]) for f in world.faults][:1], [("TOOL", "measure/keep")])


class Worse(unittest.TestCase):
    """The kind worse."""

    def test_worse_in_kinds(self):
        self.assertLess(kinds.RANK["fail"], kinds.RANK["worse"])
        self.assertLess(kinds.RANK["worse"], kinds.RANK["tool"])
        records = [kinds.record("-", "worse", "worse: step_ms: step_ms 4 (median 3, band 0.2)")]
        self.assertEqual(kinds.verdict_of([records]), "fail")
        self.assertEqual(kinds.state_of(records), "worse")
        self.assertFalse(kinds.needs_second_run(records[0]), "never run again alone")
        self.assertFalse(kinds.needs_second_run(kinds.record("step_ms", "worse", "worse: step_ms: x")), "not even as a test's result")
        self.assertTrue(kinds.needs_second_run(kinds.record("step_ms", "fail", "x")), "a fail is")
        self.assertEqual(kinds.EXIT_OF[kinds.verdict_of([records])], 1)
        self.assertIn("worse", gate.NOT_PASSED)
        self.assertEqual(kinds.worse_names("worse: render_ms+step_ms: render_ms 2.5 (median 1.4, band 0.1)"), "render_ms+step_ms")
        self.assertEqual(kinds.worse_message([("step_ms", 4.5, 3.0, 0.2), ("render_ms", 2.5, 1.4, 0.1)]),
                         "worse: render_ms+step_ms: render_ms 2.5 (median 1.4, band 0.1); step_ms 4.5 (median 3, band 0.2)")

    def test_a_worse_row_is_never_run_again_nor_a_flake_nor_set_aside(self):
        with Gates(self) as w:
            w.measure({"pair": {"states": {"d3d12": "worse"}}})
            g, _ = w.land(w.plan(*ENGINE_ONLY))
            names = [r["check"] for r in g.results]
            self.assertEqual(len(names), len(set(names)), "no check ran twice")
            self.assertEqual([c["run"] for c in w.runs()], ["pair"], "the pair ran once: it made its three rounds itself")
            self.assertFalse(gate.FLAKES.exists() and gate.FLAKES.read_text(encoding="utf-8").strip(), "no line in flakes.tsv")
            self.assertFalse(gate.SET_ASIDE.exists() and gate.SET_ASIDE.read_text(encoding="utf-8").strip())
            self.assertFalse([r for r in g.results if r.get("pending")])

    def worse_landings(self, figure_names, values):
        """Lands one branch as many times as `figure_names` has entries; each time a run whose measure was worse in those figures."""
        with tempfile.TemporaryDirectory() as tmp:
            world = lt.World(tmp)
            world.branch("feat/slow", {"docs/slow.md": "x"})
            calls = []

            class Checker:
                def run(self, tree, tip, names, groups_text, also=""):
                    figs = [(n, values.pop(0), 3.0, 0.2) for n in figure_names[len(calls)]]
                    calls.append(figs)
                    line = "KIND worse %s - :: %s" % (PC, kinds.worse_message(figs))
                    folder = world.t / "runs" / ("run-%d" % len(calls))
                    folder.mkdir(parents=True)
                    (folder / "verdict.json").write_text(json.dumps({"verdict": "fail", "lines": [line]}), encoding="utf-8")
                    return landing.Result("fail", [line], landing.failures_of_lines([line]), folder)
            codes = []
            for _ in figure_names:
                L = world.landing(checker=Checker())
                codes.append(L.land(["feat/slow"]))
            store = failures.Store(world.store_path)
            return codes, {"count": store.count("feat/slow"), "last": store.last_line("feat/slow"), "row": world.rows()["feat/slow"]}

    def test_the_same_figures_worse_three_times_stop_the_branch(self):
        codes, store = self.worse_landings([["step_ms", "render_ms"]] * 3, [4.1, 2.2, 5.5, 2.9, 4.0, 2.0])
        self.assertEqual(codes, [1, 1, 3], "the values differ each time; the figures are the same")
        self.assertEqual((store["count"], store["last"]), (3, "measure/step/pc | render_ms+step_ms"), "the signature holds names, never values")

    def test_other_figures_do_not_count_as_the_same(self):
        codes, store = self.worse_landings([["step_ms", "render_ms"], ["step_ms", "render_ms"], ["step_ms"]], [4.1, 2.2, 5.5, 2.9, 4.0])
        self.assertEqual(codes, [1, 1, 1])
        self.assertEqual(store["count"], 1, "a different set of figures starts the count again")
        self.assertEqual(store["last"], "measure/step/pc | step_ms")

    def test_the_branch_is_named_failed_with_the_figures_in_its_note(self):
        codes, store = self.worse_landings([["step_ms"]], [4.1])
        note = store["row"]["note"]
        self.assertEqual(store["row"]["state"], "failed")
        self.assertIn("step_ms 4.1 (median 3, band 0.2)", note, "the person reads the values")


class Record(unittest.TestCase):
    """The fake measure.py's record of its calls (T11-B): the pair and the tablet's measure run at once in a landing, so their fakes write at
    once. Lines appended by processes that run at the same time to one file broke on this PC (40 of 2400 lines in a trial of 8 processes);
    the record is one file for each process."""

    def test_eight_processes_at_once_leave_every_line_whole(self):
        with Gates(self) as w:
            go = w.t / "go"
            w.measure({"record_times": 1000, "start_when": str(go)})
            procs = [subprocess.Popen([sys.executable, "-X", "utf8", "-I", str(w.script), "keep", "c%d" % i, "--history", "h" * 400])
                     for i in range(8)]
            go.write_text("go", encoding="utf-8")   # all eight start their writing together
            for proc in procs:
                self.assertEqual(proc.wait(), 0)
            calls = w.calls()
            self.assertEqual(len(calls), 8 * 1000, "every line of every process is there and whole")
            self.assertEqual(sorted({c["keep"] for c in calls}), sorted("c%d" % i for i in range(8)))


class Also(unittest.TestCase):
    """--also by name."""

    CHECKS = {"stress": "stress/engine/d3d12", "sanitizer/tour": "tour-asan/d3d12", "soak": "soak", "soak/long": "soak/long",
              "replays/software": "replays/software", "speed/anchor": "speed/anchor", "replays/rates": "replays/rates"}
    # A name of the table whose part is not on main (every part is today): the landing refuses it. A plant, as the table's own
    # entry for `replays/software` was before package T9.
    UNBUILT = {"future/part": gate.AlsoPart(None, ("future/part",))}

    def run_also(self, names, setup=None, paths=("docs/plan/x.md",)):
        with Gates(self) as w:
            w.measure({"pair": {"seconds": 0.2}, "tablet": {"exit": 0, "state": "ok", "figures": HOLDS, "seconds": 0.2}})
            if setup:
                setup(w)
            g, args = w.land(w.plan(*paths), names)
            return g, w

    def test_also_names_are_planned(self):
        for name, check in self.CHECKS.items():
            g, w = self.run_also([name])
            self.assertIsNotNone(w.row(g, check), "%s: %s" % (name, [r["check"] for r in g.results]))
            self.assertEqual(w.row(g, check)["state"], "pass", name)
        self.assertEqual(sorted(gate.ALSO_PARTS), ["replays/rates", "replays/software", "sanitizer/tour", "soak", "soak/long", "speed/anchor", "stress"])
        self.assertTrue(all(part.run is not None for part in gate.ALSO_PARTS.values()), "every name of the table is built")

    def test_the_soaks_ask_for_their_lengths(self):
        g, w = self.run_also(["soak"])
        self.assertEqual(w.soaks, [30])
        g, w = self.run_also(["soak/long"])
        self.assertEqual(w.soaks, [120], "the long level's time (--soak-minutes)")

    def test_the_stress_lane_has_its_twelve_lanes_and_the_sanitizer_its_build(self):
        g, w = self.run_also(["stress"])
        self.assertEqual(len([r for r in g.results if r["check"].startswith("stress/")]), 15, "the twelve lanes' checks")
        g, w = self.run_also(["sanitizer/tour"])
        self.assertEqual(w.row(g, "build/windows-asan")["state"], "pass")
        self.assertEqual(w.row(g, "unit/game-asan")["state"], "pass")
        self.assertEqual(w.row(g, "tour-asan/d3d12")["state"], "pass")

    def test_the_sanitizer_build_runs_the_games_unit_tests_as_a_check_of_its_own(self):
        # A unit test that fails on the sanitizer build is a fail of unit/game-asan alone; the tour is still played.
        g, w = self.run_also(["sanitizer/tour"], setup=lambda w: w.fail.add("unit/game-asan"))
        self.assertEqual(w.row(g, "unit/game-asan")["state"], "fail")
        self.assertEqual(w.row(g, "build/windows-asan")["state"], "pass")
        self.assertEqual(w.row(g, "tour-asan/d3d12")["state"], "pass")
        self.assertEqual(g.verdict, "fail")
        # A build that failed: the unit tests are not run and say so, as the tour does.
        g, w = self.run_also(["sanitizer/tour"], setup=lambda w: w.fail.add("build/windows-asan"))
        for name in ("unit/game-asan", "tour-asan/d3d12"):
            row = w.row(g, name)
            self.assertEqual((row["state"], row["detail"]), ("fail", "not run: the sanitizer build failed"), name)
        self.assertNotIn("unit/game-asan", [n for n, b, e in w.times], "its tests were not started")

    def test_the_sanitizer_unit_tests_are_run_in_the_sanitizer_builds_own_folder(self):
        # game_tests(g, root) runs ctest in <root>/game: the sanitizer build's folder, never the plain build's.
        calls = []

        def fake_run(command, log, **kw):
            calls.append([str(c) for c in command])
            pathlib.Path(log).write_text("100% tests passed, 0 tests failed out of 12\n", encoding="utf-8")
            return 0
        with Gates(self) as w, unittest.mock.patch.object(gate, "run", fake_run):
            args = gate.parser().parse_args(w.argv())
            g = gate.Gate(args)
            log = w.t / "unit.log"
            self.assertEqual(gate.game_tests(g, pathlib.Path(g.args.build_root) / "asan")(log)[0], True)
            self.assertEqual(gate.game_tests(g)(log)[0], True)
            dirs = [c[c.index("-Dir") + 1] for c in calls]
            self.assertEqual(dirs, [str(pathlib.Path(g.args.build_root) / "asan" / "game"), str(pathlib.Path(g.args.build_root) / "game")])

    def test_they_run_after_the_needed_checks_and_before_the_measure_one_after_another(self):
        slow = {"engine/vulkan": 0.6, "pictures/engine/d3d12": 0.2}
        g, w = self.run_also(["sanitizer/tour", "soak"], setup=lambda w: w.slow.update(slow), paths=ENGINE_ONLY)
        also_names = ("build/windows-asan", "unit/game-asan", "tour-asan/d3d12", "soak", "soak-process")
        needed = [e for n, b, e in w.times if n not in also_names]
        first_also = min(b for n, b, e in w.times if n in also_names)
        self.assertGreaterEqual(first_also, max(needed), "after every needed check ended")
        asan = w.span("tour-asan/d3d12")
        soak = w.span("soak-process")
        self.assertGreaterEqual(soak[0], asan[1], "one after another, in the order asked")
        pc = w.runs()[0]
        self.assertGreaterEqual(pc["start"], soak[1], "before the measure")
        self.assertEqual(w.row(g, PC)["state"], "pass")

    def test_a_failed_landing_runs_none_of_them(self):
        g, w = self.run_also(["soak", "sanitizer/tour"], setup=lambda w: w.fail.add("engine/d3d11"), paths=ENGINE_ONLY)
        self.assertEqual(w.soaks, [], "no soak was started")
        self.assertIsNone(w.row(g, "soak"))
        self.assertIsNone(w.row(g, "tour-asan/d3d12"))
        self.assertIsNone(w.row(g, PC))
        self.assertEqual(g.verdict, "fail")

    def test_a_part_that_fails_stops_the_next_one(self):
        g, w = self.run_also(["sanitizer/tour", "soak"], setup=lambda w: w.fail.add("tour-asan/d3d12"))
        self.assertEqual(w.soaks, [])
        self.assertEqual(g.verdict, "fail")

    def test_asked_for_by_name_a_stress_result_is_a_fail(self):
        g, w = self.run_also(["stress"])
        self.assertTrue(w.stress_fails and all(w.stress_fails), "every stress lane is told: %s" % w.stress_fails)
        with Gates(self) as w2, contextlib.redirect_stdout(io.StringIO()):
            args = gate.parser().parse_args(w2.argv())
            args.mode = "main"
            gate.stress_part(gate.Gate(args))
        self.assertTrue(w2.stress_fails and not any(w2.stress_fails), "the nightly's stress lane is not: %s" % w2.stress_fails)

    def test_the_unbuilt_name_is_refused(self):
        with Gates(self) as w, unittest.mock.patch.dict(gate.ALSO_PARTS, self.UNBUILT):
            args = gate.parser().parse_args(w.argv())
            why = gate.also_problem(args, ["future/part"], {}, {})
            self.assertEqual(why, "--also future/part: this check is not built yet")
            self.assertEqual(gate.also_problem(args, ["soak", "soak/long"], {}, {}), "--also: soak and soak/long are one soak at two lengths; ask for one")
            self.assertEqual(gate.also_problem(args, [n for n in gate.ALSO_PARTS if n not in ("future/part", "soak/long")], {}, {}), "")
            self.assertEqual(gate.also_problem(args, ["replays/software"], {}, {}), "", "replays/software is built (T9)")
            unknown = gate.also_problem(args, ["tour/d3d99", "soak"], {}, {})
            self.assertTrue(unknown.startswith("--also names no check the gate knows: tour/d3d99."), unknown)
            self.assertIn("stress", unknown, "the table's names are listed")
            self.assertEqual(gate.also_problem(args, ["coverage"], {}, {}).split(".")[0], "--also names no check the gate knows: coverage",
                             "a name only the long run has is unknown to a landing, no longer a special case")

    def test_the_landing_refuses_before_it_queues_anything(self):
        with Gates(self) as w, unittest.mock.patch.dict(gate.ALSO_PARTS, self.UNBUILT):
            for name, words in (("future/part", "--also future/part: this check is not built yet"),
                                ("soak,soak/long", "are one soak at two lengths")):
                queue = w.t / ("queue-%s.tsv" % name.replace("/", "-").replace(",", "-"))
                err, out = io.StringIO(), io.StringIO()
                with contextlib.redirect_stderr(err), contextlib.redirect_stdout(out):
                    code = gate.land_main(["feat/x", "--also", name, "--queue-file", str(queue), "--live", str(w.live),
                                           "--runner-lock", str(w.t / "runner.lock"), "--failures", str(w.t / "f.tsv"),
                                           "--landing-tree", str(w.t / "tree"), "--work", str(w.work)])
                self.assertEqual(code, landing.EXIT_NOT_JUDGED, name)
                self.assertIn(words, err.getvalue())
                self.assertIn("VERDICT not-judged", out.getvalue())
                self.assertFalse(queue.exists(), "nothing was queued")

    def test_the_landing_prints_what_was_asked_for(self):
        with tempfile.TemporaryDirectory() as tmp:
            world = lt.World(tmp)
            world.branch("feat/a", {"docs/a.md": "a"})
            L = world.landing()
            L.land(["feat/a"], also="soak,stress")
            self.assertTrue(any("and, asked for by name: soak,stress" in l for l in L.out), L.out)


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("gate_line_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
