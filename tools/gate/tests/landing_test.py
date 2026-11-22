"""landing_test.py: the landing queue and its batches (tools/gate/landing.py, `gate.py land`, package T4), without the game, a
build, a graphics card or a tablet.

    python -X utf8 -I tools/gate/tests/landing_test.py [--work FOLDER]

The world is temporary: a bare repository as origin, a clone of it as the tree `gate.py` runs from, the gate's own work folder
made by `git worktree add`, the queue, the runner's lock, the store of the same failure three times and the fault record in
temporary folders. The checks are a fake runner: it looks at the tip's files (a file FAULT fails it, a file ENV is a tool or
machine fault) and on a pass moves origin's main to the tip, as the gate's move does. Each case plants one situation:

  batch_of_three_one_fault          three waiting branches, one fails: the other two land, the third is named failed with its
                                    own KIND lines, in at most three runs when the fault is first or last (four in the
                                    middle: every branch that lands must be proved by a run)
  a_batch_that_passes_is_one_run    three branches that pass land in one run on one tip, history a straight line, each with
                                    the commit of its own last replayed commit
  conflict_is_not_judged            a branch that does not apply is `conflict` with the file's name and is not run; the others
                                    land
  not_judged_lands_nothing          a tool or machine fault lands nothing, leaves every branch waiting, and the caller gets
                                    the tool lines and exit 2; the fault is one line of the fault record
  four_exit_codes                   0 landed, 1 failed, 2 not judged, 3 the same failure three times
  tool_fault_between_is_not_counted a tool fault between two failures does not count and does not reset them
  a_different_failure_starts_again  another first failing check or error line resets the count
  two_callers_at_once               one caller becomes the runner, the other waits, gets its own verdict, and runs nothing
  documents_only_run                a branch of documents only runs the group `repo` and prints its lines (the case prints them)
  groups_of_the_union               the groups a batch runs are those of the union of its changed files
  queue_file                        rows, states, the lock folder, a branch in the queue twice, a stale runner lock
  github_red_fails_the_batch / main_moved_is_not_judged   what a pass that did not move main means
  run_output_is_read                gate.py's lines (KIND, MOVED, NOT-MOVED, result:, VERDICT) become a Result
Exit 0 when every case holds.
"""
import contextlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import time
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent.parent / "plan"))
import failures  # noqa: E402
import gate  # noqa: E402
import kinds  # noqa: E402
import landing  # noqa: E402
import scope  # noqa: E402

GIT = gate.GIT


class FakeChecker:
    """The checks of a batch. A FAULT file in the tip fails it, an ENV file or a TOOL file is not judged, anything else
    passes and moves origin's main to the tip (as gate.move_main does)."""

    def __init__(self, world):
        self.world = world
        self.calls = []
        self.hold = None          # an Event: the run waits for it (to plant a second caller)
        self.entered = threading.Event()
        self.threads = []

    def run(self, tree, tip, names, groups_text, also=""):
        w = self.world
        self.calls.append({"names": list(names), "groups": groups_text, "tip": tip, "tree": str(tree), "also": also})
        self.threads.append(threading.current_thread().name)
        self.entered.set()
        if self.hold is not None:
            assert self.hold.wait(30), "the test did not let the run go on"
        folder = w.t / "runs" / ("run-%d" % len(self.calls))
        folder.mkdir(parents=True)
        tree = pathlib.Path(tree)
        lines, verdict, moved, why_kind, why = [], "pass", "", "", ""
        planted = {k: sorted((tree / k).glob("*")) if (tree / k).is_dir() else [] for k in ("faults", "tool", "env")}
        if planted["faults"]:
            verdict = "fail"
            lines = ["KIND fail tour/d3d12 001-stage-1 :: " + planted["faults"][0].read_text(encoding="utf-8").strip()]
        elif planted["tool"] or planted["env"]:
            kind = "tool" if planted["tool"] else "env"
            verdict = "not-judged"
            lines = ["KIND %s tour/d3d12 - :: %s" % (kind, planted[kind][0].read_text(encoding="utf-8").strip())]
        elif w.github_red:
            lines = []
            why_kind, why = "github", "GitHub's jobs on gate/x: red: Linux unit tests failure"
        elif w.main_moves:
            why_kind, why = "main-moved", "main moved during the gate (now abc): rebase on it and gate again"
        if verdict == "pass" and not why_kind:
            r = subprocess.run([GIT, "-C", str(tree), "push", "-q", "origin", "%s:refs/heads/main" % tip], capture_output=True,
                               text=True)
            assert r.returncode == 0, r.stderr
            subprocess.run([GIT, "-C", str(tree), "fetch", "-q", "origin", "main"], check=True)
            moved = tip
        (folder / "verdict.json").write_text(json.dumps({"verdict": verdict, "lines": lines}), encoding="utf-8")
        return landing.Result(verdict, lines, landing.failures_of_lines(lines), folder, moved, why, why_kind)


class World:
    def __init__(self, tmp):
        self.t = pathlib.Path(tmp)
        self.github_red = False
        self.main_moves = False
        self.origin = self.t / "origin.git"
        self.src = self.t / "src"
        self.tree = self.t / "landing"
        self.plan = self.t / "plan"
        self.plan.mkdir()
        self.lines = []
        subprocess.run([GIT, "init", "-q", "--bare", "-b", "main", str(self.origin)], check=True)
        subprocess.run([GIT, "clone", "-q", str(self.origin), str(self.src)], check=True, capture_output=True)
        for k, v in (("user.name", "keepvaibin"), ("user.email", "122556909+keepvaibin@users.noreply.github.com"),
                     ("commit.gpgsign", "false"), ("core.autocrlf", "false")):
            self.sh("config", k, v)
        self.sh("checkout", "-q", "-b", "main")
        self.write(self.src, {"README.md": "x\n", "shared.txt": "one\ntwo\nthree\n", "docs/a.md": "a\n", "native/g.cpp": "g\n",
                              "tools/gate/scope.tsv": scope.TABLE.read_text(encoding="utf-8")})
        self.sh("add", "-A")
        self.sh("commit", "-q", "-m", "start")
        self.sh("push", "-q", "origin", "main")
        self.checker = FakeChecker(self)
        self.faults = []
        self.store_path = self.plan / "failures.tsv"

    def sh(self, *a, tree=None):
        r = subprocess.run([GIT, "-C", str(tree or self.src), *a], capture_output=True, text=True)
        assert r.returncode == 0, "git %s: %s" % (" ".join(a), r.stderr + r.stdout)
        return r.stdout.strip()

    def write(self, root, files):
        for name, text in files.items():
            p = pathlib.Path(root) / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text, encoding="utf-8", newline="\n")

    def main_sha(self):
        return subprocess.run([GIT, "-C", str(self.origin), "rev-parse", "main"], capture_output=True, text=True).stdout.strip()

    def branch(self, name, files, commits=1, base="origin/main"):
        """A branch of `commits` commits off origin's main, each commit writing the files. A file named FAULT, TOOL or ENV
        plants that in the branch (a file of its own folder: faults/, tool/, env/), which the fake checks read."""
        files = {("%s/%s" % (k.lower() if k != "FAULT" else "faults", name.replace("/", "_")) if k in ("FAULT", "TOOL", "ENV") else k): v
                 for k, v in files.items()}
        self.sh("fetch", "-q", "origin")
        self.sh("checkout", "-q", "-B", name, base)
        for i in range(commits):
            self.write(self.src, {k: (v if commits == 1 else v + "#%d\n" % i) for k, v in files.items()})
            self.sh("add", "-A")
            self.sh("commit", "-q", "-m", "%s commit %d" % (name, i))
        self.sh("checkout", "-q", "main")

    def landing(self, pid=1001, checker=None, poll=0.02, alive=None):
        queue = landing.Queue(self.plan / "landing-queue.tsv", sleep=lambda s: time.sleep(0.01))
        alive = alive or (lambda p: p in (1001, 1002, os.getpid()))
        runner = landing.FolderLock(self.plan / "landing-runner.lock", alive, pid=pid)
        out = []
        L = landing.Landing(queue, checker or self.checker, landing.Repo(GIT), source=self.src, tree=self.tree,
                            store=failures.Store(self.store_path), runner=runner,
                            faults=lambda *a: self.faults.append(a), say=out.append, sleep=lambda s: time.sleep(poll),
                            poll=poll, alive=alive)
        L.out = out
        return L

    def rows(self):
        return {r["branch"]: r for r in landing.Queue(self.plan / "landing-queue.tsv").read()}

    def history(self):
        return self.sh("log", "--format=%s", "main", tree=self.origin).splitlines()


def land(world, branches, **k):
    L = world.landing(**k)
    code = L.land(branches)
    return code, L.out, L


class Cases(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.w = World(self.tmp.name)

    # --- one batch -------------------------------------------------------------------------------------------------

    def test_a_batch_that_passes_is_one_run(self):
        w = self.w
        for n in "abc":
            w.branch("feat/" + n, {"docs/%s.md" % n: n}, commits=2)
        start = w.main_sha()
        code, out, _ = land(w, ["feat/a", "feat/b", "feat/c"])
        self.assertEqual(code, 0, out)
        self.assertEqual(len(w.checker.calls), 1, "one run for the batch")
        self.assertEqual(w.checker.calls[0]["names"], ["feat/a", "feat/b", "feat/c"])
        self.assertEqual(w.history()[:7], ["feat/c commit 1", "feat/c commit 0", "feat/b commit 1", "feat/b commit 0",
                                           "feat/a commit 1", "feat/a commit 0", "start"], "a straight line, in queue order")
        self.assertEqual(w.main_sha(), w.checker.calls[0]["tip"])
        rows = w.rows()
        self.assertEqual({r["state"] for r in rows.values()}, {"landed"})
        self.assertEqual(rows["feat/c"]["commit"], w.main_sha(), "each branch's own last replayed commit")
        self.assertEqual(w.sh("rev-parse", rows["feat/a"]["commit"], tree=w.origin), rows["feat/a"]["commit"])
        self.assertEqual(w.sh("merge-base", "--is-ancestor", start, w.main_sha()) or "", "")
        self.assertEqual(out[-1], "VERDICT pass")

    def plant(self, order, fault):
        w = self.w
        names = []
        for i, n in enumerate(order):
            files = {"docs/%s.md" % n: n}
            if n == fault:
                files["FAULT"] = "boom in %s\n" % n
            w.branch("feat/" + n, files)
            names.append("feat/" + n)
        return names

    def test_batch_of_three_one_fault_last(self):
        w = self.w
        names = self.plant("abc", "c")
        code, out, _ = land(w, names)
        self.assertEqual(code, 1, out)
        self.assertLessEqual(len(w.checker.calls), 3, "at most three runs")
        rows = w.rows()
        self.assertEqual([rows[n]["state"] for n in names], ["landed", "landed", "failed"])
        self.assertEqual(rows["feat/c"]["note"], "tour/d3d12: boom in c")
        self.assertEqual(w.history()[:3], ["feat/b commit 0", "feat/a commit 0", "start"], "the third never reached main")
        self.assertIn("KIND fail tour/d3d12 001-stage-1 :: boom in c", out)
        self.assertEqual(out[-1], "VERDICT fail")
        self.assertIn("LAND feat/c failed tour/d3d12: boom in c", out)

    def test_batch_of_three_one_fault_first(self):
        w = self.w
        names = self.plant("abc", "a")
        code, out, _ = land(w, names)
        self.assertEqual(code, 1)
        self.assertLessEqual(len(w.checker.calls), 3)
        self.assertEqual([w.rows()[n]["state"] for n in names], ["failed", "landed", "landed"])
        self.assertEqual(w.checker.calls[-1]["names"], ["feat/b", "feat/c"], "the rest are run once together")

    def test_batch_of_three_one_fault_in_the_middle(self):
        w = self.w
        names = self.plant("abc", "b")
        code, out, _ = land(w, names)
        self.assertEqual(code, 1)
        self.assertLessEqual(len(w.checker.calls), 4)
        self.assertEqual([w.rows()[n]["state"] for n in names], ["landed", "failed", "landed"])
        self.assertEqual(w.history()[:3], ["feat/c commit 0", "feat/a commit 0", "start"])

    def test_two_faults_in_four(self):
        w = self.w
        names = []
        for n in "abcd":
            files = {"docs/%s.md" % n: n}
            if n in "bd":
                files["FAULT"] = "boom in %s\n" % n
            w.branch("feat/" + n, files)
            names.append("feat/" + n)
        # b and d each carry FAULT in their own commit: a tip that holds either fails.
        land(w, names)
        self.assertEqual([w.rows()[n]["state"] for n in names], ["landed", "failed", "landed", "failed"])

    def test_callers_see_only_their_own_branches(self):
        w = self.w
        names = self.plant("ab", "b")
        L = w.landing()
        L.queue.add(names, "1", L.commit_of)
        code = L.land(["feat/a"])
        self.assertEqual(code, 0, L.out)
        self.assertTrue(any("LAND feat/a landed" in l for l in L.out))
        self.assertFalse(any("feat/b" in l and "LAND" in l for l in L.out), "b's line is b's caller's")
        self.assertEqual(L.out[-1], "VERDICT pass")
        self.assertEqual(w.rows()["feat/b"]["state"], "failed")

    # --- not judged ------------------------------------------------------------------------------------------------

    def test_conflict_is_not_judged(self):
        w = self.w
        w.branch("feat/a", {"shared.txt": "one\nTWO-A\nthree\n"})
        w.branch("feat/b", {"shared.txt": "one\nTWO-B\nthree\n"})
        w.branch("feat/c", {"docs/c.md": "c"})
        code, out, _ = land(w, ["feat/a", "feat/b", "feat/c"])
        rows = w.rows()
        self.assertEqual([rows[n]["state"] for n in ("feat/a", "feat/b", "feat/c")], ["landed", "conflict", "landed"])
        self.assertIn("shared.txt", rows["feat/b"]["note"])
        self.assertEqual(len(w.checker.calls), 1, "the others go on, in one run without it")
        self.assertEqual(w.checker.calls[0]["names"], ["feat/a", "feat/c"])
        self.assertEqual(code, 2, "the caller of a branch that does not apply is not judged")
        self.assertEqual(out[-1], "VERDICT not-judged")

    def test_a_missing_branch_is_conflict(self):
        code, out, _ = land(self.w, ["feat/nothere"])
        self.assertEqual(code, 2)
        self.assertEqual(self.w.rows()["feat/nothere"]["state"], "conflict")
        self.assertEqual(self.w.checker.calls, [])

    def test_not_judged_lands_nothing_and_leaves_the_queue_waiting(self):
        w = self.w
        w.branch("feat/a", {"docs/a.md": "a", "TOOL": "the runner raised: KeyError 'x'\n"})
        w.branch("feat/b", {"docs/b.md": "b"})
        before = w.main_sha()
        code, out, _ = land(w, ["feat/a", "feat/b"])
        self.assertEqual(code, 2, out)
        self.assertEqual(w.main_sha(), before, "nothing landed")
        rows = w.rows()
        self.assertEqual([rows[n]["state"] for n in ("feat/a", "feat/b")], ["waiting", "waiting"])
        self.assertTrue(rows["feat/a"]["note"].startswith("not judged"), rows["feat/a"]["note"])
        self.assertEqual(len(w.checker.calls), 1, "the runner stops: it does not retry what it could not judge")
        self.assertEqual(out[-1], "VERDICT not-judged")
        self.assertTrue(any(l.startswith("KIND tool tour/d3d12 - ::") for l in out))
        self.assertTrue(any("this is a tool fault, not the change's: it does not count as a failure; give the landing once more." in l for l in out))
        self.assertEqual(len(w.faults), 1, "one line of the fault record")
        self.assertEqual(w.faults[0][2:5], ("TOOL", "tour/d3d12", ""))
        self.assertEqual(failures.Store(w.store_path).count("feat/a"), 0)

    def test_an_env_fault_is_not_judged_too(self):
        w = self.w
        w.branch("feat/a", {"docs/a.md": "a", "ENV": "the tablet does not answer\n"})
        code, out, _ = land(w, ["feat/a"])
        self.assertEqual(code, 2)
        self.assertTrue(any("not the change's" in l for l in out), out)
        self.assertEqual(w.faults[0][2], "ENV")

    def test_advisory_github_line_is_no_failure(self):
        """An advisory GitHub job that ended red twice is told in one line (tools/gate/github_rule.py): it is no failure of the
        batch and no tool fault; a job that could not run (env) and a red job (fail) are."""
        lines = ["KIND env github/Android start check (x86_64 emulator) - :: red twice on GitHub; advisory, blocks nothing",
                 "KIND env github/Mac - :: GitHub could not run Mac",
                 "KIND fail github/Linux - :: Linux failure on gate/x; its log is x.log"]
        self.assertEqual([c for c, _, _ in landing.failures_of_lines(lines)], ["github/Mac", "github/Linux"])

    def test_github_red_fails_the_batch(self):
        w = self.w
        w.github_red = True
        w.branch("feat/a", {"docs/a.md": "a"})
        before = w.main_sha()
        code, out, _ = land(w, ["feat/a"])
        self.assertEqual(code, 1, out)
        self.assertEqual(w.main_sha(), before)
        self.assertIn("LAND feat/a failed github: GitHub's jobs on gate/x: red: Linux unit tests failure", out)

    def test_main_moved_during_the_run_is_not_judged(self):
        w = self.w
        w.main_moves = True
        w.branch("feat/a", {"docs/a.md": "a"})
        code, out, _ = land(w, ["feat/a"])
        self.assertEqual(code, 2, out)
        self.assertEqual(w.rows()["feat/a"]["state"], "waiting")
        self.assertIn("main moved during the gate", w.rows()["feat/a"]["note"])
        self.assertEqual(w.faults, [], "not a tool fault: the next landing runs it again")

    # --- the exit codes and the stop -------------------------------------------------------------------------------

    def test_four_exit_codes(self):
        w = self.w
        w.branch("feat/ok", {"docs/ok.md": "x"})
        self.assertEqual(land(w, ["feat/ok"])[0], 0)
        w.branch("feat/bad", {"docs/bad.md": "x", "FAULT": "first\n"})
        self.assertEqual(land(w, ["feat/bad"])[0], 1)
        w.branch("feat/tool", {"docs/tool.md": "x", "TOOL": "broke\n"})
        self.assertEqual(land(w, ["feat/tool"])[0], 2)
        self.assertEqual(land(w, ["feat/bad"])[0], 1)
        code, out, _ = land(w, ["feat/bad"])
        self.assertEqual(code, 3, "the same failure three times")
        self.assertIn(failures.STOP_TEXT, out)
        self.assertEqual(out[-1], "VERDICT fail")

    def test_tool_fault_between_is_not_counted(self):
        w = self.w
        w.branch("feat/x", {"docs/x.md": "x", "FAULT": "same\n"})
        self.assertEqual(land(w, ["feat/x"])[0], 1)
        # the same branch, now hit by a tool fault (the tool broke): not a failure, not a reset
        w.branch("feat/x", {"docs/x.md": "x", "TOOL": "the runner raised\n"})
        self.assertEqual(land(w, ["feat/x"])[0], 2)
        self.assertEqual(failures.Store(w.store_path).count("feat/x"), 1, "a tool fault does not count and does not reset")
        w.branch("feat/x", {"docs/x.md": "x", "FAULT": "same\n"})
        self.assertEqual(land(w, ["feat/x"])[0], 1)
        w.branch("feat/x", {"docs/x.md": "x", "ENV": "the tablet is away\n"})
        self.assertEqual(land(w, ["feat/x"])[0], 2)
        w.branch("feat/x", {"docs/x.md": "x", "FAULT": "same\n"})
        self.assertEqual(land(w, ["feat/x"])[0], 3, "the third identical failure stops, with the tool faults between")

    def test_a_different_failure_starts_again(self):
        w = self.w
        for message in ("one\n", "one\n", "two\n", "two\n"):
            w.branch("feat/x", {"docs/x.md": "x", "FAULT": message})
            self.assertEqual(land(w, ["feat/x"])[0], 1)
        w.branch("feat/x", {"docs/x.md": "x"})
        self.assertEqual(land(w, ["feat/x"])[0], 0)
        self.assertEqual(failures.Store(w.store_path).count("feat/x"), 0, "a landing clears it")

    # --- two callers ------------------------------------------------------------------------------------------------

    def test_two_callers_at_once(self):
        w = self.w
        w.branch("feat/a", {"docs/a.md": "a"})
        w.branch("feat/b", {"docs/b.md": "b"})
        w.checker.hold = threading.Event()
        results = {}
        first = w.landing(pid=1001)
        second = w.landing(pid=1002)

        def run(name, L, branches):
            results[name] = L.land(branches)

        t1 = threading.Thread(target=run, args=("a", first, ["feat/a"]), name="caller-a")
        t1.start()
        self.assertTrue(w.checker.entered.wait(30), "the first caller became the runner and started its run")
        t2 = threading.Thread(target=run, args=("b", second, ["feat/b"]), name="caller-b")
        t2.start()
        deadline = time.time() + 30
        while w.rows().get("feat/b") is None and time.time() < deadline:
            time.sleep(0.01)
        self.assertEqual(w.rows()["feat/b"]["state"], "waiting", "the second caller is in the queue while the first runs")
        w.checker.hold.set()
        t1.join(60)
        t2.join(60)
        self.assertEqual(results, {"a": 0, "b": 0})
        self.assertEqual(set(w.checker.threads), {"caller-a"}, "every run was the first caller's: the second only waited")
        self.assertEqual([c["names"] for c in w.checker.calls], [["feat/a"], ["feat/b"]])
        self.assertTrue(any("LAND feat/b landed" in l for l in second.out))
        self.assertFalse(any("feat/a" in l for l in second.out), "each caller gets its own verdict")
        self.assertEqual(second.out[-1], "VERDICT pass")
        self.assertTrue(any("a queue runner is alive" in l for l in second.out))

    def test_a_runner_that_died_is_replaced(self):
        w = self.w
        w.branch("feat/a", {"docs/a.md": "a"})
        # a dead runner (pid 4242) left its lock and a running row
        (w.plan / "landing-runner.lock").mkdir()
        (w.plan / "landing-runner.lock" / "holder.txt").write_text("landing runner pid 4242 now\n", encoding="utf-8")
        L = w.landing()
        L.queue.add(["feat/a"], "1", L.commit_of)
        L.queue.claim()
        self.assertEqual(w.rows()["feat/a"]["state"], "running")
        code = L.land(["feat/a"])
        self.assertEqual(code, 0, L.out)
        self.assertEqual(w.rows()["feat/a"]["state"], "landed")

    def test_land_queue_runs_what_waits(self):
        w = self.w
        w.branch("feat/a", {"docs/a.md": "a"})
        L = w.landing()
        L.queue.add(["feat/a"], "1", L.commit_of)
        code = L.run_only_queue()
        self.assertEqual(code, 0)
        self.assertEqual(w.rows()["feat/a"]["state"], "landed")
        self.assertEqual(L.out[-1], "VERDICT pass")
        again = w.landing()
        self.assertEqual(again.run_only_queue(), 0)
        self.assertTrue(any("nothing waits" in l for l in again.out))

    # --- what a run runs --------------------------------------------------------------------------------------------

    def test_documents_only_run(self):
        w = self.w
        w.branch("docs/x", {"docs/x.md": "x\n", "docs/plan/ledger.tsv": "id\n"})
        code, out, _ = land(w, ["docs/x"])
        self.assertEqual(code, 0)
        self.assertEqual(w.checker.calls[0]["groups"], "repo", "documents only run the repository checks and no build")
        sys.stderr.write("\n--- what gate.py land prints for a branch of documents only ---\n" + "\n".join(out) + "\n---\n")

    def test_groups_of_the_union(self):
        w = self.w
        w.branch("feat/doc", {"docs/b.md": "b"})
        w.branch("feat/game", {"native/g2.cpp": "g"})
        w.branch("feat/render", {"runtime/src/render/r.cpp": "r"})
        land(w, ["feat/doc", "feat/game", "feat/render"])
        self.assertEqual(w.checker.calls[0]["groups"], "repo,build,unit,start,engine1,engine3,tour1,tour3,validate,android,github,measure_pc,measure_tablet")
        w.branch("feat/odd", {"installer/x.ps1": "x"})
        land(w, ["feat/odd"])
        self.assertTrue(w.checker.calls[-1]["groups"].startswith("full"), "a file no row names")

    # --- the queue file ---------------------------------------------------------------------------------------------

    def test_queue_file(self):
        w = self.w
        q = landing.Queue(w.plan / "landing-queue.tsv")
        q.add(["x", "y"], "2", lambda b: "c0ffee")
        self.assertEqual([(r["branch"], r["agent"], r["state"], r["commit"]) for r in q.read()],
                         [("x", "2", "waiting", "c0ffee"), ("y", "2", "waiting", "c0ffee")])
        header = (w.plan / "landing-queue.tsv").read_text(encoding="utf-8").splitlines()[0]
        self.assertEqual(header.split("\t"), ["time", "branch", "agent", "state", "commit", "run folder", "note", "also"])
        q.add(["x"], "1", lambda b: "other")
        self.assertEqual(q.row("x")["agent"], "2", "a branch already waiting stays as it is")
        claimed = q.claim()
        self.assertEqual([r["branch"] for r in claimed], ["x", "y"])
        self.assertEqual(q.claim(), [], "taken once")
        q.finish("x", "landed", "abc", "D:/runs/1", "ok")
        q.add(["x"], "1", lambda b: "new")
        self.assertEqual((q.row("x")["state"], q.row("x")["commit"]), ("waiting", "new"), "an ended branch starts again")
        self.assertFalse((w.plan / "landing-queue.tsv.lock").exists(), "the lock folder is freed")
        # the lock folder is held while the queue is changed
        held = []
        old = q._write

        def spy(rows):
            held.append((w.plan / "landing-queue.tsv.lock").is_dir())
            old(rows)
        q._write = spy
        q.add(["z"], "1", lambda b: "")
        self.assertEqual(held, [True])
        # a stale lock (its holder's process ended) is taken over
        (w.plan / "landing-queue.tsv.lock").mkdir()
        (w.plan / "landing-queue.tsv.lock" / "holder.txt").write_text("landing queue pid 4243 now\n", encoding="utf-8")
        q2 = landing.Queue(w.plan / "landing-queue.tsv", alive=lambda p: False)
        q2.add(["w"], "1", lambda b: "")
        self.assertIsNotNone(q2.row("w"))

    def test_the_also_of_a_branch_is_in_the_queue_and_a_file_of_seven_columns_is_read(self):
        w = self.w
        q = landing.Queue(w.plan / "landing-queue.tsv")
        q.add(["x"], "1", lambda b: "c0", also="tour/d3d11, unit/game")
        self.assertEqual(q.row("x")["also"], "tour/d3d11,unit/game")
        q.add(["x"], "1", lambda b: "c0", also="unit/game,tour/vulkan")
        self.assertEqual(q.row("x")["also"], "tour/d3d11,unit/game,tour/vulkan", "a waiting branch takes the new names in, once each")
        self.assertEqual(q.claim()[0]["also"], "tour/d3d11,unit/game,tour/vulkan")
        old = w.plan / "old-queue.tsv"
        old.write_text("time\tbranch\tagent\tstate\tcommit\trun folder\tnote\n2026-11-20T10:00:00\tb\t1\twaiting\tabc\t\t\n",
                       encoding="utf-8")
        q2 = landing.Queue(old)
        self.assertEqual([(r["branch"], r["state"], r["also"]) for r in q2.read()], [("b", "waiting", "")], "written before `also`")
        q2.add(["c"], "1", lambda b: "")
        self.assertEqual(old.read_text(encoding="utf-8").splitlines()[0].split("\t")[-1], "also", "the column is added when it writes")
        self.assertEqual(len(q2.read()), 2)

    def test_a_landing_with_also_gives_the_checker_the_names_and_without_also_it_does_not(self):
        w = self.w
        w.branch("feat/a", {"docs/a.md": "a"})
        w.branch("feat/b", {"docs/b.md": "b"})
        code, out, _ = land(w, ["feat/a"], )
        self.assertEqual((code, w.checker.calls[-1]["also"]), (0, ""))
        L = w.landing()
        self.assertEqual(L.land(["feat/b"], also="tour/d3d11,unit/game"), 0)
        self.assertEqual(w.checker.calls[-1]["also"], "tour/d3d11,unit/game")
        self.assertTrue(any("and, asked for by name: tour/d3d11,unit/game" in l for l in L.out), L.out)

    def test_the_also_of_a_waiting_caller_reaches_the_run_of_the_caller_that_is_the_runner(self):
        w = self.w
        w.branch("feat/a", {"docs/a.md": "a"})
        w.branch("feat/b", {"docs/b.md": "b"})
        w.checker.hold = threading.Event()
        first, second = w.landing(pid=1001), w.landing(pid=1002)
        results = {}
        t1 = threading.Thread(target=lambda: results.update(a=first.land(["feat/a"])), name="caller-a")
        t1.start()
        self.assertTrue(w.checker.entered.wait(30))
        t2 = threading.Thread(target=lambda: results.update(b=second.land(["feat/b"], also="tour/vulkan")), name="caller-b")
        t2.start()
        deadline = time.time() + 30
        while w.rows().get("feat/b") is None and time.time() < deadline:
            time.sleep(0.01)
        w.checker.hold.set()
        t1.join(60)
        t2.join(60)
        self.assertEqual(results, {"a": 0, "b": 0})
        self.assertEqual(set(w.checker.threads), {"caller-a"}, "the second caller only waited")
        self.assertEqual([(c["names"], c["also"]) for c in w.checker.calls], [(["feat/a"], ""), (["feat/b"], "tour/vulkan")])

    def test_one_batch_runs_the_union_of_its_branches_also(self):
        w = self.w
        w.branch("feat/a", {"docs/a.md": "a"})
        w.branch("feat/b", {"docs/b.md": "b"})
        q = landing.Queue(w.plan / "landing-queue.tsv")
        q.add(["feat/a"], "1", lambda b: "", also="tour/d3d11")
        q.add(["feat/b"], "1", lambda b: "", also="tour/d3d11,unit/game")
        L = w.landing()
        self.assertEqual(L.land(["feat/a", "feat/b"]), 0)
        self.assertEqual([(c["names"], c["also"]) for c in w.checker.calls], [(["feat/a", "feat/b"], "tour/d3d11,unit/game")])

    # --- what gate.py prints and reads ----------------------------------------------------------------------------

    def test_run_output_is_read(self):
        out = ["repo/code pass 1.0 s ok\n", "KIND flake tour/d3d12 001-a :: once\n", "gate.py: ledger row to open: x\n",
               "result: D:/runs/20261121-1-land/result.json\n", "MOVED " + "a" * 40 + "\n", "VERDICT pass\n"]
        r = gate.parse_run_output(out, 0)
        self.assertEqual((r.verdict, r.moved, r.run_folder, r.failures), ("pass", "a" * 40, str(pathlib.Path("D:/runs/20261121-1-land")), []))
        self.assertEqual(r.lines, ["KIND flake tour/d3d12 001-a :: once"])
        r = gate.parse_run_output(["KIND fail tour/d3d12 001-a :: boom: first\n", "VERDICT fail\n"], 1)
        self.assertEqual((r.verdict, r.failures), ("fail", [("tour/d3d12", "fail", "boom: first")]))
        r = gate.parse_run_output(["KIND tool x - :: raised\n", "VERDICT not-judged\n"], 2)
        self.assertEqual((r.verdict, r.failures[0][1]), ("not-judged", "tool"))
        r = gate.parse_run_output(["NOT-MOVED github GitHub's jobs on gate/x: red\n", "VERDICT fail\n"], 1)
        self.assertEqual((r.verdict, r.why_kind, r.moved), ("pass", "github", ""))
        # A pass that did not move main ends the run not-judged (exit 2) unless a job of GitHub is red: the NOT-MOVED line decides, so the
        # landing's reading is the same whatever the run's own verdict and exit code were.
        for kind, why in (("other", "the push of main failed: ! [rejected]"), ("main-moved", "main moved during the gate (now 1234)"),
                          ("env", "GitHub could not be reached for the push of main (6 tries in 8 minutes): x")):
            for verdict, code in (("not-judged", 2), ("fail", 1)):
                r = gate.parse_run_output(["NOT-MOVED %s %s\n" % (kind, why), "VERDICT %s\n" % verdict], code)
                self.assertEqual((r.verdict, r.why_kind, r.moved, r.why), ("pass", kind, "", why), (kind, verdict))
        r = gate.parse_run_output(["gate.py: no whole quick gate\n"], 2)
        self.assertEqual(r.verdict, "not-judged", "no VERDICT line: the tool broke")
        r = gate.parse_run_output(["VERDICT pass\n"], 7)
        self.assertEqual(r.verdict, "not-judged", "an exit code that is none of 0, 1, 2")
        r = gate.parse_run_output(["KIND fail t - :: x [known (P-1)]\n", "KIND fail t2 a :: y\n", "VERDICT fail\n"], 1)
        self.assertEqual(r.failures, [("t2", "fail", "y")], "a known failure does not count")

    def test_a_branch_main_holds_already_says_that_its_also_checks_were_not_run(self):
        """B15 point 11: a landing of a branch main already holds runs nothing, so the checks asked for with --also did not run; it says so."""
        w = self.w
        w.branch("feat/held", {}, commits=0)             # the branch is at a commit main holds: no commit of its own
        runs = len(w.checker.calls)
        L = w.landing()
        self.assertEqual(L.land(["feat/held"], also="stress,soak/long"), 0, L.out)
        self.assertEqual(len(w.checker.calls), runs, "nothing ran")
        said = [l for l in L.out if "main holds it already" in l and "NOT run" in l]
        self.assertEqual(len(said), 1, L.out)
        self.assertIn("stress, soak/long", said[0])
        L = w.landing()
        L.land(["feat/held"])
        self.assertFalse([l for l in L.out if "NOT run" in l], "asked for nothing: nothing to say")

    def test_the_landing_says_that_a_long_run_started_and_where_its_log_is(self):
        """B15 point 11: the last lines of a landing whose move started a long run say so, name the log, and say it gives way to landings."""
        w = self.w
        w.branch("feat/a", {"docs/a.md": "a"})
        inner = w.checker

        class Starting:
            calls = inner.calls

            def run(self, *a, **k):
                result = inner.run(*a, **k)
                result.long_log = "D:/NebulaWork/build/gate-runs/long-abc123def.log"
                return result
        code, out, _ = land(w, ["feat/a"], checker=Starting())
        self.assertEqual(code, 0, out)
        line = next(l for l in out if "a long run started after this move" in l)
        self.assertIn("long-abc123def.log", line)
        self.assertIn("gives way to every landing", line)
        self.assertLess(out.index(line), out.index("VERDICT pass"))
        r = gate.parse_run_output(["LONG-STARTED D:/runs/long-1234.log\n", "MOVED " + "a" * 40 + "\n", "VERDICT pass\n"], 0)
        self.assertEqual(r.long_log, "D:/runs/long-1234.log")
        self.assertEqual(gate.parse_run_output(["MOVED " + "a" * 40 + "\n", "VERDICT pass\n"], 0).long_log, "")

    def test_the_checker_starts_the_landing_trees_gate(self):
        class Popen:
            def __init__(self, cmd, **k):
                self.cmd, self.k = cmd, k
                self.stdout = iter(["KIND fail tour/d3d12 a :: boom\n", "result: D:/r/x/result.json\n", "VERDICT fail\n"])

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

            def wait(self):
                return 1
        made = []
        args = gate.parser(land=True).parse_args(["b", "--tablet", "T", "--agent", "2"])
        args.mode = "main"
        checker = gate.GateChecker(args, say=lambda l: None, popen=lambda cmd, **k: made.append(Popen(cmd, **k)) or made[-1])
        r = checker.run(self.tmp.name, "abc", ["b"], "repo,build")
        cmd = made[0].cmd
        self.assertTrue(cmd[4].endswith("tools/gate/gate.py") or cmd[4].endswith("tools\\gate\\gate.py"))
        self.assertEqual(cmd[cmd.index("--scope") + 1], "repo,build")
        self.assertIn("--move", cmd)
        self.assertIn("--main", cmd)
        self.assertEqual(cmd[cmd.index("--tablet") + 1], "T")
        self.assertEqual(made[0].k["cwd"], self.tmp.name, "the program of the landing tree, run in it")
        self.assertEqual(r.verdict, "fail")
        full = checker.command(self.tmp.name, ["b"], "full (no row names: x)")
        self.assertEqual(full[full.index("--scope") + 1], "full")

    def test_the_checker_gives_the_landing_trees_gate_also_when_it_has_names(self):
        args = gate.parser(land=True).parse_args(["b", "--tablet", "T"])
        args.mode = "main"
        checker = gate.GateChecker(args, say=lambda l: None)
        cmd = checker.command(self.tmp.name, ["b"], "repo,build", "tour/d3d11,unit/game")
        self.assertEqual(cmd[cmd.index("--also") + 1], "tour/d3d11,unit/game")
        self.assertEqual(cmd[cmd.index("--scope") + 1], "repo,build")
        self.assertNotIn("--also", checker.command(self.tmp.name, ["b"], "repo,build"))
        self.assertNotIn("--also", checker.command(self.tmp.name, ["b"], "repo,build", ""))

    def test_gate_land_is_a_command_of_gate_py(self):
        w = self.w
        w.branch("docs/x", {"docs/x.md": "x\n"})
        calls = []

        class Checker(FakeChecker):
            pass
        saved = gate.GateChecker, gate.TREE
        gate.GateChecker = lambda args: (calls.append(args) or w.checker)
        gate.TREE = w.src
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                code = gate.main(["land", "docs/x", "--queue-file", str(w.plan / "q.tsv"), "--landing-tree", str(w.tree),
                                  "--failures", str(w.plan / "f.tsv"), "--live", str(w.plan / "live"), "--agent", "2",
                                  "--runner-lock", str(w.plan / "runner.lock")])
        finally:
            gate.GateChecker, gate.TREE = saved
        self.assertEqual(code, 0, buf.getvalue())
        self.assertEqual(calls[0].agent, "2")
        self.assertTrue(buf.getvalue().rstrip().endswith("VERDICT pass"), buf.getvalue())
        with contextlib.redirect_stdout(io.StringIO()):
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                # land_main settles the live folder first: a temporary one, never the real D:/NebulaWork/plan/native
                self.assertEqual(gate.main(["land", "--live", str(pathlib.Path(self.tmp.name) / "live-none")]), 2)
        self.assertIn("one of the two", err.getvalue())


def args_for(*argv):
    return gate.parser().parse_args(list(argv))


class AlsoTest(unittest.TestCase):
    """`--also` in gate.py: the names are checked against the gate's own, a known one is added to a landing's checks, an unknown one
    refuses the landing before anything runs."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.known, cls.planned, problems = gate.read_lists()
        assert not problems, problems

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def args(self, mode="main"):
        a = args_for("--main", "--tablet", "T", "--work", self.tmp.name)
        a.mode = mode
        return a

    def test_a_known_name_adds_its_checks_and_what_they_need_to_the_landings_checks(self):
        plan = scope.Plan(["repo"], False, [], {})
        plain = gate.landing_selection(self.args(gate.LAND_LEVEL), plan, [], self.known, self.planned)
        self.assertIn("repo/code", plain)
        self.assertNotIn("tour/vulkan", plain, "the groups of the plan name no tour")
        more = gate.landing_selection(self.args("main"), plan, ["tour/vulkan"], self.known, self.planned)
        self.assertLessEqual({"repo/code", "tour/vulkan", "build/windows", "data/RMGE01"}, more,
                             "the check, what it needs, and the plan's own")
        self.assertNotIn("tour/d3d11", more, "only the names asked for")
        self.assertNotIn("tour/d3d12", more)
        both = gate.landing_selection(self.args("main"), plan, ["tour/vulkan", "unit/game"], self.known, self.planned)
        self.assertLessEqual({"tour/vulkan", "unit/game"}, both)

    def test_a_check_only_the_nightly_run_has_is_a_check_of_the_main_level(self):
        main_names = set(gate.level_needs(self.args("main"), "main", self.known, self.planned))
        land_names = set(gate.level_needs(self.args("main"), gate.LAND_LEVEL, self.known, self.planned))
        self.assertIn("tour-RMGP01/d3d12", main_names)
        self.assertNotIn("tour-RMGP01/d3d12", land_names, "a landing has no other disc's tour: --also brings it")
        plan = scope.Plan(["repo"], False, [], {})
        chosen = gate.landing_selection(self.args("main"), plan, ["tour-RMGP01/d3d12"], self.known, self.planned)
        self.assertIn("tour-RMGP01/d3d12", chosen)
        self.assertIn("data/RMGP01", chosen, "and the data it needs")

    def test_a_known_name_is_no_problem_and_an_unknown_name_lists_the_known_ones(self):
        self.assertEqual(gate.also_problem(self.args(), ["tour/d3d11", "unit/game"], self.known, self.planned), "")
        text = gate.also_problem(self.args(), ["tour/d3d11", "tour/d3d99"], self.known, self.planned)
        self.assertIn("--also names no check the gate knows: tour/d3d99.", text)
        self.assertNotIn("knows: tour/d3d11", text)
        self.assertIn("The checks a landing can run are: ", text)
        self.assertIn("tour/d3d12", text.split("are: ")[1].split(", "))
        self.assertIn("unit/game", text)

    def test_a_part_of_the_nightly_run_is_asked_for_by_its_name_in_the_table(self):
        # Package B8: the rule "a name only the long run has cannot be an --also" is gone. The nightly's parts that the table
        # (gate.ALSO_PARTS) names are accepted, a part not built yet is refused as such, and any other name the gate does not
        # know is unknown, whether or not the long run has a check of that name (gate_line_test.py has the table's cases).
        self.assertEqual(gate.also_problem(self.args(), ["soak"], self.known, self.planned), "")
        both = gate.also_problem(self.args(), ["coverage", "nothing/here"], self.known, self.planned)
        self.assertIn("no check the gate knows: coverage, nothing/here", both)
        self.assertNotIn("only the long run", both)

    def refused(self, argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = gate.main(argv)
        return code, out.getvalue(), err.getvalue()

    def test_gate_land_refuses_an_unknown_name_before_anything_is_queued_or_run(self):
        plan_dir = pathlib.Path(self.tmp.name) / "refused"
        plan_dir.mkdir(exist_ok=True)
        checker_made = []
        saved = gate.GateChecker
        gate.GateChecker = lambda args: checker_made.append(args)
        try:
            code, out, err = self.refused(["land", "docs/x", "--also", "tour/d3d12,tour/nowhere", "--queue-file", str(plan_dir / "q.tsv"),
                                           "--landing-tree", str(plan_dir / "tree"), "--failures", str(plan_dir / "f.tsv"),
                                           "--live", str(plan_dir / "live"), "--runner-lock", str(plan_dir / "runner.lock")])
        finally:
            gate.GateChecker = saved
        self.assertEqual(code, 2)
        self.assertIn("gate.py: --also names no check the gate knows: tour/nowhere. The checks a landing can run are: ", err)
        self.assertEqual(out.strip(), "VERDICT not-judged")
        self.assertFalse((plan_dir / "q.tsv").exists(), "nothing was put in the queue")
        self.assertEqual(checker_made, [], "no checker was made, so no run")
        self.assertFalse((plan_dir / "tree").exists())

    def test_gate_land_with_a_known_name_hands_it_to_the_runner(self):
        w_tmp = tempfile.TemporaryDirectory()
        self.addCleanup(w_tmp.cleanup)
        w = World(w_tmp.name)
        w.branch("docs/x", {"docs/x.md": "x\n"})
        saved = gate.GateChecker, gate.TREE
        gate.GateChecker = lambda args: w.checker
        gate.TREE = w.src
        try:
            code, out, err = self.refused(["land", "docs/x", "--also", "tour/d3d11,unit/game", "--queue-file", str(w.plan / "q.tsv"),
                                           "--landing-tree", str(w.tree), "--failures", str(w.plan / "f.tsv"),
                                           "--live", str(w.plan / "live"), "--runner-lock", str(w.plan / "runner.lock")])
        finally:
            gate.GateChecker, gate.TREE = saved
        self.assertEqual(code, 0, out + err)
        self.assertEqual(w.checker.calls[0]["also"], "tour/d3d11,unit/game")

    def test_also_without_scope_and_with_a_tablet_check_without_a_tablet_are_refused(self):
        code, out, err = self.refused(["--main", "--also", "tour/d3d12", "--tablet", "T"])
        self.assertEqual(code, 2)
        self.assertIn("--also goes with --scope", err)
        lock = pathlib.Path(self.tmp.name) / "runner.lock"      # the runner's mark: this process stands in for the runner
        lock.mkdir(exist_ok=True)
        (lock / "holder.txt").write_text("landing runner pid %d 2026-11-21T11:00:00" % os.getpid() + chr(10), encoding="utf-8")
        code, out, err = self.refused(["--main", "--move", "--scope", "repo", "--also", "tour/tablet",
                                       "--landing-runner", str(os.getpid()), "--runner-lock", str(lock)])
        self.assertEqual(code, 2)
        self.assertIn("needs the tablet's serial", err)
        code, out, err = self.refused(["--main", "--move", "--scope", "repo", "--also", "tour/tablet"])
        self.assertEqual(code, 2)
        self.assertIn("main moves only through gate.py land <branch>", err, "by hand it is refused first")
        code, out, err = self.refused(["land", "--queue", "--also", "tour/d3d12", "--queue-file", str(pathlib.Path(self.tmp.name) / "q2.tsv"),
                                       "--live", str(pathlib.Path(self.tmp.name) / "live-q2")])
        self.assertEqual(code, 2)
        self.assertIn("--also goes with branches", err)


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("landing_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
