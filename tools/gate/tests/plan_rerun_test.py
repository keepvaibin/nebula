"""plan_rerun_test.py: `plan.py land` run again at any time, and what goes with it (package T13 item 11, T14 items 2, 5 and 6).

The state commit is made once and nothing is ever undone; what is landed is read from origin/main; the landing record and the
join of a landing that is in the queue; a landing found on main; `plan.py card`; the commit main has for a landing; the extra
checks of a step (step-checks.tsv); the replay-reference guard; `plan.py owed`. The commands run in a temporary git tree made by
plan_fixture (a bare origin, a plan tree, one work folder) with the gate's landing step replaced by a fake and the landing queue
a temporary file: no game, no build, no network, nothing of D:/NebulaWork/plan.

    python -X utf8 -I tools/gate/tests/plan_rerun_test.py [--work <folder>]
"""
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "plan"))
import live as lv  # noqa: E402
import page  # noqa: E402
import plan  # noqa: E402
from plan_fixture import Tree, git_run, ready_a1, setup_module, teardown_module  # noqa: E402

STATE_SUBJECT = "Plan: step A.1 landed"


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


def passed_verdict(commit="0123456789"):
    v = plan.Verdict(True, [("repo/code", "PASS", "ok")])
    v.code, v.commit, v.gate_recorded = 0, commit, True
    return v


def failed_verdict():
    v = plan.Verdict(False, [("tour/d3d12", "FAIL", "replay differs")], [("tour/d3d12", "FAIL", "replay differs")])
    v.code, v.gate_recorded = 1, True
    return v


class RerunBase(unittest.TestCase):
    """A started A.1 in a temporary tree, a fake gate, a queue file of its own and no process alive."""

    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.t.run(["start", "A.1"], expect=0)
        self.folder = self.t.folder("A.1")
        self.calls = []
        self.results = []                      # what the fake gate answers, one for each call (the last one is kept)
        self.queue = self.t.dir / "queue.tsv"
        self.alive_pids = set()
        for patcher in (mock.patch.object(plan, "LAND", self.fake_land), mock.patch.object(plan, "QUEUE_FILE", str(self.queue)),
                        mock.patch.object(plan, "alive", lambda pid: pid in self.alive_pids), mock.patch.object(plan, "JOIN_POLL", 0)):
            patcher.start()
            self.addCleanup(patcher.stop)

    # -- the fake gate: what it was given, and what the landing record held while it ran
    def fake_land(self, folder, branch, tablet, wait, options=(), **kw):
        self.calls.append({"branch": branch, "options": list(options), "kw": kw, "head": self.head(),
                           "record": lv.read_record(self.t.live, 1)})
        return self.results.pop(0) if len(self.results) > 1 else self.results[0]

    def head(self):
        return self.t.git("rev-parse", "HEAD", cwd=self.folder)

    def permit(self, *files):
        """What a card that asks for the change, and the strictness log, say of these files (a step may change a test tool, a list of
        known reports or a recorded replay only so): the branch's card names them and the log has a line for each."""
        card = self.folder / "docs" / "plan" / "native" / "03-X.md"
        card.write_text(card.read_text(encoding="utf-8").replace("`runtime/src/render/a.cpp`, ", "`runtime/src/render/a.cpp`, " + ", ".join(
            "`%s`" % f for f in files) + ", ", 1), encoding="utf-8")
        log = self.folder / "docs" / "plan" / "strictness-log.tsv"
        log.write_text("\t".join(plan.strictness.LOG_COLUMNS) + "\n" + "".join("\t".join(
            ["2026-11-22", f, "a test plants it", "a self-test", "plan_rerun_test", "A.1", "-"]) + "\n" for f in files), encoding="utf-8")
        self.t.git("add", "-A", cwd=self.folder)
        self.t.git("commit", "-q", "-m", "the card and the log name them", cwd=self.folder)

    def subjects(self):
        return self.t.git("log", "--format=%s", "origin/main..HEAD", cwd=self.folder).splitlines()

    def state_commits(self):
        return [s for s in self.subjects() if s == STATE_SUBJECT]

    def land(self, expect=None, args=()):
        return self.t.run(["land", "A.1", *args], expect)

    def fix(self, text):
        """A fix commit on the branch."""
        return self.t.commit("A.1", "fix: " + text, {"runtime/src/render/a.cpp": "a\nb\nc\nd\n" + text + "\n"})

    def queue_row(self, state="waiting", branch="native/A.1"):
        q = plan.landing.Queue(self.queue)
        q.add([branch], "1", lambda b: "abc")
        if state in ("running", "landed"):
            q.claim()
        if state == "landed":
            q.finish(branch, "landed", "0123456789")
        return q.row(branch)

    def record(self, tip=None, pid=424242):
        lv.write_record(self.t.live, 1, "A.1", "native/A.1", tip or self.head(), pid)
        return lv.read_record(self.t.live, 1)

    def after_a_failed_landing(self):
        """The ledger the card wants, a failed landing (the state commit stays on the branch); returns (code commit, state tip)."""
        code_commit = ready_a1(self.t)
        self.results = [failed_verdict()]
        self.assertEqual(self.land()[0], 1)
        self.calls.clear()
        return code_commit, self.head()


# ---- A1: the state commit is made once, nothing is undone -----------------------------------------------------------

class StateCommitTest(RerunBase):
    def test_a_second_land_after_a_failure_makes_no_second_state_commit_and_hands_over_the_same_tip(self):
        ready_a1(self.t)
        self.results = [failed_verdict()]
        self.assertEqual(self.land()[0], 1)
        tip = self.head()
        self.assertEqual(self.subjects()[0], STATE_SUBJECT)
        self.assertEqual(self.land()[0], 1)
        self.assertEqual(self.head(), tip, "the second try made no commit")
        self.assertEqual(len(self.state_commits()), 1)
        self.results = [passed_verdict()]
        self.land(expect=0)
        self.assertEqual(len(self.state_commits()), 1, "and the pass made none either")
        self.assertEqual([c["head"] for c in self.calls], [tip, tip, tip], "the gate was always given the same tip")

    def test_fix_commits_on_top_of_a_state_commit_get_a_second_one_that_names_the_new_code_commit(self):
        code_commit = ready_a1(self.t)
        self.results = [failed_verdict()]
        self.land()
        fix = self.fix("the repair")
        self.results = [passed_verdict()]
        self.land(expect=0)
        self.assertEqual(self.subjects()[:3], [STATE_SUBJECT, "fix: the repair", "A.1: the code"], "the earlier state commit was taken off (E1)")
        rows = lambda ref: plan.find(plan.parse_base(self.t.git("show", ref + ":docs/plan/native/steps.tsv", cwd=self.folder), ref), "A.1")
        self.assertEqual((rows("HEAD")["state"], rows("HEAD")["commit"]), ("landed", self.t.git("rev-parse", "--short=9", "HEAD~1", cwd=self.folder)))
        self.assertNotEqual(rows("HEAD")["commit"], code_commit, "and no commit names the first code commit as the step's")
        self.assertEqual(self.t.git("show", "HEAD", "--name-only", "--format=", cwd=self.folder), "docs/plan/native/steps.tsv")

    def test_the_tool_never_calls_a_hard_reset(self):
        for path in sorted((TREE / "tools" / "plan").glob("*.py")):
            text = path.read_text(encoding="utf-8")
            self.assertNotRegex(text, r"""["']reset["']|reset\s+--hard""", path.name)

    def test_a_state_commit_that_changes_another_file_is_not_taken_for_the_tools_own(self):
        ready_a1(self.t)
        self.results = [failed_verdict()]
        self.land()
        # a commit with the state commit's subject that also changes a source file: the tool makes its own on top of it
        (self.folder / "runtime" / "src" / "render" / "a.cpp").write_text("a\nb\nc\nd\nx\ny\n", encoding="utf-8")
        self.t.git("add", "-A", cwd=self.folder)
        self.t.git("commit", "-m", STATE_SUBJECT, cwd=self.folder)
        before = len(self.state_commits())
        self.results = [passed_verdict()]
        self.land(expect=0)
        self.assertEqual(len(self.state_commits()), before, "the tool's own earlier one went, its new one came; the other commit stays")


# ---- A2: what is landed is what origin/main says ----------------------------------------------------------------------

class LandedIsWhatMainSaysTest(RerunBase):
    def run_from_folder(self, args):
        """plan.py with the work folder as the tree it reads, as an agent runs its own copy."""
        from plan_fixture import run
        return run(args, self.folder, self.t.work, self.t.store, None, self.t.live)

    def test_the_folders_landed_row_for_a_step_main_does_not_have_is_started(self):
        ready_a1(self.t)
        self.results = [failed_verdict()]
        self.land()
        self.assertEqual(plan.find(plan.read_base(self.folder), "A.1")["state"], "landed", "the branch says landed")
        code, text = self.run_from_folder(["next"])
        self.assertTrue(text.startswith("IN WORK: A.1"), text[:60])
        code, text = self.run_from_folder(["status"])
        self.assertIn("Steps landed: 0 of 5.", text)
        self.assertIn("- A.1 First step: agent 1, branch native/A.1", text)
        code, text = self.run_from_folder(["start", "C.1"])
        self.assertEqual(code, 1)
        self.assertIn("you have A.1 in work", text)

    def test_when_main_has_it_the_same_command_sees_it_landed(self):
        ready_a1(self.t)
        self.results = [failed_verdict()]
        self.land()
        git_run("push", "-q", "origin", "native/A.1:main", cwd=str(self.folder))
        code, text = self.run_from_folder(["status"])
        self.assertIn("Steps landed: 1 of 5.", text)

    def test_blocked_after_a_failed_landing_sees_the_step_as_started_not_landed(self):
        ready_a1(self.t)
        self.results = [failed_verdict()]
        self.land()
        code, text = self.run_from_folder(["blocked", "A.1", "the replay differs"])
        self.assertEqual(code, 0, text)
        self.assertEqual(lv.read_state(self.t.live)["A.1"]["state"], "blocked")

    def test_page_and_other_commands_read_the_last_fetched_main_too(self):
        ready_a1(self.t)
        self.results = [failed_verdict()]
        self.land()
        self.run_from_folder(["page"])
        data = json.loads((self.t.live / "publish" / "progress-data.json").read_text(encoding="utf-8"))
        self.assertEqual(data["steps"]["A.1"], {"state": "started", "agent": "1"}, "not landed: main does not have it")


# ---- A3: the landing record, the join, LANDING RUNNING ---------------------------------------------------------------

class RecordTest(RerunBase):
    def test_the_record_stands_while_the_gate_runs_and_goes_when_it_has_answered(self):
        ready_a1(self.t)
        for verdict in (failed_verdict(), passed_verdict()):
            self.results = [verdict]
            self.calls.clear()
            self.land()
            seen = self.calls[0]["record"]
            self.assertEqual((seen["step"], seen["branch"], seen["tip"], seen["pid"]),
                             ("A.1", "native/A.1", self.head(), str(os.getpid())))
            self.assertTrue(lv.parse_stamp(seen["time"]))
            self.assertIsNone(lv.read_record(self.t.live, 1), "removed when the gate has answered")

    def test_the_record_goes_when_the_gate_call_raises(self):
        ready_a1(self.t)
        def boom(*a, **k):
            raise OSError("the gate could not be started")
        with mock.patch.object(plan, "LAND", boom):
            code, text = self.land()
        self.assertEqual(code, 2)
        self.assertIsNone(lv.read_record(self.t.live, 1))

    def test_land_joins_a_landing_that_is_in_the_queue(self):
        code_commit, tip = self.after_a_failed_landing()
        row = self.queue_row("running")
        self.record(tip)
        (self.folder / "loose.txt").write_text("not committed", encoding="utf-8")     # the agent changed the folder meanwhile
        self.results = [passed_verdict()]
        code, text = self.land()
        self.assertEqual(code, 0, text)
        self.assertIn("joined the landing of A.1, in the queue since %s" % plan.hhmm(row["time"]), text.splitlines())
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(self.calls[0]["kw"], {"push": False}, "nothing is pushed: the gate has the branch")
        self.assertEqual(self.calls[0]["record"]["pid"], "424242", "the record of the landing that runs is left as it is")
        self.assertEqual(self.head(), tip, "nothing is committed")
        self.assertIn("LANDED A.1 as %s" % code_commit, text, "the code commit is the one the handed tip names")
        self.assertEqual(len(lv.read_landings(self.t.live)), 1)

    def test_land_joins_the_same_way_when_the_row_is_waiting(self):
        _, tip = self.after_a_failed_landing()
        self.queue_row("waiting")
        self.record(tip)
        self.results = [failed_verdict()]
        code, text = self.land()
        self.assertEqual(code, 1, "the same exit code as the call that started the landing")
        self.assertIn("joined the landing of A.1, in the queue since", text)
        self.assertIn("NOT LANDED: A.1.", text)

    def test_status_and_next_begin_with_the_running_line_and_start_refuses(self):
        _, tip = self.after_a_failed_landing()
        self.queue_row("running")
        rec = self.record(tip)
        line = "LANDING RUNNING: A.1 since %s. Run `plan.py land A.1` to wait for its end." % plan.hhmm(rec["time"])
        code, text = self.t.run(["next"])
        self.assertEqual(text.splitlines()[0], line)
        code, text = self.t.run(["status"])
        self.assertEqual(text.splitlines()[:3], ["# Native plan: status", "", line])
        code, text = self.t.run(["start", "C.1"])
        self.assertEqual(code, 1)
        self.assertEqual(text.strip(), "plan.py: " + line)

    def test_the_running_line_names_the_agent_of_a_second_agent(self):
        rec = {"step": "G6.1", "branch": "native/G6.1", "tip": "a" * 40, "time": "2026-11-21T09:05:00", "pid": "1"}
        self.assertEqual(plan.landing_running_line(rec, "2"),
                         "LANDING RUNNING: G6.1 since 09:05. Run `plan.py land G6.1 --agent 2` to wait for its end.")

    def test_a_record_whose_row_has_ended_and_whose_process_is_gone_is_dropped_with_no_line(self):
        _, tip = self.after_a_failed_landing()
        self.queue_row("landed")
        self.record(tip)
        code, text = self.t.run(["next"])
        self.assertNotIn("LANDING RUNNING", text)
        self.assertIsNone(lv.read_record(self.t.live, 1))
        self.record(tip)                                   # and a record with no queue row at all
        self.queue.unlink()
        code, text = self.t.run(["status"])
        self.assertNotIn("LANDING RUNNING", text)
        self.assertIsNone(lv.read_record(self.t.live, 1))

    def test_a_record_whose_process_still_runs_is_not_dropped_even_when_the_row_has_ended(self):
        _, tip = self.after_a_failed_landing()
        self.queue_row("landed")
        self.record(tip, pid=777)
        self.alive_pids.add(777)
        code, text = self.t.run(["next"])
        self.assertTrue(text.startswith("LANDING RUNNING: A.1"), text[:50])
        self.assertIsNotNone(lv.read_record(self.t.live, 1))

    def test_a_landing_that_is_finishing_is_waited_for_and_the_call_goes_on(self):
        _, tip = self.after_a_failed_landing()
        self.queue_row("landed")
        self.record(tip, pid=777)
        self.alive_pids.add(777)
        polls = []
        def sleeper(seconds):
            polls.append(seconds)
            self.alive_pids.discard(777)               # the process ends while it is waited for
        with mock.patch.object(plan.time, "sleep", sleeper):
            self.results = [passed_verdict()]
            code, text = self.land()
        self.assertEqual(polls, [0])
        self.assertIn("joined the landing of A.1: its row in the queue has ended and process 777 is finishing; waiting for it", text)
        self.assertEqual(code, 0, text)
        self.assertEqual(len(self.calls), 1, "then the landing went on as a new one")

    def test_a_landing_of_another_item_in_the_gates_hands_refuses_land(self):
        self.after_a_failed_landing()
        lv.write_record(self.t.live, 1, "C.1", "native/C.1", "b" * 40, 5)
        self.queue_row("running", "native/C.1")
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("LANDING RUNNING: C.1", text)
        self.assertEqual(self.calls, [])

    def test_a_queue_of_its_own_is_passed_to_the_gate_only_when_it_is_not_the_default(self):
        a = types.SimpleNamespace(live="L", failures="F")
        self.assertNotIn("--queue-file", plan.gate_options(a, "1"))
        a.queue_file = plan.QUEUE_FILE
        self.assertNotIn("--queue-file", plan.gate_options(a, "1"))
        a.queue_file = "other.tsv"
        self.assertEqual(plan.gate_options(a, "1")[-2:], ["--queue-file", "other.tsv"])


# ---- A4: the folder was changed while a landing ran ----------------------------------------------------------------

class ChangedWhileRunningTest(RerunBase):
    def test_a_joined_landing_that_failed_on_an_older_tip_lands_the_present_one_in_the_same_call(self):
        code_commit, old = self.after_a_failed_landing()
        self.queue_row("running")
        self.record(old)
        fix = self.fix("the repair")
        new = self.head()
        self.results = [failed_verdict(), passed_verdict()]
        code, text = self.land()
        self.assertEqual(code, 0, text)
        lines = text.splitlines()
        said = "the landing that was running judged an older commit (%s); landing the present one (%s) now" % (old[:9], new[:9])
        self.assertIn(said, lines)
        order = [i for i, l in enumerate(lines) if l.startswith("joined the landing") or l == said or l.startswith("LANDED A.1")]
        self.assertEqual(len(order), 3, "the join, the old verdict, the new landing, in that order")
        self.assertLess(lines.index(said), [i for i, l in enumerate(lines) if l.startswith("LANDED A.1")][0])
        self.assertEqual([c["kw"] for c in self.calls], [{"push": False}, {}], "joined first, then a landing of its own")
        self.assertEqual(self.calls[1]["head"], self.head())
        self.assertEqual(self.subjects()[:3], [STATE_SUBJECT, "fix: the repair", "A.1: the code"], "the earlier state commit is off (E1), a new one on the fix")
        self.assertIn("LANDED A.1 as %s" % self.t.git("rev-parse", "--short=9", "HEAD~1", cwd=self.folder), text)

    def test_a_joined_landing_that_passed_is_landed_and_the_later_commits_are_named_as_not_on_main(self):
        code_commit, old = self.after_a_failed_landing()
        self.queue_row("running")
        self.record(old)
        self.fix("later one")
        self.fix("later two")
        self.results = [passed_verdict()]
        code, text = self.land()
        self.assertEqual(code, 0, text)
        self.assertIn("LANDED A.1 as %s" % code_commit, text)
        notes = [l for l in text.splitlines() if l.startswith("NOTE:")]
        self.assertEqual(len(notes), 1)
        self.assertIn("fix: later one", notes[0])
        self.assertIn("fix: later two", notes[0])
        self.assertEqual(len(self.calls), 1, "no second landing")

    def test_a_joined_landing_that_failed_on_the_same_tip_is_not_started_again(self):
        _, tip = self.after_a_failed_landing()
        self.queue_row("running")
        self.record(tip)
        self.results = [failed_verdict()]
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertNotIn("judged an older commit", text)
        self.assertEqual(len(self.calls), 1)

    def test_a_stop_after_three_failures_is_not_started_again_either(self):
        _, old = self.after_a_failed_landing()
        self.queue_row("running")
        self.record(old)
        self.fix("x")
        stop = failed_verdict()
        stop.code = 3
        self.results = [stop]
        code, text = self.land()
        self.assertEqual(code, 3)
        self.assertNotIn("judged an older commit", text)
        self.assertEqual(len(self.calls), 1)


# ---- A5: the step is on main already ----------------------------------------------------------------------------------

class FoundOnMainTest(RerunBase):
    def test_a_step_main_holds_gets_what_follows_a_pass_once_and_ends_with_code_0(self):
        self.t.landed("A.1", commit="abc123def")
        lv.set_state_entry(self.t.live, "A.1", "started", 1)            # the tool was cut before it wrote the pass
        self.record(pid=424242)
        for _ in range(2):                                              # run again: nothing is done twice
            code, text = self.land()
            self.assertEqual(code, 0, text)
            self.assertIn("LANDED (found on main): A.1 as abc123def", text)
        self.assertEqual(self.calls, [], "the gate was not asked")
        rows = lv.read_landings(self.t.live)
        self.assertEqual([(r["step"], r["commit"], r["main"], r["agent"]) for r in rows], [("A.1", "abc123def", "-", "1")])
        self.assertNotIn("A.1", lv.read_state(self.t.live), "no longer started")
        self.assertIsNone(lv.read_record(self.t.live, 1))
        self.assertEqual(lv.claim_of(lv.read_claims(self.t.live), "A")["agent"], "1", "A.2 is left: the claim stands")
        self.assertIn("Steps landed: 1 of 5 (20%).", (self.t.live / "STATUS.md").read_text(encoding="utf-8"))
        self.assertEqual(self.t.git("status", "--porcelain", cwd=self.folder), "", "the work folder is not dirtied")

    def test_the_claim_goes_when_it_was_the_last_step_of_its_package(self):
        self.t.landed("A.1", "A.2", commit="abc123def")
        lv.set_state_entry(self.t.live, "A.1", "started", 1)
        code, text = self.land()
        self.assertEqual(code, 0, text)
        self.assertIsNone(lv.claim_of(lv.read_claims(self.t.live), "A"))


# ---- A6: card, and the step that follows ------------------------------------------------------------------------------

class CardTest(RerunBase):
    def snapshot(self):
        names = sorted(p.name for p in self.t.live.rglob("*") if p.is_file() and p.name != "heartbeat.txt")     # every command writes its time
        stamps = {p.name: p.stat().st_mtime_ns for p in self.t.live.rglob("*") if p.is_file() and p.name != "heartbeat.txt"}
        return names, stamps, self.t.git("status", "--porcelain", cwd=self.t.tree), self.t.git("status", "--porcelain", cwd=self.folder)

    def test_card_prints_the_card_the_rows_and_the_checks_and_changes_nothing(self):
        before = self.snapshot()
        code, text = self.t.run(["card", "B.1"])
        self.assertEqual(code, 0, text)
        self.assertEqual(text.splitlines()[0], "CARD: B.1  Other step")
        self.assertIn("### B.1 Other step", text)
        self.assertIn("| P-004 | four |", text, "its package's rows")
        self.assertIn("=== THE CHECKS ITS CHANGED PATHS WILL RUN ===", text)
        self.assertNotIn("=== COMMANDS ===", text)
        code, text = self.t.run(["card", "A.1"])
        self.assertIn("- **Rows.** P-001: whole.", text, "a step that is started has its card as well")
        self.assertEqual(self.snapshot(), before)

    def test_card_needs_no_agent_and_an_unknown_id_is_refused(self):
        args = plan.parser().parse_args(["card", "A.1"])
        self.assertFalse(hasattr(args, "agent"))
        code, text = self.t.run(["card", "Z.9"])
        self.assertEqual(code, 1)
        self.assertIn("no step Z.9", text)

    def test_card_of_a_repair_is_its_card_without_the_commands(self):
        lv.add_repair(self.t.live, "nightly", "fail", "tour/d3d11", "d3d11", "A.1", "abc123456", "D:/ev", "P-001", "stop 025 differs")
        code, text = self.t.run(["card", "R-001"])
        self.assertEqual(code, 0, text)
        self.assertEqual(text.splitlines()[0], "REPAIR: R-001  (nightly, kind fail)")
        self.assertIn("What failed: tour/d3d11 on d3d11", text)
        self.assertNotIn("=== COMMANDS ===", text)

    def test_next_ends_with_the_step_that_follows(self):
        code, text = self.t.run(["next"])                              # A.1 is in work
        self.assertEqual(text.splitlines()[0], "IN WORK: A.1  First step")
        self.assertEqual(text.splitlines()[-1], "after it: A.2 Second step; read it with `plan.py card A.2`")
        self.t.run(["blocked", "A.1", "why"], expect=0)
        code, text = self.t.run(["next"])
        self.assertTrue(text.startswith("NEXT: C.1"), text[:40])
        self.assertNotIn("after it:", text, "D.1 waits for every step: nothing follows C.1 while A.1 is blocked")

    def test_the_step_after_the_first_real_step_is_the_second(self):
        with tempfile.TemporaryDirectory() as live:
            a = types.SimpleNamespace(tree=TREE, live=live, agent=1)
            # The plan's own order, whatever has landed since: every step is read as open (T11-B: the test broke when S0.1.1 and S0.1.2 landed).
            steps = [dict(s, state="open") for s in plan.read_steps(TREE, live)]
            self.assertEqual(plan.following(a, steps, steps[0])["step"], steps[1]["step"])
            self.assertEqual(steps[1]["step"], "S0.1.2")


# ---- A7: two commits for each landing -----------------------------------------------------------------------------------

class MainCommitTest(RerunBase):
    def test_the_commit_main_moved_to_is_kept_and_shown_next_to_the_code_commit(self):
        code_commit = ready_a1(self.t)
        self.results = [passed_verdict("0123456789")]
        code, text = self.land()
        self.assertEqual(code, 0, text)
        row = lv.read_landings(self.t.live)[0]
        self.assertEqual((row["commit"], row["main"]), (code_commit, "0123456789"))
        self.assertIn("LANDED A.1 as %s (main has it as 0123456789)" % code_commit, text)
        status = (self.t.live / "STATUS.md").read_text(encoding="utf-8")
        self.assertIn("| When | Step | Title | Code commit | Main commit |", status)
        self.assertRegex(status, r"\| A\.1 \| First step \| %s \| 0123456789 \|" % code_commit)
        data = json.loads((self.t.live / "publish" / "progress-data.json").read_text(encoding="utf-8"))
        self.assertEqual((data["steps"]["A.1"]["commit"], data["steps"]["A.1"]["main"]), (code_commit, "0123456789"))
        code, text = self.t.run(["status"])
        self.assertIn("- A.1: commit %s, main 0123456789," % code_commit, text)
        html = (self.t.live / "progress.html").read_text(encoding="utf-8")
        self.assertIn('"main":"0123456789"', html)
        self.assertIn('"  main " + st[s.id].main', html, "the page's script draws it")

    def test_a_landing_whose_main_commit_is_not_known_shows_a_dash(self):
        v = passed_verdict("")
        ready_a1(self.t)
        self.results = [v]
        self.land(expect=0)
        self.assertEqual(lv.read_landings(self.t.live)[0]["main"], "-")
        self.assertIn(" | - |", (self.t.live / "STATUS.md").read_text(encoding="utf-8"))

    def test_the_overview_says_what_the_commit_column_holds(self):
        text = (TREE / "docs" / "plan" / "native" / "02-NATIVE-OVERVIEW.md").read_text(encoding="utf-8")
        self.assertIn("holds the code's commit on the step's branch", text)


# ---- B: extra checks by step -----------------------------------------------------------------------------------------

class StepChecksTest(RerunBase):
    def table(self, *lines, tree=None):
        path = plan.native(tree or self.t.tree) / plan.STEP_CHECKS_FILE
        path.write_text("step\tcheck\tserves\n" + "".join(l + "\n" for l in lines), encoding="utf-8")

    def test_the_real_table_names_real_steps_and_the_known_checks(self):
        table = plan.read_step_checks(TREE)
        ids = {s["step"] for s in plan.read_base(TREE)}
        self.assertGreaterEqual(len(table), 8)
        self.assertEqual(set(plan.ALSO_CHECKS), {"stress", "sanitizer/tour", "soak", "soak/long", "replays/software", "speed/anchor", "replays/rates"})
        for step_id, items in table.items():
            self.assertIn(step_id, ids)
            for check, serves in items:
                self.assertIn(check, plan.ALSO_CHECKS, step_id)
                self.assertTrue(serves.strip(), step_id)
        lines = (plan.native(TREE) / plan.STEP_CHECKS_FILE).read_text(encoding="utf-8").splitlines()[1:]
        self.assertEqual(len(lines), len(set(lines)), "a line twice")

    def test_next_and_card_print_each_extra_check_after_the_checks_the_paths_need(self):
        self.table("A.1\tsoak\tproof 2: both soaks clean", "A.1\tstress\tproof 1: same hash under load", "B.1\tsoak\tproof 3: soak")
        want = ["  and, because this step's proof asks for it: soak (proof 2: both soaks clean)",
                "  and, because this step's proof asks for it: stress (proof 1: same hash under load)"]
        for command in (["next"], ["card", "A.1"]):
            code, text = self.t.run(command)
            lines = text.splitlines()
            at = [i for i, l in enumerate(lines) if l.startswith("  checks:")][0]
            self.assertEqual(lines[at + 1:at + 3], want, command)
            self.assertNotIn("proof 3", text, "another step's line")

    def test_a_step_with_no_line_prints_none(self):
        self.table("B.1\tsoak\tproof 3: soak")
        self.assertNotIn("because this step's proof asks", self.t.run(["card", "A.1"])[1])

    def test_land_passes_the_steps_checks_to_the_gate_as_also(self):
        self.table("A.1\tsoak\tproof 2: x", "A.1\tstress\tproof 1: y", "A.1\tsoak\tproof 5: z")
        ready_a1(self.t)
        self.results = [passed_verdict()]
        self.land(expect=0)
        options = self.calls[0]["options"]
        self.assertEqual(options[options.index("--also"):], ["--also", "soak,stress"], "each check once, in the table's order")

    def test_land_passes_no_also_for_a_step_with_no_line(self):
        ready_a1(self.t)
        self.results = [passed_verdict()]
        self.land(expect=0)
        self.assertNotIn("--also", self.calls[0]["options"])

    def test_a_check_the_gate_does_not_take_is_refused(self):
        self.table("A.1\ttour/d3d12\tproof 1: x")
        code, text = self.t.run(["next"])
        self.assertEqual(code, 2)
        self.assertIn("check 'tour/d3d12' is none of stress, sanitizer/tour, soak, soak/long, replays/software", text)


# ---- C: the replay guard -----------------------------------------------------------------------------------------------

REPLAY = "tests/replays/001-BattleShipGalaxy-1.tsv"


class ReplayGuardTest(RerunBase):
    def card_says(self, words):
        """The card of A.1 in the work folder and in the plan tree gets a line with these words."""
        for tree in (self.folder, self.t.tree):
            path = plan.native(tree) / "03-X.md"
            path.write_text(path.read_text(encoding="utf-8").replace("- **Proof.** a test.", "- **Proof.** a test. " + words), encoding="utf-8")
        self.t.git("commit", "-q", "-am", "card", cwd=self.folder)

    def change_replay(self, message="the hashes"):
        return self.t.commit("A.1", message, {REPLAY: "# changed\n0\tabc\t3\n"})

    def test_the_four_phrases_in_any_case_allow_it_and_other_words_do_not(self):
        for words in ("re-record the stops", "Rerecord them", "the reference is recorded again", "The Replay Changes here",
                      "a NEW REFERENCE is written", "RE-RECORD"):
            self.assertTrue(plan.replay_allowed("- **Proof.** " + words), words)
        for words in ("the replay stays", "record the tour", "a reference run", "", None):
            self.assertFalse(plan.replay_allowed(words), words)

    def test_a_branch_that_changes_a_replay_file_is_refused_when_the_card_does_not_say_it_may(self):
        ready_a1(self.t)
        self.change_replay()
        before = self.head()
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("plan.py: the replay differs and this step's card does not say it may: the change altered the game; find why", text)
        self.assertIn(REPLAY, text)
        self.assertEqual(self.calls, [], "the gate was not asked")
        self.assertEqual(self.head(), before, "no state commit")

    def test_a_card_that_allows_it_still_needs_the_words_replay_reference_in_a_commit_message(self):
        ready_a1(self.t)
        self.card_says("The replay is recorded again.")
        self.change_replay("the hashes")
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn('no commit message of the landing contains the words "replay reference"', text)
        self.assertEqual(self.calls, [])
        self.t.git("commit", "--allow-empty", "-m", "Replay Reference: recorded after the change", cwd=self.folder)
        self.permit(REPLAY)
        self.results = [passed_verdict()]
        self.land(expect=0)
        self.assertEqual(len(self.calls), 1)

    def test_a_change_with_no_replay_file_is_not_asked_for_the_words(self):
        ready_a1(self.t)
        self.card_says("The replay is recorded again.")
        self.results = [passed_verdict()]
        self.land(expect=0)

    def test_start_prints_the_record_command_for_a_step_whose_card_allows_the_change(self):
        self.t.landed("A.1")
        path = plan.native(self.t.tree) / "03-X.md"
        path.write_text(path.read_text(encoding="utf-8").replace("- **Rows.** P-003 whole.", "- **Rows.** P-003 whole.\n- **Proof.** re-record."),
                        encoding="utf-8")
        self.t.push_plan_files("card")
        code, text = self.t.run(["start", "A.2"])
        self.assertEqual(code, 0, text)
        self.assertIn("record the reference with: " + plan.REPLAY_RECORD, text)
        self.assertIn('must contain the words "replay reference"', text)
        self.t.landed("A.2")
        code, text = self.t.run(["start", "C.1"])
        self.assertNotIn("record the reference", text)

    def test_the_record_command_is_the_one_step_hashs_parser_takes(self):
        self.assertEqual(plan.REPLAY_RECORD, "python -X utf8 -I tools/verify/step_hash.py record <work folder of the tour run> --out tests/replays")
        r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(TREE / "tools" / "verify" / "step_hash.py"), "record", "--help"],
                           capture_output=True, text=True, cwd=str(TREE))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertRegex(r.stdout, r"record \[-h\] \[--out OUT\] work")

    def test_a_repair_may_change_the_replay_only_when_it_names_a_step_whose_card_allows_it(self):
        lv.add_repair(self.t.live, "nightly", "fail", "replays/d3d12", "d3d12", "A.1", "abc123456", "D:/ev", "-", "differs")
        self.t.landed("A.1")
        self.t.run(["start", "R-001"], expect=0)
        self.t.commit("A.1", "hashes", {REPLAY: "# x\n0\tabc\t3\n"})
        code, text = self.t.run(["land", "R-001"])
        self.assertEqual(code, 1)
        self.assertIn(plan.REPLAY_REFUSAL, text)
        self.assertEqual(self.calls, [])


# ---- D: what is owed to a person's check --------------------------------------------------------------------------------

class OwedTest(RerunBase):
    def test_owed_writes_the_file_and_lists_it_in_status_the_status_file_the_page_and_its_data(self):
        steps_before = (plan.native(self.t.tree) / "steps.tsv").read_bytes()
        code, text = self.t.run(["owed", "A.1", "film the clap frame\nwith a phone camera"])
        self.assertEqual(code, 0, text)
        rows = lv.read_owed(self.t.live)
        self.assertEqual([(r["agent"], r["step"], r["text"]) for r in rows], [("1", "A.1", "film the clap frame with a phone camera")])
        self.assertTrue(lv.parse_stamp(rows[0]["time"]))
        code, text = self.t.run(["status"])
        self.assertIn("## For the owner to check by hand (1)", text)
        self.assertIn("- A.1: film the clap frame with a phone camera (agent 1,", text)
        status = (self.t.live / "STATUS.md").read_text(encoding="utf-8")
        self.assertIn("## For the owner to check by hand", status)
        self.assertIn("- A.1 First step: film the clap frame with a phone camera (agent 1,", status)
        data = json.loads((self.t.live / "publish" / "progress-data.json").read_text(encoding="utf-8"))
        self.assertEqual([(o["step"], o["text"]) for o in data["owed"]], [("A.1", "film the clap frame with a phone camera")])
        html = (self.t.live / "progress.html").read_text(encoding="utf-8")
        self.assertIn("For the owner to check by hand", html)
        self.assertIn("film the clap frame with a phone camera", html)
        self.assertEqual((plan.native(self.t.tree) / "steps.tsv").read_bytes(), steps_before, "steps.tsv is never changed")

    def test_owed_never_blocks_a_landing(self):
        ready_a1(self.t)
        self.t.run(["owed", "A.1", "a Bluetooth pad is needed"], expect=0)
        self.results = [passed_verdict()]
        self.land(expect=0)
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(len(lv.read_owed(self.t.live)), 1, "and the line stays")

    def test_owed_names_a_repair_as_well_and_refuses_an_unknown_id_or_empty_words(self):
        lv.add_repair(self.t.live, "nightly", "fail", "tour/d3d11", "d3d11", "-", "-", "D:/ev", "-", "x")
        self.t.run(["owed", "R-001", "a Mac run"], expect=0)
        code, text = self.t.run(["owed", "Z.9", "text"])
        self.assertEqual(code, 1)
        self.assertIn("no step or repair Z.9", text)
        self.assertEqual(self.t.run(["owed", "A.1", "  "])[0], 1)
        self.assertEqual(len(lv.read_owed(self.t.live)), 1)

    def test_the_status_text_of_the_page_module_lists_owed_lines_with_the_steps_title(self):
        static = page.make_static(plan.read_base(TREE)[:2])
        data = page.make_data([], [], {}, [], set(), [], 0, "2026-11-21T09:00:00",
                              owed=[{"time": "2026-11-21T09:00:00", "agent": "1", "step": static["steps"][0]["id"], "text": "a | pipe"}])
        text = page.make_status(static, data, [], [], "2026-11-21T09:00:00")[0]
        self.assertIn("## For the owner to check by hand\n\n- %s %s: a | pipe (agent 1, 2026-11-21 09:00)" % (
            static["steps"][0]["id"], static["steps"][0]["title"]), text)


class CodeChecksTest(RerunBase):
    """T11-B (step S0.1.3): a rule of the repository (the console census, the cast check, the byte-order check) refuses a step as a refusal,
    in seconds, with the check's own lines, before anything is committed or queued; it was "not judged" at main's move."""

    CENSUS_FAIL = "import sys" + chr(10) + "print('fifo            31 closed')" + chr(10) + "print('    11 native/include/x.h')" + chr(10) + "sys.exit(1)" + chr(10)
    PASS = "import sys" + chr(10) + "sys.exit(0)" + chr(10)

    def test_a_census_hit_refuses_the_landing_with_its_lines_and_hands_nothing_over(self):
        ready_a1(self.t)
        self.t.commit("A.1", "the hits", {"tools/verify/console_census.py": self.CENSUS_FAIL, "tools/verify/cast_check.py": self.PASS})
        before = self.head()
        self.results = [passed_verdict()]
        code, text = self.land()
        self.assertEqual(code, 1, text)
        self.assertIn("a rule of the repository refuses it", text)
        self.assertIn("console_census.py refuses it:", text)
        self.assertIn("fifo            31 closed", text)
        self.assertIn("11 native/include/x.h", text)
        self.assertNotIn("not judged", text)
        self.assertEqual(self.calls, [], "the gate was not called")
        self.assertEqual(self.head(), before, "no state commit was made")
        self.assertFalse(self.queue.exists(), "nothing was queued")
        self.assertIsNone(lv.read_record(self.t.live, 1), "no landing record")

    def test_the_other_checks_are_named_too_and_passing_checks_land(self):
        ready_a1(self.t)
        self.t.commit("A.1", "a cast", {"tools/verify/console_census.py": self.PASS,
                                        "tools/verify/cast_check.py": "print('a pointer is read as a number: x.cpp:3')" + chr(10) + "import sys" + chr(10) + "sys.exit(1)" + chr(10)})
        self.results = [passed_verdict()]
        code, text = self.land()
        self.assertEqual(code, 1, text)
        self.assertIn("cast_check.py refuses it:", text)
        self.assertIn("a pointer is read as a number: x.cpp:3", text)
        self.assertNotIn("console_census.py refuses", text)
        self.assertEqual(self.calls, [])
        self.t.commit("A.1", "fixed", {"tools/verify/cast_check.py": self.PASS})
        self.permit("tools/verify/console_census.py", "tools/verify/cast_check.py")
        self.land(expect=0)
        self.assertEqual(len(self.calls), 1, "a tree that passes its code checks is handed to the gate")


def main():
    # The gate starts every self-test with `--work <folder>`; the tests make their own temporary folders and ignore it.
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_rerun_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
