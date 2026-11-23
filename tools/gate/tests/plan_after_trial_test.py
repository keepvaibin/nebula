"""plan_after_trial_test.py: what the trial of steps 6 to 11 showed wrong in the plan tool, mended (brief AH).

The commands run in a temporary git tree made by plan_fixture (a bare origin, a plan tree, one work folder) with the gate replaced by a fake,
and, for the unit tests, with units.RUN replaced by a fake that answers as the build script, ctest and cargo do: no game, no build, no network,
nothing of D:/NebulaWork/plan.

    python -X utf8 -I tools/gate/tests/plan_after_trial_test.py

  words_rule        what `start` says of the words "replay reference" is what `check` refuses: a replay file changed with no such words is
                    refused, and a step that records nothing is not asked for them
  units             `check` builds, runs the Windows unit tests, the game's and the Rust tests, refuses a failing one by name with its own lines,
                    names what this PC cannot run as not proven here; `--no-units` and `--no-build`; `land` runs none of it
  state_commit      a landing that failed or was not judged leaves no commit `Plan: step X landed` on the branch (changes kept, later commits kept);
                    a pass keeps it; the next landing makes it once
  own_replays       `check` on a step written ahead counts the replay files of that step, not those of the step below it
  mend_ahead        while a mend is open the step after the one in work is written (`ahead`), nothing is started or landed, and a mend that touches
                    a file that step touches, or waits for no step, stops it; the helper's prompt asks for one report at its end
  decide            `decide --replaces` corrects an earlier decision, the file keeps both, the page and `--list` show the newest
"""
import pathlib
import sys
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "plan"))
import live as lv  # noqa: E402
import plan  # noqa: E402
import units  # noqa: E402
from plan_ahead_test import AheadBase, not_judged_verdict  # noqa: E402
from plan_fixture import Tree, ledger_row, ready_a1, setup_module, teardown_module  # noqa: E402
from plan_rerun_test import REPLAY, STATE_SUBJECT, RerunBase, failed_verdict, passed_verdict  # noqa: E402


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


# ---- the words "replay reference" -----------------------------------------------------------------------------------

class WordsRule(RerunBase):
    def allow(self):
        for tree in (self.folder, self.t.tree):
            path = plan.native(tree) / "03-X.md"
            path.write_text(path.read_text(encoding="utf-8").replace("- **Proof.** a test.", "- **Proof.** a test. The replay is recorded again."),
                            encoding="utf-8")
        self.t.git("commit", "-q", "-am", "card", cwd=self.folder)

    def test_start_says_what_check_refuses_and_nothing_more(self):
        self.t.landed("A.1")
        path = plan.native(self.t.tree) / "03-X.md"
        path.write_text(path.read_text(encoding="utf-8").replace("- **Rows.** P-003 whole.", "- **Rows.** P-003 whole.\n- **Proof.** re-record."),
                        encoding="utf-8")
        self.t.push_plan_files("card")
        said = self.t.run(["start", "A.2"], expect=0)[1]
        self.assertIn(plan.REPLAY_WORDS_RULE, said)
        self.assertNotIn("A commit message of the landing must contain", said, "the old sentence said it for every change")
        self.assertIn("changes a file under tests/replays/", plan.REPLAY_WORDS_RULE)
        self.assertIn("A step that records no replay reference needs no such words", plan.REPLAY_WORDS_RULE)

    def test_a_replay_file_changed_with_no_such_words_is_refused_by_check_and_with_them_it_is_not(self):
        ready_a1(self.t)
        self.allow()
        self.t.commit("A.1", "the hashes", {REPLAY: "# changed\n0\tabc\t3\n"})
        self.permit(REPLAY)
        code, text = self.t.run(["check", "--no-units"])
        self.assertEqual(code, 1)
        self.assertIn('no commit message of the landing contains the words "%s"' % plan.REPLAY_WORDS, text)
        self.t.git("commit", "--allow-empty", "-m", "A.1: the replay reference is recorded again", cwd=self.folder)
        code, text = self.t.run(["check", "--no-units"])
        self.assertNotIn("replay", text, text)

    def test_a_step_whose_card_allows_the_change_and_that_records_nothing_is_not_asked_for_the_words(self):
        ready_a1(self.t)
        self.allow()
        code, text = self.t.run(["check", "--no-units"])
        self.assertNotIn(plan.REPLAY_WORDS, text)


# ---- the unit tests `check` runs ------------------------------------------------------------------------------------

CTEST_PASS = """Test project D:/x/msvc
    Start 1: galaxy_a_tests
1/3 Test #1: galaxy_a_tests ....................   Passed    0.01 sec
2/3 Test #2: galaxy_b_tests ....................   Passed    0.01 sec
3/3 Test #3: galaxy_launch_args_tests ..........   Passed    0.02 sec

100% tests passed, 0 tests failed out of 3

Total Test time (real) =   0.04 sec
"""
CTEST_FAIL = """Test project D:/x/msvc
1/3 Test #1: galaxy_a_tests ....................   Passed    0.01 sec
2/3 Test #2: galaxy_b_tests ....................   Passed    0.01 sec
3/3 Test #3: galaxy_launch_args_tests ..........***Failed    0.02 sec
every diagnostic has a name: bit 19 has none
launch_args_tests.cpp:77: check failed

67% tests passed, 1 tests failed out of 3

Total Test time (real) =   0.04 sec

The following tests FAILED:
\t  3 - galaxy_launch_args_tests (Failed)
"""
GAME_PASS = CTEST_PASS.replace("galaxy_a_tests", "native_a_tests").replace("galaxy_b_tests", "native_b_tests").replace(
    "galaxy_launch_args_tests", "native_c_tests")
CARGO_PASS = "running 2 tests\ntest a ... ok\ntest result: ok. 2 passed; 0 failed; 0 ignored\n"
CARGO_FAIL = "running 2 tests\ntest a ... FAILED\ntest result: FAILED. 1 passed; 1 failed; 0 ignored\n"
SHARED = "galaxy_test(galaxy_a_tests SOURCES a.cpp)\ngalaxy_test(galaxy_b_tests SOURCES b.cpp)\ngalaxy_test(galaxy_launch_args_tests SOURCES c.cpp)\n"
JOBS = "job\tclass\nMac\tmust\nLinux\tmust\nLinux unit tests\tmust\nRust tests (windows-2022)\tmust\nWindows game modules\tnightly\n"


class UnitsBase(RerunBase):
    """The work folder holds the build script, the ctest script, the shared list of unit tests and the table of GitHub's jobs; `units.RUN` is
    a fake that records what it was asked and answers from `answers`."""

    def setUp(self):
        super().setUp()
        for name, text in (("tools/gate/build_windows.ps1", "# build\n"), ("tools/gate/ctest.ps1", "# ctest\n"), ("cmake/unit_tests.cmake", SHARED),
                           ("tools/gate/github_jobs.tsv", JOBS)):
            path = self.t.tree / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        self.t.push_plan_files("the scripts and lists the unit tests read")           # they are main's: the branch does not change them
        self.t.git("fetch", "-q", "origin", cwd=self.folder)
        self.t.git("rebase", "-q", "origin/main", cwd=self.folder)
        self.asked = []
        self.answers = {"build": (0, "ok"), "msvc": (0, CTEST_PASS), "game": (0, GAME_PASS), "cargo": (0, CARGO_PASS)}
        for patcher in (mock.patch.object(units, "RUN", self.fake_run), mock.patch.object(units.shutil, "which", lambda name: "cargo.exe")):
            patcher.start()
            self.addCleanup(patcher.stop)

    def fake_run(self, argv, cwd=None, env=None):
        argv = [str(x) for x in argv]
        joined = " ".join(argv)
        self.asked.append(joined)
        if "build_windows.ps1" in joined:
            return self.answers["build"]
        if "ctest.ps1" in joined:
            return self.answers["msvc" if "/msvc" in joined else "game"]
        if argv[0] == "cargo":
            return self.answers["cargo"]
        raise AssertionError("an unknown process: " + joined)

    def check(self, args=()):
        return self.t.run(["check", *args])


class UnitsTest(UnitsBase):
    def test_a_passing_run_says_what_passed_and_names_what_is_not_proven_here(self):
        ready_a1(self.t)
        code, text = self.check()
        self.assertEqual(code, 0, text)
        self.assertIn("the build passed", text)
        self.assertIn("the Windows unit tests: 3 of 3 pass", text)
        self.assertIn("the game's unit tests: 3 of 3 pass", text)
        self.assertIn("the Rust tests: 2 pass", text)
        self.assertEqual(len(self.asked), 4, self.asked)
        build = "%s/Nebula-native" % plan.BUILD_ROOT
        fill = lambda number: plan.AGENT_COMMANDS[number][2].format(folder=self.folder.as_posix(), build=build, pak="")   # noqa: E731
        self.assertEqual(plan.AGENT_COMMANDS[0][0], "build")
        self.assertEqual(self.asked[0], fill(0), "the build is the agent's own build command")
        self.assertEqual(plan.AGENT_COMMANDS[1][0], "unit")
        self.assertEqual(self.asked[1] + " -Name <name>", fill(1), "ctest is the agent's own unit command, over every test")
        self.assertIn("-Tree %s" % self.folder.as_posix(), self.asked[0])
        self.assertIn("/msvc", self.asked[1])
        self.assertIn("/game", self.asked[2])
        self.assertIn("not proven here, GitHub's jobs this PC does not run: Mac; Linux; Linux unit tests", text)
        self.assertNotIn("Rust tests (windows-2022)", text, "the Windows Rust job is the one that is run")
        self.assertNotIn("Windows game modules", text, "a job that is not of class must is not asked for")

    def test_the_unit_tests_only_the_shared_list_has_are_named_by_name(self):
        ready_a1(self.t)
        (self.folder / "cmake" / "unit_tests.cmake").write_text(SHARED + "galaxy_test(galaxy_linux_only_tests SOURCES l.cpp)\n", encoding="utf-8")
        code, text = self.check()
        self.assertIn("not proven here, GitHub's Linux and Mac unit tests build them and none ran on this PC: galaxy_linux_only_tests", text)
        self.assertNotIn("galaxy_a_tests,", text)

    def test_a_failing_unit_test_is_refused_by_name_with_its_own_lines(self):
        ready_a1(self.t)
        self.answers["msvc"] = (1, CTEST_FAIL)
        code, text = self.check()
        self.assertEqual(code, 1)
        self.assertIn("the Windows unit tests: 1 of 3 fail:", text)
        self.assertIn("galaxy_launch_args_tests: ***Failed", text)
        self.assertIn("every diagnostic has a name: bit 19 has none", text)
        self.assertNotIn("67% tests passed", text, "ctest's own summary is not the failed test's output")
        self.assertIn("the game's unit tests: 3 of 3 pass", text, "the other parts still run and say what they found")

    def test_a_failing_game_test_and_a_failing_rust_test_are_each_a_refusal(self):
        ready_a1(self.t)
        self.answers["game"] = (1, CTEST_FAIL.replace("galaxy_", "native_"))
        self.answers["cargo"] = (101, CARGO_FAIL)
        code, text = self.check()
        self.assertEqual(code, 1)
        self.assertIn("the game's unit tests: 1 of 3 fail:", text)
        self.assertIn("the Rust tests: 1 fail, 1 pass", text)
        self.assertIn("2 things would refuse `land A.1`", text)

    def test_a_ctest_run_that_ran_no_test_is_a_refusal_not_a_pass(self):
        ready_a1(self.t)
        self.answers["msvc"] = (0, "No tests were found!!!\n")
        code, text = self.check()
        self.assertEqual(code, 1)
        self.assertIn("the Windows unit tests: no test ran", text)

    def test_a_build_that_fails_is_a_refusal_and_runs_no_test(self):
        ready_a1(self.t)
        self.answers["build"] = (1, "[1/9] cl\nFAILED: runtime/x.obj\nx.cpp(3): error C2065: 'y': undeclared identifier\n")
        code, text = self.check()
        self.assertEqual(code, 1)
        self.assertIn("the build fails (exit 1):", text)
        self.assertIn("error C2065", text)
        self.assertIn("the unit tests were not run", text)
        self.assertEqual(len(self.asked), 1, "nothing was run on a tree that is not built")

    def test_a_build_that_waited_too_long_for_a_speed_measure_says_so(self):
        ready_a1(self.t)
        self.answers["build"] = (3, "a speed measure runs\n")
        code, text = self.check()
        self.assertEqual(code, 1)
        self.assertIn("a speed measure still ran after the minutes it waits; give `check` again later", text)

    def test_no_units_runs_nothing_and_says_so(self):
        ready_a1(self.t)
        code, text = self.check(["--no-units"])
        self.assertEqual(code, 0, text)
        self.assertEqual(self.asked, [])
        self.assertIn("unit tests: not run (--no-units); run `check` whole before `land`", text)

    def test_no_build_runs_the_tests_of_the_build_folder_as_it_stands(self):
        ready_a1(self.t)
        code, text = self.check(["--no-build"])
        self.assertEqual(code, 0, text)
        self.assertEqual(len(self.asked), 3, self.asked)
        self.assertFalse(any("build_windows" in a for a in self.asked))
        self.assertIn("the build was not run (--no-build)", text)

    def test_a_pc_with_no_cargo_names_the_rust_job_as_not_proven(self):
        ready_a1(self.t)
        with mock.patch.object(units.shutil, "which", lambda name: None):
            code, text = self.check()
        self.assertEqual(code, 0, text)
        self.assertIn("not proven here, GitHub's Rust job: there is no cargo on this PC", text)

    def test_a_folder_with_no_build_script_has_nothing_to_run(self):
        ready_a1(self.t)
        with mock.patch.object(units, "BUILD_SCRIPT", "tools/gate/no_such_build.ps1"):
            code, text = self.check()
        self.assertEqual(code, 0, text)
        self.assertEqual(self.asked, [])
        self.assertIn("unit tests: not run (", text)

    def test_the_build_goes_to_the_build_root_asked_for(self):
        ready_a1(self.t)
        code, text = run_with_build_root(self.t, "D:/x/plant")
        self.assertEqual(code, 0, text)
        self.assertIn("-Root D:/x/plant/Nebula-native", self.asked[0])

    def test_land_runs_no_unit_test(self):
        ready_a1(self.t)
        self.results = [passed_verdict()]
        self.land(expect=0)
        self.assertEqual(self.asked, [], "the gate runs the unit tests of a landing, once")


def run_with_build_root(t, root):
    from plan_fixture import run
    return run(["--build-root", root, "check"], t.tree, t.work, t.store, None, t.live)


# ---- a landing that did not pass leaves no commit that says `landed` -----------------------------------------------

class HookBase(RerunBase):
    hook = None

    def fake_land(self, folder, branch, tablet, wait, options=(), **kw):
        if self.hook:
            self.hook()
        return super().fake_land(folder, branch, tablet, wait, options, **kw)


class StateCommitTest(HookBase):
    def test_a_failed_landing_leaves_no_commit_that_says_landed_and_the_branch_is_as_it_was(self):
        ready_a1(self.t)
        before = self.head()
        self.results = [failed_verdict()]
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertEqual(self.state_commits(), [])
        self.assertEqual(self.head(), before)
        self.assertIn("The commit `Plan: step A.1 landed` is off native/A.1: A.1 is not on main.", text)
        self.assertEqual(plan.find(plan.read_base(self.folder), "A.1")["state"], "open", "steps.tsv of the folder says nothing is landed")
        self.assertEqual(self.t.git("status", "--porcelain", "--untracked-files=no", cwd=self.folder), "")

    def test_a_landing_not_judged_leaves_none_either(self):
        ready_a1(self.t)
        before = self.head()
        self.results = [not_judged_verdict()]
        self.assertEqual(self.land()[0], 4)
        self.assertEqual(self.state_commits(), [])
        self.assertEqual(self.head(), before)

    def test_the_third_same_failure_stops_the_retries_and_leaves_none_either(self):
        ready_a1(self.t)
        before = self.head()
        self.results = [plan.Verdict(False, [("tour/d3d12", "FAIL", "replay differs")], [("tour/d3d12", "FAIL", "replay differs")])]
        self.assertEqual([self.land()[0] for _ in range(3)], [1, 1, 3])
        self.assertEqual(self.state_commits(), [])
        self.assertEqual(self.head(), before)

    def test_the_next_landing_makes_it_once_and_a_pass_keeps_it(self):
        ready_a1(self.t)
        self.results = [failed_verdict()]
        self.land()
        self.fix("the cause")
        self.results = [passed_verdict()]
        self.land(expect=0)
        self.assertEqual(self.state_commits(), [STATE_SUBJECT], "one commit that says landed, made for a step that landed")
        self.assertEqual(self.subjects()[0], STATE_SUBJECT)

    def test_uncommitted_changes_made_while_it_played_are_kept(self):
        ready_a1(self.t)
        self.hook = lambda: (self.folder / "runtime" / "src" / "render" / "a.cpp").write_text("a\nb\nc\nd\nwork in progress\n", encoding="utf-8")
        self.results = [failed_verdict()]
        self.assertEqual(self.land()[0], 1)
        self.assertEqual(self.state_commits(), [])
        self.assertIn("work in progress", (self.folder / "runtime" / "src" / "render" / "a.cpp").read_text(encoding="utf-8"))
        self.assertEqual(plan.find(plan.read_base(self.folder), "A.1")["state"], "open", "steps.tsv is put back")
        self.assertEqual(self.t.git("status", "--porcelain", "--untracked-files=no", cwd=self.folder), "M runtime/src/render/a.cpp")

    def test_a_commit_made_while_it_played_stands_and_the_state_commit_under_it_goes(self):
        ready_a1(self.t)
        self.hook = lambda: self.t.commit("A.1", "made while it played", {"runtime/src/render/a.cpp": "a\nb\nc\nd\nlater\n"})
        self.results = [failed_verdict()]
        self.assertEqual(self.land()[0], 1)
        self.assertEqual(self.state_commits(), [])
        self.assertEqual(self.subjects()[0], "made while it played")
        self.assertIn("later", (self.folder / "runtime" / "src" / "render" / "a.cpp").read_text(encoding="utf-8"))

    def test_the_tool_does_not_take_off_a_commit_that_is_not_its_own(self):
        ready_a1(self.t)
        before = self.head()
        a = type("A", (), {"live": str(self.t.live), "work_root": str(self.t.work), "agent": 1})()
        plan.state_commit_after(a, self.folder, "native/A.1", "A.1", before, 1)
        self.assertEqual(self.head(), before, "the tip is the code commit, not the tool's own: it stays")


class StateCommitAheadTest(AheadBase):
    def hand_in_while_ahead_is_written(self, verdict, hook=None):
        """`land A.1` while the gate plays: `ahead` runs (the folder goes to native/A.2, which gets a commit of its own), then the gate answers."""
        ready_a1(self.t)
        self.results = [verdict]
        self.hooks = [hook or (lambda: (self.nested(["ahead"], 0), self.m_commit()))]
        return self.land()

    def n_subjects(self):
        return self.subjects("origin/main..native/A.1")

    def test_a_landing_not_judged_keeps_the_commit_while_the_folder_is_on_the_step_written_ahead(self):
        code, text = self.hand_in_while_ahead_is_written(not_judged_verdict())
        self.assertEqual(code, 4)
        self.assertEqual(self.on(), "native/A.2")
        self.assertEqual(self.n_subjects()[0], STATE_SUBJECT, "the same tip is handed over again from native/A.2")
        self.results = [passed_verdict()]
        code, text = self.t.run(["land", "A.1"])
        self.assertEqual(code, 0, text)
        self.assertIn("landing A.1 again from native/A.2", text)

    def test_a_failed_landing_takes_it_off_native_A1_and_moves_the_step_written_ahead_off_it_too(self):
        code, text = self.hand_in_while_ahead_is_written(failed_verdict())
        self.assertEqual(code, 1)
        self.assertEqual(self.on(), "native/A.2", "the folder is where it was")
        self.assertNotIn(STATE_SUBJECT, self.n_subjects())
        self.assertNotIn(STATE_SUBJECT, self.subjects("origin/main..native/A.2"), "no branch of the agent says landed for A.1")
        self.assertEqual(self.subjects("native/A.1..native/A.2"), ["A.2: its work"])
        self.assertEqual(self.entry()["base"], self.rev("refs/heads/native/A.1"))
        self.assertIn("The commits of native/A.2 stand on A.1's tip again.", text)

    def test_with_uncommitted_work_on_the_step_written_ahead_it_says_ahead_moves_it_later(self):
        code, text = self.hand_in_while_ahead_is_written(failed_verdict(), lambda: (self.nested(["ahead"], 0), self.m_commit(), self.dirty()))
        self.assertEqual(code, 1)
        self.assertNotIn(STATE_SUBJECT, self.n_subjects())
        self.assertIn("native/A.2 stands on the commit that was taken off; `plan.py ahead` moves it onto A.1's tip once your changes are committed.", text)
        self.assertIn("work in progress", (self.folder / "runtime" / "src" / "render" / "a.cpp").read_text(encoding="utf-8"), "the work is kept")


# ---- `check` on a step written ahead -------------------------------------------------------------------------------

class OwnReplaysTest(RerunBase):
    def test_check_on_a_step_written_ahead_counts_its_own_replay_files_not_the_step_below_it(self):
        ready_a1(self.t)
        for tree in (self.folder, self.t.tree):
            path = plan.native(tree) / "03-X.md"
            path.write_text(path.read_text(encoding="utf-8").replace("- **Proof.** a test.", "- **Proof.** a test. The replay is recorded again."),
                            encoding="utf-8")
        self.t.git("commit", "-q", "-am", "card", cwd=self.folder)
        self.t.commit("A.1", "A.1: the hashes, replay reference", {REPLAY: "# changed\n0\tabc\t3\n"})
        self.permit(REPLAY)
        self.queue_row("running")
        self.record()
        self.alive_pids.add(424242)
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 0, text)
        self.assertEqual(self.t.git("rev-parse", "--abbrev-ref", "HEAD", cwd=self.folder), "native/A.2")
        self.t.commit("A.2", "A.2: work", {"runtime/src/render/b.cpp": "b\n"})
        code, text = self.t.run(["check", "--no-units"])
        self.assertNotIn("the replay differs", text, "A.1's replay file is A.1's, and A.2 is checked on its own commits")
        self.assertNotIn(REPLAY, text)
        # A change of A.2's own is refused, and only it is named.
        other = "tests/replays/002-Other.tsv"
        self.t.commit("A.2", "A.2: a hash", {other: "# changed\n0\tabc\t3\n"})
        code, text = self.t.run(["check", "--no-units"])
        self.assertIn("the replay differs and this step's card does not say it may", text)
        self.assertIn("changed: " + other, text)
        self.assertNotIn("changed: " + REPLAY, text)

    def test_the_old_reading_would_have_refused_it(self):
        """The planted fault: the replay problems of the whole branch, from origin/main (what check did)."""
        ready_a1(self.t)
        self.t.commit("A.1", "A.1: the hashes, replay reference", {REPLAY: "# changed\n0\tabc\t3\n"})
        self.assertEqual(plan.replay_problems(self.folder, False, "origin/main")[0], plan.REPLAY_REFUSAL)
        self.assertEqual(plan.replay_problems(self.folder, False, "HEAD"), [])


# ---- a mend is open: the step after the one in work is written ----------------------------------------------------------

class MendAheadTest(AheadBase):
    def open_mend(self, *words):
        return self.t.run(["mend", "open", *words], expect=0)

    def set_sheet_files(self, text):
        sheet = lv.sheet_path(self.t.live, "M-001")
        body = sheet.read_text(encoding="utf-8")
        heading = "## The files I believe are at fault\n\n"
        self.assertIn(heading + "(path and function, or `none known`)", body)
        sheet.write_text(body.replace(heading + "(path and function, or `none known`)", heading + text), encoding="utf-8")

    def where(self, step_card_line="- **Where.** `runtime/src/render/a.cpp`"):
        """The card of A.2 names a file (the fixture's card has no Where line)."""
        for tree in (self.t.tree, self.folder):
            path = plan.native(tree) / "03-X.md"
            path.write_text(path.read_text(encoding="utf-8").replace("### A.2 Second step\n", "### A.2 Second step\n\n" + step_card_line + "\n"), encoding="utf-8")
        self.t.git("commit", "-q", "-am", "card", cwd=self.folder)
        self.t.push_plan_files("card")

    def test_ahead_writes_the_next_step_on_top_of_the_step_in_work_while_a_mend_is_open(self):
        self.t.commit("A.1", "A.1: the code")
        tip = self.rev("HEAD")
        self.open_mend("A.1", "a fault")
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 0, text)
        self.assertIn("AHEAD: A.2", text)
        self.assertIn("Mend M-001 is open: nothing is landed and nothing is handed in until main holds it", text)
        self.assertEqual(self.on(), "native/A.2")
        self.assertEqual(self.entry()["base"], tip, "written on top of the step in work")
        self.assertTrue(self.is_ancestor(tip, "native/A.2"))
        self.assertEqual(self.calls, [], "nothing was handed to the gate")

    def test_start_and_land_still_wait_and_next_says_to_write_ahead(self):
        self.open_mend("A.1", "a fault")
        for command in (["start", "C.1"], ["land", "A.1"]):
            code, text = self.t.run(command)
            self.assertEqual(code, 1, command)
            self.assertIn("WAIT: mend M-001 is open: a fault", text)
            self.assertIn("Nothing is started or landed", text)
        text = self.t.run(["next"], expect=0)[1]
        self.assertIn("Meanwhile you write the step after the one in work", text)
        self.assertIn("ahead", text)
        self.assertEqual(self.calls, [])

    def test_a_mend_that_names_a_file_the_next_step_touches_stops_it(self):
        self.where()
        self.open_mend("A.1", "a fault")
        self.set_sheet_files("`runtime/src/render/a.cpp`, function draw")
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertIn("WAIT: mend M-001 is open: a fault", text)
        self.assertIn("The mend touches runtime/src/render/a.cpp, which step A.2 touches", text)
        self.assertEqual(self.on(), "native/A.1", "nothing was written")

    def test_a_mend_whose_sheet_names_another_file_does_not(self):
        self.where()
        self.open_mend("A.1", "a fault")
        self.set_sheet_files("`tools/gate/ctest.ps1` and `tools/gate/gate.py`")
        self.assertEqual(self.t.run(["ahead"])[0], 0)

    def test_a_mend_with_no_file_named_yet_does_not_stop_it(self):
        self.where()
        self.open_mend("A.1", "a fault")
        self.assertEqual(self.t.run(["ahead"])[0], 0)

    def test_the_helpers_branch_that_changes_the_file_stops_it(self):
        self.where()
        self.open_mend("A.1", "a fault")
        git = lambda *a: self.t.git(*a, cwd=self.folder)   # noqa: E731
        git("checkout", "-q", "-b", "mend/M-001", "origin/main")
        (self.folder / "runtime" / "src" / "render" / "a.cpp").write_text("a\nb\nc\nmended\n", encoding="utf-8")
        git("commit", "-q", "-am", "the mend")
        git("checkout", "-q", "native/A.1")
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertIn("The mend touches runtime/src/render/a.cpp, which step A.2 touches", text)

    def test_a_mend_with_no_step_in_work_writes_nothing(self):
        self.open_mend("a fault")
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertIn("The mend waits for no step in work", text)

    def test_a_mend_opened_for_a_repair_lets_the_step_the_tool_gives_next_be_written_on_the_repairs_branch(self):
        """The trial's M-003: a repair waited for a mend and the agent sat idle; the step the tool gives after the repair (A.1 is blocked here, so
        C.1) is written meanwhile."""
        self.t.run(["blocked", "A.1", "to free the folder"], expect=0)
        lv.add_repair(self.t.live, "nightly", "fail", "build/windows-asan", evidence="x", note="the sanitizer build refuses the folder")
        self.t.run(["start", "R-001"], expect=0)
        self.t.commit("A.1", "R-001: the work so far")
        tip = self.rev("HEAD")
        self.open_mend("R-001", "the build script")
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 0, text)
        self.assertIn("AHEAD: C.1", text)
        self.assertEqual(self.entry()["base"], tip, "on top of the repair's branch")
        self.assertEqual(self.on(), "native/C.1")
        self.assertEqual(self.t.run(["land", "R-001"])[0], 1, "and the repair is not landed while the mend is open")

    def test_the_mend_closed_the_ordinary_refusal_is_back(self):
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertIn("ahead is for the time a landing plays", text)

    def test_the_helper_is_told_to_report_once_at_its_end(self):
        lines = plan.mend_prompt(type("A", (), {"tree": str(self.t.tree), "live": str(self.t.live)})(), "M-001")
        self.assertEqual(len(lines), 4)
        self.assertTrue(lines[0].startswith("Read ") and lines[0].endswith("TOOL-MEND.md whole."))
        self.assertIn("Report once, at your end", lines[3])
        self.assertIn("`MENDED M-001 <main's commit>` or `NOT MENDED M-001`", lines[3])
        self.assertIn("Send no progress note before it.", lines[3])
        text = self.open_mend("A.1", "a fault")[1]
        self.assertIn(lines[3], text, "`mend open` prints the lines the agent gives the helper")
        self.assertIn(lines[3], self.t.run(["next"], expect=0)[1])


# ---- `decide --replaces` --------------------------------------------------------------------------------------------

class DecideTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.path = plan.native(self.t.tree) / "decisions.tsv"

    def decide(self, what, *args, expect=0, step="A.1"):
        return self.t.run(["decide", step, what, "--because", "the reason", *args], expect=expect)

    def test_a_later_decision_replaces_an_earlier_one_by_its_number_and_the_newest_shows(self):
        self.decide("first text")
        self.decide("other text")
        code, text = self.decide("corrected text", "--replaces", "1")
        self.assertIn("decided A.1#3:", text)
        self.assertIn("replacing A.1#1", text)
        rows = self.path.read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(rows), 4, "the file keeps all three decisions and its header")
        self.assertEqual(rows[0].split("\t"), plan.DECISION_COLUMNS)
        self.assertEqual([r["what"] for r in plan.read_decisions(self.t.tree)], ["other text", "corrected text"])
        shown = self.t.run(["decide", "A.1", "--list"], expect=0)[1]
        self.assertNotIn("first text", shown)
        self.assertIn("A.1#2  ", shown)
        self.assertIn("A.1#3  ", shown)
        self.assertIn("[replaces A.1#1]", shown)
        static, data, landings, faults = plan.page_data(self.t.tree, self.t.live)
        self.assertEqual([d["what"] for d in data["decisions"]], ["other text", "corrected text"], "the page shows the newest")

    def test_a_decision_may_name_the_whole_number_and_another_steps(self):
        self.decide("a", step="A.2")
        self.decide("b", "--replaces", "A.2#1", step="A.1", expect=0)
        self.assertEqual([r["what"] for r in plan.read_decisions(self.t.tree)], ["b"])

    def test_a_decision_that_is_replaced_already_is_named_with_the_one_that_replaces_it(self):
        self.decide("first")
        self.decide("second", "--replaces", "1")
        code, text = self.decide("third", "--replaces", "1", expect=1)
        self.assertIn("plan.py: decision A.1#1 is replaced already, by A.1#2: replace that one", text)
        self.assertEqual(len(self.path.read_text(encoding="utf-8").splitlines()), 3, "nothing was written")

    def test_a_number_that_is_no_decision_is_refused(self):
        self.decide("first")
        code, text = self.decide("second", "--replaces", "7", expect=1)
        self.assertIn("plan.py: no decision A.1#7 to replace", text)
        self.assertEqual(len(self.path.read_text(encoding="utf-8").splitlines()), 2)

    def test_a_file_with_the_old_five_columns_reads_and_gets_the_sixth_when_one_replaces(self):
        self.path.write_text("date\tstep\twhat\tbecause\tdoc\n2026-11-23\tA.1\told text\told reason\t-\n", encoding="utf-8")
        self.assertEqual([r["what"] for r in plan.read_decisions(self.t.tree)], ["old text"])
        self.decide("plain")
        self.assertEqual(self.path.read_text(encoding="utf-8").splitlines()[0], "date\tstep\twhat\tbecause\tdoc", "a decision that replaces nothing leaves the header")
        self.decide("new text", "--replaces", "1")
        lines = self.path.read_text(encoding="utf-8").splitlines()
        self.assertEqual(lines[0].split("\t"), plan.DECISION_COLUMNS)
        self.assertEqual([r["what"] for r in plan.read_decisions(self.t.tree)], ["plain", "new text"])

    def test_a_decision_with_no_text_or_no_reason_is_still_refused(self):
        code, text = self.t.run(["decide", "A.1", "--because", "why"], expect=1)
        self.assertIn("a decision has a text and a reason", text)
        code, text = self.t.run(["decide", "A.1", "what"], expect=1)
        self.assertIn("a decision has a text and a reason", text)


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_after_trial_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
