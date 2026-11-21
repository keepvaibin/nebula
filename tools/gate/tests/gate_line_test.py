"""gate_line_test.py: the speed measure in a landing, the kind `worse` and `--also` by name (package B8; tools/gate/gate.py measure/step/pc,
measure/step/tablet, ALSO_PARTS, kinds.py, landing.py, scope.py), without the game, a build, a graphics card or a tablet.

    python -X utf8 -I tools/gate/tests/gate_line_test.py [--work FOLDER]

The gate's run_checks runs for real on a landing's plan. Every check but the measure is a fake that sleeps, records when it started and
ended and passes (or fails, when the case says); measure.py is a fake program that writes the result.json the case plants and exits as
planted, recording its own arguments and times. The folders under D:/NebulaWork are replaced by temporary ones.

Table B, each state of the measure (on the PC's check and on the tablet's):
  every_state_has_its_row          ok, better, no-baseline: pass, the figures named with value and median (no baseline yet said); worse: kind
                                   worse, the run fails, exit 1; not-trusted, tool, no result.json, an exit code and a state that disagree,
                                   the time limit: owed with the plain reason, one line in tool-faults.tsv; left-out: owed, no line
  a_row_fails_the_run_like_fail    the verdict and exit code of each: pass 0, worse fail 1, owed pass 0
  one_fault_line_for_a_check       two owed measures in one run: one line each, the check named as the test
Where the checks sit:
  pc_measure_runs_last             it starts after every lane ended (the slowest lane, the tablet's measure, the parts --also asked for)
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
  also_refusals                    replays/software is refused as not built (exit 2, with the words), soak with soak/long, an unknown name, as before
Exit 0 when every case holds.
"""
import contextlib
import io
import json
import os
import pathlib
import shutil
import sys
import tempfile
import time
import unittest

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

TABLE = scope.read_table()

FAKE_MEASURE = '''
import json, os, pathlib, sys, time
args = sys.argv[1:]
plan = json.loads(pathlib.Path(os.environ["FAKE_MEASURE_PLAN"]).read_text(encoding="utf-8"))
record = pathlib.Path(os.environ["FAKE_MEASURE_RECORD"])
opts = {a: args[i + 1] for i, a in enumerate(args) if a.startswith("--") and i + 1 < len(args)}
if args[0] == "keep":
    with open(record, "a", encoding="utf-8") as f:
        f.write(json.dumps({"keep": args[1], "history": opts.get("--history"), "time": time.time()}) + "\\n")
    print(plan.get("keep_says", "measure: kept 1 line(s)"))
    sys.exit(plan.get("keep_exit", 0))
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
with open(record, "a", encoding="utf-8") as f:
    f.write(json.dumps({"run": run, "args": args, "start": start, "end": time.time()}) + "\\n")
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

    def measure(self, plan):
        self.plan_file.write_text(json.dumps(plan), encoding="utf-8")

    def calls(self):
        if not self.record.is_file():
            return []
        return [json.loads(l) for l in self.record.read_text(encoding="utf-8").splitlines() if l.strip()]

    def runs(self):
        return [c for c in self.calls() if "run" in c]

    def span(self, name):
        return next(((a, b) for n, a, b in self.times if n == name), None)

    # --- the patched gate --------------------------------------------------------------------------------------------

    def __enter__(self):
        names = ("MEASURE", "FLAKES", "SET_ASIDE", "BASE_BINS", "MACHINE_JSON", "PICTURE_STORE", "machine_ready", "tablet_part", "fault_writer",
                 "android_module", "start_soak", "harness", "measure_limit")
        self.saved = {n: getattr(gate, n) for n in names}
        self.saved_check, self.saved_ensure = gate.Gate.check, gate.machine.ensure_adb_server
        self.env = {k: os.environ.get(k) for k in ("FAKE_MEASURE_PLAN", "FAKE_MEASURE_RECORD")}
        os.environ["FAKE_MEASURE_PLAN"], os.environ["FAKE_MEASURE_RECORD"] = str(self.plan_file), str(self.record)
        gate.MEASURE = self.script
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

        gate.android_module = lambda: type("Module", (), {"Device": Device})
        original = gate.Gate.check

        def check(g, name, fn, needs=()):
            if name.startswith("measure/step"):
                return original(g, name, fn, needs)

            def fake(log):
                begin = time.time()
                time.sleep(self.slow.get(name, 0.02))
                self.times.append((name, begin, time.time()))
                return name not in self.fail, "a fake: %s" % ("failed" if name in self.fail else "passed")
            return original(g, name, fake, needs)

        def tablet_part(g, stops, device=None):
            if g.android_built:
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
        args.mode = gate.LAND_LEVEL if not also else "main"
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
        if state == "left-out" and check == PC:
            return                                        # only the tablet is ever left out
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
            self.one(PC, "d3d12", case)
            self.one(TABLET, "tablet", case)

    def test_worse_row_is_a_kind_line_and_fails_the_run(self):
        g, row = self.one(PC, "d3d12", self.CASES[3])
        lines = [l for l in g.lines if l.startswith("KIND worse")]
        self.assertEqual(len(lines), 1, g.lines)
        parsed = kinds.parse_kind_line(lines[0])
        self.assertEqual(parsed[:3], ("worse", PC, "-"))
        self.assertEqual(kinds.worse_names(parsed[3]), "render_ms+step_ms")
        self.assertEqual([r["kind"] for r in row["records"]], ["worse"])
        self.assertEqual(landing.failures_of_lines(g.lines), [(PC, "worse", parsed[3])])

    def test_the_states_that_disagree_are_owed(self):
        # An exit code that is not the state's, a state measure.py does not have, no result.json, a result.json that is not one.
        cases = {"state ok, exit 1": {"exit": 1, "state": "ok", "figures": HOLDS},
                 "state worse, exit 0": {"exit": 0, "state": "worse", "figures": WORSE},
                 "result.json says exit 3, the program exited 0": {"exit": 0, "result_exit": 3, "state": "ok", "figures": HOLDS},
                 "an unknown state": {"exit": 0, "state": "great", "figures": HOLDS},
                 "no result.json": {"exit": 0, "state": "ok", "no_result": True},
                 "a worse state with no worse figure": {"exit": 1, "state": "worse", "figures": HOLDS}}
        for why, plan in cases.items():
            g, w = self.go({"d3d12": plan}, *NATIVE)
            row = w.row(g, PC)
            self.assertEqual(row["state"], "owed", why)
            self.assertTrue(row["detail"].startswith("the measure could not run: "), "%s: %s" % (why, row["detail"]))
            self.assertEqual((g.verdict, len(w.faults)), ("pass", 1), why)

    def test_the_time_limit_is_owed(self):
        with Gates(self) as w:
            w.measure({"d3d12": {"exit": 0, "state": "ok", "figures": HOLDS, "seconds": 30}})
            gate.measure_limit = lambda tablet, wait=0.0: 1
            g, _ = w.land(w.plan(*ENGINE_ONLY))
            row = w.row(g, PC)
            self.assertEqual((row["state"], row["detail"]), ("owed", "the measure could not run: its time limit"))
            self.assertEqual(len(w.faults), 1)
            self.assertEqual(g.verdict, "pass")

    def test_the_limits_follow_the_long_levels_rule(self):
        # A route of three minutes measured at most twice with two minutes for each start and end; the PC adds five minutes of waiting for
        # a quiet machine, the tablet its lock's wait and five minutes for the install and removal.
        self.assertEqual(gate.measure_limit(False), 15 * 60)
        self.assertEqual(gate.measure_limit(True, 30), (10 + 30 + 5) * 60)

    def test_one_fault_line_for_a_check(self):
        with Gates(self) as w:
            g = gate.Gate(gate.parser().parse_args(w.argv()))
            gate.measure_fault(g, PC, "TOOL", "first")
            gate.measure_fault(g, PC, "TOOL", "second")
            gate.measure_fault(g, TABLET, "ENV", "third")
            self.assertEqual([(f[3], f[4], f[5]) for f in w.faults], [(PC, PC, "first"), (TABLET, TABLET, "third")])


class Where(unittest.TestCase):
    """Where the two checks sit."""

    def run_plan(self, paths, setup=None, tablet="T", also=(), slow=None):
        with Gates(self) as w:
            self.w = w
            w.measure({"d3d12": {"exit": 0, "state": "ok", "figures": HOLDS, "seconds": 0.3},
                       "tablet": {"exit": 0, "state": "ok", "figures": HOLDS, "seconds": 0.4}})
            w.slow = dict(slow or {})
            if setup:
                setup(w)
            g, args = w.land(w.plan(*paths), also, tablet)
            return g, w, args

    def test_pc_measure_runs_last(self):
        slow = {"tour/d3d12": 1.0, "build/android": 0.3, "tablet/prepare": 0.5, "replays/d3d12": 0.2}
        g, w, _ = self.run_plan(WITH_TABLET, slow=slow)
        pc = [c for c in w.runs() if c["run"] == "d3d12"]
        self.assertEqual(len(pc), 1, w.runs())
        others = [e for n, b, e in w.times]
        self.assertGreaterEqual(pc[0]["start"], max(others), "after every other check ended")
        tablet = [c for c in w.runs() if c["run"] == "tablet"][0]
        self.assertGreaterEqual(pc[0]["start"], tablet["end"], "after the tablet's measure too")
        self.assertEqual(w.row(g, PC)["state"], "pass")
        self.assertEqual(g.results[-1]["check"], PC, "its row is the last: %s" % [r["check"] for r in g.results])

    def test_pc_measure_is_skipped_when_a_check_failed(self):
        g, w, _ = self.run_plan(NATIVE, setup=lambda w: w.fail.add("tour/d3d12"))
        self.assertIsNone(w.row(g, PC), "no row")
        self.assertEqual([c["run"] for c in w.runs()], ["tablet"], "the PC's measure was not started; the tablet's ran in its lane before")
        self.assertEqual(g.verdict, "fail")

    def test_pc_measure_is_skipped_after_a_failed_android_build_and_so_is_the_tablet(self):
        g, w, _ = self.run_plan(WITH_TABLET, setup=lambda w: w.fail.add("build/android"))
        self.assertIsNone(w.row(g, TABLET), "no tablet measure when build/android failed")
        self.assertIsNone(w.row(g, PC))
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
        self.assertEqual({c["run"] for c in w.runs()}, {"d3d12", "tablet"})

    def test_no_tablet_named_is_owed_and_nothing_starts(self):
        g, w, _ = self.run_plan(NATIVE, tablet="")
        row = w.row(g, TABLET)
        self.assertEqual(row["state"], "owed")
        self.assertIn("no tablet is named", row["detail"])
        self.assertEqual([c["run"] for c in w.runs()], ["d3d12"])
        self.assertEqual(g.verdict, "pass")
        self.assertEqual(w.faults, [], "a tablet that is away is no tool fault")

    def test_a_tablet_that_does_not_answer_is_owed(self):
        g, w, _ = self.run_plan(NATIVE, setup=lambda w: setattr(w, "answers", False))
        row = w.row(g, TABLET)
        self.assertEqual(row["state"], "owed")
        self.assertIn("does not answer adb", row["detail"])
        self.assertEqual([c["run"] for c in w.runs()], ["d3d12"])
        self.assertEqual(g.verdict, "pass")

    def test_the_plan_decides_what_is_measured(self):
        cases = [(NATIVE, True, True), (ENGINE_ONLY, True, False), (("tests/replays/x.tsv",), True, False),
                 (("platform/android/app/build.gradle",), False, False), (("tools/gate/x.py",), False, False),
                 (("crates/nebula-import/src/lib.rs",), False, False), (("docs/plan/x.md",), False, False)]
        for paths, pc, tablet in cases:
            plan = scope.plan_for(list(paths), TABLE)
            self.assertEqual(("measure_pc" in plan.groups, "measure_tablet" in plan.groups), (pc, tablet), paths)
            g, w, _ = self.run_plan(paths)
            self.assertEqual((w.row(g, PC) is not None, w.row(g, TABLET) is not None), (pc, tablet), (paths, g.printed))
            self.assertEqual({c["run"] for c in w.runs()}, ({"d3d12"} if pc else set()) | ({"tablet"} if tablet else set()), paths)
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
        pc, tablet = by["d3d12"], by["tablet"]
        for a, want in ((pc, [("--runs", "d3d12"), ("--pending", str(g.work / "measure-pc.tsv")), ("--work", str(g.work / "measure-pc")),
                              ("--quiet-minutes", "5"), ("--commit", g.commit)]),
                        (tablet, [("--runs", "tablet"), ("--device", "T"), ("--device-lock", str(w.t / "tablet.lock")),
                                  ("--device-wait", "7"), ("--android-bin", g.android_bin), ("--apk", str(w.build / "android" / "Nebula.apk")),
                                  ("--pak", "X"), ("--pending", str(g.work / "measure-tablet.tsv")),
                                  ("--work", str(g.work / "measure-tablet"))])):
            for option, value in want:
                self.assertEqual(a[a.index(option) + 1], value, "%s of %s" % (option, a[:4]))
        self.assertNotIn("--gate-lock", pc, "the landing holds the gate lock")
        self.assertNotIn("--gate-lock", tablet)
        self.assertNotIn("--device", pc)
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
            w.measure({"d3d12": {"exit": 1, "state": "worse", "figures": WORSE}})
            g, _ = w.land(w.plan(*ENGINE_ONLY))
            names = [r["check"] for r in g.results]
            self.assertEqual(len(names), len(set(names)), "no check ran twice")
            self.assertEqual([c["run"] for c in w.runs()], ["d3d12"], "the measure ran once: it measured twice itself")
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


class Also(unittest.TestCase):
    """--also by name."""

    CHECKS = {"stress": "stress/engine/d3d12", "sanitizer/tour": "tour-asan/d3d12", "soak": "soak", "soak/long": "soak/long"}

    def run_also(self, names, setup=None, paths=("docs/plan/x.md",)):
        with Gates(self) as w:
            w.measure({"d3d12": {"exit": 0, "state": "ok", "figures": HOLDS, "seconds": 0.2},
                       "tablet": {"exit": 0, "state": "ok", "figures": HOLDS, "seconds": 0.2}})
            if setup:
                setup(w)
            g, args = w.land(w.plan(*paths), names)
            return g, w

    def test_also_names_are_planned(self):
        for name, check in self.CHECKS.items():
            g, w = self.run_also([name])
            self.assertIsNotNone(w.row(g, check), "%s: %s" % (name, [r["check"] for r in g.results]))
            self.assertEqual(w.row(g, check)["state"], "pass", name)
        self.assertEqual(sorted(gate.ALSO_PARTS), ["replays/software", "sanitizer/tour", "soak", "soak/long", "stress"])

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
        self.assertEqual(w.row(g, "tour-asan/d3d12")["state"], "pass")

    def test_they_run_after_the_needed_checks_and_before_the_measure_one_after_another(self):
        slow = {"engine/vulkan": 0.6, "pictures/engine/d3d12": 0.2}
        g, w = self.run_also(["sanitizer/tour", "soak"], setup=lambda w: w.slow.update(slow), paths=ENGINE_ONLY)
        also_names = ("build/windows-asan", "tour-asan/d3d12", "soak", "soak-process")
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
        with Gates(self) as w:
            args = gate.parser().parse_args(w.argv())
            why = gate.also_problem(args, ["replays/software"], {}, {})
            self.assertEqual(why, "--also replays/software: this check is not built yet")
            self.assertEqual(gate.also_problem(args, ["soak", "soak/long"], {}, {}), "--also: soak and soak/long are one soak at two lengths; ask for one")
            self.assertEqual(gate.also_problem(args, [n for n in gate.ALSO_PARTS if n not in ("replays/software", "soak/long")], {}, {}), "")
            unknown = gate.also_problem(args, ["tour/d3d99", "soak"], {}, {})
            self.assertTrue(unknown.startswith("--also names no check the gate knows: tour/d3d99."), unknown)
            self.assertIn("stress", unknown, "the table's names are listed")
            self.assertEqual(gate.also_problem(args, ["coverage"], {}, {}).split(".")[0], "--also names no check the gate knows: coverage",
                             "a name only the long run has is unknown to a landing, no longer a special case")

    def test_the_landing_refuses_before_it_queues_anything(self):
        with Gates(self) as w:
            for name, words in (("replays/software", "--also replays/software: this check is not built yet"),
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
