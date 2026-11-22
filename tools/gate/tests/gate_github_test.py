"""gate_github_test.py: gate.py main() with the GitHub rule of package T8a (tools/gate/github_rule.py), with fakes in place of
GitHub, git and the checks: main() really runs (the plan, the locks, the candidate, the watch, the wait, the move, the end-of-run
lines), and what it would push, read, move or delete is recorded.

    python -X utf8 -I tools/gate/tests/gate_github_test.py [--work FOLDER]

  no_github_in_the_plan             a plan without `github` pushes no candidate, starts no watch, reads nothing, and moves main
  every_must_green_moves            every `must` job green: the candidate is pushed, main moves, the candidate is deleted
  must_running_waits_then_moves     one `must` job still running: main does not move until it is green
  nightly_changes_nothing           a `nightly` job red or running changes nothing
  unknown_job_is_must_with_warning  an unknown job is waited for like a `must` job, and one line of kind tool names it
  red_must_before_checks_refuses    a red `must` job of the commit refuses the run: no candidate, no check, no move
  red_must_during_checks_stops_them the checks are stopped (fail), the log is saved as github-<job>.log with its first 20 lines
                                    printed, and main does not move
  cancelled_once_is_rerun            a cancelled job is run again once, and main moves when it is green
  cancelled_twice_is_env            the same job cancelled again: KIND env, not judged, main does not move, the branch waits
  advisory_red_never_blocks         an advisory job red twice is one line and blocks nothing
  time_limit_is_env_and_the_branch_stays  the limit: KIND env naming the jobs, not judged, no move, the queue keeps the branch
  no_job_finished_is_waited_for     no run yet, then one: waited for, not refused
  setup_step_is_rerun_and_moves     a job red at its checkout is run again once, and main moves when the new run is green
  red_must_refuses_with_the_way_out a red `must` job refuses the start; its log is saved; the message ends with the --github-again line
  github_again_reruns_and_goes_on   --github-again: the red jobs are run again once, each is a tool-fault line of kind ENV with the
                                    job's name and the first line of its log, the run is not refused and judges by the new result
  github_again_refused_still_refuses  a re-run GitHub refuses leaves the red: the run is refused as without the switch
  no_switch_no_rerun                without the switch nothing is run again, nothing is written
  android_alone_needs_no_tablet     a plan with the group `android` and no `tablet12` does not ask for the tablet
  github_check_without_move         a main gate without --move (the same limit as a move: 120 minutes unless --github-wait) reads the same jobs as one of its checks (rows github/<job>)
Exit 0 when every case holds.
"""
import contextlib
import io
import os
import pathlib
import sys
import tempfile
import threading
import time
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402
import github_rule  # noqa: E402
import kinds  # noqa: E402
import landing  # noqa: E402

SHA = "0123456789abcdef0123456789abcdef01234567"
CLASSES = github_rule.read_table()
MUSTS = [n for n, c in CLASSES.items() if c == "must"]
START = "Android start check (x86_64 emulator)"
COUNTER = [5000]


def job(name, state="completed", conclusion="success"):
    COUNTER[0] += 1
    return {"name": name, "status": state, "conclusion": conclusion if state == "completed" else None,
            "completedAt": "2026-11-21T08:30:00Z", "databaseId": COUNTER[0]}


def green(*left_out):
    return [job(n) for n in CLASSES if n not in left_out]


class World:
    """What GitHub says, and what the gate did to it: every call the gate makes lands here."""

    def __init__(self, runs, hook=None):
        self.runs, self.hook = runs, hook
        self.now, self.slept = 1000.0, 0
        self.pushed, self.deleted, self.moved, self.reruns, self.log_asks, self.reads = [], [], [], [], [], 0
        self.moved_after = None
        self.logs = {}

    def commit_runs(self, sha):
        self.reads += 1
        return self.runs

    def sleep(self, seconds):
        self.now += seconds
        self.slept += 1
        if self.hook:
            self.hook(self)

    def rerun(self, run_id, job_id):
        self.reruns.append((run_id, job_id))
        return True, "asked"

    def gh_log(self, job_id):
        self.log_asks.append(job_id)
        return "\n".join("log line %d" % i for i in range(1, 301))


def runs_of(*jobs_lists):
    return [{"id": 100 - i, "branch": "gate/x" if i == 0 else "work/x", "jobs": jobs} for i, jobs in enumerate(jobs_lists)]


def drive(test, world, scope_text="repo,github-first", checks=None, extra=(), allow_github=True, move_why=None, push_why=None):
    """main() of `--main --move --scope scope_text` with the fakes: (exit code, the output's lines, the work folder)."""
    tmp = pathlib.Path(tempfile.mkdtemp())
    test.addCleanup(lambda: __import__("shutil").rmtree(tmp, ignore_errors=True))
    names = ("fault_writer", "git", "git_ok", "read_lists", "run_checks", "push_checks", "push_candidate", "delete_candidate", "commit_runs",
             "gh_rerun", "gh_log", "move_main", "approve_pictures", "start_long", "GITHUB_SLEEP", "GITHUB_CLOCK",
             "WATCH_SECONDS", "GithubWatch", "FLAKES", "SET_ASIDE", "BASE_BINS", "make_rule")
    saved = {n: getattr(gate, n) for n in names}
    measure_before = gate.__dict__.get("start_measure", None)
    saved_keep = kinds.keep_base

    def fake_git(*a):
        if a[:2] == ("rev-parse", "--short"):
            return a[-1] if a[-1] not in ("HEAD",) else SHA[:7]
        if a[:1] == ("rev-parse",):
            return SHA
        return ""

    def fake_run_checks(args, known=None, planned=None, selected=None, dry=False):
        g = gate.Gate(args, dry=dry, known=known, planned=planned, selected=selected)
        if dry:
            g.needs = {"repo/audit": set()}
            return g
        if checks:
            checks(g, world)
        g.record("repo/audit", "pass", 0.1, "ok")
        g.write()
        return g

    def push_candidate(commit, candidate):
        world.pushed.append((commit, candidate))
        return None

    def move_main(sha, run_name, work, board, who, covers, tree=None):
        world.moved.append(sha)
        world.moved_after = world.slept
        return move_why

    def no_github(*a, **k):
        raise AssertionError("GitHub was used by a plan without the group `github`")

    try:
        gate.fault_writer = lambda live: (lambda *fault: getattr(world, "faults", []).append(fault))
        gate.git, gate.git_ok = fake_git, lambda *a: True
        gate.read_lists = lambda: ({}, {}, [])
        gate.run_checks, gate.push_checks = fake_run_checks, lambda sha: push_why
        gate.push_candidate = push_candidate
        gate.delete_candidate = lambda c: world.deleted.append(c)
        gate.gh_rerun, gate.gh_log = world.rerun, world.gh_log
        gate.move_main = move_main
        gate.approve_pictures = lambda g, commit, say=print: (commit, None)
        # A move starts no speed measure (package T13c): a stand-in that records a call, which the tests check is none.
        gate.start_measure = lambda sha, g, args: test.measure_calls.append(sha) or "a measure was started"
        test.measure_calls = []
        gate.start_long = lambda sha, args: (None, "no long run in a test")
        kinds.keep_base = lambda *a, **k: None
        gate.GITHUB_SLEEP, gate.GITHUB_CLOCK, gate.WATCH_SECONDS = world.sleep, (lambda: world.now), 0.02
        gate.FLAKES, gate.SET_ASIDE, gate.BASE_BINS = tmp / "flakes.tsv", tmp / "set-aside.tsv", tmp / "base-bins"
        if allow_github:
            gate.commit_runs = world.commit_runs
        else:
            gate.commit_runs = no_github
            gate.GithubWatch = no_github
            gate.make_rule = no_github
        # The runner's mark (main moves only through gate.py land): this process stands in for the runner holding its lock.
        (tmp / "runner.lock").mkdir()
        (tmp / "runner.lock" / "holder.txt").write_text("landing runner pid %d 2026-11-21T11:00:00\n" % os.getpid(), encoding="utf-8")
        argv = ["--main", "--move", "--scope", scope_text, "--work", str(tmp / "work"), "--board", "", "--status", "",
                "--gate-lock", str(tmp / "gate.lock"), "--long-lock", str(tmp / "long.lock"), "--wait", "0",
                "--build-root", str(tmp / "build"), "--live", str(tmp / "live"),
                "--landing-runner", str(os.getpid()), "--runner-lock", str(tmp / "runner.lock")] + list(extra)
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = gate.main(argv)
    finally:
        for n, v in saved.items():
            setattr(gate, n, v)
        kinds.keep_base = saved_keep
        if measure_before is None:
            gate.__dict__.pop("start_measure", None)
        else:
            gate.start_measure = measure_before
        gate.STOPPED.clear()
        gate.STOPPED_WHY[0] = ""
    return code, out.getvalue().splitlines(), tmp / "work"


def test_measures(test):
    return test.measure_calls


def kind_lines(lines):
    return [l for l in lines if l.startswith("KIND ")]


class Rule(unittest.TestCase):
    def test_no_github_in_the_plan(self):
        world = World(None)
        code, lines, _ = drive(self, world, "repo", allow_github=False)
        self.assertEqual(code, 0, "\n".join(lines))
        self.assertEqual((world.pushed, world.deleted, world.reads, world.slept), ([], [], 0, 0))
        self.assertEqual(world.moved, [SHA], "main moves when its own checks pass")
        self.assertIn("MOVED " + SHA, lines)
        self.assertEqual(test_measures(self), [], "a move starts no speed measure")
        self.assertFalse(any("measure" in l.lower() for l in lines), "and says nothing of one: %s" % lines)
        self.assertEqual(lines[-1], "VERDICT pass")

    def test_not_moved_ends_not_judged_and_fail_only_for_a_red_job_of_github(self):
        # Every check passed and main did not move: `fail` (exit 1) is the change's fault, so only a red job of GitHub; main that moved, the
        # push, a lock, GitHub not finishing judged nothing: not-judged, exit 2. The landing reads the NOT-MOVED line, so its result is the same.
        cases = (("main moved during the gate (now 123456789): rebase on it and gate again", "main-moved", 2, "not-judged"),
                 ("the push of main failed: ! [rejected] main -> main", "other", 2, "not-judged"),
                 ("GitHub could not be reached for the push of main (6 tries in 8 minutes): fatal: unable to access", "env", 2, "not-judged"),
                 ("GitHub did not finish: Mac (no result after 120 minutes)", "env", 2, "not-judged"),
                 ("the gate lock D:/x/move.lock is held: someone", "other", 2, "not-judged"),
                 ("GitHub's jobs on gate/x: red: Mac failure on gate/x at 10:34", "github", 1, "fail"),
                 ("the pre-push hook refuses main (a repository rule): pre-push: refs/heads/main refused: the console census", "hook", 1, "fail"))
        for why, kind, want_code, want_verdict in cases:
            world = World(None)
            code, lines, _ = drive(self, world, "repo", allow_github=False, move_why=why)
            self.assertEqual((code, lines[-1]), (want_code, "VERDICT " + want_verdict), "%s\n%s" % (why, "\n".join(lines)))
            self.assertIn("NOT-MOVED %s %s" % (kind, why), lines)
            result = gate.parse_run_output([l + "\n" for l in lines], code)
            self.assertEqual((result.verdict, result.moved, result.why_kind, result.why), ("pass", "", kind, why), "the landing's reading")
            if kind == "hook":
                self.assertTrue(any(l.startswith("KIND fail pre-push - :: the pre-push hook refuses main") for l in lines), "the hook's refusal is a KIND line")
                self.assertEqual([f[:2] for f in landing.failures_of_lines(kind_lines(lines))], [("pre-push", "fail")])
            else:
                self.assertEqual(landing.failures_of_lines(kind_lines(lines)), [], "no failure of a check")

    def test_a_rule_of_the_repository_that_refuses_before_the_run_is_a_fail_with_the_checks_lines_not_a_not_judged(self):
        # T11-B (step S0.1.3): the census refused main's move at the start of the run and the landing was "not judged" (code 4): land once more.
        why = "the console census refuses it (a closed group is not zero, or an open one is over its ceiling): fifo 31 closed; files: 11 native/include/x.h"
        world = World(None)
        code, lines, _ = drive(self, world, "repo", allow_github=False, push_why=why)
        self.assertEqual((code, lines[-1]), (1, "VERDICT fail"), "\n".join(lines))
        self.assertIn("KIND fail repo/code - :: " + why, lines)
        self.assertEqual(world.moved, [], "main was not asked to move")
        result = gate.parse_run_output([l + chr(10) for l in lines], code)
        self.assertEqual(result.verdict, "fail")
        self.assertEqual([f[:2] for f in result.failures], [("repo/code", "fail")])
        self.assertIn("fifo 31 closed", result.failures[0][2])
        ratchet = "the native ratchet refuses it: PUSH REFUSED"
        code, lines, _ = drive(self, World(None), "repo", allow_github=False, push_why=ratchet)
        self.assertEqual((code, lines[-1]), (1, "VERDICT fail"))
        self.assertIn("KIND fail push/ratchet - :: " + ratchet, lines)

    def test_every_must_green_moves(self):
        world = World(runs_of(green()))
        code, lines, _ = drive(self, world)
        self.assertEqual(code, 0, "\n".join(lines))
        self.assertEqual([c for c, _ in world.pushed], [SHA])
        self.assertTrue(world.pushed[0][1].startswith("gate/") and world.pushed[0][1].endswith(SHA[:9]))
        self.assertEqual((world.moved, world.deleted), ([SHA], [world.pushed[0][1]]))
        self.assertEqual(lines[-1], "VERDICT pass")
        self.assertTrue(any(l.startswith("github ") and "green" in l for l in lines), "the run shows the GitHub row")

    def test_the_last_summary_line_on_the_screen_is_the_one_in_result_txt(self):
        world = World(runs_of(green()))
        code, lines, work = drive(self, world)
        self.assertEqual(code, 0, "\n".join(lines))
        heads = [l for l in lines if l.startswith("gate ")]
        written = next(p for p in work.rglob("result.txt"))
        in_file = next(l for l in written.read_text(encoding="utf-8").splitlines() if l.startswith("gate "))
        self.assertEqual(heads[-1], in_file, "the same checks and seconds: %s" % heads)
        self.assertIn("(2 checks:", in_file, "the GitHub row is counted")
        self.assertNotEqual(heads[0], in_file, "the first line was written before the GitHub row")

    def test_must_running_waits_then_moves(self):
        def turn(world):
            if world.slept == 2:
                world.runs = runs_of(green())
        world = World(runs_of(green("Mac") + [job("Mac", "in_progress")]), hook=turn)
        code, lines, _ = drive(self, world)
        self.assertEqual(code, 0, "\n".join(lines))
        self.assertEqual((world.moved, world.moved_after), ([SHA], 2), "it moved only after Mac turned green")
        shown = [l for l in lines if l.startswith("gate.py: GitHub: ")]
        self.assertTrue(any("not finished: Mac" in l for l in shown) and shown[-1].endswith("must jobs green"),
                        "the wait prints what it reads: %s" % shown)

    def test_nightly_changes_nothing(self):
        jobs = green("Windows (three renderers, unit tests)", "Windows game modules") + [
            job("Windows (three renderers, unit tests)", conclusion="failure"), job("Windows game modules", "in_progress")]
        world = World(runs_of(jobs))
        code, lines, _ = drive(self, world)
        self.assertEqual((code, world.moved, world.slept, world.reruns), (0, [SHA], 0, []), "\n".join(lines))
        self.assertEqual(kind_lines(lines), [])

    def test_unknown_job_is_must_with_warning(self):
        def turn(world):
            world.runs = runs_of(green() + [job("Mac (Intel)")])
        world = World(runs_of(green() + [job("Mac (Intel)", "in_progress")]), hook=turn)
        code, lines, work = drive(self, world)
        self.assertEqual(code, 0, "\n".join(lines))
        self.assertEqual(world.moved_after, 1, "waited for like a must job")
        told = [l for l in lines if l.startswith("gate.py: GitHub job Mac (Intel) is not in tools/gate/github_jobs.tsv")]
        self.assertEqual(len(told), 1, "named on the screen at the first reading: %s" % lines)
        self.assertIn("Add a line for it", told[0])
        self.assertLess(lines.index(told[0]), next(i for i, l in enumerate(lines) if l.startswith("KIND tool github/Mac (Intel)")))
        log = next(p for p in work.rglob("github.log")).read_text(encoding="utf-8")
        self.assertEqual(len([l for l in log.splitlines() if "Mac (Intel) is not in tools/gate/github_jobs.tsv" in l]), 1, log)
        warn = [l for l in kind_lines(lines) if l.startswith("KIND tool github/Mac (Intel) - :: ")]
        self.assertEqual(len(warn), 1, "one warning line of kind tool: %s" % kind_lines(lines))
        self.assertEqual(lines[-1], "VERDICT pass", "a warning changes no verdict")

    def test_red_must_before_checks_refuses(self):
        world = World(runs_of(green("Mac") + [job("Mac", conclusion="failure")]))
        ran = []
        code, lines, _ = drive(self, world, checks=lambda g, w: ran.append(1))
        self.assertEqual(code, 2, "\n".join(lines))
        self.assertEqual((world.pushed, world.moved, ran), ([], [], []))
        self.assertTrue(any("Mac failure on gate/x" in l for l in lines), "\n".join(lines))
        self.assertEqual(lines[-1], "VERDICT not-judged")

    def test_red_must_during_checks_stops_them(self):
        started = threading.Event()

        def checks(g, world):
            world.runs = runs_of(green("Linux unit tests") + [job("Linux unit tests", conclusion="failure")])
            started.set()
            deadline = time.monotonic() + 20
            while not gate.STOPPED.is_set() and time.monotonic() < deadline:
                time.sleep(0.01)
            g.record("harness/self", "fail" if gate.STOPPED.is_set() else "tool", 0.0, "stopped: " + gate.STOPPED_WHY[0])
        world = World(runs_of(green("Linux unit tests") + [job("Linux unit tests", "in_progress")]))
        code, lines, work = drive(self, world, checks=checks)
        self.assertEqual(code, 1, "\n".join(lines))
        self.assertEqual((world.moved, world.slept), ([], 0), "main does not move and nothing waits")
        runs = [d for d in work.iterdir() if d.is_dir()]
        self.assertEqual(len(runs), 1)
        saved = runs[0] / "github-Linux-unit-tests.log"
        self.assertTrue(saved.is_file(), "the log is saved under the job's name: %s" % sorted(p.name for p in runs[0].iterdir()))
        text = saved.read_text(encoding="utf-8").splitlines()
        self.assertEqual((len(text), text[0], text[-1]), (200, "log line 101", "log line 300"))
        shown = [l for l in lines if l.startswith("  log line ")]
        self.assertEqual(shown, ["  " + l for l in text[:20]], "the first 20 lines are printed")
        at = next(i for i, l in enumerate(lines) if "GitHub job Linux unit tests failure" in l)
        self.assertEqual(lines[at + 2], "  log line 101", "after the line that names the job")
        self.assertTrue(any(l.startswith("KIND fail github/Linux unit tests - ::") for l in lines), kind_lines(lines))
        self.assertEqual(lines[-1], "VERDICT fail")
        self.assertTrue((runs[0] / "github.log").is_file())

    def test_cancelled_once_is_rerun(self):
        def turn(world):
            if world.slept == 1:
                world.runs = runs_of(green())
        mac = job("Mac", conclusion="cancelled")
        world = World(runs_of(green("Mac") + [mac]), hook=turn)
        code, lines, work = drive(self, world)
        self.assertEqual(code, 0, "\n".join(lines))
        self.assertEqual(world.reruns, [(100, mac["databaseId"])], "run again once")
        self.assertEqual(world.moved, [SHA])
        notes = next(work.glob("*/github.log")).read_text(encoding="utf-8")
        self.assertIn("run again once", notes, "github.log says so")

    def test_cancelled_twice_is_env(self):
        mac1, mac2 = job("Mac", conclusion="cancelled"), job("Mac", conclusion="cancelled")

        def turn(world):
            world.runs = runs_of(green("Mac") + [mac2])
        world = World(runs_of(green("Mac") + [mac1]), hook=turn)
        code, lines, _ = drive(self, world)
        self.assertEqual(code, 2, "\n".join(lines))
        self.assertEqual((world.moved, len(world.reruns)), ([], 1))
        self.assertIn("KIND env github/Mac - :: GitHub could not run Mac", lines)
        self.assertEqual(lines[-1], "VERDICT not-judged")
        self.assertTrue(any(l.startswith("NOT-MOVED env ") for l in lines), "\n".join(lines))
        self.assertEqual(world.deleted, [world.pushed[0][1]])

    def test_advisory_red_never_blocks(self):
        red1, red2 = job(START, conclusion="failure"), job(START, conclusion="failure")

        world = World(runs_of(green(START) + [red1]))
        # The must jobs are green, so the wait ends at once; the advisory job is read by the watch while the checks run.
        def checks(g, w):
            deadline = time.monotonic() + 5
            while len(w.reruns) < 1 and time.monotonic() < deadline:
                time.sleep(0.01)
            w.runs = runs_of(green(START) + [red2])
            w.now += 1000
            time.sleep(0.2)
        code, lines, _ = drive(self, world, checks=checks)
        self.assertEqual(code, 0, "\n".join(lines))
        self.assertEqual(world.moved, [SHA])
        self.assertEqual(len(world.reruns), 1, "run again once")
        self.assertIn("KIND env github/%s - :: red twice on GitHub; advisory, blocks nothing" % START, lines)
        self.assertEqual(lines[-1], "VERDICT pass")
        # What the landing makes of that line: no failure, no fault record.
        self.assertEqual(landing.failures_of_lines(kind_lines(lines)), [])

    def test_time_limit_is_env_and_the_branch_stays(self):
        world = World(runs_of(green("Mac", "Linux") + [job("Mac", "in_progress"), job("Linux", "queued")]))
        code, lines, work = drive(self, world, extra=["--github-wait", "5"])
        self.assertEqual(code, 2, "\n".join(lines))
        self.assertEqual(world.moved, [])
        self.assertTrue(any(l.startswith("KIND env github/Mac - :: not finished after 5 minutes") for l in lines), kind_lines(lines))
        self.assertTrue(any(l.startswith("KIND env github/Linux - :: not finished after 5 minutes") for l in lines))
        self.assertEqual(lines[-1], "VERDICT not-judged")
        result = gate.parse_run_output([l + "\n" for l in lines], code)
        # gate.py land requeues a pass that did not move main unless a red job made it a failure of the batch.
        self.assertEqual((result.verdict, result.moved, result.why_kind), ("pass", "", "env"))
        self.assertFalse(result.verdict == "not-judged" or result.why_kind == "github")
        self.assertIn("GitHub did not finish", result.why)
        self.assertEqual(world.deleted, [world.pushed[0][1]])
        self.assertEqual((work / "waiting.tsv").read_text(encoding="utf-8"), "", "no candidate waits any more")

    def test_the_queue_keeps_the_branch(self):
        """The same limit through landing.Landing's own rule: a pass that did not move main, why_kind env, is requeued."""
        res = landing.Result("pass", [], [], "", "", "GitHub did not finish: Mac (no result after 5 minutes)", "env")
        nj = res.verdict == "not-judged" or (res.verdict == "pass" and not res.moved and res.why_kind != "github")
        self.assertTrue(nj)
        red = landing.Result("pass", [], [], "", "", "GitHub's jobs on gate/x: red: Mac failure", "github")
        self.assertFalse(red.verdict == "not-judged" or (red.verdict == "pass" and not red.moved and red.why_kind != "github"))

    def test_no_job_finished_is_waited_for(self):
        def turn(world):
            if world.slept == 1:
                world.runs = runs_of([job(n, "queued") for n in CLASSES])
            if world.slept == 3:
                world.runs = runs_of(green())
        world = World([], hook=turn)
        code, lines, _ = drive(self, world)
        self.assertEqual(code, 0, "\n".join(lines))
        self.assertEqual((world.moved, world.moved_after), ([SHA], 3), "no run yet, then jobs queued: waited for, not refused")

    def test_setup_step_is_rerun_and_moves(self):
        def turn(world):
            if world.slept == 1:
                world.runs = runs_of(green())
        bad = dict(job("Mac", conclusion="failure"), steps=[{"name": "Run actions/checkout@v7", "number": 2, "conclusion": "failure"}])
        world = World(runs_of(green("Mac") + [bad]), hook=turn)
        code, lines, _ = drive(self, world)
        self.assertEqual(code, 0, "\n".join(lines))
        self.assertEqual(world.reruns, [(100, bad["databaseId"])])
        self.assertEqual(world.moved, [SHA])

    def test_red_must_refuses_with_the_way_out(self):
        world = World(runs_of(green("Mac") + [job("Mac", conclusion="failure")]))
        code, lines, work = drive(self, world)
        self.assertEqual(code, 2, "\n".join(lines))
        self.assertEqual((world.pushed, world.moved, world.reruns), ([], [], []))
        saved = list(work.glob("github-red-*/github-Mac.log"))
        self.assertEqual(len(saved), 1, "the red job's log is saved")
        way = "If github-Mac.log shows a download or an install failing, and not the game's own build or tests: land again with --github-again."
        at = max(i for i, l in enumerate(lines) if "main could not move to" in l)
        self.assertEqual(lines[at + 1], way, "the message ends with the way out")

    def test_github_again_reruns_and_goes_on(self):
        mac = job("Mac", conclusion="failure")
        world = World(runs_of(green("Mac") + [mac]))
        world.faults = []

        def turn(w):
            if w.slept == 1:
                w.runs = runs_of(green())
        world.hook = turn
        code, lines, work = drive(self, world, extra=["--github-again"])
        self.assertEqual(code, 0, "\n".join(lines))
        self.assertEqual(world.reruns, [(100, mac["databaseId"])], "run again once")
        self.assertEqual(world.moved, [SHA], "the landing judged by the new result")
        self.assertEqual(len(world.faults), 1)
        agent, step, kind, check, test, message, folder = world.faults[0]
        self.assertEqual((kind, check), ("ENV", "github/Mac"))
        self.assertIn("Mac", message)
        self.assertIn("log line 101", message, "the first line of its saved log")
        self.assertTrue((pathlib.Path(folder) / "github-Mac.log").is_file())
        self.assertTrue(any("run again once, at the request of --github-again" in l for l in
                            next(work.glob("*/github.log")).read_text(encoding="utf-8").splitlines()), "github.log has it")

    def test_github_again_refused_still_refuses(self):
        world = World(runs_of(green("Mac") + [job("Mac", conclusion="failure")]))
        world.faults = []
        world.rerun = lambda run_id, job_id: (False, "refused")
        code, lines, _ = drive(self, world, extra=["--github-again"])
        self.assertEqual(code, 2, "\n".join(lines))
        self.assertEqual((world.faults, world.moved, world.pushed), ([], [], []))
        self.assertTrue(any("could not be run again" in l for l in lines))

    def test_no_switch_no_rerun(self):
        world = World(runs_of(green("Mac") + [job("Mac", conclusion="failure")]))
        world.faults = []
        code, lines, _ = drive(self, world)
        self.assertEqual((code, world.reruns, world.faults), (2, [], []))

    def test_android_alone_needs_no_tablet(self):
        # No --tablet is given by drive(): a plan with `android` and not `tablet12` must not be refused for it.
        world = World(runs_of(green()))
        code, lines, _ = drive(self, world, scope_text="repo,build,android,github")
        self.assertEqual(code, 0, "\n".join(lines))
        self.assertFalse(any("needs the tablet" in l for l in lines))
        world = World(runs_of(green()))
        code, lines, _ = drive(self, world, scope_text="repo,tablet12,github")
        self.assertEqual(code, 2)
        self.assertTrue(any("needs the tablet" in l for l in lines))

    def test_github_check_uses_the_moves_limit(self):
        self.assertEqual(gate.GITHUB_WAIT_MINUTES, 120)
        self.assertFalse(hasattr(gate, "GITHUB_CHECK_MINUTES"), "no limit of its own any more")
        tmp = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(tmp, ignore_errors=True))
        saved = {n: getattr(gate, n) for n in ("git", "FLAKES", "SET_ASIDE")}
        gate.git = lambda *a: SHA[:7] if "--short" in a else ("" if a[:1] == ("status",) else SHA)
        gate.FLAKES, gate.SET_ASIDE = tmp / "f.tsv", tmp / "a.tsv"
        try:
            for given, minutes in ((None, 120), ("7", 7)):
                argv = ["--main", "--tablet", "T", "--work", str(tmp / "w"), "--board", "", "--status", ""]
                args = gate.parser().parse_args(argv + (["--github-wait", given] if given else []))
                self.assertEqual(args.github_wait, minutes)
                world = World(runs_of(green("Mac") + [job("Mac", "in_progress")]))
                g = gate.Gate(args)
                rule = github_rule.Rule(SHA, CLASSES, world.commit_runs, world.rerun, clock=lambda: world.now, say=lambda s: None)
                start = world.now
                with contextlib.redirect_stdout(io.StringIO()):
                    gate.github_part(g, rule=rule, sleep=world.sleep)
                self.assertGreaterEqual(world.now - start, 60.0 * minutes)
                self.assertLess(world.now - start, 60.0 * minutes + 61)
                self.assertIn("%d minutes" % minutes, g.results[0]["detail"] if g.results[0]["check"] == "github" else "%d minutes" % minutes)
        finally:
            for n, v in saved.items():
                setattr(gate, n, v)

    def test_github_check_without_move(self):
        tmp = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(tmp, ignore_errors=True))
        saved = {n: getattr(gate, n) for n in ("git", "gh_log", "FLAKES", "SET_ASIDE")}
        gate.git = lambda *a: SHA[:7] if "--short" in a else ("" if a[:1] == ("status",) else SHA)
        gate.FLAKES, gate.SET_ASIDE = tmp / "f.tsv", tmp / "a.tsv"
        try:
            args = gate.parser().parse_args(["--main", "--tablet", "T", "--work", str(tmp / "w"), "--board", "", "--status", ""])
            for jobs, kind, name, text in ((green(), "pass", "github", "green"),
                                           (green("Mac") + [job("Mac", conclusion="failure")], "fail", "github/Mac", "Mac failure"),
                                           (green("Mac") + [job("Mac", "in_progress")], "env", "github/Mac", "not finished")):
                world = World(runs_of(jobs))
                gate.gh_log = world.gh_log
                g = gate.Gate(args)
                rule = github_rule.Rule(SHA, CLASSES, world.commit_runs, world.rerun, clock=lambda: world.now, say=lambda s: None)
                with contextlib.redirect_stdout(io.StringIO()):
                    gate.github_part(g, 3, rule=rule, sleep=world.sleep)
                    g.write()
                self.assertEqual([r["check"] for r in g.results], [name], jobs[-1])
                self.assertEqual(g.results[0]["state"], kind)
                self.assertIn(text, g.results[0]["detail"])
                self.assertEqual(g.verdict, {"pass": "pass", "fail": "fail", "env": "not-judged"}[kind])
        finally:
            for n, v in saved.items():
                setattr(gate, n, v)


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("gate_github_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
