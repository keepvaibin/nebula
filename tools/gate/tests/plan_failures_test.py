"""plan_failures_test.py: tools/plan/failures.py, the store behind "same failure three times, stop" (package T13).

The third failure in a row with one signature stops the retries (exit code 3, the sentence of the plan); another
signature or a pass starts the count again; a TOOL or ENV result never counts and says what to do instead. Runs in a
temporary folder: no game, no build, no network, no real store.

    python -X utf8 -I tools/gate/tests/plan_failures_test.py [--work <folder>]
"""
import datetime
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "plan"))
import failures  # noqa: E402

FAIL = [("tour/d3d12", "FAIL", "replay 025-EggStarGalaxy-4 step 0: 0873f048 against 521573f1")]
OTHER = [("unit", "FAIL", "galaxy_texture_pack_tests: 1 of 40 failed")]


class StoreTest(unittest.TestCase):
    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="plan-failures-"))
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.store = failures.Store(self.dir / "sub" / "failures.tsv")

    def test_third_same_failure_in_a_row_stops_with_code_three(self):
        first = self.store.record("native/A.1", FAIL)
        second = self.store.record("native/A.1", FAIL)
        self.assertEqual((first.count, first.stop, first.exit_code), (1, False, 1))
        self.assertEqual((second.count, second.stop, second.exit_code), (2, False, 1))
        self.assertEqual(first.message + second.message, "")
        third = self.store.record("native/A.1", FAIL)
        self.assertEqual((third.count, third.stop, third.exit_code), (3, True, 3))
        self.assertEqual(third.message, "Same failure three times. Stop retrying. Run plan.py blocked, then plan.py next.")

    def test_the_stop_holds_for_every_try_after_the_third(self):
        for _ in range(3):
            self.store.record("native/A.1", FAIL)
        fourth = self.store.record("native/A.1", FAIL)
        self.assertEqual((fourth.count, fourth.exit_code), (4, 3))

    def test_another_signature_starts_the_count_again(self):
        self.store.record("native/A.1", FAIL)
        self.store.record("native/A.1", FAIL)
        other = self.store.record("native/A.1", OTHER)
        self.assertEqual((other.count, other.stop), (1, False))
        back = self.store.record("native/A.1", FAIL)
        self.assertEqual(back.count, 1, "the earlier run of two does not come back")

    def test_a_pass_clears_the_count(self):
        self.store.record("native/A.1", FAIL)
        self.store.record("native/A.1", FAIL)
        self.store.clear("native/A.1")
        self.assertEqual(self.store.count("native/A.1"), 0)
        self.assertEqual(self.store.record("native/A.1", FAIL).count, 1)
        self.store.clear("native/never-failed")        # clearing a branch with no row is not an error

    def test_branches_count_apart(self):
        self.store.record("native/A.1", FAIL)
        self.store.record("native/A.1", FAIL)
        self.assertEqual(self.store.record("native/B.1", FAIL).count, 1)
        self.assertEqual(self.store.record("native/A.1", FAIL).count, 3)

    def test_the_count_survives_a_new_process(self):
        self.store.record("native/A.1", FAIL)
        again = failures.Store(self.store.path)
        self.assertEqual(again.record("native/A.1", FAIL).count, 2)
        self.assertTrue(self.store.path.read_text(encoding="utf-8").startswith("branch\tsignature\tcount\tlast\n"))

    def test_tool_result_never_counts_and_says_what_to_do(self):
        self.store.record("native/A.1", FAIL)
        self.store.record("native/A.1", FAIL)
        tool = self.store.record("native/A.1", [("harness/self", "TOOL", "result.json is missing")])
        self.assertFalse(tool.counted)
        self.assertFalse(tool.stop)
        self.assertEqual(tool.exit_code, 4)
        self.assertEqual(tool.message, "this is a tool fault, not the change's: it does not count as a failure; give the landing once "
                                       "more. Not judged a second time: stop (an agent of the plan: plan.py blocked <ID> \"tool: <the "
                                       "fault's first line>\", then plan.py next). Do not debug the tool.")
        self.assertEqual(self.store.count("native/A.1"), 2, "a tool result leaves the count of real failures alone")
        self.assertEqual(self.store.record("native/A.1", FAIL).count, 3, "and does not break the row of the same failure")

    def test_env_result_never_counts(self):
        for _ in range(5):
            out = self.store.record("native/A.1", [("tablet/short", "ENV", "the tablet does not answer")])
            self.assertEqual((out.counted, out.stop, out.exit_code), (False, False, 4))
            self.assertIn("machine", out.message)
        self.assertEqual(self.store.count("native/A.1"), 0)
        self.assertFalse(self.store.path.exists(), "nothing was stored")

    def test_a_real_failure_next_to_a_tool_result_counts(self):
        out = self.store.record("native/A.1", [("harness/self", "TOOL", "boom"), *FAIL])
        self.assertEqual((out.counted, out.count), (True, 1))

    def test_other_kinds_are_not_failures(self):
        out = self.store.record("native/A.1", [("tour/d3d11", "FLAKE", "passed alone"), ("pictures", "NEW", "no approved copy")])
        self.assertEqual((out.counted, out.exit_code, out.message), (False, 1, ""))
        self.assertEqual(self.store.count("native/A.1"), 0)

    def test_the_signature_is_the_first_failure_by_check_name_whatever_the_order(self):
        both = [("unit", "FAIL", "u"), ("tour/d3d12", "FAIL", "t")]
        self.assertEqual(failures.first_counted(both), failures.first_counted(list(reversed(both))))
        self.assertTrue(failures.first_counted(both).startswith("tour/d3d12"))

    def test_the_signature_leaves_out_what_changes_from_run_to_run(self):
        a = failures.signature("build", "exit 1, 1 FAILED: C:/Users/x/build-20261120-101500/a.obj  12.3 s at 4a67ed3e6")
        b = failures.signature("build", "exit 1, 1 FAILED: D:/NebulaWork/build-20261121-090000/a.obj  98.7 s at 790626cb5")
        self.assertEqual(a, b)
        self.assertNotEqual(failures.signature("build", "exit 1, 2 FAILED: link"), failures.signature("build", "exit 1, 2 FAILED: compile"))
        self.assertNotEqual(failures.signature("unit", "x"), failures.signature("build", "x"))
        self.assertEqual(failures.signature(" build ", "  a   b  "), "build | a b")

    def test_a_signature_is_one_line_with_no_tab(self):
        s = failures.signature("c", "a\tb\nc")
        self.assertNotIn("\t", s)
        self.assertNotIn("\n", s)

    def test_rows_with_a_bad_count_are_skipped(self):
        self.store.path.parent.mkdir(parents=True)
        self.store.path.write_text("branch\tsignature\tcount\tlast\nnative/A.1\tx\tmany\t2026-11-20\nnative/B.1\ty\t2\t2026-11-20\n",
                                   encoding="utf-8")
        self.assertEqual(self.store.count("native/A.1"), 0)
        self.assertEqual(self.store.count("native/B.1"), 2)

    def test_the_date_is_written(self):
        self.store.record("native/A.1", FAIL, today=datetime.date(2026, 11, 20))
        self.assertIn("\t2026-11-20", self.store.path.read_text(encoding="utf-8"))

    def test_no_temporary_file_is_left_beside_the_store(self):
        self.store.record("native/A.1", FAIL)
        self.assertEqual([p.name for p in self.store.path.parent.iterdir()], ["failures.tsv"])


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_failures_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
