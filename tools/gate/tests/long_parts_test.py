"""long_parts_test.py: how the long run's parts are cut and how they give way to landings (package B15, points 10 and 12), without the game.

python -X utf8 -I tools/gate/tests/long_parts_test.py

- pieces_of: a list that would take 30 minutes is cut into pieces of at most 7 minutes, the sizes alike; the fixed seconds of a piece (the
  tablet's install) count; one item is the least a piece holds; no item, no piece. The numbers of the long runs on record (102 stops) give
  two pieces for the PC's tours, the sanitizer's tour and the coverage tour, three for the validate lane, and for the tablet pieces of
  at most 14 stops, each within 7 minutes.
- split, piece_label, part_of.
- LandingWatch: a waiting row counts; a running row counts while the runner's process lives and not after it has ended; the ended rows
  and no file count for nothing.
- wait_clear: waits while a landing waits, goes on when the queue is empty, gives the reason after the limit.
- measure_held: measure.lock of a live holder, of a dead one, none.
Exit 0 when every case holds.
"""
import pathlib
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import long_parts as lp  # noqa: E402


def queue_text(*rows):
    return "time\tbranch\tagent\tstate\tcommit\trun folder\tnote\talso\n" + "".join(
        "2026-11-21T10:00:00\t%s\t1\t%s\t\t\t\t\n" % r for r in rows)


class Pieces(unittest.TestCase):
    def test_thirty_minutes_become_pieces_of_seven(self):
        sizes = lp.pieces_of(100, 18.0)             # 1800 s in all
        self.assertEqual(sum(sizes), 100)
        self.assertGreater(len(sizes), 1)
        self.assertTrue(all(s * 18.0 <= lp.PIECE_SECONDS for s in sizes), sizes)
        self.assertLessEqual(max(sizes) - min(sizes), 1)

    def test_fixed_seconds_count(self):
        sizes = lp.pieces_of(40, 10.0, fixed=130)    # 130 + n * 10 <= 420: 29 at most
        self.assertTrue(all(130 + s * 10.0 <= lp.PIECE_SECONDS for s in sizes), sizes)
        self.assertEqual(sum(sizes), 40)

    def test_one_item_is_the_least(self):
        self.assertEqual(lp.pieces_of(3, 500.0), [1, 1, 1])
        self.assertEqual(lp.pieces_of(0, 1.0), [])
        self.assertEqual(lp.pieces_of(5, 1.0), [5])

    def test_the_record_gives_pieces_within_seven_minutes(self):
        tours = lp.pieces_of(102, lp.TOURS_PER_STOP)
        self.assertEqual(len(tours), 2)
        self.assertTrue(all(s * lp.TOURS_PER_STOP <= lp.PIECE_SECONDS for s in tours))
        self.assertEqual(len(lp.pieces_of(102, lp.ASAN_PER_STOP)), 2)
        self.assertEqual(len(lp.pieces_of(102, lp.COVERAGE_PER_STOP)), 2)
        self.assertEqual(len(lp.pieces_of(102, lp.VALIDATE_PER_STOP)), 3)
        tablet = lp.pieces_of(102, lp.TABLET_PER_STOP, lp.TABLET_FIXED + lp.TABLET_SUITE)
        self.assertEqual(sum(tablet), 102)
        self.assertTrue(all(lp.TABLET_FIXED + lp.TABLET_SUITE + s * lp.TABLET_PER_STOP <= lp.PIECE_SECONDS for s in tablet), tablet)
        self.assertTrue(all(s <= 20 for s in tablet), "pieces of at most 20 stops: %s" % tablet)
        pc = lp.pieces_of(102, lp.TABLET_PC_PER_STOP, 0)
        self.assertTrue(all(s * lp.TABLET_PC_PER_STOP <= lp.PIECE_SECONDS for s in pc))

    def test_split_and_labels(self):
        self.assertEqual(lp.split(list("abcdefg"), [3, 2, 2]), [list("abc"), list("de"), list("fg")])
        self.assertEqual(lp.piece_label("tours", 1, 3), "tours 2/3")
        self.assertEqual(lp.piece_label("tours", 0, 1), "tours")
        self.assertEqual(lp.part_of("tours 2/3"), "tours")
        self.assertEqual(lp.part_of("coverage build"), "coverage build")
        self.assertEqual(lp.part_of("sanitizer tour 1/2"), "sanitizer tour")
        self.assertEqual(lp.seconds_of([{"part": "tours 1/2", "held": 300}, {"part": "tours 2/2", "held": 200}, {"part": "again", "held": 9}], "tours"),
                         (500, 300))


class Watch(unittest.TestCase):
    def watch(self, text, holder_pid, alive):
        tmp = tempfile.mkdtemp()
        queue = pathlib.Path(tmp) / "q.tsv"
        if text is not None:
            queue.write_text(text, encoding="utf-8")
        return lp.LandingWatch(queue, pathlib.Path(tmp) / "runner.lock", lambda pid: pid in alive, lambda lock: holder_pid)

    def test_no_file_and_ended_rows_are_not_busy(self):
        self.assertEqual(self.watch(None, None, ()).busy(), "")
        self.assertEqual(self.watch(queue_text(("a", "landed"), ("b", "failed")), None, ()).busy(), "")

    def test_a_waiting_row_is_busy(self):
        said = self.watch(queue_text(("native/R1.1", "waiting")), None, ()).busy()
        self.assertIn("1 waiting", said)
        self.assertIn("native/R1.1", said)

    def test_a_running_row_is_busy_while_its_runner_lives(self):
        self.assertIn("1 running", self.watch(queue_text(("a", "running")), 77, {77}).busy())
        self.assertEqual(self.watch(queue_text(("a", "running")), 77, ()).busy(), "", "a runner that ended leaves no hold")
        self.assertEqual(self.watch(queue_text(("a", "running")), None, ()).busy(), "", "no runner at all")


class Waiting(unittest.TestCase):
    class Fake:
        def __init__(self, answers):
            self.answers = list(answers)
            self.looks = 0

        def busy(self):
            self.looks += 1
            return self.answers.pop(0) if self.answers else ""

    def test_waits_while_busy_then_goes_on(self):
        watch, slept, said = self.Fake(["the landing queue has 1 waiting (a)", "the landing queue has 1 waiting (a)", ""]), [], []
        now = [0.0]
        why = lp.wait_clear(watch, 60, say=said.append, sleep=lambda s: (slept.append(s), now.__setitem__(0, now[0] + s)), clock=lambda: now[0])
        self.assertEqual(why, "")
        self.assertEqual(slept, [lp.POLL_SECONDS, lp.POLL_SECONDS])
        self.assertEqual(len(said), 1, "the wait is said once")

    def test_gives_the_reason_at_the_limit(self):
        watch = self.Fake(["busy"] * 100)
        now = [0.0]
        why = lp.wait_clear(watch, 2, say=lambda t: None, sleep=lambda s: now.__setitem__(0, now[0] + s), clock=lambda: now[0])
        self.assertEqual(why, "busy")

    def test_the_extra_reason_holds_too(self):
        watch = self.Fake([])
        why = lp.wait_clear(watch, 0, say=lambda t: None, sleep=lambda s: None, extra=lambda: "a speed measure holds measure.lock")
        self.assertIn("measure.lock", why)


class Measure(unittest.TestCase):
    def test_measure_lock(self):
        tmp = pathlib.Path(tempfile.mkdtemp())
        self.assertEqual(lp.measure_held(tmp, lambda p: True, lambda l: 1), "")
        (tmp / "measure.lock").mkdir()
        self.assertIn("measure.lock", lp.measure_held(tmp, lambda p: True, lambda l: 5))
        self.assertEqual(lp.measure_held(tmp, lambda p: False, lambda l: 5), "", "a measure whose process ended holds nothing")
        self.assertIn("measure.lock", lp.measure_held(tmp, lambda p: False, lambda l: None), "a lock taken by hand holds")


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=0).result
    print("long_parts_test: %s" % ("pass (%d tests)" % result.testsRun if result.wasSuccessful() else "FAILED"))
    sys.exit(0 if result.wasSuccessful() else 1)
