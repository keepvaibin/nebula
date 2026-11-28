"""plan_fill_test.py: the fill rule of `plan.py stack` (mend M-014, docs/plan/native/AGENT.md section 8; the owner's plan of 11-27, B1).
While the next step in plan order is a measuring step (its card says "This is a measuring step"), `stack` writes the next steps in plan
order whose package needs are met, at most FILL_MAX of them; the order inside a package is kept; a step of the pacing package G10 has the
gate first (`start` of a fill step waits for it, `status` lists it first).

The commands run in the temporary git tree of plan_fixture with the gate replaced by a fake that moves origin's main as a pass does: no
game, no build, no network, nothing of D:/NebulaWork/plan.

    python -X utf8 -I tools/gate/tests/plan_fill_test.py

Each test was seen to fail with the thing it guards broken (tools/plan/plan.py, planted for real and put back byte for byte), and with
the whole fill rule taken out of plan.py (five of the six fail; the sixth guards the code-step case that stays as it was):
  test_a_measuring_step_next_offers_the_fill_steps   the measuring step treated as landed (G10.3 written)
  test_a_code_step_next_gets_no_fill                 the fill taken for every next step (a FILL line on a code step)
  test_a_blocked_step_still_holds_what_needs_it      the measuring step treated as landed (the steps that need it written)
  test_the_fill_has_a_ceiling                        FILL_MAX not read
  test_a_pacing_step_lands_before_a_fill_step        stack_hold taken out (F.1 started before G10.3)
  test_next_names_the_measuring_step                 say_after without the measuring line
"""
import sys
import unittest
from unittest import mock
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import plan_stack_test as pst  # noqa: E402
from plan_fixture import Tree, moving_land, setup_module, teardown_module, write_steps  # noqa: E402
from plan_stack_test import lv, plan  # noqa: E402

MEASURING = "- **What changes.** This is a measuring step, done by one helper on the tablet.\n"
NOTHING_LEFT = "STACK END: the next step, G10.2m, is a measuring step and no later step has its needs met."


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


def cards(rows, measuring=()):
    return "\n".join("### %s Step %s\n\n- **Rows.** None.\n%s" % (r[0], r[0], MEASURING if r[0] in measuring else "") for r in rows)


class FillBase(pst.StackBase):
    """The plan of ROWS, its first step started, the gate's fake and the queue file as plan_stack_test makes them."""
    ROWS = (("G10.1", "G10", "", "open"), ("G10.2m", "G10", "", "open"), ("G10.3", "G10", "", "open"), ("F.1", "F", "", "open"),
            ("F.2", "F", "", "open"), ("X.1", "X", "F", "open"), ("Y.1", "Y", "W", "open"), ("W.1", "W", "", "open"))
    MEASURING = ("G10.2m", "W.1")

    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.plan_rows(self.ROWS, self.MEASURING)
        first = self.ROWS[0][0]
        self.t.run(["start", first], expect=0)
        self.folder = self.t.folder(first)
        self.queue = self.t.dir / "queue.tsv"
        for patcher in (mock.patch.object(plan, "LAND", moving_land()), mock.patch.object(plan, "QUEUE_FILE", str(self.queue)),
                        mock.patch.object(plan, "alive", lambda pid: False), mock.patch.object(plan, "JOIN_POLL", 0),
                        mock.patch.object(plan, "WAIT_POLL", 0.01), mock.patch.object(plan, "WAIT_APPEAR", 0.3)):
            patcher.start()
            self.addCleanup(patcher.stop)

    def plan_rows(self, rows, measuring=()):
        (plan.native(self.t.tree) / "03-X.md").write_text(cards(rows, measuring), encoding="utf-8")
        write_steps(self.t, rows)

    def branches(self):
        return sorted(l.strip().lstrip("* ") for l in self.t.git("branch", "--list", "native/*", cwd=self.folder).splitlines())

    def stacked(self):
        return [r["step"] for r in lv.read_stack(self.t.live, 1)]


class FillTest(FillBase):
    def test_a_measuring_step_next_offers_the_fill_steps(self):
        g1 = self.code("G10.1", "g1")
        code, text = self.stack()
        self.assertEqual(code, 0, text)
        self.assertIn("STACKED: F.1", text, "G10.2m is a measuring step: the first later step with its needs met is written")
        self.assertIn("FILL: the next step in order, G10.2m, is a measuring step", text)
        self.assertEqual(self.on(), "native/F.1")
        self.assertEqual(self.rev("native/F.1"), self.rev(g1), "the fill step stands on the last step written")
        self.code("F.1", "f1")
        self.assertIn("STACKED: F.2", self.stack(0)[1], "the order inside a package is kept: F.2 after F.1")
        self.code("F.2", "f2")
        self.assertIn("STACKED: X.1", self.stack(0)[1], "the needs of X.1 (package F) are met by the steps written")
        self.code("X.1", "x1")
        code, text = self.stack()
        self.assertEqual(code, 1)
        self.assertIn(NOTHING_LEFT, text)
        self.assertEqual(self.stacked(), ["F.1", "F.2", "X.1"])
        for left in ("G10.3", "Y.1", "W.1", "G10.2m"):
            self.assertNotIn("native/" + left, self.branches(), "%s is never offered: its needs are not met or it is a measuring step" % left)

    def test_a_blocked_step_still_holds_what_needs_it(self):
        self.plan_rows((("G10.1", "G10", "", "open"), ("G10.2m", "G10", "", "open"), ("F.1", "F", "", "blocked"), ("F.2", "F", "", "open"),
                        ("X.1", "X", "F", "open"), ("Z.1", "Z", "", "open")), ("G10.2m",))
        self.code("G10.1", "g1")
        self.assertIn("STACKED: Z.1", self.stack(0)[1], "F.1 is blocked, F.2 follows it in its package, X.1 needs F")
        self.code("Z.1", "z1")
        self.assertIn("no later step has its needs met", self.stack(1)[1])
        self.assertEqual(self.stacked(), ["Z.1"])

    def test_the_fill_has_a_ceiling(self):
        self.code("G10.1", "g1")
        with mock.patch.object(plan, "FILL_MAX", 1):
            self.assertIn("STACKED: F.1", self.stack(0)[1])
            self.code("F.1", "f1")
            code, text = self.stack()
            self.assertEqual(code, 1)
            self.assertIn("is a measuring step and 1 fill steps are written already", text)
            self.assertNotIn("native/F.2", self.branches())
        self.assertIn("STACKED: F.2", self.stack(0)[1], "with the ceiling gone the next fill step is written")

    def test_next_names_the_measuring_step(self):
        self.code("G10.1", "g1")
        text = self.t.run(["next"], expect=0)[1]
        self.assertIn("after it: G10.2m", text)
        self.assertIn("G10.2m is a measuring step: a writer does not write it", text)


class CodeStepTest(FillBase):
    ROWS = (("C.1", "C", "", "open"), ("C.2", "C", "", "open"), ("D.1", "D", "", "open"))
    MEASURING = ()

    def test_a_code_step_next_gets_no_fill(self):
        self.code("C.1", "c1")
        code, text = self.stack()
        self.assertEqual((code, "STACKED: C.2" in text, "FILL" in text), (0, True, False))
        self.code("C.2", "c2")
        code, text = self.stack()
        self.assertEqual(code, 1)
        self.assertIn("STACK END: package C is written up to C.2. The next step, D.1, is of package D", text)
        self.assertNotIn("native/D.1", self.branches())
        self.assertEqual(self.stacked(), ["C.2"])


class PacingTest(FillBase):
    def test_a_pacing_step_lands_before_a_fill_step(self):
        self.code("G10.1", "g1")
        self.stack(0)
        self.code("F.1", "f1")
        self.stack(0)
        self.code("F.2", "f2")
        tip = self.rev("native/F.2")
        self.t.git("checkout", "-q", "-b", "native/G10.3", cwd=self.folder)       # G10.3, written after the fill steps
        self.code("G10.3", "g3")
        lv.push_stack(self.t.live, 1, "G10.3", "F.2", tip)
        listed = self.stack(0, "--list")[1]
        self.assertLess(listed.index("G10.3 on native/F.2"), listed.index("F.1 on native/G10.1"), "a pacing step is listed first")
        status = self.t.run(["status"], expect=0)[1]
        self.assertLess(status.index("- G10.3:"), status.index("- F.1:"))
        self.checkout("native/G10.1")
        self.assertIn("step F.1 waits: it stands on G10.1, which is not landed", self.t.run(["start", "F.1"], 1)[1])
        self.t.landed("G10.1", "G10.2m")                                          # G10.1 has landed, the measuring step is done
        text = self.t.run(["start", "F.1"], 1)[1]
        self.assertIn("step F.1 waits: G10.3, a step of package G10, is written in the stack and has the gate first", text)
        self.assertIn("from the stack", self.t.run(["start", "G10.3"], expect=0)[1])
        self.t.run(["land", "G10.3"], expect=0)
        self.assertIn("from the stack", self.t.run(["start", "F.1"], expect=0)[1], "the fill step goes when no pacing step waits")


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_fill_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    sys.exit(0 if result.wasSuccessful() else 1)
