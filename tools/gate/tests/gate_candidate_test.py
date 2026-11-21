"""gate_candidate_test.py: a landing's candidate branch on origin (gate/<run>) is removed when the run ends, whatever the end (brief T10, G1).

    python -X utf8 -I tools/gate/tests/gate_candidate_test.py [--work FOLDER]

The gate's main() runs with the fakes of gate_github_test (what GitHub says, the candidate's push and removal, the move of main); no
game, no build, no network.

  a_passed_run_leaves_none          every `must` job green: pushed, main moves, removed (the case before this change too)
  a_failed_run_leaves_none          a check fails (exit 1): main does not move and the candidate is removed
  a_stopped_run_leaves_none         a red job of GitHub stops the checks: removed
  a_not_judged_run_leaves_none      a check ends tool (exit 2): removed
  a_refused_run_leaves_none         the gate lock is held after the candidate was pushed: refused, removed
  an_exception_leaves_none          the checks raise: the exception goes on and the candidate is removed
  main_not_moved_leaves_none        the checks pass and main does not move (the move said why): removed once
  a_pending_one_stays               main moved with GitHub's jobs still running: the candidate stays for the settle (github-pending.tsv)
  no_github_part_no_candidate       a plan without the group `github`: none pushed, none removed

Each case was seen to fail with the removal taken out of main() (the `finally`).
"""
import pathlib
import sys
import tempfile
import threading
import time
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import gate  # noqa: E402
import gate_github_test as gh  # noqa: E402


class Candidate(unittest.TestCase):
    def once(self, world):
        self.assertEqual(len(world.pushed), 1, "one candidate pushed")
        return world.pushed[0][1]

    def test_a_passed_run_leaves_none(self):
        world = gh.World(gh.runs_of(gh.green()))
        code, lines, _ = gh.drive(self, world)
        self.assertEqual((code, world.moved), (0, [gh.SHA]), "\n".join(lines))
        self.assertEqual(world.deleted, [self.once(world)])

    def test_a_failed_run_leaves_none(self):
        world = gh.World(gh.runs_of(gh.green()))
        code, lines, _ = gh.drive(self, world, checks=lambda g, w: g.record("engine/d3d12", "fail", 0.1, "a test failed"))
        self.assertEqual((code, world.moved, lines[-1]), (1, [], "VERDICT fail"), "\n".join(lines))
        self.assertEqual(world.deleted, [self.once(world)])

    def test_a_stopped_run_leaves_none(self):
        def checks(g, world):
            world.runs = gh.runs_of(gh.green("Linux unit tests") + [gh.job("Linux unit tests", conclusion="failure")])
            deadline = time.monotonic() + 20
            while not gate.STOPPED.is_set() and time.monotonic() < deadline:
                time.sleep(0.01)
            g.record("harness/self", "fail" if gate.STOPPED.is_set() else "tool", 0.0, "stopped: " + gate.STOPPED_WHY[0])
        world = gh.World(gh.runs_of(gh.green("Linux unit tests") + [gh.job("Linux unit tests", "in_progress")]))
        code, lines, _ = gh.drive(self, world, checks=checks)
        self.assertEqual((code, world.moved), (1, []), "\n".join(lines))
        self.assertEqual(world.deleted, [self.once(world)])

    def test_a_not_judged_run_leaves_none(self):
        world = gh.World(gh.runs_of(gh.green()))
        code, lines, _ = gh.drive(self, world, checks=lambda g, w: g.record("start/d3d12", "tool", 0.1, "no result file"))
        self.assertEqual((code, world.moved, lines[-1]), (2, [], "VERDICT not-judged"), "\n".join(lines))
        self.assertEqual(world.deleted, [self.once(world)])

    def test_a_refused_run_leaves_none(self):
        held = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(held, ignore_errors=True))
        lock = held / "gate.lock"
        lock.mkdir()
        (lock / "holder.txt").write_text("another gate by hand 2026-11-21 13:00:00\n", encoding="utf-8")
        world = gh.World(gh.runs_of(gh.green()))
        code, lines, _ = gh.drive(self, world, extra=["--gate-lock", str(lock)])
        self.assertEqual((code, world.moved, lines[-1]), (2, [], "VERDICT not-judged"), "\n".join(lines))
        self.assertTrue(any("is held" in l for l in lines), "\n".join(lines))
        self.assertEqual(world.deleted, [self.once(world)], "pushed before the lock was asked for, removed when it was refused")

    def test_an_exception_leaves_none(self):
        def checks(g, w):
            raise RuntimeError("the checks broke")
        world = gh.World(gh.runs_of(gh.green()))
        with self.assertRaises(RuntimeError):
            gh.drive(self, world, checks=checks)
        self.assertEqual(world.deleted, [self.once(world)])

    def test_main_not_moved_leaves_none_once(self):
        world = gh.World(gh.runs_of(gh.green()))
        code, lines, _ = gh.drive(self, world, move_why="the push of main failed: ! [rejected] main -> main")
        self.assertEqual((code, lines[-1]), (2, "VERDICT not-judged"), "\n".join(lines))
        self.assertEqual(world.deleted, [self.once(world)], "removed once")

    def test_a_pending_one_stays(self):
        world = gh.World(gh.runs_of(gh.green("Mac") + [gh.job("Mac", "in_progress")]))
        code, lines, _ = gh.drive(self, world, scope_text="repo,github")
        self.assertEqual((code, world.moved), (0, [gh.SHA]), "\n".join(lines))
        self.once(world)
        self.assertEqual(world.deleted, [], "it stays until the settle has read its jobs")

    def test_no_github_part_no_candidate(self):
        world = gh.World(None)
        code, lines, _ = gh.drive(self, world, "repo", allow_github=False)
        self.assertEqual((code, world.pushed, world.deleted), (0, [], []), "\n".join(lines))


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("gate_candidate_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
