"""plan_section_test.py: the two tiers of a landing (mend M-006, docs/plan/04-GATE-AND-MAIN.md "Two tiers"). `plan.py land` asks the
gate for the section's full check (`--section`) at the last step of a package, or at the fourth step landed since the last check;
it keeps that count in the live folder; and `ahead` writes no step of the next package while a section's check is in work.

The commands run in the temporary git tree of plan_fixture with the gate replaced by plan_ahead_test's fake: no game, no build, no
network, nothing of D:/NebulaWork/plan.

    python -X utf8 -I tools/gate/tests/plan_section_test.py

Each test was seen to fail with the thing it guards broken (named in a phrase after the test).
  a_step_that_is_not_the_last_of_its_package_lands_as_a_step   --section given while another step of the package is open
  the_fourth_step_since_the_last_check_is_one                  the count not read: three steps since the check gave a step's landing
  the_last_step_of_its_package_is_one                          --section left out for the package's last open step
  the_count_and_the_barrier                                    the count not raised after a step's pass or not set back after a
                                                               section's; `ahead` wrote native/B.1 while A.2's section check played
"""
import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import plan_ahead_test as base  # noqa: E402
from plan_ahead_test import AheadBase, plan  # noqa: E402
from plan_fixture import ledger_row, ready_a1, setup_module, teardown_module, write_steps  # noqa: E402
from plan_rerun_test import failed_verdict, passed_verdict  # noqa: E402


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


class SectionBase(AheadBase):
    def since(self):
        path = pathlib.Path(self.t.live) / plan.SECTION_SINCE
        return path.read_text(encoding="utf-8").strip() if path.exists() else None

    def set_since(self, n):
        (pathlib.Path(self.t.live) / plan.SECTION_SINCE).write_text("%d\n" % n, encoding="utf-8")

    def landed_options(self):
        ready_a1(self.t)
        self.results = [failed_verdict()]
        self.land()
        return self.calls[-1]["options"]


class StepTest(SectionBase):
    def test_a_step_that_is_not_the_last_of_its_package_lands_as_a_step(self):
        self.assertNotIn("--section", self.landed_options())

    def test_the_fourth_step_since_the_last_check_is_one(self):
        self.set_since(3)               # three steps landed since the last check: this is the fourth
        self.assertIn("--section", self.landed_options())


class LastOfPackageTest(SectionBase):
    ROWS = (("A.1", "A", "", "open"), ("B.1", "B", "A", "open"))

    def test_the_last_step_of_its_package_is_one(self):
        self.assertIn("--section", self.landed_options())


class CountTest(SectionBase):
    def test_the_count_and_the_barrier(self):
        ready_a1(self.t)
        self.results = [passed_verdict("1111111111")]
        said = []

        def while_a1_plays():
            said.append(self.nested(["ahead"], 0)[1])
            self.t.write_ledger([ledger_row("P-001", "fixed", "tests/render/a_test 1-9 fails before"),
                                 ledger_row("P-002", note="advanced 11-20: half done"),
                                 ledger_row("P-003", "fixed", "tests/render/b_test 1-9 fails before"), ledger_row("P-004")],
                                tree=self.folder)
            self.m_commit()
            self.pass_moving_main()()

        self.hooks = [while_a1_plays]
        code, text = self.land()
        self.assertEqual(code, 0, text)
        self.assertNotIn("--section", self.calls[-1]["options"])
        self.assertEqual(self.since(), "1", "one step landed since the last check")
        self.assertEqual(self.t.run(["start", "A.2"], expect=0)[1].splitlines()[0].split(":")[0], "started A.2")

        def while_a2_plays():
            said.append(self.nested(["ahead"], 1)[1])
            self.pass_moving_main("native/A.2")()

        self.results = [passed_verdict("2222222222")]
        self.hooks = [while_a2_plays]
        code, text = self.t.run(["land", "A.2"])
        self.assertEqual(code, 0, text)
        self.assertIn("--section", self.calls[-1]["options"], "A.2 is the last step of package A")
        self.assertIn("the landing of A.2 is the full check of package A, and the next step, B.1, is of package B", said[-1])
        self.assertFalse(plan.branch_exists(self.folder, "refs/heads/native/B.1"), "no step of the next package was written")
        self.assertEqual(self.since(), "0", "the section's check sets the count back")


if __name__ == "__main__":
    del base
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_section_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    sys.exit(0 if result.wasSuccessful() else 1)
