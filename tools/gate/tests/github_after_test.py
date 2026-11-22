"""github_after_test.py: main moves when this machine's checks pass; GitHub's Mac and Linux jobs are read after the move (brief B12),
without GitHub, a build or a tablet: fakes for GitHub, git and the checks, a temporary live folder, a temporary git tree.

    python -X utf8 -I tools/gate/tests/github_after_test.py [--work FOLDER]

  Scope (A)        the group `github-first` and the forms of a plan; the rows of the real tree (every .mm and .m file and every file
                   in a folder named metal, apple, macos, darwin or linux gets `github-first`, a source file both builds here compile
                   gets `github` and not `github-first`, no row is dead); the anchored pattern and `-full` of the table
  After (B)        a landing with `github` alone never waits (the move comes before the fake jobs end, with no sleep): a running
                   job -> one line, the move, one line in github-pending.tsv, the candidate stays; all green -> judged at once; a
                   red -> judged at once, no move; GitHub away -> the move; the waiting form (github-first, a full plan) still
                   waits; the record (result.json, the history line) says which form it was
  Settle (C)       every row of the table of `settle_pending`: green, red (one repair for each red job, the text, the page, the
                   log command), a second red of the same job, running < 120 minutes, running >= 120 minutes, GitHub away, a
                   cancelled job run again once across two readings, one question for each line, never raises
  Repairs (D)      the repair's text and its one writer; --github first reaches the batch as the group github-first; the repair
                   of a red job is landed with `--github first` (a nightly repair with --also); a step in work with its work
                   committed is kept when the repair comes, the repair is started and landed, and `start <step>` goes on with
                   the step on its own branch on top of the new main, nothing lost; a conflict leaves the branch as it was
  Workflow (E)     check.yml runs on main and gate/** only (and pull requests and by hand); the jobs of class nightly and the
                   Android start check do not run on a candidate, the others do (the table's classes against the jobs' `if:`)
  Mark (F)         --move by hand is refused (exit 2) with no lock and no run folder, on a real command line too; only the runner's
                   own call, with its process id as the holder of its lock, moves
Exit 0 when every case holds.
"""
import contextlib
import datetime
import io
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(TREE / "tools" / "plan"))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import gate  # noqa: E402
import github_rule  # noqa: E402
import landing  # noqa: E402
import live as lv  # noqa: E402
import plan  # noqa: E402
import scope  # noqa: E402
import gate_github_test as ggt  # noqa: E402
import landing_test as lt  # noqa: E402
import plan_fixture  # noqa: E402
from plan_fixture import Tree, ledger_row, setup_module, teardown_module  # noqa: E402

GIT = gate.GIT
SHA = ggt.SHA
CLASSES = github_rule.read_table()
TABLE = scope.read_table()


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


def tracked_files():
    out = subprocess.run([GIT, "-C", str(TREE), "ls-files"], capture_output=True, text=True, encoding="utf-8")
    assert out.returncode == 0, out.stderr
    return out.stdout.splitlines()


# ---- A: the scope table -------------------------------------------------------------------------------------------

PLATFORM_WORDS = ("metal", "apple", "macos", "darwin", "linux")


def platform_file(path):
    """The files the brief names: a name ending in .mm or .m, or a folder of the path named metal, apple, macos, darwin or linux."""
    folders = path.split("/")[:-1]
    return path.endswith((".mm", ".m")) or any(f.lower() in PLATFORM_WORDS for f in folders)


class ScopeTest(unittest.TestCase):
    def groups(self, *paths):
        return scope.plan_for(list(paths), TABLE)

    def test_the_forms_of_a_plan(self):
        self.assertEqual(scope.github_form(self.groups("docs/a.md")), None, "documents: no candidate, no wait")
        self.assertEqual(scope.github_form(self.groups("native/a.cpp")), "after")
        self.assertEqual(scope.github_form(self.groups("runtime/src/render/mtl/a.mm")), "first")
        self.assertEqual(scope.github_form(self.groups("native/a.cpp", "platform/linux/build_linux.sh")), "first", "one file is enough")
        self.assertEqual(scope.github_form(self.groups("somewhere/no/row/names.bin")), "first", "a full run waits")
        self.assertEqual(scope.github_form(None), "first", "no plan: the waiting form")
        groups, full = scope.groups_from_text("repo,github")
        self.assertEqual(scope.github_form(scope.Plan(groups, full, [], {})), "after")
        groups, full = scope.groups_from_text("repo,github,github-first")
        self.assertEqual(scope.github_form(scope.Plan(groups, full, [], {})), "first")
        added = scope.with_group(self.groups("native/a.cpp"), "github-first")
        self.assertEqual(scope.github_form(added), "first")
        self.assertEqual(added.groups, [g for g in scope.GROUP_NAMES if g in self.groups("native/a.cpp").groups + ["github-first"]])
        self.assertEqual(scope.check_patterns(["github-first"]), [], "no local check")

    def test_platform_files_wait_and_shared_files_do_not(self):
        files = tracked_files()
        wanted = [f for f in files if platform_file(f)]
        self.assertGreater(len(wanted), 30, "the tree has the Metal renderer and the Apple and Linux parts")
        for f in wanted:
            self.assertIn("github-first", self.groups(f).groups, f)
        # the Metal renderer's other files, the update code of one system, a unit test only Linux builds
        metal = [f for f in files if f.startswith(("runtime/src/render/mtl/", "runtime/include/galaxy/render/mtl/"))]
        self.assertGreater(len(metal), 30)
        for f in metal + ["runtime/src/update/update_install_linux.cpp", "runtime/src/update/update_transport_curl.cpp",
                          "runtime/src/update/update_probe_posix.cpp", "runtime/src/update/update_transport_apple.mm",
                          "runtime/src/perf_hint_tests.cpp"]:
            self.assertIn(f, files, "the file is in the tree: " + f)
            self.assertIn("github-first", self.groups(f).groups, f)
        for f in ("runtime/src/update/update_install_windows.cpp", "runtime/src/update/update.cpp", "runtime/src/perf_hint.cpp"):
            self.assertNotIn("github-first", self.groups(f).groups, "Windows builds it too: " + f)
        # a new Objective-C++ file in a folder of shared code waits too (the row of its name)
        self.assertIn("github-first", self.groups("runtime/src/new_thing.mm").groups)
        self.assertIn("github-first", self.groups("runtime/src/new_thing.m").groups)
        shared =[f for f in files if re.match(r"^runtime/src/render/[^/]+\.cpp$", f)
                  and not any(w in f.lower() for w in ("mtl", "metal", "apple"))]
        self.assertGreater(len(shared), 10)
        for f in shared:
            p = self.groups(f)
            self.assertIn("github", p.groups, f)
            self.assertNotIn("github-first", p.groups, f)
            self.assertFalse(p.full, f)
            self.assertEqual(scope.github_form(p), "after", f)
        for f in ("native/petari/src/Game/Foo.cpp", "runtime/src/audio/mixer.cpp", "platform/sdl/host_window_sdl.cpp",
                  "crates/nebula-import/src/lib.rs", "tools/test_driver/luma_test/luma_test.c"):
            self.assertEqual(scope.github_form(self.groups(f)), "after", f)

    def test_no_row_is_dead_and_the_counts_are_what_the_report_says(self):
        files = tracked_files()
        counts = {}
        for pattern, groups, _ in TABLE.rows:
            if "github-first" in groups:
                counts[pattern] = sum(1 for f in files if scope.path_matches(f, pattern))
        for pattern, n in counts.items():
            if pattern != "*.m":
                self.assertGreater(n, 0, "the row %s matches no file" % pattern)
        self.assertEqual(counts["*.m"], 0, "no .m file in the tree today: the row is for one that comes")
        print("github-first rows and the files each matches: " + "; ".join("%s %d" % (p, n) for p, n in counts.items()))

    def test_cmake_files_make_a_full_run_that_waits_except_the_list_of_unit_tests(self):
        for f in ("cmake/dav1d.cmake", "CMakeLists.txt", "cmake/vulkan_deps.cmake"):
            p = self.groups(f)
            self.assertTrue(p.full and "github-first" in p.groups, f)
        for f in ("native/CMakeLists.txt", "platform/sdl/CMakeLists.txt", "tests/unit/CMakeLists.txt", "native/tests/CMakeLists.txt"):
            p = self.groups(f)
            self.assertFalse(p.full, "a CMake file of a folder keeps that folder's checks, not a full run: " + f)
            self.assertIn("github-first", p.groups, f)
        p = self.groups("cmake/unit_tests.cmake")
        self.assertFalse(p.full, "-full: the list of unit tests keeps its narrow run")
        self.assertEqual(p.groups, ["repo", "build", "unit", "github-first"])
        self.assertIn("github-first", self.groups(".github/workflows/check.yml").groups)

    def test_the_anchored_pattern_and_minus_full(self):
        table = scope.parse("mods = off\npattern\tgroups\n/a.txt\trepo\nsub/**\tfull,github\nsub/keep.txt\trepo,-full\n")
        self.assertFalse(scope.plan_for(["a.txt"], table).full, "the top folder's file has its row")
        self.assertTrue(scope.plan_for(["x/a.txt"], table).full, "the a.txt of another folder has none: unmatched is full")
        self.assertTrue(scope.plan_for(["sub/other.txt"], table).full)
        kept = scope.plan_for(["sub/keep.txt"], table)
        self.assertFalse(kept.full, "-full takes away the full another row brought")
        self.assertEqual(kept.groups, ["repo", "github"])
        self.assertTrue(scope.plan_for(["sub/keep.txt", "sub/other.txt"], table).full, "another file of the change still makes it full")
        with self.assertRaises(scope.ScopeError):
            scope.parse("mods = off\npattern\tgroups\na\t-github\n")


# ---- B: the landing ----------------------------------------------------------------------------------------------

def pending(tmp):
    return lv.read_pending(tmp / "live")


def result_json(work):
    return json.loads(next(pathlib.Path(work).rglob("result.json")).read_text(encoding="utf-8"))


class AfterTest(unittest.TestCase):
    def running(self):
        return ggt.runs_of(ggt.green("Mac") + [ggt.job("Mac", "in_progress")])

    def test_github_alone_never_waits_and_leaves_one_pending_line(self):
        world = ggt.World(self.running())
        code, lines, work = ggt.drive(self, world, "repo,github", extra=("--covers", "native/A.1, native/A.2"))
        self.assertEqual(code, 0, "\n".join(lines))
        self.assertEqual((world.moved, world.slept, world.moved_after), ([SHA], 0, 0), "the move came before any wait: no sleep at all")
        self.assertIn("GitHub's jobs for %s are read after the move" % SHA[:9], lines)
        self.assertEqual(lines.count("GitHub's jobs for %s are read after the move" % SHA[:9]), 1, "one line")
        self.assertEqual(lines[-1], "VERDICT pass")
        self.assertEqual(world.deleted, [], "the candidate stays on GitHub until the line is settled")
        rows = pending(work.parent)
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual((row["commit"], row["branches"], row["candidate"]), (SHA, "native/A.1, native/A.2", world.pushed[0][1]))
        self.assertTrue(row["candidate"].startswith("gate/"))
        self.assertEqual(pathlib.Path(row["run_folder"]).parent, work, "the run folder is the landing's")
        self.assertTrue(datetime.datetime.fromisoformat(row["time"]) <= datetime.datetime.now())
        self.assertFalse(any(l.startswith("github ") for l in lines), "no waiting row of the old form")

    def test_all_green_at_once_is_judged_at_once(self):
        world = ggt.World(ggt.runs_of(ggt.green()))
        code, lines, work = ggt.drive(self, world, "repo,github")
        self.assertEqual((code, world.moved, world.slept, world.deleted), (0, [SHA], 0, [world.pushed[0][1]]), "\n".join(lines))
        self.assertEqual(pending(work.parent), [], "nothing is left to read")
        self.assertFalse(any(l.startswith("GitHub's jobs for") for l in lines), "no line about reading later")
        self.assertTrue(any(l.startswith("github ") and "green" in l for l in lines), "the row of the green jobs")
        self.assertEqual(result_json(work)["github"], "after")

    def test_a_red_job_at_the_check_is_judged_at_once_and_main_does_not_move(self):
        def turn_red(g, world):
            world.runs = ggt.runs_of(ggt.green("Mac") + [ggt.job("Mac", conclusion="failure")])
        world = ggt.World(self.running())
        code, lines, work = ggt.drive(self, world, "repo,github", checks=turn_red)
        self.assertEqual((code, world.moved), (1, []), "\n".join(lines))
        self.assertEqual(pending(work.parent), [], "no line: main did not move")
        self.assertEqual(lines[-1], "VERDICT fail")

    def test_a_cancelled_job_does_not_stop_the_move_it_is_run_again_and_read_later(self):
        world = ggt.World(ggt.runs_of(ggt.green("Mac") + [ggt.job("Mac", conclusion="cancelled")]))
        code, lines, work = ggt.drive(self, world, "repo,github")
        self.assertEqual((code, world.moved, world.slept), (0, [SHA], 0), "\n".join(lines))
        self.assertEqual(len(world.reruns), 1, "run again once")
        self.assertEqual(len(pending(work.parent)), 1)

    def test_github_away_moves_and_leaves_the_line(self):
        class Away(ggt.World):
            def commit_runs(self, sha):
                self.reads += 1
                raise RuntimeError("gh run list: timeout")
        world = Away(None)
        code, lines, work = ggt.drive(self, world, "repo,github")
        self.assertEqual((code, world.moved, world.slept), (0, [SHA], 0), "\n".join(lines))
        self.assertEqual(len(pending(work.parent)), 1)
        self.assertEqual(world.deleted, [])

    def test_a_move_that_fails_leaves_no_line_and_no_candidate(self):
        world = ggt.World(self.running())
        code, lines, work = ggt.drive(self, world, "repo,github", move_why="main moved during the gate (now 123456789): rebase on it")
        self.assertEqual(code, 2, "\n".join(lines))
        self.assertEqual(pending(work.parent), [])
        self.assertEqual(world.deleted, [world.pushed[0][1]])

    def test_the_waiting_form_still_waits(self):
        for scope_text in ("repo,github-first", "repo,github,github-first", "full"):
            def turn(world):
                if world.slept == 2:
                    world.runs = ggt.runs_of(ggt.green())
            world = ggt.World(self.running(), hook=turn)
            code, lines, work = ggt.drive(self, world, scope_text, extra=("--tablet", "T"))
            self.assertEqual((code, world.moved, world.moved_after), (0, [SHA], 2), "%s\n%s" % (scope_text, "\n".join(lines)))
            self.assertEqual(pending(work.parent), [], scope_text)
            self.assertEqual(world.deleted, [world.pushed[0][1]])
            self.assertEqual(result_json(work)["github"], "first", scope_text)

    def test_the_record_says_which_form_it_was(self):
        for scope_text, form in (("repo,github", "after"), ("repo,github-first", "first")):
            world = ggt.World(ggt.runs_of(ggt.green()))
            code, lines, work = ggt.drive(self, world, scope_text)
            self.assertEqual(result_json(work)["github"], form)
            history = (work / "history.tsv").read_text(encoding="utf-8").splitlines()
            runs = [l.split("\t") for l in history[1:] if l.split("\t")[2] != "move"]      # the move's own line (level move) follows the run's
            self.assertIn("github-form=" + form, runs[-1][12])
            self.assertEqual(history[-1].split("\t")[2], "move", "main moved: its line is the last")
        world = ggt.World(None)
        code, lines, work = ggt.drive(self, world, "repo", allow_github=False)
        self.assertNotIn("github", result_json(work), "a plan with no GitHub part has no form")
        self.assertNotIn("github-form", (work / "history.tsv").read_text(encoding="utf-8"))

    def test_the_two_forms_differ_in_time(self):
        """The same running job: the waiting form sleeps and moves after the job ended; the other moves with no sleep before it."""
        def turn(world):
            if world.slept == 3:
                world.runs = ggt.runs_of(ggt.green())
        slow = ggt.World(self.running(), hook=turn)
        ggt.drive(self, slow, "repo,github-first")
        fast = ggt.World(self.running(), hook=turn)
        ggt.drive(self, fast, "repo,github")
        self.assertEqual((slow.moved_after, fast.moved_after), (3, 0))


# ---- C: reading the results afterwards ----------------------------------------------------------------------------

LOG = "\n".join(["Run cargo test", "error[E0425]: cannot find value x in this scope", "more"])
CANDIDATE = "gate/20261121-100000-012345678"


class Hub:
    """GitHub as the settle reads it: the runs of each commit, what was asked, run again and deleted."""

    def __init__(self, runs_by_sha=None):
        self.runs_by_sha = dict(runs_by_sha or {})
        self.asked, self.reruns, self.deleted, self.said, self.log_asks = [], [], [], [], []
        self.now = 1000.0

    def runs_of(self, sha):
        self.asked.append(sha)
        value = self.runs_by_sha.get(sha)
        if isinstance(value, Exception):
            raise value
        return value or []

    def rerun(self, run_id, job_id):
        self.reruns.append((run_id, job_id))
        return True, "asked"

    def rule_for(self, sha):
        return github_rule.Rule(sha, CLASSES, self.runs_of, self.rerun, clock=lambda: self.now, say=lambda s: None)

    def logs_of(self, job_id):
        self.log_asks.append(job_id)
        return LOG

    def settle(self, live, now, **k):
        return github_rule.settle_pending(live, self.rule_for, self.deleted.append, self.logs_of, say=self.said.append, now=now, **k)


def job(name, state="completed", conclusion="success", job_id=None):
    made = ggt.job(name, state, conclusion)
    if job_id:
        made["databaseId"] = job_id
    made["url"] = "https://github.com/example/repo/actions/runs/100/job/%d" % (job_id or made["databaseId"])
    return made


def run_of(jobs, run_id=100, branch="gate/x"):
    return [{"id": run_id, "branch": branch, "jobs": jobs}]


MOVED = datetime.datetime(2026, 11, 21, 10, 0, 0)


class SettleTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.live = self.root / "live"
        self.run_folder = self.root / "run"
        self.run_folder.mkdir()
        self.hub = Hub()

    def add(self, sha="0123456789abcdef0123456789abcdef01234567", branches="native/A.1", moved=MOVED, candidate=None):
        lv.add_pending(self.live, sha, branches, candidate or "gate/20261121-100000-" + sha[:9], self.run_folder, now=moved)
        return sha

    def settle(self, minutes_after=42):
        return self.hub.settle(self.live, MOVED + datetime.timedelta(minutes=minutes_after))

    def test_every_must_job_green_removes_the_line_and_says_so(self):
        sha = self.add()
        self.hub.runs_by_sha[sha] = run_of([job(n) for n in CLASSES])
        events = self.settle()
        self.assertEqual([(c, w) for c, w, _ in events], [(sha, "green")])
        self.assertEqual(self.hub.said, ["GitHub: 012345678 green, 42 minutes after the move"])
        self.assertEqual(lv.read_pending(self.live), [])
        self.assertEqual(self.hub.deleted, ["gate/20261121-100000-012345678"], "the candidate is removed the way the gate removes it")
        text = (self.run_folder / "github.log").read_text(encoding="utf-8")
        self.assertIn("GitHub: 012345678 green, 42 minutes after the move", text, "one line in the landing's run folder")
        self.assertEqual(lv.read_repairs(self.live), [])

    def test_the_newest_result_wins(self):
        sha = self.add()
        self.hub.runs_by_sha[sha] = ([{"id": 101, "branch": "gate/x", "jobs": [job(n) for n in CLASSES]},
                                      {"id": 99, "branch": "work/x", "jobs": [job("Mac", conclusion="failure")]}])
        self.assertEqual(self.settle()[0][1], "green", "a red of an older run does not count against a newer green")
        sha2 = self.add("1" * 40)
        self.hub.runs_by_sha[sha2] = ([{"id": 101, "branch": "gate/x", "jobs": [job("Mac", conclusion="failure")]},
                                       {"id": 99, "branch": "work/x", "jobs": [job(n) for n in CLASSES]}])
        self.assertEqual(self.settle()[0][1], "red", "a newer red wins over an older green")

    def test_a_red_job_opens_one_repair_with_the_text_the_page_and_the_log_command(self):
        sha = self.add(branches="native/A.1, native/A.2")
        self.hub.runs_by_sha[sha] = run_of([job(n) for n in CLASSES if n not in ("Mac", "Linux")] + [
            job("Mac", conclusion="failure", job_id=5001), job("Linux", conclusion="failure", job_id=5002)])
        events = self.settle(55)
        self.assertEqual([w for _, w, _ in events], ["red"])
        self.assertEqual(lv.read_pending(self.live), [], "the line is removed")
        self.assertEqual(self.hub.deleted, ["gate/20261121-100000-012345678"])
        repairs = {r["check"]: r for r in lv.read_repairs(self.live)}
        self.assertEqual(sorted(repairs), ["github/Linux", "github/Mac"], "one repair for each red job")
        r = repairs["github/Mac"]
        self.assertEqual((r["source"], r["kind"], r["state"], r["commit"], r["step"], r["renderer"]), ("github", "fail", "open", sha, "A.1", "-"))
        self.assertIn("GitHub's job Mac is red on 012345678, landed from native/A.1, native/A.2: Run cargo test", r["note"],
                      "the text, with the first line of the job's log")
        self.assertIn("https://github.com/example/repo/actions/runs/100/job/5001", r["note"], "the job's page")
        self.assertIn("gh run view 100 --log-failed --job 5001", r["note"], "the whole command that prints the failed part of its log")
        self.assertEqual(sorted(self.hub.log_asks), [5001, 5002])
        self.assertTrue(any("repair R-001" in l and "repair R-002" in l for l in self.hub.said), self.hub.said)
        self.assertEqual(r["evidence"], str(self.run_folder))

    def test_a_second_red_of_the_same_job_adds_a_line_to_its_open_repair_and_opens_nothing(self):
        first = self.add(branches="native/A.1")
        self.hub.runs_by_sha[first] = run_of([job(n) for n in CLASSES if n != "Mac"] + [job("Mac", conclusion="failure", job_id=5001)])
        self.settle()
        second = self.add("2" * 40, branches="native/B.1")
        self.hub.runs_by_sha[second] = run_of([job(n) for n in CLASSES if n != "Mac"] + [job("Mac", conclusion="failure", job_id=6001)])
        self.settle()
        repairs = lv.read_repairs(self.live)
        self.assertEqual(len(repairs), 1, "nothing new is opened")
        self.assertEqual(repairs[0]["seen"], "2")
        self.assertIn("red again on 222222222, landed from native/B.1", repairs[0]["note"], "the commit and the branch")
        self.assertIn("012345678", repairs[0]["note"], "the first red is still there")
        self.assertTrue(any("added to repair R-001" in l for l in self.hub.said), self.hub.said)
        # another job is another repair; a repair that landed is no longer open: the next red opens a new one
        third = self.add("3" * 40)
        self.hub.runs_by_sha[third] = run_of([job(n) for n in CLASSES if n != "Linux"] + [job("Linux", conclusion="failure")])
        self.settle()
        self.assertEqual(sorted(r["check"] for r in lv.read_repairs(self.live)), ["github/Linux", "github/Mac"])
        lv.set_repair(self.live, "R-001", "landed", "abc")
        fourth = self.add("4" * 40)
        self.hub.runs_by_sha[fourth] = run_of([job(n) for n in CLASSES if n != "Mac"] + [job("Mac", conclusion="failure")])
        self.settle()
        self.assertEqual(len(lv.read_repairs(self.live)), 3)

    def test_a_job_that_has_not_ended_stays_for_120_minutes_and_then_leaves_a_fault(self):
        sha = self.add()
        self.hub.runs_by_sha[sha] = run_of([job(n) for n in CLASSES if n != "Mac"] + [job("Mac", "in_progress")])
        events = self.settle(119)
        self.assertEqual([w for _, w, _ in events], ["kept"])
        self.assertEqual(len(lv.read_pending(self.live)), 1, "the line stays")
        self.assertEqual((self.hub.deleted, lv.read_repairs(self.live), lv.read_faults(self.live)), ([], [], []))
        events = self.settle(120)
        self.assertEqual([w for _, w, _ in events], ["none"])
        self.assertEqual(lv.read_pending(self.live), [])
        faults = lv.read_faults(self.live)
        self.assertEqual(len(faults), 1)
        self.assertEqual((faults[0]["kind"], faults[0]["first_error"]), ("ENV", "GitHub gave no result for 012345678 in 120 minutes"))
        self.assertEqual(self.hub.deleted, ["gate/20261121-100000-012345678"])
        self.assertEqual(lv.read_repairs(self.live), [])

    def test_a_job_with_no_run_yet_is_not_finished_too(self):
        sha = self.add()
        self.hub.runs_by_sha[sha] = []
        self.assertEqual(self.settle(5)[0][1], "kept")

    def test_github_away_changes_nothing_and_asks_once(self):
        a = self.add()
        b = self.add("5" * 40)
        self.hub.runs_by_sha[a] = RuntimeError("gh run list: timeout")
        events = self.settle()
        self.assertEqual([w for _, w, _ in events], ["away"])
        self.assertEqual(len(lv.read_pending(self.live)), 2)
        self.assertEqual(self.hub.asked, [a], "the next line would meet the same answer: not asked")
        self.assertEqual(len(self.hub.said), 1, "one printed line")
        self.assertIn("cannot be reached", self.hub.said[0])
        self.assertEqual((self.hub.deleted, lv.read_repairs(self.live), lv.read_faults(self.live)), ([], [], []))
        self.assertIsNotNone(b)

    def test_one_question_for_each_line(self):
        shas = [self.add(c * 40) for c in "6789"]
        for sha in shas:
            self.hub.runs_by_sha[sha] = run_of([job(n) for n in CLASSES])
        self.settle()
        self.assertEqual(self.hub.asked, shas, "each commit once, no waiting")

    def test_a_cancelled_job_is_run_again_once_across_two_readings(self):
        sha = self.add()
        cancelled = run_of([job(n) for n in CLASSES if n != "Mac"] + [job("Mac", conclusion="cancelled", job_id=777)])
        self.hub.runs_by_sha[sha] = cancelled
        self.assertEqual(self.settle(10)[0][1], "kept")
        self.assertEqual(self.hub.reruns, [(100, 777)], "run again once")
        self.assertIn("777", lv.read_pending(self.live)[0]["rerun"], "and remembered between two calls of the tool")
        self.hub.now += 4000.0     # long after the request: the same end is not a new result, but the job has ended that way twice
        self.assertEqual(self.settle(20)[0][1], "kept", "ended twice: no result is to be had, it waits for the limit")
        self.assertEqual(self.hub.reruns, [(100, 777)], "never a second request")
        self.hub.runs_by_sha[sha] = run_of([job(n) for n in CLASSES])
        self.assertEqual(self.settle(30)[0][1], "green", "the new run's result is read")

    def test_it_never_raises(self):
        file_not_folder = self.root / "afile"
        file_not_folder.write_text("x", encoding="utf-8")
        said = []
        out = github_rule.settle_pending(file_not_folder / "sub", self.hub.rule_for, self.hub.deleted.append, self.hub.logs_of,
                                         say=said.append, now=MOVED)
        self.assertEqual(out, [])
        self.add()

        def broken(commit):
            raise KeyError("rule broke")
        out = github_rule.settle_pending(self.live, broken, self.hub.deleted.append, self.hub.logs_of, say=said.append, now=MOVED)
        self.assertEqual(len(lv.read_pending(self.live)), 1, "the line stays")
        self.assertTrue(said and "could not be read" in said[0], said)

    def test_a_line_with_no_pending_file_asks_nothing(self):
        self.assertEqual(self.settle(), [])
        self.assertEqual(self.hub.asked, [])

    def test_the_gates_wrapper_reads_nothing_when_nothing_is_pending(self):
        said = []
        with mock.patch.object(gate, "read_lists", side_effect=AssertionError("the lists are read for nothing")), \
                mock.patch.object(gate, "commit_runs", side_effect=AssertionError("GitHub was asked for nothing")):
            self.assertEqual(gate.settle_github_pending(str(self.live), say=said.append), [])
        self.assertEqual(said, [])

    def test_the_gates_wrapper_reads_with_the_gates_readers(self):
        sha = self.add()
        asked = []
        said = []
        deleted = []
        with mock.patch.object(gate, "commit_runs", lambda s: asked.append(s) or run_of([job(n) for n in CLASSES])), \
                mock.patch.object(gate, "delete_candidate", deleted.append), \
                mock.patch.object(gate, "read_lists", lambda: ({}, {}, [])):
            events = gate.settle_github_pending(str(self.live), say=said.append)
        self.assertEqual(([c for c, _, _ in events], asked, deleted), ([sha], [sha], ["gate/20261121-100000-012345678"]))
        self.assertEqual(len(said), 1)
        self.assertRegex(said[0], r"^GitHub: 012345678 green, \d+ minutes after the move$")


# ---- D: the repair ------------------------------------------------------------------------------------------------

class LandingGithubFirstTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.w = lt.World(self.tmp.name)

    def test_github_first_reaches_the_batch_as_a_group_and_never_as_a_check_name(self):
        w = self.w
        w.branch("feat/a", {"native/a.cpp": "a"})
        w.branch("feat/b", {"native/b.cpp": "b"})
        L = w.landing()
        self.assertEqual(L.land(["feat/a"]), 0)
        plain = w.checker.calls[-1]
        self.assertIn("github", plain["groups"].split(","))
        self.assertNotIn("github-first", plain["groups"], "game code alone is the form read after the move")
        L = w.landing()
        self.assertEqual(L.land(["feat/b"], also=landing.GITHUB_FIRST), 0)
        forced = w.checker.calls[-1]
        self.assertIn("github-first", forced["groups"].split(","), forced["groups"])
        self.assertEqual(forced["also"], "", "the word names no check: the gate gets no --also for it")
        self.assertTrue(any("GitHub first" in l for l in L.out), L.out)

    def test_a_waiting_caller_and_a_batch_of_two_carry_it_too(self):
        w = self.w
        w.branch("feat/a", {"native/a.cpp": "a"})
        w.branch("feat/b", {"native/b.cpp": "b"})
        q = landing.Queue(w.plan / "landing-queue.tsv")
        q.add(["feat/a"], "1", lambda b: "c0")
        q.add(["feat/b"], "1", lambda b: "c1", also=landing.GITHUB_FIRST)
        L = w.landing()
        self.assertEqual(L.run_only_queue(), 0)
        self.assertEqual(len(w.checker.calls), 1, "one batch")
        self.assertIn("github-first", w.checker.calls[0]["groups"], "one branch of the batch asks: the batch waits")

    def test_gate_land_github_first_puts_the_word_in_the_queue(self):
        queued = []

        class Spy(landing.Landing):
            def land(self, branches, agent="1", wait_minutes=600.0, also=""):
                queued.append(also)
                return 0
        with mock.patch.object(landing, "Landing", Spy):
            code = gate.land_main(["native/a", "--github", "first", "--live", str(pathlib.Path(self.tmp.name) / "live"),
                                   "--queue-file", str(pathlib.Path(self.tmp.name) / "q.tsv"),
                                   "--failures", str(pathlib.Path(self.tmp.name) / "f.tsv"),
                                   "--runner-lock", str(pathlib.Path(self.tmp.name) / "runner.lock"),
                                   "--landing-tree", str(pathlib.Path(self.tmp.name) / "tree")])
        self.assertEqual(code, 0)
        self.assertEqual(queued, [landing.GITHUB_FIRST])
        with mock.patch.object(landing, "Landing", Spy):
            gate.land_main(["native/a", "--live", str(pathlib.Path(self.tmp.name) / "live"),
                            "--queue-file", str(pathlib.Path(self.tmp.name) / "q.tsv"),
                            "--failures", str(pathlib.Path(self.tmp.name) / "f.tsv"),
                            "--runner-lock", str(pathlib.Path(self.tmp.name) / "runner.lock"),
                            "--landing-tree", str(pathlib.Path(self.tmp.name) / "tree")])
        self.assertEqual(queued[-1], "", "without the option nothing is added")

    def test_gate_land_reads_the_pending_results_first(self):
        calls = []
        with mock.patch.object(gate, "settle_github_pending", lambda live, say=print: calls.append(live) or []), \
                mock.patch.object(landing, "Landing") as fake:
            fake.return_value.land.return_value = 0
            gate.land_main(["native/a", "--live", str(pathlib.Path(self.tmp.name) / "live2"),
                            "--queue-file", str(pathlib.Path(self.tmp.name) / "q.tsv"),
                            "--failures", str(pathlib.Path(self.tmp.name) / "f.tsv"),
                            "--runner-lock", str(pathlib.Path(self.tmp.name) / "runner.lock"),
                            "--landing-tree", str(pathlib.Path(self.tmp.name) / "tree")])
        self.assertEqual(calls, [str(pathlib.Path(self.tmp.name) / "live2")])
        self.assertTrue(fake.return_value.land.called)


def github_repair(t, job_name="Mac", sha="0123456789abcdef0123456789abcdef01234567", branches="native/A.2"):
    """What settle_pending writes for a red job, through the same writer as `plan.py repair add`."""
    row, new = lv.add_repair(t.live, "github", "fail", "github/" + job_name, "-", "A.2", sha, "D:/x/run", "-",
                             github_rule.repair_note(github_rule.State(job_name, "must", "red", 100, 5001,
                                                                       job={"url": "https://github.com/e/r/actions/runs/100/job/5001"}),
                                                     sha, branches, "error: boom"))
    return row["id"]


class RepairWorkTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.calls, self.settled = [], []
        self.verdict = plan.Verdict(True, [("repo/code", "PASS", "ok")])
        self.verdict.commit = "0123456789"

        def fake_land(folder, branch, tablet, wait, *rest):
            self.calls.append({"folder": pathlib.Path(folder), "branch": branch, "options": list(rest[0]) if rest else []})
            return self.verdict
        for patcher in (mock.patch.object(plan, "LAND", fake_land),
                        mock.patch.object(plan, "SETTLE", lambda live: self.settled.append(live) or [])):
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_the_repair_of_a_red_job_is_in_one_file_and_says_what_to_do(self):
        rid = github_repair(self.t)
        text = self.t.run(["next"], expect=0)[1]
        self.assertTrue(text.startswith("REPAIR: %s" % rid), text[:60])
        self.assertIn("What failed: github/Mac", text)
        self.assertIn("GitHub's job Mac is red on 012345678, landed from native/A.2: error: boom", text)
        self.assertIn("gh run view 100 --log-failed --job 5001", text, "the whole command that prints the failed part of the log")
        self.assertIn("https://github.com/e/r/actions/runs/100/job/5001", text)
        self.assertEqual([r["source"] for r in lv.read_repairs(self.t.live)], ["github"])

    def test_next_status_and_land_read_the_pending_results_first_and_land_again_after(self):
        self.t.run(["next"], expect=0)
        self.t.run(["status"], expect=0)
        self.assertEqual(len(self.settled), 2, "next and status read it once each")
        self.settled.clear()
        self.t.run(["start", "A.1"], expect=0)
        ready = self.t.commit("A.1", "A.1: the code")
        self.assertTrue(ready)
        self.t.run(["land", "A.1"])
        self.assertEqual(len(self.settled), 2, "at its start and after its landing ended")
        self.settled.clear()
        self.t.run(["start", "B.1"])
        self.assertEqual(self.settled, [], "start reads nothing from GitHub")

    def test_a_repair_of_a_red_job_is_landed_in_the_waiting_form_and_another_repair_keeps_its_also(self):
        rid = github_repair(self.t)
        self.t.run(["start", rid], expect=0)
        self.t.commit("R", "fix")
        self.t.run(["land", rid], expect=0)
        options = self.calls[-1]["options"]
        self.assertEqual(options[-2:], ["--github", "first"], "the fix is proven on GitHub before main moves")
        self.assertNotIn("--also", options, "github/Mac is no check of the gate")
        self.assertEqual(lv.read_repairs(self.t.live)[0]["state"], "landed")
        # a nightly repair keeps its --also and is not forced to wait
        self.t.run(["next"], expect=0)
        lv.add_repair(self.t.live, "nightly", "fail", "tour/d3d11", "d3d11", note="stop 25 differs", evidence="e")
        self.t.run(["start", "R-002"], expect=0)
        self.t.commit("R", "fix 2", {"runtime/src/render/a.cpp": "a\nz\n"})
        self.t.run(["land", "R-002"], expect=0)
        self.assertEqual(self.calls[-1]["options"][-2:], ["--also", "tour/d3d11"])
        self.assertNotIn("--github", self.calls[-1]["options"])

    def test_a_step_in_work_is_kept_while_the_repair_lands_and_goes_on_on_top_of_the_new_main(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        step_commit = t.commit("A.1", "A.1: the code", {"runtime/src/render/a.cpp": "a\nb\nc\nd\n", "docs/step-note.txt": "mine\n"})
        step_tip = t.git("rev-parse", "HEAD", cwd=t.folder())
        # another landing moves main while the step is in work
        t.landed("C.1")
        main_before = t.git("rev-parse", "origin/main")
        rid = github_repair(t)
        # next names the repair first, and says the step is kept on its branch
        text = t.run(["next"], expect=0)[1]
        self.assertTrue(text.startswith("REPAIR: %s" % rid), text[:80])
        self.assertNotIn("IN WORK", text)
        self.assertIn("KEPT: step A.1 is in work on branch native/A.1.", text)
        self.assertIn("`plan.py start %s` keeps it on that branch" % rid, text)
        self.assertIn("`plan.py start A.1` goes on with the step on its own branch, on top of the new main", text)
        # a repair that is not GitHub's does not push a step aside
        self.assertEqual(len(self.calls), 0)
        # start: the step is kept (blocked in the live state with the reason), its branch is on origin, the repair is started
        code, text = t.run(["start", rid], expect=0)
        self.assertIn("kept A.1 on its branch native/A.1", text)
        self.assertIn("started %s: branch native/repair-%s" % (rid, rid[2:]), text)
        state = lv.read_state(t.live)
        self.assertEqual((state["A.1"]["state"], state["A.1"]["reason"]), ("blocked", "kept on its branch for repair %s" % rid))
        self.assertEqual(lv.read_repairs(t.live)[0]["state"], "started")
        self.assertEqual(t.git("rev-parse", "refs/remotes/origin/native/A.1"), step_tip, "the branch is on origin: nothing is lost")
        self.assertEqual(t.git("rev-parse", "--abbrev-ref", "HEAD", cwd=t.folder()), "native/repair-%s" % rid[2:])
        self.assertEqual(t.git("rev-parse", "HEAD", cwd=t.folder()), main_before, "the repair is cut from main's newest commit")
        # the repair is landed with --github first; the steps stay as they are meanwhile
        t.commit("R", "fix the mac build", {"runtime/src/render/b_mac.cpp": "fix\n"})
        t.run(["land", rid], expect=0)
        self.assertEqual(self.calls[-1]["options"][-2:], ["--github", "first"])
        self.assertEqual(self.calls[-1]["branch"], "native/repair-%s" % rid[2:])
        # main moves again (the repair, and one more landing): the step is now two commits behind
        t.git("checkout", "-q", "main")
        (t.tree / "docs" / "after-repair.txt").write_text("r\n", encoding="utf-8")
        t.push_plan_files("main moves again")
        # next offers the kept step, nothing else
        text = t.run(["next"], expect=0)[1]
        self.assertTrue(text.startswith("NEXT: A.1"), text[:60])
        self.assertIn("kept on its branch native/A.1", text)
        self.assertNotIn("REPAIR:", text)
        # start goes on with the step on its own branch, on top of the new main, with its commits
        code, text = t.run(["start", "A.1"], expect=0)
        self.assertIn("started A.1: branch native/A.1", text)
        folder = t.folder()
        self.assertEqual(t.git("rev-parse", "--abbrev-ref", "HEAD", cwd=folder), "native/A.1")
        self.assertEqual(t.git("merge-base", "--is-ancestor", "origin/main", "HEAD", cwd=folder), "", "on top of the new main")
        self.assertEqual(t.git("log", "-1", "--format=%s", cwd=folder), "A.1: the code", "its own commit is the tip, nothing lost")
        self.assertEqual((folder / "docs" / "step-note.txt").read_text(encoding="utf-8"), "mine\n")
        self.assertEqual((folder / "docs" / "after-repair.txt").read_text(encoding="utf-8"), "r\n", "and main's newest file is there")
        self.assertEqual(t.git("rev-list", "--count", "origin/main..HEAD", cwd=folder), "1")
        self.assertEqual(lv.read_state(t.live)["A.1"]["state"], "started")
        self.assertIsNotNone(step_commit)

    def test_uncommitted_work_is_not_put_aside_and_nothing_changes(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        rid = github_repair(t)
        (t.folder() / "scratch.txt").write_text("not committed\n", encoding="utf-8")
        code, text = t.run(["start", rid])
        self.assertEqual(code, 1)
        self.assertIn("uncommitted changes", text)
        self.assertEqual(lv.read_state(t.live)["A.1"]["state"], "started", "the step is still in work")
        self.assertEqual(lv.read_repairs(t.live)[0]["state"], "open")
        self.assertEqual(t.git("rev-parse", "--abbrev-ref", "HEAD", cwd=t.folder()), "native/A.1")

    def test_a_repair_that_is_not_githubs_does_not_push_a_step_aside(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        lv.add_repair(t.live, "nightly", "fail", "tour/d3d11", "d3d11", note="x", evidence="e")
        text = t.run(["next"], expect=0)[1]
        self.assertTrue(text.startswith("IN WORK: A.1"), text[:40])
        code, text = t.run(["start", "R-001"])
        self.assertEqual(code, 1)
        self.assertIn("you have A.1 in work", text)

    def test_a_kept_step_that_does_not_apply_on_the_new_main_is_left_as_it_was(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        t.commit("A.1", "A.1: the code", {"runtime/src/render/a.cpp": "a\nb\nc\nstep\n"})
        step_tip = t.git("rev-parse", "HEAD", cwd=t.folder())
        rid = github_repair(t)
        t.run(["start", rid], expect=0)
        t.run(["blocked", rid, "later"], expect=0)           # the repair is set aside: the step may go on
        t.git("checkout", "-q", "main")
        (t.tree / "runtime" / "src" / "render" / "a.cpp").write_text("a\nb\nc\nmain\n", encoding="utf-8")
        t.push_plan_files("main changes the same line")
        code, text = t.run(["start", "A.1"])
        self.assertEqual(code, 1, text)
        self.assertIn("do not apply on top of origin/main", text)
        self.assertEqual(t.git("rev-parse", "native/A.1", cwd=t.folder()), step_tip, "the branch is as it was")
        self.assertEqual(t.git("status", "--porcelain", cwd=t.folder()), "", "no rebase is left half done")
        self.assertEqual(lv.read_state(t.live)["A.1"]["state"], "blocked", "still kept")

    def test_a_blocked_step_that_was_not_kept_is_started_as_before(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        t.commit("A.1", "A.1: the code")
        t.run(["blocked", "A.1", "stuck"], expect=0)
        text = t.run(["next"], expect=0)[1]
        self.assertNotIn("kept on its branch", text)
        self.assertFalse(plan.is_kept({"state": "blocked", "reason": "stuck"}))


# ---- E: the workflow ----------------------------------------------------------------------------------------------

WORKFLOW = (TREE / ".github" / "workflows" / "check.yml").read_text(encoding="utf-8")


def condition_holds(text, ref, event):
    """The value of a workflow `if:` for a push to `ref` (or another event): what check.yml uses (== on github.ref and
    github.event_name, startsWith(github.ref, ...), && || ! and brackets), nothing else."""
    expr = text.strip()
    if expr.startswith("${{") and expr.endswith("}}"):
        expr = expr[3:-2].strip()
    expr = re.sub(r"startsWith\(github\.ref,\s*('[^']*')\)", r"REF.startswith(\1)", expr)
    expr = expr.replace("github.ref", "REF").replace("github.event_name", "EVENT")
    expr = expr.replace("&&", " and ").replace("||", " or ")
    expr = re.sub(r"!(?!=)", " not ", expr)
    leftover = re.sub(r"'[^']*'", "", expr)
    leftover = re.sub(r"\b(REF|EVENT|and|or|not|startswith)\b", "", leftover)
    assert re.fullmatch(r"[\s().=!]*", leftover), "a condition this test cannot read: %r" % text
    return bool(eval(expr, {"__builtins__": {}}, {"REF": ref, "EVENT": event}))  # noqa: S307 - the text is check.yml's own


def job_blocks(text):
    """{job id: {name, needs, if}} of the top-level jobs of check.yml."""
    jobs, current = {}, None
    in_jobs = False
    for line in text.splitlines():
        if re.match(r"^jobs:\s*$", line):
            in_jobs = True
            continue
        if not in_jobs:
            continue
        m = re.match(r"^  ([A-Za-z0-9_-]+):\s*$", line)
        if m:
            current = jobs.setdefault(m.group(1), {})
            continue
        if current is None:
            continue
        for key in ("name", "needs", "if"):
            m = re.match(r"^    %s:\s*(.*?)\s*$" % key, line)
            if m and key not in current:
                current[key] = m.group(1).strip("'\"") if key != "if" else m.group(1)
    return jobs


class WorkflowTest(unittest.TestCase):
    def test_the_triggers_are_main_and_candidates_pull_requests_and_by_hand(self):
        m = re.search(r"^on:\n(?P<body>(?:[ \t]+.*\n|\n)+)", WORKFLOW, re.M)
        body = m.group("body")
        push = re.search(r"^  push:\n    branches:\s*\[(?P<list>[^\]]*)\]", body, re.M)
        self.assertIsNotNone(push, "on.push.branches is a list")
        branches = [b.strip().strip("'\"") for b in push.group("list").split(",")]
        self.assertEqual(branches, ["main", "gate/**"], "a work branch's push starts nothing")
        self.assertIn("\n  pull_request:", "\n" + body)
        self.assertIn("\n  workflow_dispatch:", "\n" + body)
        self.assertNotIn("'**'", body)
        self.assertNotIn("Every push", WORKFLOW, "the comments say what is true")

    def test_the_jobs_a_landing_does_not_read_do_not_run_on_a_candidate(self):
        jobs = job_blocks(WORKFLOW)

        def classes_of(block_name):
            """The table rows a job's name stands for (a matrix leg's `${{ ... }}` is any text)."""
            parts = re.split(r"\$\{\{.*?\}\}", block_name)
            pattern = ".+".join(re.escape(p) for p in parts)
            return {name: klass for name, klass in CLASSES.items() if re.fullmatch(pattern, name)}
        nightly_ids = {ident for ident, block in jobs.items() if set(classes_of(block["name"]).values()) == {"nightly"}}
        self.assertEqual(len(nightly_ids), 3, "the two Windows jobs and the Android build (its matrix is two table rows): %s" % nightly_ids)
        candidate, main_push, hand, pull = ("refs/heads/gate/20261121-1-abc", "push"), ("refs/heads/main", "push"), \
            ("refs/heads/some-branch", "workflow_dispatch"), ("refs/pull/1/merge", "pull_request")
        seen = set()
        for ident, block in jobs.items():
            found = classes_of(block["name"])
            self.assertTrue(found, "the table names the job %s" % block["name"])
            seen.update(found)
            needs_nightly = block.get("needs") in nightly_ids
            condition = block.get("if")

            def runs(ref, event):
                return True if condition is None else condition_holds(condition, ref, event)
            if ident in nightly_ids or needs_nightly:
                self.assertFalse(runs(*candidate), "%s (%s) must not run on a candidate" % (block["name"], sorted(set(found.values()))))
                self.assertTrue(runs(*main_push) and runs(*hand), "%s runs on main and by hand" % block["name"])
                self.assertFalse(runs(*pull), "%s does not run on a pull request" % block["name"])
            else:
                self.assertTrue(runs(*candidate), "%s is read by a landing: it runs on a candidate" % block["name"])
        self.assertEqual(seen, set(CLASSES), "every row of the table is a job of the workflow")
        start = next(b for b in jobs.values() if b["name"] == "Android start check (x86_64 emulator)")
        self.assertEqual(start["needs"], "android")
        self.assertIn("if", start, "it needs an Android build, so it has its own condition")

    def test_the_conditions_are_read_as_github_reads_them(self):
        cond = "${{ github.ref == 'refs/heads/main' || github.event_name == 'workflow_dispatch' }}"
        self.assertTrue(condition_holds(cond, "refs/heads/main", "push"))
        self.assertTrue(condition_holds(cond, "refs/heads/x", "workflow_dispatch"))
        self.assertFalse(condition_holds(cond, "refs/heads/gate/1", "push"))
        old = "${{ github.ref == 'refs/heads/main' || startsWith(github.ref, 'refs/heads/gate/') || github.event_name == 'workflow_dispatch' }}"
        self.assertTrue(condition_holds(old, "refs/heads/gate/1", "push"), "the old condition ran everything on a candidate")
        self.assertTrue(condition_holds("github.ref != 'refs/heads/main' && !startsWith(github.ref, 'refs/heads/gate/')", "refs/heads/x", "push"))

    def test_the_table_still_names_every_job(self):
        self.assertEqual(github_rule.table_problems(CLASSES, WORKFLOW), [])


# ---- F: main moves in one way only ---------------------------------------------------------------------------------

class MarkTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)

    def runner_lock(self, pid):
        lock = self.root / "runner.lock"
        lock.mkdir(exist_ok=True)
        (lock / "holder.txt").write_text("landing runner pid %d 2026-11-21T11:00:00\n" % pid, encoding="utf-8")
        return str(lock)

    def test_a_move_by_hand_is_refused_before_anything_runs_on_a_real_command_line(self):
        work, gate_lock = self.root / "work", self.root / "gate.lock"
        cmd = [sys.executable, "-X", "utf8", "-I", str(TREE / "tools" / "gate" / "gate.py"), "--main", "--move", "--scope", "repo",
               "--work", str(work), "--gate-lock", str(gate_lock), "--board", "", "--status", "", "--tablet", "T",
               "--live", str(self.root / "live")]
        done = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", cwd=str(TREE), timeout=120)
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertIn("main moves only through gate.py land <branch>", done.stderr)
        self.assertEqual(done.stdout.strip().splitlines()[-1], "VERDICT not-judged")
        self.assertFalse(work.exists(), "no run folder is made")
        self.assertFalse(gate_lock.exists(), "no lock is taken")
        self.assertFalse((self.root / "live").exists())

    def test_a_guess_does_not_pass(self):
        def quiet(argv):
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                return gate.main(argv)
        for pid in (0, 1, os.getpid()):
            argv = ["--main", "--move", "--scope", "repo", "--work", str(self.root / "w"), "--board", "", "--status", "",
                    "--landing-runner", str(pid), "--runner-lock", str(self.root / "no-such.lock")]
            self.assertEqual(quiet(argv), 2, "no lock at all: pid %d" % pid)
        other = self.runner_lock(os.getpid() + 1000000)         # the lock is held by another process
        argv = ["--main", "--move", "--scope", "repo", "--work", str(self.root / "w"), "--board", "", "--status", "",
                "--landing-runner", str(os.getpid()), "--runner-lock", other]
        self.assertEqual(quiet(argv), 2, "a pid that is not the holder of the runner's lock")
        self.assertFalse((self.root / "w").exists())

    def test_called_by_runner(self):
        lock = self.runner_lock(4242)
        alive = lambda pid: pid == 4242  # noqa: E731
        self.assertTrue(landing.called_by_runner(4242, lock, alive))
        self.assertFalse(landing.called_by_runner(4243, lock, alive), "not the holder")
        self.assertFalse(landing.called_by_runner(4242, lock, lambda pid: False), "the holder's process has ended")
        self.assertFalse(landing.called_by_runner(0, lock, alive))
        self.assertFalse(landing.called_by_runner(4242, str(self.root / "none.lock"), alive), "no lock")
        (pathlib.Path(lock) / "holder.txt").write_text("garbage\n", encoding="utf-8")
        self.assertFalse(landing.called_by_runner(4242, lock, alive))

    def test_the_runners_own_call_carries_the_mark_and_it_moves(self):
        args = gate.parser(land=True).parse_args(["b", "--tablet", "T", "--runner-lock", str(self.root / "runner.lock")])
        args.mode = "main"
        cmd = gate.GateChecker(args, say=lambda l: None).command(self.root, ["b"], "repo,github")
        self.assertEqual(cmd[cmd.index("--landing-runner") + 1], str(os.getpid()))
        self.assertEqual(cmd[cmd.index("--runner-lock") + 1], str(self.root / "runner.lock"))
        # the runner takes its lock as the process that makes the call: the mark is then true
        lock = landing.FolderLock(self.root / "runner.lock")
        self.assertTrue(lock.try_take("landing runner"))
        self.addCleanup(lock.free)
        self.assertTrue(landing.called_by_runner(int(cmd[cmd.index("--landing-runner") + 1]), cmd[cmd.index("--runner-lock") + 1]))
        # and gate.main of that command line gets past the refusal (it moves, with the fakes of the other tests)
        world = ggt.World(ggt.runs_of(ggt.green()))
        code, lines, work = ggt.drive(self, world, "repo,github")
        self.assertEqual((code, world.moved), (0, [SHA]), "\n".join(lines))

    def test_the_options_are_not_in_the_help_of_a_person(self):
        self.assertNotIn("landing-runner", gate.parser().format_help())
        self.assertNotIn("runner-lock", gate.parser().format_help())


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("github_after_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
