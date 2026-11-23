"""plan_mend_test.py: the tool mend and the unattended run in tools/plan/plan.py (brief U), on the temporary git tree of plan_fixture.

    python -X utf8 -I tools/gate/tests/plan_mend_test.py [--work FOLDER]

No game, no build, no network; the landing is a fake; nothing of D:/NebulaWork/plan is touched. Each test was seen to fail with the thing
it guards broken (named in a phrase after the test).
  mend open / next / close / failed / list
    open_writes_the_sheet_and_the_step_waits   no sheet, or the step not marked waiting, or a second open allowed
    next_says_wait_and_nothing_else            `next` gave a step's card while a mend was open
    close_needs_a_commit_on_main_that_touches_a_tool   a commit that is not on main, or one that changes no tool or document, was taken
    close_puts_the_step_back_and_opens_the_blocked_ones   the step stayed waiting; a step blocked `tool: M-001` stayed blocked
    failed_counts_three_attempts               the first failure blocked the step; the attempts were not on the sheet; SECOND HELPER, YOUR OWN
                                             ATTEMPT and BLOCKED were not told apart; `next` said no WAIT after an attempt
  drop_ends_a_mend_that_needs_game_code      the step stayed waiting after `NOT MENDED, needs game code`
  start_ahead_and_land_wait                  `start`, `ahead` or `land` went on while a mend was open
    a_third_mend_for_the_same_check_is_refused   the per-check limit of two
    landed_records_a_mend_made_by_hand         `mend open ... --landed` for a commit that is not on main, or a mend left open
  repair end (a repair ends by its mend on every route: mend close, mend open --landed, repair end)
    landed_for_a_repair_records_the_mend_and_ends_the_repair   `--landed R-00N` left the repair open (an open and a started one)
    landed_refuses_a_repair_it_cannot_end_and_records_nothing  a mend was recorded for a missing or an ended repair
    repair_end_ends_an_open_repair_and_a_started_one           no `repair end` command; the page, `repair list`, `status` or `next` still showed it
    repair_end_refuses_a_mend_that_is_not_closed / _a_mend_of_another_step_and_a_missing_mend / _a_repair_that_is_landed_already /
    _a_repair_that_waits_for_another_open_mend / _a_commit_that_is_not_on_main     each refusal let the repair be ended
    a_flake_repair_ended_by_its_mend_ends_the_set_aside        the test stayed set aside after the end
  mend land
    mend_land_refuses_game_source_and_lands_a_tool_change   a mend branch that changed runtime/ was handed to the gate; the gate got no --who
  decide, alive, check, the heartbeat
    decide_writes_its_row                      no row, an unknown step taken
    alive_says_the_three_words                 each word: a fresh heartbeat, an old one, a note that begins Done:
    every_command_writes_the_heartbeat         a command that left no heartbeat; `alive` that wrote one
    check_names_a_tool_file_the_card_does_not_name  the tool-file rule of land and check
  the landing
    a_new_landing_takes_the_earlier_state_commit_off   two commits `Plan: step A.1 landed` on the branch after a failed landing and a fix
    the_speed_figures_are_kept_with_the_step   the landings list and the page had no figures
  the page
    the_page_has_the_new_parts                 mends, decisions, the owner's questions, the checks changed, the tablet, the old-page line
    a_lock_held_by_hand_is_named               no note for a by-hand lock older than 30 minutes; a young one named
    a_flake_repair_ends_the_set_aside          a landed repair of source flake left the test set aside
"""
import contextlib
import datetime
import io
import os
import pathlib
import sys
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "plan"))
sys.path.insert(0, str(TREE / "tools" / "gate"))
import kinds  # noqa: E402
import live as lv  # noqa: E402
import page  # noqa: E402
import plan  # noqa: E402
from plan_fixture import Tree, git_run, ledger_row, setup_module, teardown_module  # noqa: E402


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


def tool_commit_on_main(t, name="tools/verify/doc_refs_allow.txt", text="# a mend\n"):
    """A commit on origin's main that changes a tool file: the commit a mend closes with."""
    path = t.tree / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    t.push_plan_files("mend")
    return t.git("rev-parse", "HEAD")


class MendTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)

    def test_open_writes_the_sheet_and_the_step_waits(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        code, text = t.run(["mend", "open", "A.1", "the census refuses a name the design prescribes"], expect=0)
        self.assertIn("opened M-001", text)
        sheet = lv.sheet_path(t.live, "M-001")
        body = sheet.read_text(encoding="utf-8")
        for heading in ("What I ran", "What the tool said", "What it should have said, and which document and section says so",
                        "The files I believe are at fault", "What must stay refused after the mend"):
            self.assertIn("## " + heading, body)
        m = lv.read_mends(t.live)[0]
        self.assertEqual((m["id"], m["step"], m["state"]), ("M-001", "A.1", "open"))
        entry = lv.read_state(t.live)["A.1"]
        self.assertEqual((entry["state"], entry["reason"]), ("started", "waits for M-001"), "not blocked: it waits")
        self.assertIn("Read ", text)
        self.assertIn("Your mend is M-001.", text)
        code, text = t.run(["mend", "open", "A.1", "another fault"])
        self.assertEqual(code, 1)
        self.assertIn("mend M-001 is open", text, "one mend at a time: the open one's id is named")
        self.assertEqual(len(lv.read_mends(t.live)), 1)

    def test_open_with_last_starts_the_sheet_from_the_last_fault(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        lv.append_fault(t.live, 1, "A.1", "TOOL", "harness/self", "result.json is missing", run_folder="D:/x/run")
        t.run(["mend", "open", "A.1", "harness/self writes no result", "--last"], expect=0)
        body = lv.sheet_path(t.live, "M-001").read_text(encoding="utf-8")
        self.assertIn("result.json is missing", body)
        self.assertIn("D:/x/run", body)
        self.assertEqual(self.t.run(["mend", "open", "x", "--last"])[0], 1, "no such step")

    def test_next_says_wait_and_nothing_else(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        t.run(["mend", "open", "A.1", "a fault"], expect=0)
        code, text = t.run(["next"], expect=0)
        self.assertIn("WAIT: mend M-001 is open: a fault", text)
        self.assertIn("mend close M-001", text)
        self.assertNotIn("=== COMMANDS", text)
        self.assertNotIn("Card", text)

    def test_close_needs_a_commit_on_main_that_touches_a_tool(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        t.run(["mend", "open", "A.1", "a fault"], expect=0)
        off_main = t.commit("A.1", "not on main", {"tools/verify/x.py": "x\n"})
        code, text = t.run(["mend", "close", "M-001", off_main])
        self.assertEqual(code, 1)
        self.assertIn("is not on origin/main", text)
        no_tool = t.git("rev-parse", "HEAD", cwd=t.tree)
        (t.tree / "runtime" / "src" / "render" / "a.cpp").write_text("a\nb\nc\nd\n", encoding="utf-8")
        t.push_plan_files("game")
        game = t.git("rev-parse", "HEAD")
        code, text = t.run(["mend", "close", "M-001", game])
        self.assertEqual(code, 1)
        self.assertIn("changes no tool or document path", text)
        self.assertEqual(lv.read_mends(t.live)[0]["state"], "open", "nothing changed")
        self.assertEqual(t.run(["mend", "close", "M-009", game])[0], 1, "no such mend")

    def test_close_puts_the_step_back_and_opens_the_blocked_ones(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        t.run(["blocked", "C.1", "tool: M-001 the census refused it"], expect=0)
        t.run(["blocked", "B.1", "main changed the same code"], expect=0)
        t.run(["mend", "open", "A.1", "a fault"], expect=0)
        sha = tool_commit_on_main(t)
        code, text = t.run(["mend", "close", "M-001", sha], expect=0)
        m = lv.read_mends(t.live)[0]
        self.assertEqual((m["state"], m["commit"]), ("closed", sha))
        state = lv.read_state(t.live)
        self.assertEqual((state["A.1"]["state"], state["A.1"]["reason"]), ("started", ""), "the step is in work again")
        self.assertNotIn("C.1", state, "blocked `tool: M-001`: open again")
        self.assertEqual(state["B.1"]["state"], "blocked", "blocked for another reason: stays")
        self.assertIn("git rebase origin/main", text)
        self.assertIn("C.1", text)
        self.assertEqual(t.run(["mend", "close", "M-001", sha])[0], 1, "closed once")
        code, text = t.run(["next"], expect=0)
        self.assertNotIn("WAIT", text)

    def test_failed_counts_three_attempts_and_only_the_third_blocks_the_step(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        t.run(["mend", "open", "A.1", "a fault"], expect=0)
        code, text = t.run(["mend", "failed", "M-001", "the helper found no cause"], expect=0)
        self.assertIn("SECOND HELPER", text)
        self.assertIn("Your mend is M-001.", text)
        m = lv.read_mends(t.live)[0]
        self.assertEqual((m["state"], m["attempts"]), ("open", "1"))
        self.assertEqual(lv.read_state(t.live)["A.1"]["state"], "started", "the step keeps waiting, not blocked")
        code, text = t.run(["next"], expect=0)
        self.assertIn("WAIT: mend M-001 is open", text)
        self.assertIn("(now 1 of 3)", text)
        code, text = t.run(["mend", "failed", "M-001", "the second helper found no cause either"], expect=0)
        self.assertIn("YOUR OWN ATTEMPT", text)
        self.assertIn("TOOL-MEND.md to the letter", text)
        self.assertIn("mend/M-001", text)
        self.assertEqual(lv.read_mends(t.live)[0]["attempts"], "2")
        self.assertEqual(lv.read_state(t.live)["A.1"]["state"], "started")
        sheet = lv.sheet_path(t.live, "M-001").read_text(encoding="utf-8")
        self.assertIn("## Attempt 1 (the helper) ended: NOT MENDED", sheet)
        self.assertIn("the helper found no cause", sheet, "the next one reads the notes on the sheet")
        self.assertIn("## Attempt 2 (the second helper) ended: NOT MENDED", sheet)
        code, text = t.run(["mend", "failed", "M-001", "my own attempt found none"], expect=0)
        self.assertIn("BLOCKED:", text)
        self.assertIn("NEXT:", text)
        self.assertIn("## Attempt 3 (the agent itself) ended: NOT MENDED", lv.sheet_path(t.live, "M-001").read_text(encoding="utf-8"))
        self.assertEqual((lv.read_mends(t.live)[0]["state"], lv.read_mends(t.live)[0]["attempts"]), ("failed", "3"))
        e = lv.read_state(t.live)["A.1"]
        self.assertEqual(e["state"], "blocked")
        self.assertTrue(e["reason"].startswith("tool: M-001 my own attempt"), e["reason"])
        code, text = t.run(["next"], expect=0)
        self.assertNotIn("WAIT", text, "the agent goes on with the steps that do not need it")
        static, data, landings, faults = plan.page_data(t.tree, t.live)
        said = [q["question"] for q in data["owner"]]
        self.assertIn("A test tool is at fault and three attempts did not mend it (M-001: a fault). The steps that need it wait; the other steps go on.", said)
        self.assertIn("three attempts did not mend it (M-001: a fault)", plan.status_text(t.tree, t.work, live=t.live), "and the status text says it")
        self.assertEqual(t.run(["mend", "failed", "M-001", "again"])[0], 1, "nothing is open to fail")

    def test_the_third_failure_keeps_the_wait_when_the_step_cannot_be_blocked(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        t.run(["mend", "open", "A.1", "a fault"], expect=0)
        t.run(["mend", "failed", "M-001", "one"], expect=0)
        t.run(["mend", "failed", "M-001", "two"], expect=0)
        (t.folder() / "unfinished.txt").write_text("x", encoding="utf-8")
        code, text = t.run(["mend", "failed", "M-001", "three"])
        self.assertEqual(code, 1)
        self.assertEqual(lv.read_mends(t.live)[0]["state"], "open")
        self.assertEqual(lv.read_state(t.live)["A.1"]["reason"], "waits for M-001")

    def test_drop_ends_a_mend_that_needs_game_code_and_gives_the_step_back(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        t.run(["mend", "open", "A.1", "a fault"], expect=0)
        code, text = t.run(["mend", "drop", "M-001", "runtime/src/render/a.cpp"], expect=0)
        self.assertIn("yours again", text)
        m = lv.read_mends(t.live)[0]
        self.assertEqual(m["state"], "dropped")
        self.assertEqual(lv.read_state(t.live)["A.1"], dict(lv.read_state(t.live)["A.1"], state="started", reason=""))
        self.assertNotIn("WAIT", t.run(["next"], expect=0)[1])

    def test_start_ahead_and_land_wait_as_next_does(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        t.run(["mend", "open", "A.1", "a fault"], expect=0)
        for command in (["start", "C.1"], ["ahead"], ["land", "A.1"]):
            code, text = t.run(command)
            self.assertEqual(code, 1, command)
            self.assertIn("WAIT: mend M-001 is open: a fault", text, command)
            self.assertIn("Nothing is started, written ahead or landed", text)
        self.assertEqual(plan.find(plan.read_steps(t.tree, t.live), "C.1")["state"], "open", "nothing was started")
        for command in (["status"], ["page"], ["note", "x"], ["alive"], ["mend", "list"]):
            self.assertNotIn("WAIT: mend", t.run(command)[1], command)

    def test_a_third_mend_for_the_same_check_is_refused(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        for n in (1, 2):
            t.run(["mend", "open", "A.1", "fault %d" % n, "--check", "repo/code"], expect=0)
            sha = tool_commit_on_main(t, text="# mend %d\n" % n)
            t.run(["mend", "close", "M-00%d" % n, sha], expect=0)
        code, text = t.run(["mend", "open", "A.1", "fault 3", "--check", "repo/code"])
        self.assertEqual(code, 1)
        self.assertIn("M-001, M-002", text)
        self.assertEqual(t.run(["mend", "open", "A.1", "another check", "--check", "tour/d3d12"])[0], 0)

    def test_landed_records_a_mend_made_by_hand(self):
        t = self.t
        sha = tool_commit_on_main(t)
        code, text = t.run(["mend", "open", "plan.py cannot start a step", "--landed", sha], expect=0)
        m = lv.read_mends(t.live)[0]
        self.assertEqual((m["state"], m["commit"], m["step"]), ("closed", sha, "-"))
        self.assertIsNone(lv.open_mend(t.live), "no mend is left open")
        self.assertEqual(t.run(["mend", "open", "x", "--landed", "0123456789abcdef0123456789abcdef01234567"])[0], 1)
        code, text = t.run(["mend", "list"], expect=0)
        self.assertIn("M-001  closed", text)

    def test_the_wait_does_not_count_for_the_three_failures(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        from unittest import mock as m
        import failures
        failures.Store(t.store).record("native/A.1", [("tour/d3d12", "FAIL", "replay differs")])
        t.run(["mend", "open", "A.1", "a fault"], expect=0)
        self.assertEqual(failures.Store(t.store).count("native/A.1"), 1, "opening a mend changes no count")


class MendLandTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.calls = []
        patcher = mock.patch.object(plan, "LAND", self.fake_land)
        patcher.start()
        self.addCleanup(patcher.stop)
        lv.add_mend(self.t.live, "-", "a fault")

    def fake_land(self, folder, branch, tablet, wait, *rest):
        self.calls.append((branch, list(rest[0]) if rest else []))
        v = plan.Verdict(True, [("repo/code", "PASS", "ok")])
        v.code, v.commit, v.gate_recorded = 0, "feedbeef0", True
        return v

    def on_branch(self, files):
        t = self.t
        t.git("checkout", "-q", "-b", "mend/M-001")
        for name, text in files.items():
            (t.tree / name).parent.mkdir(parents=True, exist_ok=True)
            (t.tree / name).write_text(text, encoding="utf-8")
        t.git("add", "-A")
        t.git("commit", "-q", "-m", "a mend")

    def test_mend_land_refuses_game_source_and_lands_a_tool_change(self):
        t = self.t
        self.on_branch({"runtime/src/render/a.cpp": "a\nb\nc\nd\n"})
        code, text = t.run(["mend", "land", "M-001"])
        self.assertEqual(code, 1)
        self.assertIn("changes files of the game", text)
        self.assertIn("runtime/src/render/a.cpp", text)
        self.assertEqual(self.calls, [], "nothing was handed to the gate")
        t.git("reset", "-q", "--hard", "origin/main")
        (t.tree / "tools" / "verify" / "doc_refs_allow.txt").write_text("# narrower\n", encoding="utf-8")
        t.git("commit", "-q", "-am", "a tool")
        # a changed line of an existing test needs its log line; this is a table row of a list that only adds: it does too
        code, text = t.run(["mend", "land", "M-001"])
        self.assertEqual(code, 1, text)
        self.assertIn("tools/verify/doc_refs_allow.txt", text)
        self.assertIn("strictness-log.tsv", text)
        log = t.tree / "docs" / "plan" / "strictness-log.tsv"
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text("\t".join(plan.strictness.LOG_COLUMNS) + "\n" + "\t".join(
            ["2026-11-22", "tools/verify/doc_refs_allow.txt", "a comment", "a test", "TOOL-MEND.md 6", "M-001", "-"]) + "\n", encoding="utf-8")
        t.git("add", "-A")
        t.git("commit", "-q", "-m", "its line")
        code, text = t.run(["mend", "land", "M-001"], expect=0)
        self.assertEqual(self.calls[0][0], "mend/M-001")
        self.assertIn("--who", self.calls[0][1])
        self.assertIn("mend M-001", self.calls[0][1])
        self.assertIn("MENDED M-001 feedbeef0", text)

    def test_mend_land_is_refused_from_the_agents_folder_and_off_its_branch(self):
        t = self.t
        code, text = t.run(["mend", "land", "M-001"])
        self.assertEqual(code, 1)
        self.assertIn("mend/M-001", text, "the folder is on main, not on the mend's branch")


class RepairMendTest(unittest.TestCase):
    """A repair whose cause is a test tool: a mend opened for it ends it (the page shows it closed by the mend); the mend's landing runs the
    failed check as an extra check where the gate can."""

    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.calls = []
        patcher = mock.patch.object(plan, "LAND", self.fake_land)
        patcher.start()
        self.addCleanup(patcher.stop)

    def fake_land(self, folder, branch, tablet, wait, *rest):
        self.calls.append((branch, list(rest[0]) if rest else []))
        v = plan.Verdict(True, [("repo/code", "PASS", "ok")])
        v.code, v.commit, v.gate_recorded = 0, "feedbeef0", True
        return v

    def started_repair(self, check="build/windows-asan"):
        lv.add_repair(self.t.live, "nightly", "fail", check, "-", evidence="D:/x/evidence", note="the build folder was set up with another sanitizer")
        self.t.run(["start", "R-001"], expect=0)

    def test_a_mend_opened_for_a_repair_ends_it_when_it_is_closed(self):
        t = self.t
        self.started_repair()
        code, text = t.run(["mend", "open", "R-001", "the build folder keeps another sanitizer setting", "--check", "build/windows-asan"], expect=0)
        self.assertEqual(lv.read_state(t.live)["R-001"]["reason"], "waits for M-001")
        self.assertIn("WAIT: mend M-001", t.run(["next"])[1])
        sha = tool_commit_on_main(t)
        code, text = t.run(["mend", "close", "M-001", sha], expect=0)
        self.assertIn("Repair R-001 is ended by the mend", text)
        rep = lv.read_repairs(t.live)[0]
        self.assertEqual((rep["state"], rep["closed"]), ("landed", sha), "closed by the mend's commit")
        self.assertIn("ended by mend M-001", rep["note"])
        self.assertNotIn("R-001", lv.read_state(t.live), "nothing of it is in work")
        code, text = t.run(["repair", "list"], expect=0)
        self.assertIn("closed by " + sha, text)
        code, text = t.run(["next"], expect=0)
        self.assertNotIn("R-001", text.splitlines()[0], "the repair is not given again")
        static, data, landings, faults = plan.page_data(t.tree, t.live)
        self.assertEqual((data["repairs"][0]["state"], data["repairs"][0]["closed"]), ("landed", sha))

    def test_a_repair_that_is_not_there_or_not_in_work_gets_no_mend(self):
        t = self.t
        self.assertEqual(t.run(["mend", "open", "R-099", "x"])[0], 1)
        lv.add_repair(t.live, "nightly", "fail", "soak", "-", evidence="D:/x", note="n")
        self.assertEqual(t.run(["mend", "open", "R-001", "x"])[0], 1, "an open repair is not in work yet: start it first")

    def test_the_mends_landing_runs_the_failed_check_as_an_extra_check(self):
        t = self.t
        for check, expected in (("soak", "soak"), ("build/windows-asan", "sanitizer/tour"), ("stress/engine/d3d11", "stress"), ("coverage", None)):
            self.assertEqual(plan.also_for_check(check), [expected] if expected else [], check)
        self.started_repair("soak")
        t.run(["mend", "open", "R-001", "the soak's folder", "--check", "soak"], expect=0)
        t.git("checkout", "-q", "-b", "mend/M-001", "origin/main", cwd=t.tree)
        (t.tree / "tools" / "verify" / "doc_refs_allow.txt").write_text("# narrower\n", encoding="utf-8")
        log = t.tree / "docs" / "plan" / "strictness-log.tsv"
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text("\t".join(plan.strictness.LOG_COLUMNS) + "\n" + "\t".join(
            ["2026-11-22", "tools/verify/doc_refs_allow.txt", "a comment", "a test", "TOOL-MEND.md 6", "M-001", "-"]) + "\n", encoding="utf-8")
        t.git("add", "-A", cwd=t.tree)
        t.git("commit", "-q", "-m", "a mend", cwd=t.tree)
        code, text = t.run(["mend", "land", "M-001"], expect=0)
        options = self.calls[0][1]
        self.assertEqual(options[options.index("--also") + 1], "soak", options)
        self.assertIn("runs it as an extra check", text)


class RepairEndTest(unittest.TestCase):
    """A repair whose cause was a test tool is ended by its mend on every route: `mend close` (tested above), `mend open R-00N ... --landed
    COMMIT` and `repair end R-00N --mend M-00N`. Every test has its own live folder (the fixture's); nothing of D:/NebulaWork/plan is touched."""

    TITLE = "the step tool cannot end a repair"

    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)

    def repair(self, started=False, check="build/windows-asan"):
        row, _ = lv.add_repair(self.t.live, "nightly", "fail", check, "-", evidence="D:/x/evidence", note="a tool fault")
        if started:
            self.t.run(["start", row["id"]], expect=0)
        return row["id"]

    def closed_mend(self, step, name="tools/verify/doc_refs_allow.txt"):
        """A mend recorded closed for `step` by a commit on main, and the repair not ended (the state the live list is in)."""
        sha = tool_commit_on_main(self.t, name, "# %s\n" % name)
        row = lv.add_mend(self.t.live, step, self.TITLE, "build/windows-asan", "closed", sha, "recorded after a landing by hand")
        return row["id"], sha

    def state_of(self, rid):
        return next(r for r in lv.read_repairs(self.t.live) if r["id"] == rid)["state"]

    def assert_ended(self, rid, sha, mid):
        t = self.t
        rep = next(r for r in lv.read_repairs(t.live) if r["id"] == rid)
        self.assertEqual((rep["state"], rep["closed"]), ("landed", sha), "closed by the mend's commit")
        self.assertIn("ended by mend %s (%s)" % (mid, self.TITLE), rep["note"])
        self.assertNotIn(rid, lv.read_state(t.live), "nothing of it is in work")
        code, text = t.run(["repair", "list"], expect=0)
        self.assertIn("closed by " + sha, text)
        code, text = t.run(["status"], expect=0)
        self.assertNotIn("%s repair of" % rid, text, "status lists it in no work")
        code, text = t.run(["next"], expect=0)
        self.assertNotIn("REPAIR", text.splitlines()[0], "the repair is not given again")
        self.assertIn("A.1", text, "the step that comes after is given")
        static, data, landings, faults = plan.page_data(t.tree, t.live)
        self.assertEqual([(r["state"], r["closed"]) for r in data["repairs"] if r["id"] == rid], [("landed", sha)])

    def test_landed_for_a_repair_records_the_mend_and_ends_the_repair(self):
        t = self.t
        for started in (False, True):          # never started (the step tool was at fault before `start`), and started
            with self.subTest(started=started):
                rid = self.repair(started)
                sha = tool_commit_on_main(t, "tools/verify/landed_%s.txt" % rid, "# a mend\n")
                code, text = t.run(["mend", "open", rid, self.TITLE, "--landed", sha], expect=0)
                self.assertIn("Repair %s is ended by the mend" % rid, text)
                self.assertEqual(lv.read_mends(t.live)[-1]["step"], rid)
                self.assert_ended(rid, sha, lv.read_mends(t.live)[-1]["id"])

    def test_landed_refuses_a_repair_it_cannot_end_and_records_nothing(self):
        t = self.t
        sha = tool_commit_on_main(t)
        self.assertEqual(t.run(["mend", "open", "R-099", self.TITLE, "--landed", sha])[0], 1, "no such repair")
        self.assertEqual(lv.read_mends(t.live), [])
        rid = self.repair()
        t.run(["mend", "open", rid, self.TITLE, "--landed", sha], expect=0)
        code, text = t.run(["mend", "open", rid, "again", "--landed", sha])
        self.assertEqual(code, 1)
        self.assertIn("landed already", text)
        self.assertEqual(len(lv.read_mends(t.live)), 1, "the refused one is not recorded")

    def test_repair_end_ends_an_open_repair_and_a_started_one(self):
        t = self.t
        for started in (False, True):
            with self.subTest(started=started):
                rid = self.repair(started)
                mid, sha = self.closed_mend(rid, "tools/verify/end_%s.txt" % rid)
                self.assertEqual(self.state_of(rid), "started" if started else "open")
                code, text = t.run(["repair", "end", rid, "--mend", mid], expect=0)
                self.assertIn("Repair %s is ended by mend %s" % (rid, mid), text)
                self.assert_ended(rid, sha, mid)

    def test_repair_end_refuses_a_mend_that_is_not_closed(self):
        t = self.t
        rid = self.repair(True)
        t.run(["mend", "open", rid, self.TITLE], expect=0)
        code, text = t.run(["repair", "end", rid, "--mend", "M-001"])
        self.assertEqual(code, 1)
        self.assertIn("mend M-001 is open, not closed", text)
        self.assertEqual(self.state_of(rid), "started")
        self.assertEqual(lv.read_state(t.live)[rid]["reason"], "waits for M-001", "nothing changed")

    def test_repair_end_refuses_a_mend_of_another_step_and_a_missing_mend(self):
        t = self.t
        first, second = self.repair(), self.repair(check="soak")
        mid, sha = self.closed_mend(first)
        code, text = t.run(["repair", "end", second, "--mend", mid])
        self.assertEqual(code, 1)
        self.assertIn("was opened for %s, not for %s" % (first, second), text)
        code, text = t.run(["repair", "end", second, "--mend", "M-099"])
        self.assertEqual(code, 1)
        self.assertIn("no mend M-099", text)
        code, text = t.run(["repair", "end", "R-099", "--mend", mid])
        self.assertEqual(code, 1)
        self.assertIn("no repair R-099", text)
        self.assertEqual([r["state"] for r in lv.read_repairs(t.live)], ["open", "open"])

    def test_repair_end_refuses_a_repair_that_is_landed_already(self):
        t = self.t
        rid = self.repair()
        mid, sha = self.closed_mend(rid)
        t.run(["repair", "end", rid, "--mend", mid], expect=0)
        code, text = t.run(["repair", "end", rid, "--mend", mid])
        self.assertEqual(code, 1)
        self.assertIn("is landed already (closed by %s)" % sha, text)

    def test_repair_end_refuses_a_repair_that_waits_for_another_open_mend(self):
        t = self.t
        rid = self.repair(True)
        mid, sha = self.closed_mend(rid)
        t.run(["mend", "open", rid, "a second fault"], expect=0)
        self.assertEqual(lv.read_state(t.live)[rid]["reason"], "waits for M-002")
        code, text = t.run(["repair", "end", rid, "--mend", mid])
        self.assertEqual(code, 1)
        self.assertIn("waits for mend M-002, which is open", text)
        code, text = t.run(["mend", "open", rid, "by hand", "--landed", sha])
        self.assertEqual(code, 1, "the by-hand route refuses it too")
        self.assertEqual(self.state_of(rid), "started")
        self.assertEqual(len(lv.read_mends(t.live)), 2)

    def test_repair_end_refuses_a_commit_that_is_not_on_main(self):
        t = self.t
        rid = self.repair()
        row = lv.add_mend(t.live, rid, self.TITLE, "-", "closed", "0123456789abcdef0123456789abcdef01234567", "x")
        code, text = t.run(["repair", "end", rid, "--mend", row["id"]])
        self.assertEqual(code, 1)
        self.assertEqual(self.state_of(rid), "open")

    def test_a_flake_repair_ended_by_its_mend_ends_the_set_aside(self):
        t = self.t
        t.live.mkdir(parents=True, exist_ok=True)
        aside = t.live / "set-aside.tsv"
        aside.write_text("2026-11-20T10:00:00\tboot\td3d12\t3 flakes in 7 days\n", encoding="utf-8")
        row, _ = lv.add_repair(t.live, "flake", "fail", "flake:boot", "d3d12", evidence="D:/x", note="boot set aside")
        mid, sha = self.closed_mend(row["id"])
        t.run(["repair", "end", row["id"], "--mend", mid], expect=0)
        self.assertEqual(aside.read_text(encoding="utf-8").strip(), "", "the test counts again")


class DecideAliveTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        patcher = mock.patch.object(plan, "QUEUE_FILE", str(self.t.dir / "queue.tsv"))      # never the gate's real queue
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_decide_writes_its_row(self):
        t = self.t
        code, text = t.run(["decide", "A.1", "the design document wins over the card", "--because", "AGENT.md section 7", "--doc",
                            "docs/engine/FRAME-MODEL.md 2.9"], expect=0)
        rows = plan.read_decisions(t.tree)
        self.assertEqual(len(rows), 1)
        self.assertEqual((rows[0]["step"], rows[0]["what"], rows[0]["because"], rows[0]["doc"]),
                         ("A.1", "the design document wins over the card", "AGENT.md section 7", "docs/engine/FRAME-MODEL.md 2.9"))
        self.assertEqual(t.run(["decide", "Q.9", "x", "--because", "y"])[0], 1, "an unknown step")
        t.run(["decide", "A.1", "second", "--because", "z"], expect=0)
        self.assertEqual(len(plan.read_decisions(t.tree)), 2, "rows are added, the file keeps its header")

    def test_alive_says_the_three_words(self):
        t = self.t
        t.run(["status"], expect=0)                       # a command: the heartbeat is now
        code, text = t.run(["alive"], expect=0)
        self.assertEqual(text.strip(), "working")
        old = (datetime.datetime.now() - datetime.timedelta(hours=4)).isoformat(timespec="seconds")
        lv.heartbeat_path(t.live).write_text(old + "\n", encoding="utf-8")
        self.assertEqual(t.run(["alive"])[1].strip(), "stale", "the alive command does not write the heartbeat itself")
        self.assertEqual(t.run(["alive"])[1].strip(), "stale")
        lv.heartbeat_path(t.live).unlink()
        self.assertEqual(t.run(["alive"])[1].strip(), "stale", "no heartbeat at all")
        t.run(["note", "Stopped: nothing can start"], expect=0)
        self.assertEqual(t.run(["alive"])[1].strip(), "ended")

    def test_alive_is_working_while_a_landing_of_the_agent_is_in_the_gates_line(self):
        t = self.t
        lv.write_record(t.live, "mend", "mend/M-001", "mend/M-001", "abc", os.getpid())
        old = (datetime.datetime.now() - datetime.timedelta(hours=5)).isoformat(timespec="seconds")
        lv.heartbeat_path(t.live).write_text(old + "\n", encoding="utf-8")
        self.assertEqual(t.run(["alive"])[1].strip(), "working", "a mend landing that plays")

    def test_every_command_writes_the_heartbeat(self):
        t = self.t
        self.assertFalse(lv.heartbeat_path(t.live).exists())
        t.run(["rows"], expect=0)
        self.assertTrue(lv.heartbeat_path(t.live).exists())
        self.assertLess(lv.heartbeat_age_hours(t.live), 0.01)
        lv.heartbeat_path(t.live).unlink()
        t.run(["alive"], expect=0)
        self.assertFalse(lv.heartbeat_path(t.live).exists(), "alive reads the heartbeat, it never writes it")


class CheckTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.t.run(["start", "A.1"], expect=0)

    def test_check_names_a_tool_file_the_card_does_not_name(self):
        t = self.t
        t.commit("A.1", "a tool in a step", {"tools/verify/doc_refs_allow.txt": "# x\n"})
        code, text = t.run(["check"])
        self.assertEqual(code, 1)
        self.assertIn("would refuse `land A.1`", text)
        self.assertIn("its card does not name them: tools/verify/doc_refs_allow.txt", text)
        self.assertIn("plan.py mend open", text)
        # the card that names the file allows it (the card is the folder's own copy)
        cards = t.folder() / "docs" / "plan" / "native" / "03-X.md"
        cards.write_text(cards.read_text(encoding="utf-8").replace("`runtime/src/render/a.cpp`, ", "`runtime/src/render/a.cpp`, `tools/verify/doc_refs_allow.txt`, "),
                         encoding="utf-8")
        t.git("commit", "-q", "-am", "card", cwd=t.folder())
        code, text = t.run(["check"])
        self.assertNotIn("its card does not name them", text)

    def test_check_lists_every_refusal_at_once_and_changes_nothing(self):
        t = self.t
        head = t.git("rev-parse", "HEAD", cwd=t.folder())
        t.commit("A.1", "bad", {"tools/verify/doc_refs_allow.txt": "# x\n", "docs/notes.md": "see `no/such/file.cpp`\n"})
        head = t.git("rev-parse", "HEAD", cwd=t.folder())
        code, text = t.run(["check"])
        self.assertEqual(code, 1)
        self.assertGreaterEqual(text.count("would refuse"), 1)
        self.assertIn("The ledger", text)
        self.assertIn("no/such/file.cpp", text)
        self.assertIn("its card does not name them", text)
        self.assertEqual(t.git("rev-parse", "HEAD", cwd=t.folder()), head, "nothing was committed")
        self.assertEqual(lv.read_record(t.live, "1"), None, "nothing was queued")

    def test_a_step_branch_that_changes_a_listed_place_needs_the_log_line(self):
        t = self.t
        t.commit("A.1", "a known failure", {"tools/gate/known-failures.tsv": "check\ttest\tledger row\tfirst seen\twhat\nunit\tx\tP-001\tabc\tbroken\n"})
        code, text = t.run(["check"])
        self.assertIn("tools/gate/known-failures.tsv", text)
        self.assertIn("strictness-log.tsv", text)


class LandingTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.t.run(["start", "A.1"], expect=0)
        self.verdict = plan.Verdict(False, [], [("tour/d3d12", "FAIL", "replay differs")])
        self.calls = []
        patcher = mock.patch.object(plan, "LAND", self.fake_land)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.t.write_ledger([ledger_row("P-001", "fixed", "tests/render/a_test 1-9 fails before"), ledger_row("P-002", note="advanced 11-20: half"),
                             ledger_row("P-003"), ledger_row("P-004")], tree=self.t.folder())
        self.t.commit("A.1", "A.1: the code")

    def fake_land(self, folder, branch, tablet, wait, *rest):
        self.calls.append(branch)
        return self.verdict

    def state_commits(self):
        out = self.t.git("log", "--format=%s", "origin/main..HEAD", cwd=self.t.folder())
        return [l for l in out.splitlines() if l.startswith("Plan: step A.1 landed")]

    def test_a_new_landing_takes_the_earlier_state_commit_off(self):
        t = self.t
        self.assertEqual(t.run(["land", "A.1"])[0], 1)
        self.assertEqual(len(self.state_commits()), 1)
        first_code = t.git("log", "-1", "--format=%b", cwd=t.folder())
        t.commit("A.1", "the fix", {"runtime/src/render/a.cpp": "a\nb\nc\nd\ne\n"})
        self.assertEqual(t.run(["land", "A.1"])[0], 1)
        self.assertEqual(len(self.state_commits()), 1, "one commit that says landed, not two")
        subjects = t.git("log", "--format=%s", "origin/main..HEAD", cwd=t.folder()).splitlines()
        self.assertEqual(subjects[0], "Plan: step A.1 landed", "the newest is the tool's own")
        self.assertEqual(subjects[1], "the fix", "the fix stands under it")
        code_commit = t.git("rev-parse", "--short=9", "HEAD~1", cwd=t.folder())
        self.assertIn("The step's code is %s." % code_commit, t.git("log", "-1", "--format=%b", cwd=t.folder()))
        self.assertNotIn(first_code, t.git("log", "-1", "--format=%b", cwd=t.folder()))

    def test_land_refuses_a_step_branch_that_changes_gate_py_without_its_card(self):
        t = self.t
        t.commit("A.1", "the gate in a step", {"tools/gate/gate.py": "# changed\n"})
        code, text = t.run(["land", "A.1"])
        self.assertEqual(code, 1)
        self.assertIn("its card does not name them: tools/gate/gate.py", text)
        self.assertEqual(self.calls, [], "nothing was queued")
        self.assertEqual(t.git("log", "-1", "--format=%s", cwd=t.folder()), "the gate in a step", "and no state commit was made")

    def test_the_speed_figures_are_kept_with_the_step(self):
        t = self.t
        self.verdict = plan.Verdict(True, [("repo/code", "PASS", "ok"), ("measure/step/pc", "PASS", "ok: d3d12 cpu_work_ms 6.4 (median 6.9)"),
                                           ("measure/step/tablet", "PASS", "better: gap_p99_ms 19.9")])
        self.verdict.code, self.verdict.commit, self.verdict.gate_recorded = 0, "0123456789", True
        code, text = t.run(["land", "A.1"], expect=0)
        self.assertIn("measure/step/pc: ok: d3d12 cpu_work_ms 6.4 (median 6.9)", text)
        row = lv.read_landings(t.live)[0]
        self.assertIn("measure/step/pc: ok: d3d12 cpu_work_ms 6.4", row["figures"])
        self.assertIn("measure/step/tablet: better", row["figures"])
        static, data, landings, faults = plan.page_data(t.tree, t.live)
        self.assertIn("measure/step/pc", data["steps"]["A.1"]["figures"])
        self.assertIn("Speed: ", page.render(static, data), "the page draws the line")


class PageTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)

    def test_the_page_has_the_new_parts(self):
        t = self.t
        t.run(["start", "A.1"], expect=0)
        t.run(["decide", "A.1", "the document wins", "--because", "the order", "--doc", "AGENT.md 7"], expect=0)
        t.run(["mend", "open", "A.1", "a fault"], expect=0)
        t.run(["blocked", "C.1", "owner: keep the old jump height or the new one?"], expect=0)
        (t.live / "tablet-owed.tsv").write_text("2026-11-22T10:00:00\tabc123def\tandroid/tablet,tour/tablet\n"
                                                "2026-11-22T11:00:00\tdef456abc\tandroid/tablet\n", encoding="utf-8")
        static, data, landings, faults = plan.page_data(t.tree, t.live)
        self.assertEqual([m["id"] for m in data["mends"]], ["M-001"])
        self.assertEqual(data["decisions"][0]["what"], "the document wins")
        self.assertEqual(data["owner"][0]["question"], "keep the old jump height or the new one?")
        self.assertEqual([b["step"] for b in data["blocked"]], [], "the owner's question is listed apart, not as blocked")
        self.assertEqual(data["tablet"], {"since": "2026-11-22T10:00:00", "owed": 3})
        html = page.render(static, data)
        for text in ("Waiting for the owner", "Tool mends", "Decisions", "Checks changed during the run", "Tablet away since",
                     "the work has stopped: open the session and send the word continue"):
            self.assertIn(text, html)
        status = plan.status_text(t.tree, t.work, live=t.live)
        self.assertIn("## Waiting for the owner (1)", status)
        self.assertIn("M-001 open", status)
        self.assertIn("away since 2026-11-22T10:00:00: 3 checks owed", status)
        written = page.make_status(static, data, landings, faults, data["generated"])[0]
        self.assertIn("Tablet away since 2026-11-22 10:00: 3 checks owed.", written)
        self.assertIn("## Tool mends", written)

    def test_a_lock_held_by_hand_is_named(self):
        root = pathlib.Path(self.t.dir) / "plan"
        hand = root / "gate.lock"
        hand.mkdir(parents=True)
        (hand / "holder.txt").write_text("U mend by hand 2026-11-22 10:00:00\n", encoding="utf-8")
        old = (datetime.datetime.now() - datetime.timedelta(minutes=45)).timestamp()
        os.utime(hand, (old, old))
        young = root / "tablet.lock"
        young.mkdir()
        (young / "holder.txt").write_text("gate by hand\n", encoding="utf-8")
        dead = root / "measure.lock"
        dead.mkdir()
        (dead / "holder.txt").write_text("measure pid 4242\n", encoding="utf-8")
        os.utime(dead, (old, old))
        notes = plan.lock_notes(root, alive_fn=lambda pid: False)
        self.assertEqual(len(notes), 2, notes)
        self.assertIn("gate.lock is held by hand since", notes[0])
        self.assertIn("nothing takes it", notes[0])
        self.assertIn("measure.lock", notes[1])
        self.assertIn("the next run takes it over", notes[1])
        self.assertEqual(plan.lock_notes(root, alive_fn=lambda pid: True), notes[:1], "a holder that runs is no note")

    def test_a_flake_repair_ends_the_set_aside(self):
        t = self.t
        t.live.mkdir(parents=True, exist_ok=True)
        aside = t.live / "set-aside.tsv"
        aside.write_text("2026-11-20T10:00:00\tmenu-key-icons\td3d11\t3 flakes in 7 days\n2026-11-20T10:00:00\tboot\td3d12\t3 flakes in 7 days\n",
                         encoding="utf-8")
        flakes = t.live / "flakes.tsv"
        flakes.write_text("2026-11-20T09:00:00\tmenu-key-icons\td3d11\tabc\tmsg\n2026-11-20T09:30:00\tboot\td3d12\tabc\tmsg\n", encoding="utf-8")
        self.assertTrue(kinds.end_set_aside(aside, "flake:menu-key-icons", "d3d11"))
        self.assertEqual([l.split("\t")[1] for l in aside.read_text(encoding="utf-8").splitlines()], ["boot"], "only that test and renderer")
        self.assertEqual([l.split("\t")[1] for l in flakes.read_text(encoding="utf-8").splitlines()], ["boot"], "its flakes start again")
        self.assertFalse(kinds.end_set_aside(aside, "tour/d3d12", "d3d12"), "a repair of another source ends nothing")
        # the repair lands: plan.py ends the time aside
        row, _ = lv.add_repair(t.live, "flake", "fail", "flake:boot", "d3d12", evidence="D:/x", note="boot set aside")
        lv.set_repair(t.live, row["id"], "started")
        lv.set_state_entry(t.live, row["id"], "started", 1)
        a = type("A", (), dict(live=str(t.live)))()
        with contextlib.redirect_stdout(io.StringIO()) as out:
            plan.end_aside(a, dict(row, source="flake"))
        self.assertEqual(aside.read_text(encoding="utf-8").strip(), "")
        self.assertIn("counts again", out.getvalue())

    def test_a_flake_repair_card_says_what_to_do(self):
        t = self.t
        lv.add_repair(t.live, "flake", "fail", "flake:boot", "d3d12", evidence="D:/x", note="boot set aside")
        code, text = t.run(["next"], expect=0)
        self.assertIn("This test is set aside", text)
        self.assertIn("mend open R-001", text)
        self.assertIn("with a ledger row", text)


def test_names(args):
    """The test names asked for on the command line; the harness's own option --work FOLDER is not one."""
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
        elif a == "--work":
            skip = True
        elif not a.startswith("--work="):
            out.append(a)
    return out


def main():
    result = unittest.main(argv=[sys.argv[0]] + test_names(sys.argv[1:]), exit=False, verbosity=1).result
    print("plan_mend_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
