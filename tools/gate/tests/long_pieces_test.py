"""long_pieces_test.py: the long run's parts as pieces that give way to landings (package B15, points 10, 12 and 14), with fakes.

python -X utf8 -I tools/gate/tests/long_pieces_test.py

Real lock folders and a real queue file in a scratch folder; the game's runner is replaced by one that writes each test's result.json.
- A part whose list would take 30 minutes is played as pieces, each under the gate lock for itself.
- A branch put into the landing queue while piece 2 plays has the lock before piece 3 (piece 3 waits, holds nothing while it waits).
- The part's one line (one check, all tests): right when all pieces pass; names the test that failed in piece 3; is `tool` for a piece
  that could not start (its tests left no result).
- Landings first (point 10): a part does not take the lock while a branch waits or runs; a landing that arrives during a part waits for
  that part only; a branch that arrives between the look and the lock is let go first.
- The tablet's pieces play BESIDE the PC's parts and take no gate lock: a tablet piece and a PC part overlap in time (neither can end
  before the other has started); a landing that holds the gate lock and needs no tablet is not held by a piece; a tablet piece does not
  start while a branch waits in the queue, nor while a speed measure holds measure.lock (unless the proof allows it).
- Point 14: two long runs cannot hold the long lock at once; a long run whose process ended leaves its long lock and its part's gate
  lock to be taken over; a landing never takes the long lock.
Exit 0 when every case holds.
"""
import json
import os
import pathlib
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402
import long_parts  # noqa: E402

QUEUE_HEAD = "time\tbranch\tagent\tstate\tcommit\trun folder\tnote\talso\n"


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp())
        t = self.tmp
        self.queue = t / "landing-queue.tsv"
        self.queue.write_text(QUEUE_HEAD, encoding="utf-8")
        self.args = gate.parser().parse_args([
            "--long", "--tablet", "T", "--work", str(t / "runs"), "--board", "", "--status", "", "--gate-lock", str(t / "gate.lock"),
            "--long-lock", str(t / "long.lock"), "--tablet-lock", str(t / "tablet.lock"), "--queue-file", str(self.queue),
            "--runner-lock", str(t / "runner.lock"), "--wait", "1"])
        self.g = gate.Gate(self.args)
        self.now = [0.0]
        self.patches = [mock.patch.object(gate, "PART_CLOCK", lambda: self.now[0]), mock.patch.object(gate, "PART_SLEEP", self.sleep),
                        mock.patch.object(gate, "PART_POLL", [30.0])]
        for p in self.patches:
            p.start()
        self.on_sleep = []
        self.events = []

    def tearDown(self):
        for p in self.patches:
            p.stop()

    def sleep(self, seconds):
        self.now[0] += seconds
        if self.on_sleep:
            self.on_sleep.pop(0)()

    def add_row(self, branch, state):
        self.queue.write_text(self.queue.read_text(encoding="utf-8") + "2026-11-21T10:00:00\t%s\t1\t%s\t\t\t\t\n" % (branch, state), encoding="utf-8")

    def clear_rows(self):
        self.queue.write_text(QUEUE_HEAD, encoding="utf-8")

    def lock_free(self):
        return not (self.tmp / "gate.lock").exists()


class Pieces(Fixture):
    def make_run(self, planted=None, dead_piece=None, stops=100, per_stop=18.0):
        """A PiecedRun over a suite of `stops` tests whose runner is a fake: it writes result.json for the piece's tests (a planted test
        fails; a piece in dead_piece writes nothing and exits 1). Returns (the run, the sizes' list is on the run)."""
        names = ["%03d-Stage-1" % (i + 1) for i in range(stops)]
        suite = self.tmp / "tour" / "suite.json"
        suite.parent.mkdir(exist_ok=True)
        suite.write_text(json.dumps({"harness": 1, "tests": names}), encoding="utf-8")
        run = gate.PiecedRun(self.g, suite, "d3d12", "tour", "tour/d3d12")
        played = []

        def start(source, work, bin_dir, names_, log, lock):
            index = len(played)
            played.append(list(names_))
            self.events.append("piece %d plays %d tests (lock %s)" % (index + 1, len(names_), "held" if not self.lock_free() else "NOT HELD"))
            pathlib.Path(log).write_text("$ runner --test %s\n" % " --test ".join(names_), encoding="utf-8")
            if dead_piece == index:
                return 1
            for n in names_:
                folder = pathlib.Path(work) / "runs" / "tour" / n / "d3d12"
                folder.mkdir(parents=True, exist_ok=True)
                bad = n == planted
                (folder / "result.json").write_text(json.dumps({"test": n, "verdict": "fail" if bad else "pass", "kind": "fail" if bad else "pass",
                                                                "reason": "the stage did not load" if bad else ""}), encoding="utf-8")
            return 1 if planted in names_ else 0
        self.start_patch = mock.patch.object(gate.HarnessSource, "start", start)
        self.start_patch.start()
        self.addCleanup(self.start_patch.stop)
        return run, played

    def play(self, run, per_stop, name="tours"):
        def hold(i):
            self.events.append("piece %d took the lock" % (i + 1))
        return gate.play_in_pieces(self.g, name, [run], per_stop)

    def test_a_thirty_minute_part_is_played_as_pieces_each_under_the_lock(self):
        run, played = self.make_run()
        sizes = self.play(run, 18.0)
        self.assertGreater(len(sizes), 3)
        self.assertEqual(len(played), len(sizes))
        self.assertEqual(sum(len(p) for p in played), 100)
        self.assertEqual([r["part"] for r in self.g.part_log], ["tours %d/%d" % (i + 1, len(sizes)) for i in range(len(sizes))])
        self.assertTrue(all("lock held" in e for e in self.events if e.startswith("piece")), self.events)
        self.assertTrue(self.lock_free(), "the lock is given back after the last piece")

    def test_a_branch_put_in_the_queue_during_piece_two_is_served_before_piece_three(self):
        run, played = self.make_run()
        real = gate.take_gate_lock

        def landing_arrives(*a, **k):
            return real(*a, **k)
        first = []

        def start_with_arrival(source, work, bin_dir, names_, log, lock):
            n = len(played)
            if n == 1:
                self.add_row("native/R9.9", "waiting")            # a branch is put in the queue while piece 2 plays

                def landing():
                    self.events.append("the landing takes and gives back the lock (free: %s)" % self.lock_free())
                    self.clear_rows()
                self.on_sleep.append(landing)
            return orig(source, work, bin_dir, names_, log, lock)
        orig = gate.HarnessSource.start
        with mock.patch.object(gate.HarnessSource, "start", start_with_arrival):
            self.play(run, 18.0)
        text = "\n".join(self.events)
        self.assertLess(text.index("piece 2 plays"), text.index("the landing takes and gives back"), text)
        self.assertLess(text.index("the landing takes and gives back"), text.index("piece 3 plays"), text)
        self.assertIn("free: True", text, "piece 3 waited without holding the lock")

    def test_one_line_for_all_pieces_passing(self):
        run, played = self.make_run()
        self.play(run, 18.0)
        self.g.check("tour/d3d12", gate.harness(self.g, run.source.suite, "d3d12", "tour", pieced=run))
        row = self.g.results[-1]
        self.assertEqual((row["check"], row["state"]), ("tour/d3d12", "pass"), row)
        self.assertEqual(len([r for r in self.g.results if r["check"] == "tour/d3d12"]), 1)
        self.assertGreaterEqual(row["seconds"], 0)

    def test_one_failing_test_in_piece_three_is_named(self):
        sizes = long_parts.pieces_of(100, 18.0)
        planted = "%03d-Stage-1" % (sizes[0] + sizes[1] + 2)                 # a test of the third piece
        run, played = self.make_run(planted=planted)
        self.play(run, 18.0)
        self.g.check("tour/d3d12", gate.harness(self.g, run.source.suite, "d3d12", "tour", pieced=run))
        row = self.g.results[-1]
        self.assertEqual(row["state"], "fail", row)
        self.assertTrue(any(r["test"] == planted and r["kind"] == "fail" for r in row["records"]), row["records"])
        self.assertIn(planted, [n for p in played[2:3] for n in p])

    def test_a_piece_that_could_not_start_is_kind_tool(self):
        run, played = self.make_run(dead_piece=1)
        self.play(run, 18.0)
        self.g.check("tour/d3d12", gate.harness(self.g, run.source.suite, "d3d12", "tour", pieced=run))
        row = self.g.results[-1]
        self.assertEqual(row["state"], "tool", row)
        self.assertIn("no result.json", row["detail"] + " ".join(r["message"] for r in row["records"]))


class LandingsFirst(Fixture):
    def test_a_part_waits_while_a_branch_waits_and_goes_when_it_is_gone(self):
        self.add_row("native/S1.1", "waiting")
        self.on_sleep.append(lambda: self.events.append("polled: lock free %s" % self.lock_free()))
        self.on_sleep.append(self.clear_rows)
        ran = gate.locked_part(self.g, "tours", lambda: self.events.append("ran") or "result")
        self.assertEqual(ran, "result")
        self.assertEqual(self.events, ["polled: lock free True", "ran"])
        self.assertEqual(self.g.part_log[0]["part"], "tours")
        self.assertGreaterEqual(self.g.part_log[0]["waited"], 30.0)

    def test_a_running_branch_holds_a_part_back_too(self):
        self.add_row("native/S1.1", "running")
        (self.tmp / "runner.lock").mkdir()
        (self.tmp / "runner.lock" / "holder.txt").write_text("landing runner pid %d" % os.getpid(), encoding="utf-8")
        self.on_sleep.append(self.clear_rows)
        self.assertEqual(gate.locked_part(self.g, "tours", lambda: "ok"), "ok")
        self.assertGreaterEqual(self.now[0], 30.0, "it waited one look")

    def test_a_branch_that_arrives_between_the_look_and_the_lock_goes_first(self):
        real = gate.take_gate_lock
        calls = []

        def take(*a, **k):
            got = real(*a, **k)
            if not calls:
                calls.append(1)
                self.add_row("native/S2.2", "waiting")            # it came after the look, before the lock was had
                self.on_sleep.append(self.clear_rows)
            return got
        with mock.patch.object(gate, "take_gate_lock", take):
            self.assertEqual(gate.locked_part(self.g, "tours", lambda: self.events.append("ran") or "ok"), "ok")
        self.assertEqual(self.events, ["ran"])
        self.assertEqual(len(calls), 1)
        self.assertGreaterEqual(self.now[0], 30.0, "the lock was given back and the look made again")

    def test_a_part_that_never_gets_the_way_clear_is_an_env_row(self):
        self.add_row("native/S1.1", "waiting")
        self.assertIsNone(gate.locked_part(self.g, "tours", lambda: self.events.append("ran")))
        self.assertEqual(self.events, [])
        row = self.g.results[-1]
        self.assertEqual((row["check"], row["state"]), ("part/tours", "env"))
        self.assertIn("landing queue", row["detail"])

    def test_a_landing_that_arrives_during_a_part_waits_for_that_part_only(self):
        landing_got = []

        def part():
            holder = "main gate of X@abc pid %d" % os.getpid()
            outcome = []

            def landing():
                outcome.append(gate.take_gate_lock(str(self.tmp / "gate.lock"), holder, 0.0, "", "t"))
            thread = threading.Thread(target=landing)
            thread.start()
            thread.join()
            landing_got.append(outcome[0])
        gate.locked_part(self.g, "tours", part)
        self.assertIn("held", landing_got[0], "during the part the landing waits (its wait here is 0 minutes)")
        self.assertIsNone(gate.take_gate_lock(str(self.tmp / "gate.lock"), "main gate of X@abc pid %d" % os.getpid(), 0.0, "", "t"),
                          "after the part the landing has the lock at once")


class Beside(Fixture):
    def test_a_tablet_piece_and_a_pc_part_overlap_in_time(self):
        started = {"tablet": threading.Event(), "pc": threading.Event()}
        order = []

        def tablet_piece():
            started["tablet"].set()
            order.append("tablet in")
            ok = started["pc"].wait(5)               # it can only end once the PC part has started
            order.append("tablet out")
            return ok

        def pc_part():
            started["pc"].set()
            order.append("pc in")
            ok = started["tablet"].wait(5)
            order.append("pc out")
            return ok
        result = {}
        thread = threading.Thread(target=lambda: result.update(tablet=gate.beside_part(self.g, "tablet 1/2", tablet_piece)))
        thread.start()
        pc = gate.locked_part(self.g, "tours", pc_part)
        thread.join(10)
        self.assertTrue(pc and result["tablet"] == (True, True), (pc, result, order))
        self.assertEqual(sorted(order[:2]), ["pc in", "tablet in"])

    def test_a_landing_without_tablet_checks_is_not_held_by_a_tablet_piece(self):
        def tablet_piece():
            (self.tmp / "tablet.lock").mkdir()                # the piece holds the tablet's lock
            try:
                got = gate.take_gate_lock(str(self.tmp / "gate.lock"), "land gate of X@abc pid %d" % os.getpid(), 0.0, "", "t")
                self.events.append("landing got the gate lock: %s" % (got is None))
                gate.free_gate_lock(str(self.tmp / "gate.lock"), "land gate of X@abc pid %d" % os.getpid(), "", "t", "done")
            finally:
                (self.tmp / "tablet.lock").rmdir()
        gate.beside_part(self.g, "tablet 1/2", tablet_piece)
        self.assertEqual(self.events, ["landing got the gate lock: True"])
        self.assertEqual([p["part"] for p in self.g.part_log if p.get("beside")], ["tablet 1/2"])

    def test_a_tablet_piece_waits_for_a_branch_in_the_queue(self):
        self.add_row("native/S1.1", "waiting")
        self.on_sleep.append(self.clear_rows)
        ran, value = gate.beside_part(self.g, "tablet 1/2", lambda: "played")
        self.assertEqual((ran, value), (True, "played"))
        self.assertGreaterEqual(self.now[0], 30.0)

    def test_a_tablet_piece_waits_while_a_speed_measure_is_measured_unless_the_proof_allows(self):
        (self.tmp / "measure.lock").mkdir()
        (self.tmp / "measure.lock" / "holder.txt").write_text("measure.py abc pid %d" % os.getpid(), encoding="utf-8")
        ran, why = gate.beside_part(self.g, "tablet 1/2", lambda: "played")
        self.assertFalse(ran)
        self.assertIn("measure.lock", why)
        with mock.patch.object(long_parts, "TABLET_BESIDE_MEASURE", True):
            self.assertEqual(gate.beside_part(self.g, "tablet 1/2", lambda: "played"), (True, "played"))


class Track(Fixture):
    """The whole tablet track (TabletTrack) with a fake tablet and a fake runner, beside a PC part."""

    def test_the_tablet_track_plays_beside_a_pc_part_and_the_rows_are_one_each(self):
        g = self.g
        tablet_running, pc_running = threading.Event(), threading.Event()
        overlap = {"tablet": False, "pc": False}
        starts = []

        def start(source, work, bin_dir, names_, log, lock):
            names_ = names_ or source.listed
            starts.append((source.tag, len(names_), threading.current_thread().name))
            pathlib.Path(log).write_text("$ runner\n", encoding="utf-8")
            if source.device and source.tag == "tour-tablet-vulkan" and len(starts) < 4:
                tablet_running.set()
                overlap["tablet"] = overlap["tablet"] or pc_running.wait(10)     # a piece of the tablet that ends only while a PC part plays
            for n in names_:
                folder = pathlib.Path(work) / "runs" / source.suite_dir / n / source.renderer
                folder.mkdir(parents=True, exist_ok=True)
                (folder / "result.json").write_text(json.dumps({"test": n, "verdict": "pass", "kind": "pass"}), encoding="utf-8")
            return 0

        class Device:
            adb_path = "adb"

            def __init__(self, serial=""):
                pass

            def answers(self):
                return True
        fake_android = type("M", (), {"Device": Device})
        patches = [mock.patch.object(gate.HarnessSource, "start", start), mock.patch.object(gate, "android_module", lambda: fake_android),
                   mock.patch.object(gate.machine, "ensure_adb_server", lambda *a: None),
                   mock.patch.object(gate, "build_android", lambda g: (lambda log: (True, "built"))),
                   mock.patch.object(gate, "device_step", lambda g, step: (lambda log: (True, step + " ok"))),
                   mock.patch.object(gate, "device_screen", lambda work: (1920, 1200)),
                   mock.patch.object(gate, "replays", lambda *a, **k: (lambda log: (True, "102 of 102 the same")))]
        for p in patches:
            p.start()
            self.addCleanup(p.stop)
        g.android_bin = str(self.tmp / "android")
        track = gate.TabletTrack(g)
        track.start()

        def pc_part():
            pc_running.set()
            overlap["pc"] = tablet_running.wait(10)
        for _ in range(600):                 # the track's Android build is a part too: it takes the gate lock first, then its pieces play beside
            if any(r["check"] == "build/android" for r in g.rows()):
                break
            time.sleep(0.05)
        gate.locked_part(g, "tours", pc_part)
        track.pc_pieces(block=False)
        track.pc_pieces(block=True)
        track.join()
        self.assertTrue(overlap["tablet"] and overlap["pc"], "a tablet piece and a PC part played at the same time: %s" % overlap)
        rows = {r["check"]: r for r in g.results}
        self.assertEqual([r["check"] for r in g.results].count("tour/tablet"), 1)
        for name in ("build/android", "tablet/prepare", "android/tablet", "tour/tablet", "tablet/remove", "tour/tablet-pc", "replays/tablet"):
            self.assertEqual(rows[name]["state"], "pass", (name, rows.get(name)))
        tablet_pieces = [p for p in g.part_log if p["part"].startswith("tablet ") and p.get("beside")]
        pc_pieces = [p for p in g.part_log if p["part"].startswith("tablet-pc")]
        self.assertGreater(len(tablet_pieces), 3, "the tablet's 102 stops are pieces")
        self.assertEqual(len(pc_pieces), len(tablet_pieces), "each tablet piece has its PC piece")
        self.assertTrue(all(not p.get("beside") for p in pc_pieces), "the PC's piece is a PC part under the gate lock")
        self.assertEqual(sum(n for tag, n, _ in starts if tag == "tour-tablet-vulkan"), 102)
        self.assertEqual(sum(n for tag, n, _ in starts if tag == "tour-tablet-pc-d3d12"), 102)
        self.assertTrue(self.lock_free() and not (self.tmp / "tablet.lock").exists(), "both locks are given back")

    def test_a_tablet_that_does_not_answer_owes_its_part_and_plays_no_piece(self):
        g = self.g

        class Away:
            adb_path = "adb"

            def __init__(self, serial=""):
                pass

            def answers(self):
                return False
        with mock.patch.object(gate, "android_module", lambda: type("M", (), {"Device": Away})), (
                mock.patch.object(gate.machine, "ensure_adb_server", lambda *a: None)), (
                mock.patch.object(gate, "build_android", lambda g: (lambda log: (True, "built")))):
            track = gate.TabletTrack(g)
            track.start()
            track.pc_pieces(block=True)
            track.join()
        states = {r["check"]: r["state"] for r in g.results}
        self.assertEqual({states[n] for n in gate.TABLET_CHECKS}, {"owed"}, states)


class Locks(Fixture):
    def test_two_long_runs_cannot_hold_the_long_lock_together(self):
        me = "long gate of Nebula-long@abc %s" % gate.lockholder.named()
        self.assertIsNone(gate.take_gate_lock(str(self.tmp / "long.lock"), me, 0.0, "", "t"))
        second = gate.take_gate_lock(str(self.tmp / "long.lock"), "long gate of Nebula-long@def %s" % gate.lockholder.named(), 0.0, "", "t")
        self.assertIn("held", second or "")
        self.assertFalse(gate.long_lock_free(str(self.tmp / "long.lock"), alive=lambda pid: True), "no new long run is started under it")

    def test_a_long_run_that_died_leaves_nothing_a_landing_waits_for(self):
        dead = 2 ** 22 + 12345
        (self.tmp / "long.lock").mkdir()
        (self.tmp / "long.lock" / "holder.txt").write_text("long gate of Nebula-long@abc pid %d" % dead, encoding="utf-8")
        self.assertTrue(gate.long_lock_free(str(self.tmp / "long.lock")), "the long lock of an ended run is taken over")
        # its part held the gate lock when it died: a landing takes the lock over at once
        (self.tmp / "gate.lock").mkdir()
        (self.tmp / "gate.lock" / "holder.txt").write_text("long run part tours 1/2 of Nebula-long pid %d" % dead, encoding="utf-8")
        got = gate.take_gate_lock(str(self.tmp / "gate.lock"), "land gate of X@abc pid %d" % os.getpid(), 0.0, "", "t")
        self.assertIsNone(got)
        # and a landing never takes the long lock
        self.assertNotIn(str(self.tmp / "long.lock"), gate.locks_for("land", [], str(self.tmp / "gate.lock"), str(self.tmp / "long.lock")))
        self.assertNotIn(str(self.tmp / "long.lock"), gate.locks_for("main", [], str(self.tmp / "gate.lock"), str(self.tmp / "long.lock")))
        self.assertEqual(gate.locks_for("long", [], str(self.tmp / "gate.lock"), str(self.tmp / "long.lock")), [str(self.tmp / "long.lock")])


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=0).result
    print("long_pieces_test: %s" % ("pass (%d tests)" % result.testsRun if result.wasSuccessful() else "FAILED"))
    sys.exit(0 if result.wasSuccessful() else 1)
