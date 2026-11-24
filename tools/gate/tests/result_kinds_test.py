"""result_kinds_test.py: the kinds of result (docs/plan/04-GATE-AND-MAIN.md section 9, tools/gate/kinds.py), without a
game, a build, a graphics card or a tablet. The runner is tests/fake_runner.py: it writes the result.json files the real
one would, from a scenario; the folders under D:/NebulaWork are replaced by temporary ones.

    python -X utf8 -I tools/gate/tests/result_kinds_test.py [--work FOLDER]

Each case below plants one situation and reads the kind back:
  always_fails              a test that fails every time is fail; the run is VERDICT fail, exit 1
  fails_once                a test that fails and passes alone is flake: exit 0, a line in flakes.tsv, its second run
                            takes the first's place
  third_flake_sets_aside    the third flake of a test and renderer in seven days sets it aside; set aside, its fail
                            does not count and the summary names it first
  fails_on_main_too         a test that fails alone and on main's build in the same way is main: exit 0, first line
  main_build_without_pak_path a kept build of main with no pak.path gets the run's before a test is run on it
  judged_fail_is_not_a_tool_line a test the lane's judge fails, the runner having passed it, is fail alone, with no tool line
  fails_on_main_differently the same test failing on main in another way stays fail
  main_check_skipped        the check against main is skipped when the test's folder changed, when it is new, or when
                            no build of main is kept; the test stays fail
  slow_passes_alone         a test too slow in the crowd (env, verdict too-slow) is run again alone like a fail; passing alone it is flake
  slow_again_alone          too slow again alone, or failing alone, it is fail ("too slow even alone: the game ran at N% of full speed")
  runner_exception          a test the runner left no result for is tool (exit 2), and is not run again
  unreadable_result         a result.json that is not JSON, or names no verdict, is tool
  count_differs / zero_cases  a check that ran another number of cases than it declares, or none, is tool
  tablet_away, runner_refused, lock_not_had, other_graphics_card   env, which does not fail a run
  played_again_by_the_runner a test the runner played once more after a fault of the tool's and that then passed is flake, with
                            its line in flakes.tsv, on the tablet too (whose tests the gate never runs again); the third sets it
                            aside; a fault of the tool's on both plays stays tool
  lines_and_verdict_json    the KIND lines and verdict.json say the same
and the rest of the rules: known failures and planned checks keep working, a tablet check is not run again, a check that
raised is tool, no test in ctest is tool, the kinds' order, the exit codes, the two newest builds of main are kept.
"""
import contextlib
import datetime
import inspect
import io
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402
import kinds  # noqa: E402

FAKE = HERE / "fake_runner.py"
RENDERER = "d3d12"
CHECK = "tour/d3d12"


class Scratch:
    """A temporary world: a git repository as the tree under test (a suite of two tests, a and b, committed on main), the
    folders the gate keeps outside the repository, and a scenario for the fake runner."""

    def __init__(self, tmp, tests=("a", "b")):
        self.t = pathlib.Path(tmp)
        self.tree = self.t / "tree"
        self.work = self.t / "work"
        self.build = self.t / "build"
        self.base_bins = self.t / "base-bins"
        self.flakes = self.t / "native" / "flakes.tsv"
        self.aside = self.t / "native" / "set-aside.tsv"
        self.scenario = self.t / "scenario.json"
        self.gates = 0
        self.suite = self.tree / "tests" / "x" / "suite.json"
        for d in (self.build / "bins", self.work):
            d.mkdir(parents=True)
        self.suite.parent.mkdir(parents=True)
        self.suite.write_text(json.dumps({"harness": 1, "tests": list(tests)}), encoding="utf-8")
        for name in tests:
            (self.suite.parent / name).mkdir()
            (self.suite.parent / name / "test.json").write_text(json.dumps({"name": name}), encoding="utf-8")
        self.sh("init", "-q", "-b", "main")
        for k, v in (("user.name", "keepvaibin"), ("user.email", "122556909+keepvaibin@users.noreply.github.com"),
                     ("core.hooksPath", ".git/hooks"), ("commit.gpgsign", "false")):
            self.sh("config", k, v)
        self.sh("add", "-A")
        self.sh("commit", "-q", "-m", "tests")
        self.main_sha = self.sh("rev-parse", "main").stdout.strip()
        self.saved = {k: getattr(gate, k) for k in ("RUNNER", "FLAKES", "SET_ASIDE", "BASE_BINS")}
        gate.RUNNER, gate.FLAKES, gate.SET_ASIDE, gate.BASE_BINS = FAKE, self.flakes, self.aside, self.base_bins
        os.environ["FAKE_RUNNER_SCENARIO"] = str(self.scenario)
        self.plan({})

    def close(self):
        for k, v in self.saved.items():
            setattr(gate, k, v)
        os.environ.pop("FAKE_RUNNER_SCENARIO", None)

    def sh(self, *a):
        return subprocess.run([gate.GIT, "-C", str(self.tree), *a], capture_output=True, text=True)

    def plan(self, tests, **more):
        """The scenario: {test: {phase: outcome}} and more keys (extra, exit, refuse)."""
        self.scenario.write_text(json.dumps(dict(more, tests=tests)), encoding="utf-8")

    def keep_main(self):
        """A kept build of main (the gate keeps one when it moves main)."""
        (self.build / "bins" / "Nebula.exe").write_text("x", encoding="utf-8")
        assert kinds.keep_base(self.main_sha, self.build / "bins", self.base_bins) is None

    def gate(self, level="main", known=None, extra=()):
        os.environ["NEBULA_PLAN_NO_PUSH"] = "1"     # the repair a third flake opens runs plan.py, which must push no progress branch
        args = gate.parser().parse_args(["--" + level, "--tablet", "T", "--work", str(self.work), "--board", "",
                                         "--status", "", "--build-root", str(self.build), "--paks", "", "--keep-runs",
                                         "--wait", "0", "--gate-lock", str(self.t / "gate.lock"),
                                         "--queue-file", str(self.t / "landing-queue.tsv"), "--runner-lock", str(self.t / "runner.lock"),
                                         "--live", str(self.t / "live"), *extra])
        g = gate.Gate(args, known=known)
        g.tree, g.main_ref = self.tree, "main"
        # A run's folder is named by the second it starts in: every gate of a case has its own.
        self.gates += 1
        g.work = self.work / ("run-%d" % self.gates)
        g.work.mkdir()
        return g

    def run(self, g=None, known=None, check=CHECK, label="tour", renderer=RENDERER, device=None):
        """The suite as a check, then every lane's end (settle), then the run's end: the Gate."""
        g = g or self.gate(known=known)
        with contextlib.redirect_stdout(io.StringIO()):
            g.check(check, gate.harness(g, self.suite, renderer, label, device=device), ())
            g.settle()
            g.write()
        return g

    def flake_lines(self):
        return kinds.read_lines(self.flakes)


def row(g, check=CHECK):
    return next(r for r in g.results if r["check"] == check)


def record_of(g, test, check=CHECK):
    return next(r for r in row(g, check)["records"] if r["test"] == test)


def phase_of(scratch, g, test, where="runs-tour-d3d12"):
    folder = g.work / where / "runs" / "x" / test / RENDERER / "phase.txt"
    return folder.read_text(encoding="utf-8") if folder.is_file() else None


def kind_line_tuples(lines):
    return [kinds.parse_kind_line(l) for l in lines]


# --- the cases ----------------------------------------------------------------------------------------------------------


def always_fails(s, check):
    s.keep_main()
    s.plan({"a": {"first": "fail:boom", "again": "fail:boom", "main": "pass"}})
    g = s.run()
    r = record_of(g, "a")
    check(r["kind"] == "fail" and r["counts"] and row(g)["state"] == "fail", "always fails: kind fail, it counts: %s" % r)
    check(g.verdict == "fail" and g.code == 1, "always fails: VERDICT fail, exit 1: %s %s" % (g.verdict, g.code))
    check(phase_of(s, g, "a") == "first" and phase_of(s, g, "b") == "first", "a test that stayed fail keeps its first run")
    check((g.work / "main" / "tour-d3d12" / "runs" / "x" / "a" / RENDERER / "phase.txt").is_file(),
          "it was run on main's build, which passed it: the change's fault")
    check(not s.flake_lines(), "no flake line for a fail that stayed")


def fails_once(s, check):
    s.keep_main()
    s.plan({"a": {"first": "fail:boom", "again": "pass"}})
    g = s.run()
    r = record_of(g, "a")
    check(r["kind"] == "flake" and not r["counts"] and row(g)["state"] == "flake", "fails once: kind flake, it does not count: %s" % r)
    check(g.verdict == "pass" and g.code == 0, "fails once: VERDICT pass, exit 0: %s %s" % (g.verdict, g.code))
    lines = s.flake_lines()
    check(len(lines) == 1 and lines[0][1:5] == ["a", RENDERER, g.commit, "boom"] and kinds.parse_time(lines[0][0]) is not None,
          "fails once: one line in flakes.tsv (time, test, renderer, commit, first message): %s" % lines)
    check(phase_of(s, g, "a") == "again", "the second run takes the first's place for the checks that read the runs")
    check((g.work / "rerun" / "first" / "tour-d3d12" / "a" / RENDERER / "phase.txt").read_text(encoding="utf-8") == "first",
          "the first run is kept in rerun/first")
    check(phase_of(s, g, "b") == "first", "a test that passed is not run again")
    check(not (g.work / "main").exists(), "a flake is not run on main's build")
    check("a flake" in row(g)["detail"], "the row says it: %s" % row(g)["detail"])


def third_flake_sets_aside(s, check):
    s.keep_main()
    now = datetime.datetime.now()
    ago = lambda days: (now - datetime.timedelta(days=days)).isoformat(timespec="seconds")  # noqa: E731
    kinds.append_line(s.flakes, (ago(1), "a", RENDERER, "c1", "m1"))
    kinds.append_line(s.flakes, (ago(10), "a", RENDERER, "c0", "too old"))
    kinds.append_line(s.flakes, (ago(2), "a", "vulkan", "c2", "another renderer"))
    s.plan({"a": {"first": "fail:boom", "again": "pass"}})
    g = s.run()
    check(("a", RENDERER) not in kinds.read_aside(s.aside) and len(g.open_rows) == 0,
          "two flakes in seven days (one older, one on another renderer) do not set it aside")
    kinds.append_line(s.flakes, (ago(3), "a", RENDERER, "c3", "m3"))
    g = s.run(g=s.gate())
    check(("a", RENDERER) in kinds.read_aside(s.aside), "the third flake in seven days sets the test aside: %s" % s.aside)
    check([(f["test"], f["renderer"]) for f in g.flake_results] == [("a", RENDERER)] and not g.open_rows,
          "one repair is opened for it (the gate's flake results), and no ledger row is asked for: %s %s" % (g.flake_results, g.open_rows))
    # Set aside: it still runs and is reported, it does not count.
    s.plan({"a": {"first": "fail:boom", "again": "fail:boom"}})
    g = s.run(g=s.gate())
    r = record_of(g, "a")
    check(r["kind"] == "fail" and not r["counts"] and r["note"] == "set aside" and row(g)["state"] == "known",
          "set aside: reported as fail, does not count: %s %s" % (r, row(g)["state"]))
    check(g.verdict == "pass" and g.code == 0, "set aside: the run still passes")
    check(phase_of(s, g, "a") == "first" and not (g.work / "rerun").exists(), "set aside: it still runs, but is not run again")
    text = (g.work / "result.txt").read_text(encoding="utf-8").splitlines()
    check(text[0].startswith("set aside") and "a %s" % RENDERER in text[0], "the summary names it first: %s" % text[:2])
    check(any(l.startswith("KIND fail %s a ::" % CHECK) and "[set aside]" in l for l in g.lines), "and it is a KIND line: %s" % g.lines)


def fails_on_main_too(s, check):
    s.keep_main()
    s.plan({"a": {"first": "fail:boom", "again": "fail:boom", "main": "fail:boom"}})
    g = s.run()
    r = record_of(g, "a")
    check(r["kind"] == "main" and not r["counts"] and row(g)["state"] == "main", "fails on main too: kind main: %s" % r)
    check(g.verdict == "pass" and g.code == 0, "fails on main too: the run is not failed: %s %s" % (g.verdict, g.code))
    check(any("main is at fault: a on %s" % RENDERER in t for t in g.open_rows), "and a row is asked for: %s" % g.open_rows)
    check(g.lines and g.lines[0].startswith("KIND main %s a ::" % CHECK), "and it is printed first: %s" % g.lines)
    # With another test that fails for real: main's line is still first.
    s.plan({"a": {"first": "fail:boom", "again": "fail:boom", "main": "fail:boom"},
            "b": {"first": "fail:bang", "again": "fail:bang", "main": "pass"}})
    g = s.run(g=s.gate())
    check(g.verdict == "fail" and g.code == 1 and g.lines[0].startswith("KIND main ") and g.lines[1].startswith("KIND fail "),
          "main's line first, then the change's fail: %s" % g.lines)


def main_build_without_pak_path(s, check):
    # A kept build of main made by a run that found no game data (a documents landing) has no pak.path: the run that needs it
    # gives it the data it found, and the test is run on main's build (T11-B).
    s.keep_main()
    kept = kinds.base_of(s.base_bins, s.main_sha)
    check(not (kept / "pak.path").exists(), "the kept build has no pak.path to begin with")
    s.plan({"a": {"first": "fail:boom", "again": "fail:boom", "main": "fail:boom"}})
    g = s.run(g=s.gate(extra=("--pak", "D:/data/game.pak")))
    check((kept / "pak.path").is_file() and (kept / "pak.path").read_text(encoding="ascii").strip() == "D:/data/game.pak",
          "the kept build was given the run's pak.path before the test ran on it")
    check(record_of(g, "a")["kind"] == "main", "and the test is main: %s" % record_of(g, "a"))


def judged_fail_is_not_a_tool_line(s, check):
    # A lane's own judge (validate, stress) fails a test the runner passed: the check says fail, and no `tool` line goes with it (T11-B).
    s.plan({})
    g = s.gate()
    with contextlib.redirect_stdout(io.StringIO()):
        g.check(CHECK, gate.harness(g, s.suite, RENDERER, "tour", judge=lambda source, work, results: dict(results, a=("fail", "the judge"))), ())
        g.settle()
        g.write()
    check(record_of(g, "a")["kind"] == "fail" and g.verdict == "fail", "the judged test is fail: %s" % record_of(g, "a"))
    check(not any(r["kind"] == "tool" for r in row(g)["records"]) and not any(l.startswith("KIND tool") for l in g.lines),
          "and no tool line goes with it: %s" % g.lines)
    # The runner itself failing a test while exiting 0 stays tool.
    s.plan({"a": {"first": "fail:boom", "again": "fail:boom", "main": "fail:boom"}}, exit=0)
    g = s.run(g=s.gate())
    check(any(l.startswith("KIND tool") for l in g.lines), "a runner that fails a test and exits 0 is still tool: %s" % g.lines)


def fails_on_main_differently(s, check):
    s.keep_main()
    s.plan({"a": {"first": "fail:boom", "again": "fail:boom", "main": "fail:another way"}})
    g = s.run()
    check(record_of(g, "a")["kind"] == "fail" and g.code == 1,
          "failing on main with another message is not main's fault: %s" % record_of(g, "a"))
    s.plan({"a": {"first": "fail:boom", "again": "fail:boom", "main": "tool"}})
    g = s.run(g=s.gate())
    check(record_of(g, "a")["kind"] == "fail", "a run on main that is tool says nothing: still fail")


def main_check_skipped(s, check):
    s.keep_main()
    s.plan({"a": {"first": "fail:boom", "again": "fail:boom", "main": "fail:boom"},
            "b": {"first": "fail:boom", "again": "fail:boom", "main": "fail:boom"}})
    (s.suite.parent / "a" / "test.json").write_text(json.dumps({"name": "a", "changed": True}), encoding="utf-8")
    g = s.run()
    a, b = record_of(g, "a"), record_of(g, "b")
    check(a["kind"] == "fail" and "not run on main" in a["note"] and "differs" in a["note"],
          "the test's folder changed: the check against main is skipped, it stays fail: %s" % a)
    check(b["kind"] == "main", "a test whose folder is main's is checked on main: %s" % b)
    check(not (g.work / "main" / "tour-d3d12" / "runs" / "x" / "a").exists(), "the skipped test was not run on main")
    check(g.code == 1, "so the run fails")
    # The definition check itself.
    paths = ["tests/x/b"]
    check(kinds.definition_same(gate.GIT, s.tree, "main", paths), "same folder: the same definition")
    check(not kinds.definition_same(gate.GIT, s.tree, "main", ["tests/x/a"]), "an edited file differs")
    (s.suite.parent / "b" / "new.txt").write_text("x", encoding="utf-8")
    check(not kinds.definition_same(gate.GIT, s.tree, "main", paths), "a file main does not have differs")
    check(not kinds.definition_same(gate.GIT, s.tree, "main", ["tests/x/none"]), "a folder main does not have differs")
    check(not kinds.definition_same(gate.GIT, s.tree, "no-such-ref", paths), "an unknown ref says nothing")
    # No kept build of main: skipped, with its line.
    shutil.rmtree(s.base_bins)
    (s.suite.parent / "b" / "new.txt").unlink()
    (s.suite.parent / "a" / "test.json").write_text(json.dumps({"name": "a"}), encoding="utf-8")
    g = s.run(g=s.gate())
    check(record_of(g, "b")["kind"] == "fail" and "no kept build of main" in record_of(g, "b")["note"],
          "no kept build for main's commit: skipped with a line, stays fail: %s" % record_of(g, "b"))


def runner_exception(s, check):
    s.plan({"a": {"first": "none"}})
    g = s.run()
    r = record_of(g, "a")
    check(r["kind"] == "tool" and "no result.json" in r["message"], "a runner that raised: tool, with its words: %s" % r)
    check(g.verdict == "not-judged" and g.code == 2, "tool: VERDICT not-judged, exit 2: %s %s" % (g.verdict, g.code))
    check(not (g.work / "runs-tour-d3d12" / "runs" / "x" / "a" / RENDERER / "result.json").exists() and not (g.work / "rerun").exists(),
          "a tool result is not run again")
    check(row(g)["state"] == "tool", "the check's kind is tool: %s" % row(g)["state"])
    # A real exception of the check function itself.
    g = s.gate()
    with contextlib.redirect_stdout(io.StringIO()):
        g.check("script", lambda log: 1 / 0)
        g.write()
    check(row(g, "script")["state"] == "tool" and "ZeroDivisionError" in row(g, "script")["detail"] and g.code == 2,
          "a check that raised: tool, exit 2: %s" % row(g, "script"))
    import errno

    def full(log):
        raise OSError(errno.ENOSPC, "No space left on device")
    g = s.gate()
    with contextlib.redirect_stdout(io.StringIO()):
        g.check("script", full)
        g.write()
    check(row(g, "script")["state"] == "env" and "drive is full" in row(g, "script")["detail"] and g.code == 2,
          "a write that failed because the drive is full: env, not the tool's: %s" % row(g, "script"))


def unreadable_result(s, check):
    s.plan({"a": {"first": "garbage"}})
    g = s.run()
    check(record_of(g, "a")["kind"] == "tool" and "cannot be read" in record_of(g, "a")["message"] and g.code == 2,
          "a result file that is not JSON: tool: %s" % record_of(g, "a"))
    s.plan({"a": {"first": "no-verdict"}})
    g = s.run(g=s.gate())
    check(record_of(g, "a")["kind"] == "tool" and "no verdict" in record_of(g, "a")["message"],
          "a result that names no verdict: tool: %s" % record_of(g, "a"))
    check(kinds.read_result(s.t / "nothing.json", gate.kind_of_verdict)[0] == "tool", "a missing result file: tool")
    f = s.t / "old.json"
    f.write_text(json.dumps({"verdict": "crashed", "reason": "dump x"}), encoding="utf-8")
    check(kinds.read_result(f, gate.kind_of_verdict) == ("fail", "crashed: dump x"),
          "a result with no kind (an older runner) is read through the runner's own mapping")


def count_differs(s, check):
    s.plan({}, extra=["c"])
    g = s.run()
    check(any(r["test"] == "-" and r["kind"] == "tool" and "3 cases, 2 declared" in r["message"] for r in row(g)["records"])
          and g.code == 2, "a check that ran 3 of the 2 cases it declares: tool: %s" % row(g)["records"])
    s.plan({"a": {"first": "none"}, "b": {"first": "none"}})
    g = s.run(g=s.gate())
    check(any(r["test"] == "-" and "no case of the 2 declared" in r["message"] for r in row(g)["records"]) and g.code == 2,
          "a check that ran none of its cases: tool: %s" % row(g)["records"])


def zero_cases(s, check):
    s.suite.write_text(json.dumps({"harness": 1, "tests": []}), encoding="utf-8")
    s.plan({})
    g = s.run()
    check(row(g)["state"] == "tool" and any("lists no case" in r["message"] for r in row(g)["records"]) and g.code == 2,
          "a suite that lists no case: tool, never a pass: %s" % row(g))
    g = s.gate()
    with contextlib.redirect_stdout(io.StringIO()):
        g.write()
    check(row(g, "gate/none")["state"] == "tool" and g.code == 2, "a run in which no check ran is tool too")
    check(gate.counted(0, "x", "no tests were found")[2:] == (None, "tool"), "ctest ran no test: tool")
    check(gate.counted(0, "100% tests passed", "tests passed, 0 tests failed out of 12") == (True, "100% tests passed"),
          "ctest ran 12: pass")
    check(gate.counted(8, "1 test failed", "tests passed, 1 tests failed out of 12")[:2] == (False, "1 test failed"),
          "ctest failed one: a fail, not tool")
    scripts = gate.scripts(s.gate(), [])
    check(scripts(s.t / "none.log")[2:] == (None, "tool"), "no self-test script to run: tool")


def tablet_away(s, check):
    class Away:
        def answers(self):
            return False
    g = s.gate()
    g.android_built = True
    with contextlib.redirect_stdout(io.StringIO()):
        gate.tablet_part(g, gate.QUICK_STOPS, device=Away())
        g.write()
    first = g.results[0]
    check(first["state"] == "owed" and first["records"][0]["kind"] == "env" and not first["records"][0]["counts"],
          "the tablet away: the row stays owed and its kind is env, which does not count: %s" % first)
    check(g.verdict == "pass" and g.code == 0, "the owed tablet part does not change the verdict (main may move)")
    check(any(l.startswith("KIND env tablet/prepare - :: ") and l.endswith("[owed]") for l in g.lines),
          "and it is reported: %s" % g.lines[:1])


def runner_refused(s, check):
    s.plan({}, refuse=True)
    g = s.run()
    check(record_of(g, "a")["kind"] == "env" and record_of(g, "b")["kind"] == "env" and row(g)["state"] == "env",
          "the runner would not start (the device away, a lock held): env: %s" % row(g)["records"])
    check(g.verdict == "not-judged" and g.code == 2, "env: VERDICT not-judged, exit 2: %s %s" % (g.verdict, g.code))
    check("does not answer adb" in record_of(g, "a")["message"], "with the runner's words: %s" % record_of(g, "a")["message"])
    # The runner's own tests of the exit code and the kinds are in tools/lumacore/harness_kind_test.py.
    s.plan({"a": {"first": "env"}})
    g = s.run(g=s.gate())
    check(record_of(g, "a")["kind"] == "env" and g.code == 2, "a test whose disc's game is not given (not-run): env")


def lock_not_had(s, check):
    (s.t / "gate.lock").mkdir()
    (s.t / "gate.lock" / "holder.txt").write_text("another gate", encoding="utf-8")
    g = s.gate(level="long")
    with contextlib.redirect_stdout(io.StringIO()):
        ran = gate.locked_part(g, "tours", lambda: "ran")
        g.write()
    check(ran is None and row(g, "part/tours")["state"] == "env" and g.code == 2,
          "a gate lock not had in time: env, exit 2: %s" % row(g, "part/tours"))


def other_graphics_card(s, check):
    def with_log(lines, code):
        def fake(cmd, log, **kw):
            pathlib.Path(log).write_text("\n".join(lines) + "\n", encoding="utf-8")
            return code
        return fake
    real = gate.run
    try:
        for lines, code, want, why in (
                (["machine  d3d12  approved on X driver 1; these runs: Y driver 2", "pictures: 0 of 0 the same"], 1, "env",
                 "another graphics card or driver than the approved pictures name"),
                (["changed  tour/a/d3d12/p  5 of 100 pixels", "pictures: 0 of 1 the same"], 1, "fail", "a picture that differs"),
                (["machine  d3d12  approved on X", "changed  tour/a/d3d12/p  5 of 100 pixels"], 1, "fail",
                 "a card that differs and a picture that differs"),
                (["pictures: 0 of 0 the same"], 0, "tool", "no picture taken at all"),
                (["same  tour/a/d3d12/p", "pictures: 1 of 1 the same"], 0, "pass", "every picture the same")):
            gate.run = with_log(lines, code)
            g = s.gate()
            with contextlib.redirect_stdout(io.StringIO()):
                g.check("pictures/tour/d3d12", gate.pictures(g, "tour", RENDERER), ())
            check(row(g, "pictures/tour/d3d12")["state"] == want, "%s: %s (got %s)" % (why, want, row(g, "pictures/tour/d3d12")["state"]))
    finally:
        gate.run = real


def lines_and_verdict_json(s, check):
    s.keep_main()
    s.plan({"a": {"first": "fail:boom", "again": "pass"},
            "b": {"first": "fail:bang", "again": "fail:bang", "main": "fail:bang"}})
    g = s.run(known={CHECK: {"zz": {"ledger row": "P-1"}}})
    with contextlib.redirect_stdout(io.StringIO()):
        g.check("repo/x", lambda log: (False, "x broke"))
        g.check("repo/y", lambda log: 1 / 0)
        g.owe("tablet/prepare", "away")
        g.write()
    body = json.loads((g.work / "verdict.json").read_text(encoding="utf-8"))
    from_lines = [t[:3] + (t[3],) for t in kind_line_tuples(g.lines)]
    from_json = [(r["kind"], r["check"], r["test"], r["message"] + (" [%s]" % r["note"] if r["note"] else ""))
                 for r in sorted(body["results"], key=lambda r: (r["kind"] != "main", kinds.RANK[r["kind"]]))]
    check(None not in kind_line_tuples(g.lines) and len(from_lines) == len(from_json) == 5 and sorted(from_lines) == sorted(from_json),
          "the KIND lines and verdict.json say the same:\n    %s\n    %s" % (from_lines, from_json))
    check(body["lines"] == g.lines and body["verdict"] == g.verdict == "fail" and body["exit"] == 1,
          "verdict.json holds the lines, the verdict and the exit code")
    check(kinds.verdict_line(body["verdict"]) == "VERDICT fail", "the last line is VERDICT and the verdict")
    kinds_seen = [t[0] for t in kind_line_tuples(g.lines)]
    check(kinds_seen == sorted(kinds_seen, key=lambda k: (k != "main", kinds.RANK[k])), "main first, then in the kinds' order: %s" % kinds_seen)
    result = json.loads((g.work / "result.json").read_text(encoding="utf-8"))
    check(result["verdict"] == "fail" and result["passed"] is False, "result.json carries the verdict too")
    history = (s.work / "history.tsv").read_text(encoding="utf-8").splitlines()[-1].split("\t")
    check(history[4] == "fail", "history.tsv: the verdict word")
    # main()'s last line and exit code when it refuses to start.
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        code = gate.main(["--main", "--work", str(s.work), "--board", "", "--status", ""])
    check(code == 2 and out.getvalue().splitlines()[-1] == "VERDICT not-judged", "a refused start ends VERDICT not-judged, exit 2")
    check("if args.move and code == 0" in inspect.getsource(gate.run_main) and "kinds.keep_base" in inspect.getsource(gate.run_main),
          "main moves only on a pass, and its build is kept then")


def known_and_planned_still_work(s, check):
    s.plan({"a": {"first": "fail:boom"}})
    g = s.run(known={CHECK: {"a": {"ledger row": "P-1"}}})
    check(row(g)["state"] == "known" and g.code == 0 and "known: a (P-1)" in row(g)["detail"],
          "a listed failure is known and passes the run: %s" % row(g))
    check(phase_of(s, g, "a") == "first" and not (g.work / "rerun").exists(), "a known failure is not run again")
    s.plan({})
    g = s.run(known={CHECK: {"a": {"ledger row": "P-1"}}})
    check(row(g)["state"] == "fail" and "remove the line" in row(g)["detail"] and g.code == 1,
          "a listed test that passes fails the check: %s" % row(g)["detail"])
    g = s.gate()
    g.planned = {"built": {"level": "main", "plan step or ledger row": "N-3", "owner": "x"},
                 "later": {"level": "main", "plan step or ledger row": "N-4", "owner": "x"}}
    with contextlib.redirect_stdout(io.StringIO()):
        g.check("built", lambda log: (True, "it ran"))
        g.plan_rows()
        g.write()
    states = {r["check"]: r["state"] for r in g.results}
    check(states == {"built": "fail", "later": "planned"} and g.code == 1, "planned: a check that runs fails, one not built is planned")


def device_is_not_run_again(s, check):
    s.keep_main()
    s.plan({"a": {"first": "fail:boom", "again": "pass"}})
    g = s.run(device="T")
    check(record_of(g, "a")["kind"] == "fail" and g.code == 1 and not (g.work / "rerun").exists(),
          "a tablet check is not run again (its test app is removed when its part ends): %s" % record_of(g, "a"))


def played_again_by_the_runner(s, check):
    """The runner played a test once more at once after a fault of the tool's (luma_test.py RETRIED) and it passed: flake."""
    for device in (None, "T"):
        s.plan({"a": {"first": "retried:output-missing"}})
        g = s.run(g=s.gate(), device=device)
        r = record_of(g, "a")
        check(r["kind"] == "flake" and not r["counts"] and r["note"] == kinds.PLAYED_AGAIN
              and r["message"].startswith("output-missing: the game ended"),
              "a pass after a first play with a fault of the tool's is flake, its first fault said (device %s): %s" % (device, r))
        check(row(g)["state"] == "flake" and g.verdict == "pass" and g.code == 0,
              "the run passes (device %s): %s %s %s" % (device, row(g)["state"], g.verdict, g.code))
        check(not any((g.work / d).exists() for d in ("rerun", "main")), "the gate runs it no more (device %s)" % device)
    lines = s.flake_lines()
    check(len(lines) == 2 and all(l[1] == "a" and l[4].startswith("output-missing") for l in lines),
          "a line in flakes.tsv for each, the tablet's too: %s" % lines)
    # The third in seven days sets the test aside and opens a repair, as for a flake the gate's second run found.
    g = s.run(g=s.gate(), device="T")
    check(("a", RENDERER) in kinds.read_aside(s.aside) and g.flake_results and g.flake_results[0]["test"] == "a",
          "the third sets it aside: %s %s" % (kinds.read_aside(s.aside), g.flake_results))
    # A planted fault: the tool's fault on both plays is tool (not judged), never a pass or a flake.
    s.plan({"b": {"first": "tool"}})
    g = s.run(g=s.gate(), device="T")
    check(record_of(g, "b")["kind"] == "tool" and g.verdict == "not-judged", "a fault on both plays stays tool: %s" % record_of(g, "b"))
    # The reader: such a result is a pass for every reader and judge (read_result); played_again names it beside the results, and a
    # retried result that is not a pass is not named.
    for name, result in (("p", {"verdict": "pass", "kind": "pass", "reason": ""}),
                         ("t", {"verdict": "output-missing", "kind": "tool", "reason": "x",
                                "retried": {"verdict": "output-missing", "reason": "y"}}),
                         ("f", {"verdict": "pass", "kind": "pass", "reason": "", "retried": {"verdict": "no-exit-record", "reason": ""}})):
        folder = s.t / "read" / "runs" / "x" / name / RENDERER
        folder.mkdir(parents=True)
        (folder / "result.json").write_text(json.dumps(dict(result, test=name)), encoding="utf-8")
    got = {n: kinds.read_result(s.t / "read" / "runs" / "x" / n / RENDERER / "result.json", lambda v: "fail") for n in ("p", "t", "f")}
    check(got == {"p": ("pass", ""), "t": ("tool", "output-missing: x"), "f": ("pass", "")}, "read_result: %s" % got)
    check(kinds.played_again(s.t / "read", "x", RENDERER) == {"f": "no-exit-record"},
          "played_again names the retried pass only: %s" % kinds.played_again(s.t / "read", "x", RENDERER))
    # A lane's judge that fails such a test (a picture or a step hash that differs) makes it fail, never flake: it is judged as any pass.
    cases = kinds.Cases({"f": ("fail", "the picture after differs from its approved copy")}, 1, None, RENDERER, again={"f": "no-exit-record"})
    records, state = kinds.judge_cases(cases)
    check(state == "fail" and records[0]["kind"] == "fail" and records[0]["counts"], "a judge's fail stays fail: %s" % records)


def settle_rules(s, check):
    """The Settler with a fake source: what it does with a second run that is not a pass, one that breaks, a source that
    cannot run on main."""
    class Source:
        def __init__(self, again, main=None, skip=None, broken=False):
            self.again_results, self.main_results, self.skip, self.broken, self.adopted = again, main or {}, skip or {}, broken, []

        def alone(self, names):
            if self.broken:
                raise OSError("disk")
            return {n: self.again_results.get(n, ("pass", "")) for n in names}

        def adopt(self, name):
            self.adopted.append(name)

        def on_main(self, names):
            return {n: self.main_results[n] for n in names if n in self.main_results}, {n: self.skip[n] for n in names if n in self.skip}

    def settle(results, source):
        cases = kinds.Cases(results, len(results), None, RENDERER, source)
        records, _ = kinds.judge_cases(cases)
        said = []
        st = kinds.Settler(s.flakes, s.aside, "abc", say=said.append)
        return st.settle("c", cases, records), st, said

    records, st, said = settle({"a": ("fail", "m")}, Source({"a": ("tool", "broke")}))
    check(records[0]["kind"] == "fail" and records[0]["again"] == ("tool", "broke"), "a second run that is tool leaves the fail")
    records, st, said = settle({"a": ("fail", "m")}, Source({}, broken=True))
    check(records[0]["kind"] == "fail" and any("second run broke" in t for t in said), "a second run that raised leaves the fail: %s" % said)
    source = Source({"a": ("pass", ""), "b": ("fail", "m")}, main={"b": ("fail", "m")})
    records, st, said = settle({"a": ("fail", "m"), "b": ("fail", "m"), "c": ("tool", "t"), "d": ("pass", "")}, source)
    by = {r["test"]: r["kind"] for r in records}
    check(by == {"a": "flake", "b": "main", "c": "tool"} and source.adopted == ["a"],
          "flake, main, and what is not a fail is left alone: %s" % by)
    check(kinds.state_of(records) == "tool", "the check's kind is the worst of its tests: %s" % kinds.state_of(records))
    records, st, said = settle({"a": ("fail", "m")}, Source({"a": ("fail", "m2")}, main={"a": ("fail", "m2")}))
    check(records[0]["kind"] == "main", "the same first message as either of its own runs makes it main")
    records, st, said = settle({"a": ("fail", "m")}, Source({"a": ("fail", "m")}, skip={"a": "no build"}))
    check(records[0]["kind"] == "fail" and any("skipped: no build" in t for t in said), "skipped on main: said, stays fail")


def slow_passes_alone(s, check):
    """A test too slow in the crowd (env, verdict too-slow) is run again alone like a fail; alone it passes: flake."""
    s.keep_main()
    s.plan({"a": {"first": "slow:45", "again": "pass"}})
    g = s.run()
    r = record_of(g, "a")
    check(r["kind"] == "flake" and not r["counts"] and row(g)["state"] == "flake",
          "too slow in the crowd, alone it passes: flake, as a fail that passes alone: %s" % r)
    check(g.verdict == "pass" and g.code == 0, "the run passes: %s %s" % (g.verdict, g.code))
    lines = s.flake_lines()
    check(len(lines) == 1 and lines[0][1] == "a" and "too slow" in lines[0][4], "a line in flakes.tsv: %s" % lines)
    check(phase_of(s, g, "a") == "again", "the second run takes the first's place")
    check(not (g.work / "main").exists(), "it is not run on main's build")


def slow_again_alone(s, check):
    """Too slow in the crowd and too slow alone: fail, with the speed alone; and never env twice."""
    s.keep_main()
    s.plan({"a": {"first": "slow:45", "again": "slow:38", "main": "pass"}})
    g = s.run()
    r = record_of(g, "a")
    check(r["kind"] == "fail" and r["counts"] and r["message"] == "too slow even alone: the game ran at 38% of full speed",
          "too slow again alone: fail with the speed alone: %s" % r)
    check(g.verdict == "fail" and g.code == 1, "the run fails: %s %s" % (g.verdict, g.code))
    check(not s.flake_lines() and not (g.work / "main").exists(), "no flake line, and not run on main's build")
    # A fail alone, after a slow run in the crowd, is a fail too.
    s.plan({"a": {"first": "slow:45", "again": "fail:boom"}})
    g = s.run(g=s.gate())
    r = record_of(g, "a")
    check(r["kind"] == "fail" and r["counts"] and "failed alone" in r["message"] and "boom" in r["message"],
          "a fail alone after too slow: fail: %s" % r)
    # A second run that gives nothing (the machine not fit) leaves it env: not judged, never a pass.
    s.plan({"a": {"first": "slow:45", "again": "env"}})
    g = s.run(g=s.gate())
    r = record_of(g, "a")
    check(r["kind"] == "env" and g.verdict == "not-judged", "a second run that is env leaves env: %s %s" % (r, g.verdict))
    # A source-less check cannot run it again: it stays env.
    check(kinds.is_slow(kinds.record("a", "env", "too-slow: x")) and not kinds.is_slow(kinds.record("a", "env", "not-run: x"))
          and not kinds.is_slow(kinds.record("a", "fail", "too-slow: x")), "is_slow names only a too-slow env result")


def kinds_order_and_exit_codes(s, check):
    check(kinds.worst(["pass", "main", "flake", "env", "tool", "fail"]) == "fail", "the worst of the kinds: fail")
    check([kinds.worst(p) for p in (["pass", "main"], ["main", "flake"], ["flake", "env"], ["env", "tool"], ["known", "pass"], [])]
          == ["main", "flake", "env", "tool", "known", "pass"], "the order: fail, tool, env, flake, main, known, pass")
    rec = kinds.record
    combos = [([[rec("a", "fail")]], "fail", 1), ([[rec("a", "tool")]], "not-judged", 2), ([[rec("a", "env")]], "not-judged", 2),
              ([[rec("a", "tool"), rec("b", "fail")]], "fail", 1), ([[rec("a", "flake", counts=False)]], "pass", 0),
              ([[rec("a", "main", counts=False)]], "pass", 0), ([[rec("a", "fail", counts=False)]], "pass", 0),
              ([[rec("a", "env", counts=False)]], "pass", 0), ([[]], "pass", 0)]
    for records, verdict, code in combos:
        got = kinds.verdict_of(records)
        check(got == verdict and kinds.EXIT_OF[got] == code, "verdict %s, exit %d for %s: got %s" % (verdict, code, records, got))


def base_builds_kept(s, check):
    bins = s.t / "bins"
    bins.mkdir()
    (bins / "Nebula.exe").write_text("x", encoding="utf-8")
    for n, sha in enumerate(("aaa", "bbb", "ccc")):
        check(kinds.keep_base(sha, bins, s.base_bins) is None, "main's build kept for %s" % sha)
        for folder in s.base_bins.iterdir():
            if folder.name == sha:
                os.utime(folder, (1000 + n, 1000 + n))
    kept = sorted(d.name for d in s.base_bins.iterdir())
    check(kept == ["bbb", "ccc"], "the two newest builds of main are kept: %s" % kept)
    check((s.base_bins / "ccc" / "bin" / "Nebula.exe").is_file() and kinds.base_of(s.base_bins, "ccc") == s.base_bins / "ccc" / "bin"
          and kinds.base_of(s.base_bins, "aaa") is None, "found by commit; one that was pruned is not there")


def pictures_and_replays_read_the_settled_runs(s, check):
    """The pictures are checked after the second runs (a flake's second run is the run they read), and a long run waits
    for its soak, which plays beside the lanes, before running anything again alone."""
    names = {}
    for level in ("quick", "main", "long"):
        dry = gate.run_checks(gate.parser().parse_args(["--" + level, "--tablet", "T", "--work", str(s.work)]), {}, {}, dry=True)
        names[level] = dry
        shutil.rmtree(dry.work, ignore_errors=True)
    needs = names["main"].needs
    check(set(needs["pictures/tour/d3d12"]) == {"tour/d3d12"} and set(needs["pictures/engine/vulkan"]) == {"engine/vulkan"}
          and set(needs["pictures/tour-RMGP01/d3d12"]) == {"tour-RMGP01/d3d12"}, "main: each picture check follows its tests: %s" % needs.get("pictures/tour/d3d12"))
    check("pictures/tour/d3d12" in names["quick"].needs and "pictures/tour/d3d12" in names["long"].needs
          and "pictures/engine/d3d12" not in names["long"].needs, "quick and long ask for the pictures they did before")
    source = inspect.getsource(gate.run_checks)
    order = [source.index(x) for x in ('g.check("soak"', "g.settle", "picture_part(g)", "replay_part(g, renderers)")]
    check(order == sorted(order), "the soak is waited for, then the tests run again, then the pictures, then the replays")
    check("picture_part" in inspect.getsource(gate.tour_lane) or "picture_jobs" in inspect.getsource(gate.tour_lane),
          "a tour lane no longer checks its own pictures")


CASES = [always_fails, fails_once, third_flake_sets_aside, fails_on_main_too, main_build_without_pak_path, judged_fail_is_not_a_tool_line, fails_on_main_differently, main_check_skipped,
         runner_exception, unreadable_result, count_differs, zero_cases, tablet_away, runner_refused, lock_not_had,
         other_graphics_card, lines_and_verdict_json, known_and_planned_still_work, device_is_not_run_again, settle_rules,
         slow_passes_alone, slow_again_alone, kinds_order_and_exit_codes, base_builds_kept, pictures_and_replays_read_the_settled_runs]
CASES += [played_again_by_the_runner]


def main():
    failures = []
    for case in CASES:
        found = []
        with tempfile.TemporaryDirectory(prefix="kinds-", ignore_cleanup_errors=True) as tmp:
            s = Scratch(tmp)
            try:
                case(s, lambda ok, what: ok or found.append(what))
            except Exception as error:  # noqa: BLE001 - a case that raised is a failed case, said with its name
                import traceback
                found.append("raised %s: %s\n%s" % (type(error).__name__, error, traceback.format_exc()))
            finally:
                s.close()
        print("%-30s %s" % (case.__name__, "pass" if not found else "FAILED"))
        failures += ["%s: %s" % (case.__name__, f) for f in found]
    if failures:
        print("result_kinds_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("result_kinds_test.py: pass (%d cases)" % len(CASES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
