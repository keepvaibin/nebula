"""plan_stack_test.py: the section written ahead and the lander's commands (mend M-007, docs/plan/native/AGENT.md). `plan.py stack`
writes every step of a section before any lands, each branch on the one before; `start` takes a stacked step up once the one it
stands on has landed, its own commits moved onto main; `handoff` and `land`'s refusal of a step with no complete hand-off sheet;
`hard` blocks the step for the fixer with its report; `wait` gives one line when a landing ends.

The commands run in the temporary git tree of plan_fixture with the gate replaced by a fake that moves origin's main as a pass
does: no game, no build, no network, nothing of D:/NebulaWork/plan.

    python -X utf8 -I tools/gate/tests/plan_stack_test.py

Each test was seen to fail with the thing it guards broken (tools/plan/plan.py, planted for real and put back byte for byte):
  test_the_section_is_written_in_order          the barrier gone (native/B.1 made); a step made on main, not on the one before
  test_start_moves_each_stacked_step_onto_main  start checks a stacked branch out as it is (not on main)
  test_hard_blocks_and_holds_the_steps_after    hard does not block; a hard step taken up again off main's tip
  test_land_needs_a_complete_hand_off_sheet     land without the sheet; a heading with nothing under it taken as filled
  test_wait_gives_one_line_when_a_landing_ends  a landing in the gate's hands not seen; a landed step not said LANDED
"""
import pathlib
import sys
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from plan_ahead_test import AheadBase, lv, plan  # noqa: E402
from plan_fixture import Tree, moving_land, setup_module, teardown_module, write_steps  # noqa: E402

ROWS = (("A.1", "A", "", "open"), ("A.2", "A", "", "open"), ("A.3", "A", "", "open"), ("B.1", "B", "", "open"))
CARDS = "\n".join(["### %s Step %s" % (s, s) + "\n\n- **Rows.** None.\n" for s, _, _, _ in ROWS])
SHEET = "# Hand-off sheet of A.1\n\n## Changes\n\nthe draw list\n\n## Pictures declared\n\nnone\n\n## Expected test effects\n\nnone\n\n## Hard if\n\n%s\n"


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


class StackBase(AheadBase):
    """A.1 started in a plan of A.1, A.2, A.3 (package A) and B.1, whose cards ask for no ledger rows; the gate's fake moves main."""

    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        (plan.native(self.t.tree) / "03-X.md").write_text(CARDS, encoding="utf-8")
        write_steps(self.t, ROWS)
        self.t.run(["start", "A.1"], expect=0)
        self.folder = self.t.folder("A.1")
        self.queue = self.t.dir / "queue.tsv"
        for patcher in (mock.patch.object(plan, "LAND", moving_land()), mock.patch.object(plan, "QUEUE_FILE", str(self.queue)),
                        mock.patch.object(plan, "alive", lambda pid: False), mock.patch.object(plan, "JOIN_POLL", 0),
                        mock.patch.object(plan, "WAIT_POLL", 0.01), mock.patch.object(plan, "WAIT_APPEAR", 0.3)):
            patcher.start()
            self.addCleanup(patcher.stop)

    def code(self, step_id, name):
        return self.t.commit(step_id, "%s: the code" % step_id, {"runtime/src/render/%s.cpp" % name: name + "\n"})

    def stack(self, expect=None, *more):
        return self.t.run(["stack", *more], expect)

    def checkout(self, branch):
        self.t.git("checkout", "-q", branch, cwd=self.folder)

    def state(self, step_id):
        return plan.find(plan.read_steps(self.t.tree, self.t.live), step_id)

    def written(self):
        """A.1, A.2 and A.3 written, each with one commit of its own, A.2 and A.3 stacked."""
        self.code("A.1", "a")
        self.stack(0)
        self.code("A.2", "b")
        self.stack(0)
        self.code("A.3", "c")


class SectionTest(StackBase):
    def test_the_section_is_written_in_order(self):
        a1 = self.code("A.1", "a")
        text = self.stack(0)[1]
        self.assertIn("STACKED: A.2", text)
        self.assertEqual(self.on(), "native/A.2")
        self.assertEqual(self.rev("native/A.2"), self.rev(a1), "A.2 stands on A.1's tip")
        self.code("A.2", "b")
        self.assertIn("STACKED: A.3", self.stack(0)[1])
        self.assertEqual(self.rev("native/A.3"), self.rev("native/A.2"), "A.3 stands on A.2's tip")
        self.code("A.3", "c")
        code, text = self.stack()
        self.assertEqual(code, 1)
        self.assertIn("STACK END: package A is written up to A.3. The next step, B.1, is of package B", text)
        self.assertFalse(plan.branch_exists(self.folder, "refs/heads/native/B.1"), "no step of the next package is written")
        self.assertEqual([r["step"] for r in lv.read_stack(self.t.live, 1)], ["A.2", "A.3"])
        self.assertEqual([r["after"] for r in lv.read_stack(self.t.live, 1)], ["A.1", "A.2"])
        listed = self.stack(0, "--list")[1]
        self.assertIn("A.2 on native/A.1", listed)
        self.assertIn("A.3 on native/A.2", listed)
        self.assertIn("written in the stack, not started", self.t.run(["land", "A.2"], 1)[1])

    def test_its_refusals(self):
        self.code("A.1", "a")
        (self.folder / "runtime" / "src" / "render" / "a.cpp").write_text("changed\n", encoding="utf-8")
        self.assertIn("uncommitted changes", self.stack(1)[1])
        self.t.git("checkout", "--", ".", cwd=self.folder)
        self.t.git("branch", "native/A.2", cwd=self.folder)
        self.assertIn("exists already and the stack has no row", self.stack(1)[1])
        self.t.git("branch", "-D", "native/A.2", cwd=self.folder)
        self.stack(0)
        self.checkout("native/A.1")
        self.assertIn("not native/A.2", self.stack(1)[1])
        self.checkout("native/A.2")
        self.plant(self.rev("native/A.1"))
        self.assertIn("in the gate's hands", self.stack(1)[1])
        self.unplant()
        self.t.run(["blocked", "A.1", "x"], expect=0)
        self.assertIn("nothing is in work", self.stack(1)[1])


class TakeUpTest(StackBase):
    def test_start_moves_each_stacked_step_onto_main(self):
        self.written()
        self.checkout("native/A.1")
        self.t.run(["land", "A.1"], expect=0)
        self.assertEqual(self.state("A.1")["state"], "landed")
        self.assertIn("STACKED: branch native/A.2, written on native/A.1; plan.py start A.2 takes it up", self.t.run(["next"])[1])
        old_a2 = self.rev("native/A.2")
        text = self.t.run(["start", "A.2"], expect=0)[1]
        self.assertIn("from the stack: 1 commits of A.2 are on top of main", text)
        self.assertTrue(self.is_ancestor("origin/main", "refs/heads/native/A.2"), "A.2 is on main's tip")
        self.assertEqual(self.subjects("origin/main..native/A.2"), ["A.2: the code"])
        self.assertNotEqual(self.rev("native/A.2"), old_a2)
        self.assertEqual([r["step"] for r in lv.read_stack(self.t.live, 1)], ["A.3"], "A.2's row is taken up")
        self.t.run(["land", "A.2"], expect=0)
        self.assertIn("from the stack: 1 commits of A.3", self.t.run(["start", "A.3"], expect=0)[1])
        self.assertEqual(self.subjects("origin/main..native/A.3"), ["A.3: the code"])
        self.assertEqual(lv.read_stack(self.t.live, 1), [])

    def test_hard_blocks_and_holds_the_steps_after(self):
        self.written()
        self.checkout("native/A.1")
        self.t.run(["land", "A.1"], expect=0)
        self.t.run(["start", "A.2"], expect=0)
        self.assertIn("for the step in work", self.t.run(["hard", "B.1", "x"], 1)[1])
        text = self.t.run(["hard", "A.2", "the picture after differs on d3d11"], expect=0)[1]
        report = plan.hard_path(self.t.live, "A.2")
        self.assertIn("HARD: A.2 is blocked for the fixer. Fill the report: " + report.as_posix(), text)
        s = self.state("A.2")
        self.assertEqual(s["state"], "blocked")
        self.assertTrue(s["reason"].startswith("hard: the picture after differs on d3d11 (report: "), s["reason"])
        body = report.read_text(encoding="utf-8")
        for h in plan.HARD_HEADINGS:
            self.assertIn("## " + h, body)
        report.write_text("kept\n", encoding="utf-8")
        self.assertIn("hard: the picture after differs", self.t.run(["status"], expect=0)[1])
        self.assertIn("step A.3 needs first: A.2 (blocked)", self.t.run(["start", "A.3"], 1)[1])
        self.t.git("fetch", "-q", "origin", cwd=self.folder)            # main moves meanwhile: another landing's commit
        moved = self.t.git("commit-tree", "origin/main^{tree}", "-p", "origin/main", "-m", "another landing", cwd=self.folder)
        self.t.git("push", "-q", "origin", moved + ":refs/heads/main", cwd=self.folder)
        self.t.run(["start", "A.2"], expect=0)
        self.assertTrue(self.is_ancestor("origin/main", "refs/heads/native/A.2"), "the fixer's step is taken up on main's new tip")
        self.t.run(["blocked", "A.2", "x"], expect=0)
        self.t.run(["start", "A.2"], expect=0)
        self.t.run(["hard", "A.2", "again"], expect=0)
        self.assertEqual(report.read_text(encoding="utf-8"), "kept\n", "a report that is there is never written over")
        self.t.run(["hard", "A.3", "its commits do not apply on main"], expect=0)       # a stacked step: blocked, its row kept
        self.assertEqual(self.state("A.3")["state"], "blocked")
        self.assertEqual([r["step"] for r in lv.read_stack(self.t.live, 1)], ["A.3"])


class HandoffTest(StackBase):
    def test_land_needs_a_complete_hand_off_sheet(self):
        self.code("A.1", "a")
        main = self.t.git("rev-parse", "origin/main", cwd=self.folder)
        with mock.patch.object(plan, "HANDOFF_REQUIRED", True):
            text = self.t.run(["land", "A.1"], 1)[1]
            self.assertIn("step A.1 has no complete hand-off sheet", text)
            self.assertIn("is missing (plan.py handoff A.1 makes it", text)
            code, text = self.t.run(["handoff", "A.1"])
            path = plan.handoff_path(self.t.live, "A.1")
            self.assertEqual(code, 1)
            self.assertEqual(text.splitlines()[0], path.as_posix())
            self.assertIn("to fill: nothing is written under '## Changes'", text)
            path.write_text(SHEET % "", encoding="utf-8")
            self.assertIn("nothing is written under '## Hard if'", self.t.run(["land", "A.1"], 1)[1])
            path.write_text(SHEET.replace("## Pictures declared\n\nnone\n\n", "") % "a crash", encoding="utf-8")
            self.assertIn("the heading '## Pictures declared' is missing", self.t.run(["land", "A.1"], 1)[1])
            self.t.git("fetch", "-q", "origin", cwd=self.folder)
            self.assertEqual(self.t.git("rev-parse", "origin/main", cwd=self.folder), main, "nothing was handed to the gate")
            path.write_text(SHEET % "a crash\n### more\n", encoding="utf-8")
            self.assertIn("the sheet is complete", self.t.run(["handoff", "A.1"], 0)[1])
            self.t.run(["land", "A.1"], expect=0)
        self.assertEqual(self.state("A.1")["state"], "landed")


class WaitTest(StackBase):
    def test_wait_gives_one_line_when_a_landing_ends(self):
        self.code("A.1", "a")
        log = self.t.dir / "land.txt"
        log.write_text("gate.py: started\ntour/d3d12  pass\n\n", encoding="utf-8")
        self.plant(self.rev("native/A.1"))
        code, text = self.t.run(["wait", "A.1", "--minutes", "0", "--log", str(log)])
        self.assertEqual(code, 3, text)
        self.assertRegex(text, r"^STILL PLAYING: A\.1, in the gate's hands since \d\d:\d\d; the last line of land\.txt: tour/d3d12  pass\n$")
        self.unplant()
        code, text = self.t.run(["wait", "A.1", "--minutes", "0.05"])
        self.assertEqual((code, text), (1, "ENDED: A.1 is not landed (it is started)\n"))
        self.assertEqual(self.t.run(["wait", "A.1", "--minutes", "10"])[0], 2)
        self.assertIn("no step or repair Q.9", self.t.run(["wait", "Q.9"], 1)[1])
        self.t.run(["land", "A.1"], expect=0)
        self.assertEqual(self.t.run(["wait", "A.1", "--minutes", "0"]), (0, "LANDED: A.1\n"))


if __name__ == "__main__":
    # the gate runs each self-test through guarded.py with options of its own (--work): they are not unittest's
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_stack_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    sys.exit(0 if result.wasSuccessful() else 1)
