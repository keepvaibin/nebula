"""machine_ready_test.py: what a run asks of the machine before it starts the game (tools/gate/machine.py and gate.py
machine_ready, package T5), on fakes: no game, no tablet, no graphics card is touched, and nothing is ended or thinned but
what a test made.

    python -X utf8 -I tools/gate/tests/machine_ready_test.py

  leftover_is_ended_and_named           a game process under D:/NebulaWork/ whose parent is gone and that is older than ten minutes
                                        is ended and named in the summary; a young one, one whose parent is there, one that is not
                                        the game and one outside D:/NebulaWork/ are left (a parent's number used again is gone)
  leftover_of_a_locked_run_is_not_ended a process a held lock may own (its holder alive and older, or a lock taken by hand) is not
                                        ended; a stale lock, this run's own lock and a holder that began later do not protect it
  too_little_disk_thins_then_passes     under the level's floor the old run folders are thinned, then the disk is looked at again
  too_little_disk_after_thinning_is_env still too little is env, with one plain line; the floors are 30, 80 and 150 GB
  thinning_keeps_the_right_folders      the three newest passing and the three newest failing runs of each level stay whole, the
                                        others keep their top-level files; a run still going, one waiting for GitHub and what is
                                        not a run are left alone
  tablet_not_answering_its_part_is_owed_the_run_is_judged   not listed, not authorised or offline is no stop and no row: a note
                                        says its part is owed (tablet_part) and the run goes on; an answering tablet passes
  wrong_data_on_tablet_is_pushed_again  the data of another store version is pushed again; a failed push, a wrong --pak is env; no
                                        test app on it is left to the run
  copies_formula                        the card's limit wins; the memory's wins; the lanes' win; exactly one fits; none fits
  no_copy_fits_then_one_does            the wait looks every minute and the run starts when one fits
  no_copy_fits_for_four_hours_is_env    after four hours of a fake clock the run is env with the card's line
  card_shared_in_summary                other programs holding over half the card is said ("card shared"); a failure in such a run
                                        is a failure, a pass a pass
  machine_json_learns                   the largest of the last 20 runs for each renderer; 3 GB until known
  sampler_reads_the_games_use           the thread keeps the largest use of the games of the run per renderer, on the game's card
  sampling_error_does_not_fail_a_run    an error in the reading is one warning line; a failed update of machine.json is one line
  documents_only_run_does_not_call_machine_ready   a run whose checks start no game never calls it; one that does, calls it once
  disk_under_its_floor_is_env_not_fail  a whole run started with D: under its floor ends not judged (exit 2), as env, with the reason
  lanes_ask_what_the_lanes_ask          lanes_asked equals what the lanes of every level ask in a dry run; every check is a game
                                        check or not
  slots_limit_the_copies                no more game copies run at once than the places
  real_tablet_reading                   the tablet's state and test data read as adb gives them (a stand-in for adb)
Exit 0 when every case holds.
"""
import contextlib
import io
import json
import os
import pathlib
import shutil
import sys
import tempfile
import threading
import types
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402
import kinds  # noqa: E402
import machine  # noqa: E402

GB = machine.GB
NOW = 1_800_000_000.0
ORPHAN = "D:/NebulaWork/build/gate-runs/20261117-000000-abc123def-long/runs-tour-d3d12/001/bin/Nebula.exe"


def proc(pid, ppid, name="Nebula.exe", path=ORPHAN, age=3600.0):
    return machine.Proc(pid, ppid, name, path, NOW - age)


class FakeTablet:
    def __init__(self, state="device", installed=True, version=38, push_ok=True, kept=None):
        self.the_state, self.installed, self.version, self.push_ok, self.kept_value = state, installed, version, push_ok, kept
        self.pushed = []

    def kept(self):
        return self.kept_value

    def state(self):
        return self.the_state

    def data(self):
        return self.installed, self.version

    def push(self, pak):
        self.pushed.append(pak)
        return self.push_ok, "pushed" if self.push_ok else "adb said the pak did not reach the device"


class FakeSampler:
    def __init__(self, peaks=None):
        self.peaks, self.started = peaks or {}, False

    def start(self):
        self.started = True
        return self

    def stop(self):
        return self.peaks


def card(total=32, used=2):
    return machine.Card("Test card", int(total * GB), int(used * GB), (0, 1), "fake")


class FakeProbes(machine.Probes):
    """Every reading of the machine, with answers the test gives; time moves only when the gate sleeps."""

    def __init__(self, procs=(), disk=(500 * GB,), cards=None, memory=(64 * GB,), tablet=None, store=38, pak=("D:/pak/game.pak", "")):
        self.now = NOW
        self.slept = []
        self.procs = list(procs)
        self.disk, self.cards, self.memory = list(disk), list(cards or [card()]), list(memory)
        self.the_tablet = tablet
        self.store, self.pak = store, pak
        self.calls, self.ended = [], []
        self.the_sampler = FakeSampler()

    def clock(self):
        return self.now

    def sleep(self, seconds):
        self.slept.append(seconds)
        self.now += seconds

    @staticmethod
    def pick(values):
        return values.pop(0) if len(values) > 1 else values[0]

    def processes(self):
        self.calls.append("processes")
        return list(self.procs)

    def end(self, pid):
        self.calls.append("end")
        self.ended.append(pid)
        gone = {pid}
        grew = True
        while grew:
            grew = False
            for p in self.procs:
                if p.ppid in gone and p.pid not in gone:
                    gone.add(p.pid)
                    grew = True
        self.procs = [p for p in self.procs if p.pid not in gone]
        return True

    def free_disk(self, path="D:/"):
        self.calls.append("disk")
        return self.pick(self.disk)

    def adb_server(self, g):
        self.calls.append("adb")

    def tablet(self, g):
        self.calls.append("tablet")
        return self.the_tablet

    def tree_store_version(self):
        return self.store

    def data_pak(self, g, want):
        return self.pak

    def card(self):
        self.calls.append("card")
        value = self.pick(self.cards)
        if isinstance(value, Exception):
            raise value
        return value

    def memory_free(self):
        self.calls.append("memory")
        return self.pick(self.memory)

    def sampler(self, work, luids, say=print):
        return self.the_sampler


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.t = pathlib.Path(self.tmp.name)
        (self.t / "runs").mkdir()
        (self.t / "plan").mkdir()
        self.learned = machine.Learned(self.t / "machine.json")
        self.gates = []

    def tearDown(self):
        for g in self.gates:
            shutil.rmtree(g.work, ignore_errors=True)
        self.tmp.cleanup()

    def make_gate(self, level="main", tablet="T", *extra):
        flag = "--main" if level == "land" else "--" + level
        args = gate.parser().parse_args([flag, "--work", str(self.t / "runs"), "--gate-lock", str(self.t / "plan" / "gate.lock"),
                                         "--long-lock", str(self.t / "plan" / "long.lock"), "--board", "", "--status", "",
                                         "--build-root", str(self.t / "build")] + (["--tablet", tablet] if tablet else []) + list(extra))
        if level == "land":
            args.mode = gate.LAND_LEVEL
        g = gate.Gate(args, dry=True)
        self.gates.append(g)
        return g

    def ready(self, g, probes, asked=4, tablet=False, soak=0):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            ok = gate.machine_ready(g, asked, tablet, soak, probes, self.learned)
        self.said = out.getvalue()
        return ok

    @staticmethod
    def record(g, *row):
        with contextlib.redirect_stdout(io.StringIO()):
            g.record(*row)

    def lock(self, name, text):
        folder = self.t / "plan" / name
        folder.mkdir()
        (folder / "holder.txt").write_text(text + "\n", encoding="utf-8")

    def make_run(self, stamp, commit, level, passed, with_result=True, subs=("runs-a/x", "runs-b")):
        d = self.t / "runs" / ("%s-%s-%s" % (stamp, commit, level))
        d.mkdir()
        if with_result:
            (d / "result.json").write_text(json.dumps({"passed": passed}), encoding="utf-8")
        (d / "result.txt").write_text("text", encoding="utf-8")
        for sub in subs:
            (d / sub).mkdir(parents=True)
            (d / sub / "big.bin").write_bytes(b"0" * 1000)
        return d

    @staticmethod
    def whole(d):
        return (d / "runs-a" / "x" / "big.bin").exists()


class Leftovers(Base):
    def test_leftover_is_ended_and_named(self):
        g = self.make_gate()
        procs = [
            proc(1, 0, "python.exe", "C:/Python/python.exe", 99999),           # the parent of pid 120, alive
            proc(2, 0, "python.exe", "C:/Python/python.exe", 10),              # a new process that has the number pid 150's parent had
            proc(100, 999),                                                    # old, parent gone: a leftover
            proc(101, 100),                                                    # its child (taskkill /T takes it)
            proc(110, 999, age=300),                                           # young: not yet
            proc(120, 1),                                                      # old, parent alive
            proc(130, 999, "python.exe", "D:/NebulaWork/x/python.exe"),        # not the game
            proc(140, 999, path="C:/Games/Nebula.exe"),                        # outside D:/NebulaWork/
            proc(150, 2, age=7200),                                            # its parent's number is used again: gone
        ]
        probes = FakeProbes(procs=procs)
        self.assertTrue(self.ready(g, probes))
        self.assertEqual(sorted(probes.ended), [100, 150], "the leftovers, and only them, are ended")
        note = next(n for n in g.machine_notes if n.startswith("ended"))
        self.assertIn("ended 2 game processes", note)
        self.assertIn(ORPHAN, note)
        self.assertIn("process 100", note)
        self.assertEqual(sorted(p.pid for p in probes.procs), [1, 2, 110, 120, 130, 140], "the others, and the child, went or stayed right")
        self.assertEqual(g.results, [], "no row: it is not a fault of the run")

    def test_leftover_of_a_locked_run_is_not_ended(self):
        orphan = proc(100, 999, age=3600)
        holder = proc(4242, 1, "python.exe", "C:/Python/python.exe", age=7200)
        # A held lock whose holder is alive and began before the process: it may own it.
        self.lock("gate.lock", "main gate of Nebula-T4@c693079e8 pid 4242 2026-11-21 02:00:00")
        probes = FakeProbes(procs=[holder, orphan])
        g = self.make_gate()
        self.assertTrue(self.ready(g, probes))
        self.assertEqual(probes.ended, [], "a process of a run that holds a lock is not ended")
        self.assertTrue(any("was not ended" in n and "gate.lock" in n for n in g.machine_notes), g.machine_notes)
        # The holder began after the process: the process cannot be its.
        probes = FakeProbes(procs=[proc(4242, 1, "python.exe", "C:/Python/python.exe", age=60), orphan])
        self.assertTrue(self.ready(self.make_gate(), probes))
        self.assertEqual(probes.ended, [100])
        # A stale lock (its holder's process ended) protects nothing.
        probes = FakeProbes(procs=[orphan])
        self.assertTrue(self.ready(self.make_gate(), probes))
        self.assertEqual(probes.ended, [100])
        # This run's own lock protects nothing (it has started no game yet).
        probes = FakeProbes(procs=[proc(os.getpid(), 1, "python.exe", "C:/Python/python.exe", age=7200), orphan])
        shutil.rmtree(self.t / "plan" / "gate.lock")
        self.lock("gate.lock", "main gate of Nebula-T13@abc pid %d 2026-11-21 02:00:00" % os.getpid())
        self.assertTrue(self.ready(self.make_gate(), probes))
        self.assertEqual(probes.ended, [100])
        # A lock taken by hand (no process in its line) may own anything.
        shutil.rmtree(self.t / "plan" / "gate.lock")
        self.lock("gate.lock", "gate by hand (Agent 2) 2026-11-17 13:00:00")
        probes = FakeProbes(procs=[orphan])
        self.assertTrue(self.ready(self.make_gate(), probes))
        self.assertEqual(probes.ended, [])

    def test_a_leftover_that_cannot_be_ended_is_env(self):
        class Stuck(FakeProbes):
            def end(self, pid):
                self.ended.append(pid)
                return False
        g = self.make_gate()
        self.assertFalse(self.ready(g, Stuck(procs=[proc(100, 999)])))
        self.assertEqual(g.results[0]["check"], "machine/leftovers")
        self.assertEqual(g.results[0]["state"], "env")


class Disk(Base):
    def test_too_little_disk_thins_then_passes(self):
        runs = [self.make_run("202611%02d-120000" % day, "abc%06d" % day, "main", True) for day in range(1, 7)]
        g = self.make_gate("main")
        probes = FakeProbes(disk=[10 * GB, 120 * GB])
        self.assertTrue(self.ready(g, probes))
        self.assertEqual(probes.calls.count("disk"), 2, "looked at again after the thinning")
        self.assertEqual([self.whole(r) for r in runs], [False, False, False, True, True, True])
        note = next(n for n in g.machine_notes if "thinned" in n)
        self.assertIn("3 old run folders thinned", note)
        self.assertEqual(g.results, [])
        # Enough room at once: nothing is thinned.
        again = self.make_run("20261107-120000", "abc000007", "main", True)
        self.assertTrue(self.ready(self.make_gate("main"), FakeProbes(disk=[500 * GB])))
        self.assertTrue(self.whole(again) and self.whole(runs[3]))

    def test_too_little_disk_after_thinning_is_env(self):
        self.make_run("20261101-120000", "abc000001", "main", True)
        g = self.make_gate("main")
        self.assertFalse(self.ready(g, FakeProbes(disk=[10 * GB, 20 * GB])))
        row = g.results[0]
        self.assertEqual((row["check"], row["state"]), ("machine/disk", "env"))
        self.assertIn("drive D: has 20.0 GB free", row["detail"])
        self.assertIn("80 GB", row["detail"])
        self.assertEqual(row["records"][0]["kind"], "env")
        self.assertTrue(any(line.startswith("KIND env machine/disk") for line in kinds.kind_lines(g.results)))
        # The floors by level: 30 for a landing (and the quick gate), 80 for the full run, 150 for the nightly run.
        self.assertEqual([machine.disk_need(l) // GB for l in ("land", "quick", "main", "long", "final")], [30, 30, 80, 150, 150])
        for level, free, ok in (("land", 40, True), ("main", 40, False), ("main", 90, True), ("long", 100, False), ("long", 160, True)):
            self.assertEqual(self.ready(self.make_gate(level), FakeProbes(disk=[free * GB])), ok, "%s with %d GB" % (level, free))

    def test_thinning_keeps_the_right_folders(self):
        main_pass = [self.make_run("202611%02d-120000" % d, "aaa%06d" % d, "main", True) for d in (1, 3, 5, 7, 9)]
        main_fail = [self.make_run("202611%02d-130000" % d, "bbb%06d" % d, "main", False) for d in (2, 4, 6, 8, 10)]
        quick = [self.make_run("202611%02d-140000" % d, "ccc%06d" % d, "quick", p) for d, p in ((1, True), (2, True), (3, False))]
        going = self.make_run("20261111-120000", "ddd000011", "main", True, with_result=False)
        waiting = self.make_run("20261102-120000", "eee000002", "long", True)
        long_old = self.make_run("20261103-120000", "fff000003", "long", True)
        long_new = [self.make_run("202611%02d-150000" % d, "fab%06d" % d, "long", True) for d in (4, 5, 6)]
        other = self.t / "runs" / "tablet-check-697535a86"
        (other / "deep").mkdir(parents=True)
        (self.t / "runs" / "history.tsv").write_text("x", encoding="utf-8")
        count, freed = machine.thin_runs(self.t / "runs", held={"eee000002"})
        # Main: the 3 newest passing (days 5, 7, 9) and the 3 newest failing (6, 8, 10) stay whole.
        self.assertEqual([self.whole(r) for r in main_pass], [False, False, True, True, True])
        self.assertEqual([self.whole(r) for r in main_fail], [False, False, True, True, True])
        self.assertTrue(all(self.whole(r) for r in quick), "quick has fewer than three of each")
        self.assertTrue(self.whole(going), "a run still going (no result.json) is left alone")
        self.assertTrue(self.whole(waiting), "a run waiting for GitHub is left alone")
        self.assertFalse(self.whole(long_old), "an old run that nothing holds is thinned")
        self.assertTrue(all(self.whole(r) for r in long_new), "the three newest of the long level stay whole")
        self.assertTrue((other / "deep").is_dir() and (self.t / "runs" / "history.tsv").exists(), "what is not a run is left alone")
        for r in main_pass[:2] + main_fail[:2]:
            self.assertTrue((r / "result.json").exists() and (r / "result.txt").exists(), "a thinned run keeps its top-level files")
            self.assertEqual([p.name for p in r.iterdir() if p.is_dir()], [], "and loses what is inside folders")
        self.assertEqual(count, 5)
        self.assertGreater(freed, 0)
        self.assertEqual(machine.thin_runs(self.t / "runs", held={"eee000002"}), (0, 0), "a second thinning finds nothing")


class Tablet(Base):
    def test_tablet_not_answering_its_part_is_owed_the_run_is_judged(self):
        for state, words in (("absent", "does not answer adb"), ("offline", "offline"), ("unauthorized", "not authorised")):
            g = self.make_gate("main")
            tablet = FakeTablet(state=state)
            probes = FakeProbes(tablet=tablet)
            self.assertTrue(self.ready(g, probes, tablet=True), state)
            self.assertEqual(g.results, [], "no row, no stop: the tablet may be away for a day")
            self.assertTrue(any("its part is owed" in n and words in n for n in g.machine_notes), g.machine_notes)
            self.assertIn("card", probes.calls, "the run goes on to the card")
            self.assertEqual(tablet.pushed, [], "nothing is pushed to a tablet that does not answer")
            self.assertIsNotNone(g.slots, "and its copies are set as usual")
        # An answering tablet with the right data passes; the adb server is made sure of first; a run with no tablet part asks nothing.
        g = self.make_gate("main")
        probes = FakeProbes(tablet=FakeTablet())
        self.assertTrue(self.ready(g, probes, tablet=True))
        self.assertEqual(probes.calls[:probes.calls.index("tablet") + 1].count("adb"), 1, "the adb server before the first call")
        probes = FakeProbes(tablet=FakeTablet(state="absent"))
        self.assertTrue(self.ready(self.make_gate("quick", None), probes, tablet=False))
        self.assertNotIn("adb", probes.calls)
        self.assertNotIn("tablet", probes.calls)

    def test_order_of_the_points(self):
        probes = FakeProbes(tablet=FakeTablet())
        self.assertTrue(self.ready(self.make_gate("main"), probes, tablet=True))
        order = [c for c in probes.calls if c in ("processes", "disk", "adb", "tablet", "card")]
        first = lambda name: order.index(name)   # noqa: E731
        self.assertTrue(first("processes") < first("disk") < first("adb") < first("tablet") < first("card"), order)

    def test_wrong_data_on_tablet_is_pushed_again(self):
        tablet = FakeTablet(version=37)
        g = self.make_gate("main")
        self.assertTrue(self.ready(g, FakeProbes(tablet=tablet, store=38), tablet=True))
        self.assertEqual(tablet.pushed, ["D:/pak/game.pak"], "the pak of this tree's version is pushed again")
        self.assertTrue(any("store version 37" in n and "pushed again" in n for n in g.machine_notes), g.machine_notes)
        # The right version: nothing is pushed.
        tablet = FakeTablet(version=38)
        self.assertTrue(self.ready(self.make_gate("main"), FakeProbes(tablet=tablet), tablet=True))
        self.assertEqual(tablet.pushed, [])
        # No game.pak it can read in the app: pushed.
        tablet = FakeTablet(version=None)
        g = self.make_gate("main")
        self.assertTrue(self.ready(g, FakeProbes(tablet=tablet), tablet=True))
        self.assertEqual(len(tablet.pushed), 1)
        self.assertTrue(any("none it can read" in n for n in g.machine_notes))
        # A push that fails is env.
        g = self.make_gate("main")
        self.assertFalse(self.ready(g, FakeProbes(tablet=FakeTablet(version=37, push_ok=False)), tablet=True))
        self.assertEqual((g.results[0]["check"], g.results[0]["state"]), ("machine/tablet", "env"))
        self.assertIn("pushing it again failed", g.results[0]["detail"])
        # The data of this tree is not made yet: the run makes it and pushes it itself.
        tablet = FakeTablet(version=37)
        g = self.make_gate("main")
        self.assertTrue(self.ready(g, FakeProbes(tablet=tablet, pak=(None, "")), tablet=True))
        self.assertEqual(tablet.pushed, [])
        self.assertTrue(any("not made yet" in n for n in g.machine_notes))
        # --pak names data of another version: env, with the numbers.
        g = self.make_gate("main")
        probes = FakeProbes(tablet=FakeTablet(version=37), pak=(None, "the game data --pak names (x) is store version 36, this tree's is 38"))
        self.assertFalse(self.ready(g, probes, tablet=True))
        self.assertIn("store version 36", g.results[0]["detail"])
        # No test app on the tablet: the run installs it; nothing is pushed.
        tablet = FakeTablet(installed=False, version=None)
        g = self.make_gate("main")
        self.assertTrue(self.ready(g, FakeProbes(tablet=tablet), tablet=True))
        self.assertEqual(tablet.pushed, [])
        self.assertTrue(any("not on the tablet" in n for n in g.machine_notes))

    def test_the_game_data_kept_on_the_tablet_is_said(self):
        # B14 part 2: the landing's start says what is kept there (the key and the size), or that the run pushes it once.
        tablet = FakeTablet(installed=False, version=None, kept=("RMGE01-37-3aa6533b1f08", 4_020_000_000))
        g = self.make_gate("main")
        self.assertTrue(self.ready(g, FakeProbes(tablet=tablet), tablet=True))
        self.assertIn("game data kept on the tablet: RMGE01-37-3aa6533b1f08, 4.0 GB", g.machine_notes)
        self.assertEqual(tablet.pushed, [])
        tablet = FakeTablet(installed=False, version=None, kept=None)
        g = self.make_gate("main")
        self.assertTrue(self.ready(g, FakeProbes(tablet=tablet), tablet=True))
        self.assertTrue(any(n.startswith("no game data is kept on the tablet") for n in g.machine_notes), g.machine_notes)
        # a tablet that does not answer says nothing of it
        g = self.make_gate("main")
        self.assertTrue(self.ready(g, FakeProbes(tablet=FakeTablet(state="absent", kept=("K", 1))), tablet=True))
        self.assertFalse(any("kept on the tablet" in n for n in g.machine_notes))

    def test_real_tablet_reading(self):
        g = self.make_gate("main", "R52")
        device = types.SimpleNamespace(adb_path="adb-stand-in", env={}, installed=lambda: True,
                                       pak_header=lambda: b"NBSTORE\0" + (38).to_bytes(4, "little") + b"\0" * 52)
        real = gate.RealTablet(g, device)
        listing = {"text": "List of devices attached\nR52\tunauthorized\nOTHER\tdevice\n"}

        def fake_run(command, **kw):
            self.assertEqual(command, ["adb-stand-in", "devices"])
            return types.SimpleNamespace(stdout=listing["text"].encode())
        with mock.patch.object(gate.subprocess, "run", fake_run):
            self.assertEqual(real.state(), "unauthorized")
            listing["text"] = "List of devices attached\nR52\tdevice\n"
            self.assertEqual(real.state(), "device")
            listing["text"] = "List of devices attached\nOTHER\tdevice\n"
            self.assertEqual(real.state(), "absent")
        self.assertEqual(real.data(), (True, 38))
        device.pak_header = lambda: b""
        self.assertEqual(real.data(), (True, None))
        device.installed = lambda: False
        self.assertEqual(real.data(), (False, None))


class Card(Base):
    def test_copies_formula(self):
        cap = machine.copies_at_once
        need = 3 * GB
        # The card's limit wins: 20 GB free, 70% is 14 GB, four copies of 3 GB; memory and lanes allow more.
        self.assertEqual(cap(20 * GB, 100 * GB, need, 6), (4, 4, 35))
        # The memory's limit wins: 7 GB of the PC's 10 free is three copies of 2 GB.
        self.assertEqual(cap(28.8 * GB, 10 * GB, need, 6), (3, 6, 3))
        # The lanes' limit wins.
        self.assertEqual(cap(28.8 * GB, 64 * GB, need, 2)[0], 2)
        # Exactly one fits (70% of 4.5 GB is 3.15 GB), and a little less is none (70% of 4.2 GB is 2.94 GB).
        self.assertEqual(cap(4.5 * GB, 64 * GB, need, 6)[0], 1)
        self.assertEqual(cap(4.2 * GB, 64 * GB, need, 6)[0], 0)
        self.assertEqual(cap(28 * GB, 2.5 * GB, need, 6)[0], 0, "no memory for one copy: none")
        # A reading that is not there sets no limit; what a renderer was seen to need changes the count.
        self.assertEqual(cap(None, 64 * GB, need, 5)[0], 5)
        self.assertEqual(cap(20 * GB, None, 6 * GB, 9)[0], 2)
        # The same through machine_ready: it sets the places the lanes may take.
        for cards, memory, asked, want in (([card(32, 22)], 64, 6, 2), ([card(32, 2)], 10, 6, 3), ([card(32, 2)], 64, 2, 2),
                                           ([card(32, 27.5)], 64, 6, 1)):
            g = self.make_gate("main")
            self.assertTrue(self.ready(g, FakeProbes(cards=cards, memory=[memory * GB]), asked=asked), (cards, memory, asked))
            self.assertEqual(g.slots.total, want, (cards[0].used / GB, memory, asked))

    def test_no_copy_fits_then_one_does(self):
        full = card(32, 31)
        probes = FakeProbes(cards=[full, full, full, card(32, 2)])
        g = self.make_gate("main")
        self.assertTrue(self.ready(g, probes, asked=4))
        self.assertEqual(probes.slept, [60.0, 60.0, 60.0], "looked once a minute until a copy fitted")
        self.assertEqual(g.slots.total, 4)
        self.assertEqual(self.said.count("not even one copy"), 1, "one line at the start of the wait: %s" % self.said)
        self.assertTrue(probes.the_sampler.started, "the run starts, and its sampler with it")
        self.assertEqual(g.results, [])
        # Said every ten minutes, not every look.
        probes = FakeProbes(cards=[full] * 25 + [card(32, 2)])
        self.assertTrue(self.ready(self.make_gate("main"), probes))
        self.assertEqual(self.said.count("still waiting"), 2, self.said)

    def test_no_copy_fits_for_four_hours_is_env(self):
        probes = FakeProbes(cards=[card(32, 31)])
        g = self.make_gate("main")
        self.assertFalse(self.ready(g, probes))
        self.assertGreaterEqual(probes.now - NOW, 4 * 3600)
        self.assertLess(probes.now - NOW, 4 * 3600 + 120)
        row = g.results[0]
        self.assertEqual((row["check"], row["state"]), ("machine/graphics-card", "env"))
        self.assertEqual(row["detail"], "the graphics card has no room for one copy of the game: other programs hold 31.0 GB")
        self.assertLessEqual(self.said.count("still waiting"), 24, "a line every ten minutes")
        self.assertGreaterEqual(self.said.count("still waiting"), 22)
        # No room in the PC's memory is its own line.
        probes = FakeProbes(memory=[2 * GB])
        g = self.make_gate("main")
        self.assertFalse(self.ready(g, probes))
        self.assertEqual(g.results[0]["check"], "machine/memory")
        self.assertIn("the PC's memory has no room for one copy of the game", g.results[0]["detail"])

    def test_a_card_that_cannot_be_read_sets_no_limit(self):
        g = self.make_gate("main")
        self.assertTrue(self.ready(g, FakeProbes(cards=[OSError("no counter names the card")]), asked=5))
        self.assertEqual(g.slots.total, 5)
        self.assertTrue(any("could not be read" in n for n in g.machine_notes))

    def test_card_shared_in_summary(self):
        g = self.make_gate("main")
        self.assertTrue(self.ready(g, FakeProbes(cards=[card(32, 20)]), asked=6))
        self.assertTrue(g.card_shared)
        shared = next(n for n in g.machine_notes if n.startswith("card shared"))
        self.assertIn("20.0 GB", shared)
        self.assertIn("judged as usual", shared)
        self.assertEqual(g.slots.total, 2, "it starts at once with the copies that fit")
        # Half exactly is not shared; over half is.
        g2 = self.make_gate("main")
        self.assertTrue(self.ready(g2, FakeProbes(cards=[card(32, 16)])))
        self.assertFalse(g2.card_shared)
        # A failure in a card-shared run is a failure (exit 1), not "not judged"; a pass is a pass.
        self.record(g, "tour/d3d12", "fail", 1.0, "a test failed")
        g._save()
        self.assertEqual((g.verdict, g.code), ("fail", 1))
        result = json.loads((g.work / "result.json").read_text(encoding="utf-8"))
        self.assertTrue(any(n.startswith("card shared") for n in result["machine"]), "the summary says it")
        self.assertIn("card shared", (g.work / "result.txt").read_text(encoding="utf-8"))
        g3 = self.make_gate("main")
        self.assertTrue(self.ready(g3, FakeProbes(cards=[card(32, 20)])))
        self.record(g3, "tour/d3d12", "pass", 1.0, "ok")
        g3._save()
        self.assertEqual((g3.verdict, g3.code), ("pass", 0))


class Learning(Base):
    def test_machine_json_learns(self):
        learned = self.learned
        self.assertEqual(learned.need(("d3d12",)), 3 * GB, "3 GB until a figure exists")
        self.assertEqual(learned.read(), {})
        learned.add({"d3d12": 5 * GB, "vulkan": 0})
        self.assertEqual(learned.need(("d3d12",)), 5 * GB)
        self.assertEqual(learned.need(("vulkan",)), 3 * GB, "a renderer none has shown still assumes 3 GB")
        self.assertEqual(learned.need(("d3d12", "vulkan")), 5 * GB, "the largest of the renderers a run plays on")
        # The biggest run falls out of the last 20: its figure no longer counts.
        learned.add({"d3d12": 9 * GB})
        for _ in range(19):
            learned.add({"d3d12": 4 * GB})
        self.assertEqual(learned.need(("d3d12",)), 9 * GB, "the 9 GB run is the 2nd of the last 20")
        learned.add({"d3d12": 4 * GB})
        self.assertEqual(learned.need(("d3d12",)), 5 * GB if False else 4 * GB, "the 9 GB and the 5 GB runs have left the last 20")
        self.assertEqual(len(learned.read()["d3d12"]), 20)
        learned.add({"d3d11": 6 * GB})
        self.assertEqual(learned.need(("d3d11",)), 6 * GB)
        self.assertEqual(learned.need(("d3d12",)), 4 * GB, "renderers are kept apart")
        body = json.loads(self.learned.path.read_text(encoding="utf-8"))
        self.assertEqual(sorted(body["peaks"]), ["d3d11", "d3d12", "vulkan"][:0] or ["d3d11", "d3d12"])
        # A file that cannot be read is "no figure", not a fault.
        self.learned.path.write_text("{not json", encoding="utf-8")
        self.assertEqual(self.learned.need(("d3d12",)), 3 * GB)
        # A run that saw no game adds nothing.
        self.learned.path.unlink()
        self.learned.add({})
        self.assertFalse(self.learned.path.exists())
        # The run's end adds what its sampler saw.
        g = self.make_gate("main")
        g.sampler = FakeSampler({"d3d12": 7 * GB})
        gate.machine_learn(g, self.learned)
        self.assertIsNone(g.sampler)
        self.assertEqual(self.learned.need(("d3d12",)), 7 * GB)

    def test_sampler_reads_the_games_use(self):
        work = "D:/NebulaWork/build/gate-runs/run-1"
        procs = [machine.Proc(500, 1, "python.exe", "C:/Python/python.exe", NOW),
                 machine.Proc(600, 500, "NebulaVk.exe", work + "/runs/a/bin/NebulaVk.exe", NOW),
                 machine.Proc(610, 1, "Nebula.exe", work + "/runs/b/bin/Nebula.exe", NOW),    # under the run's folder, parent gone
                 machine.Proc(700, 1, "Nebula.exe", "D:/NebulaWork/build/other-run/bin/Nebula.exe", NOW)]   # another run's
        values = {"pid_600_luid_0x00000000_0x0001a050_phys_0": 5 * GB,
                  "pid_600_luid_0x00000000_0x0001b56c_phys_0": 9 * GB,      # the small card's instance of the same process
                  "pid_610_luid_0x00000000_0x0001a050_phys_0": 4 * GB,
                  "pid_700_luid_0x00000000_0x0001a050_phys_0": 7 * GB}

        class Reader:
            def read(self):
                return {machine.PROCESS_DEDICATED: values}
        sampler = machine.Sampler(work, {(0, 0x1a050)}, lambda t: None, processes=lambda: procs, gate_pid=500)
        sampler.sample(Reader())
        self.assertEqual(sampler.peaks, {"vulkan": 5 * GB, "d3d12": 4 * GB}, "the run's games on the game's card only")
        values["pid_600_luid_0x00000000_0x0001a050_phys_0"] = 6 * GB
        sampler.sample(Reader())
        self.assertEqual(sampler.peaks["vulkan"], 6 * GB, "the largest is kept")
        values["pid_600_luid_0x00000000_0x0001a050_phys_0"] = 1 * GB
        sampler.sample(Reader())
        self.assertEqual(sampler.peaks["vulkan"], 6 * GB)
        # The card's total, and what is held of it, from the counters of a card Windows lists under two numbers.
        adapter = {"name": "X", "vendor": 1, "device": 2, "subsystem": 3, "luid": (0, 0x1a050), "dedicated": 32 * GB,
                   "shared": 48 * GB, "software": False}
        found = {machine.ADAPTER_DEDICATED: {"luid_0x00000000_0x0001A050_phys_0": 2 * GB, "luid_0x00000000_0x0001B56C_phys_0": GB}}
        self.assertEqual(machine.card_used(adapter, found), 2 * GB, "the other card's use is not the card's")
        self.assertEqual(machine.card_used(adapter, found, [(0, 0x1a050), (0, 0x2969d)]), 2 * GB)
        self.assertIsNone(machine.card_used(adapter, {machine.ADAPTER_DEDICATED: {}}), "no counter names it: unknown, not zero")
        small = dict(adapter, dedicated=GB // 8)
        self.assertEqual(machine.card_total(small), GB // 8 + 48 * GB, "a card with no memory of its own shares the PC's")
        self.assertEqual(machine.game_adapter([dict(adapter, software=True), adapter, dict(adapter, luid=(0, 9), device=5)]), adapter)
        self.assertEqual(machine.game_adapter([adapter, dict(adapter, luid=(0, 9), device=5)], "0001:0005:00000003")["luid"], (0, 9),
                         "LUMA_TEST_GPU names the card")

    def test_sampling_error_does_not_fail_a_run(self):
        said = []

        def no_counters(paths):
            raise OSError("no performance counters here")
        sampler = machine.Sampler("D:/NebulaWork/x", None, said.append, interval=0.01, counters=no_counters)
        sampler.start()
        sampler.thread.join(10)
        self.assertFalse(sampler.thread.is_alive(), "the thread ended by itself")
        self.assertEqual(len(said), 1, "one warning line: %s" % said)
        self.assertTrue(said[0].startswith("gate.py: tool:") and "no performance counters here" in said[0])
        self.assertEqual(sampler.stop(), {})
        # An error in a single reading ends the reading the same way.
        said = []

        class Reader:
            def read(self):
                raise RuntimeError("the counter set went away")

            def close(self):
                pass
        sampler = machine.Sampler("D:/NebulaWork/x", None, said.append, interval=0.01, counters=lambda paths: Reader(),
                                  processes=lambda: [machine.Proc(5, 1, "Nebula.exe", "D:/NebulaWork/x/Nebula.exe", NOW)])
        sampler.start()
        sampler.thread.join(10)
        self.assertEqual(len(said), 1)
        sampler.stop()
        # A warning that cannot be printed does not raise either.
        machine.Sampler("D:/NebulaWork/x", None, lambda text: 1 / 0, counters=no_counters).run()
        # A sampler that breaks when the run ends, and a machine.json that cannot be written, are one line each.
        g = self.make_gate("main")

        class Exploding:
            def stop(self):
                raise RuntimeError("boom")
        g.sampler = Exploding()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            gate.machine_learn(g, self.learned)
        self.assertIn("gate.py: tool:", out.getvalue())
        self.assertEqual(out.getvalue().count("\n"), 1)
        self.record(g, "tour/d3d12", "pass", 1.0, "ok")
        g._save()
        self.assertEqual((g.verdict, g.code), ("pass", 0), "the run's verdict is not touched")
        g.sampler = FakeSampler({"d3d12": GB})
        blocked = types.SimpleNamespace(add=lambda peaks: (_ for _ in ()).throw(PermissionError("read-only")))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            gate.machine_learn(g, blocked)
        self.assertIn("was not updated", out.getvalue())

    def test_slots_limit_the_copies(self):
        slots = machine.Slots(2)
        lock = threading.Lock()
        state = {"running": 0, "most": 0, "arrived": 0}
        arrived = threading.Condition()
        release = threading.Event()

        def worker():
            with arrived:
                state["arrived"] += 1
                arrived.notify_all()
            with slots.holding():
                with lock:
                    state["running"] += 1
                    state["most"] = max(state["most"], state["running"])
                release.wait(30)
                with lock:
                    state["running"] -= 1
        threads = [threading.Thread(target=worker) for _ in range(6)]
        for t in threads:
            t.start()
        with arrived:
            arrived.wait_for(lambda: state["arrived"] == 6, 30)
        for _ in range(3000):
            if slots.free == 0:
                break
            threading.Event().wait(0.01)
        self.assertEqual(slots.free, 0)
        release.set()
        for t in threads:
            t.join(30)
        self.assertEqual(state["most"], 2, "never more than the places at once")
        self.assertEqual(slots.free, 2, "every place is given back")
        # A weight above the whole is the whole; a stand-in run object (no limit set yet) takes none.
        self.assertEqual(machine.Slots(2).take(5), 2)
        with gate.game_slot(types.SimpleNamespace()):
            pass
        with self.make_gate("main").game_slot():
            pass


class Runs(Base):
    def run_with(self, selected, level="main", extra=()):
        args = gate.parser().parse_args(["--main" if level == "land" else "--" + level, "--tablet", "T", "--work", str(self.t / "runs"),
                                         "--build-root", str(self.t / "b"), "--gate-lock", str(self.t / "plan" / "gate.lock"),
                                         "--status", "", "--board", ""] + list(extra))
        if level == "land":
            args.mode = gate.LAND_LEVEL
        known, planned, _ = gate.read_lists()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            g = gate.run_checks(args, known, planned, selected)
        self.gates.append(g)
        return g

    def test_documents_only_run_does_not_call_machine_ready(self):
        self.assertFalse(gate.run_starts_game({"repo/settings", "repo/audit", "build/windows", "unit/windows", "rust"}))
        self.assertTrue(gate.run_starts_game({"repo/settings", "tour/d3d12"}))
        self.assertTrue(gate.run_starts_game(None), "a run of every check of its level starts the game")
        calls = []
        saved = gate.machine_ready, gate.settings_table
        gate.settings_table = lambda g: (lambda log: (True, "ok"))
        gate.machine_ready = lambda *a, **k: calls.append(a) or True
        try:
            g = self.run_with({"repo/settings"}, "land")
            self.assertEqual(calls, [], "no game: machine_ready is not called")
            self.assertEqual([r["check"] for r in g.results], ["repo/settings"])
            # A selection with a game check calls it once, with what the lanes ask for and whether there is a tablet part.
            gate.machine_ready = lambda g, asked, tablet, soak=0, **k: calls.append((asked, tablet, soak)) or False
            self.run_with({"repo/settings", "tour/d3d12"}, "land")
        finally:
            gate.machine_ready, gate.settings_table = saved
        self.assertEqual(calls, [(gate.lanes_asked(self.gates[-1]), False, 0)], calls)

    def test_disk_under_its_floor_is_env_not_fail(self):
        for day in range(1, 3):
            self.make_run("202611%02d-120000" % day, "abc%06d" % day, "main", True)
        saved = gate.GateProbes
        gate.GateProbes = lambda: FakeProbes(disk=[5 * GB])
        try:
            g = self.run_with(None)
        finally:
            gate.GateProbes = saved
        self.assertEqual((g.verdict, g.code), ("not-judged", 2))
        self.assertEqual([(r["check"], r["state"]) for r in g.results], [("machine/disk", "env")])
        self.assertTrue(any(line.startswith("KIND env machine/disk") for line in g.lines))
        self.assertIn("machine/disk", (g.work / "result.txt").read_text(encoding="utf-8"))
        self.assertFalse((g.work / "build-windows.log").exists(), "nothing else ran")

    def test_lanes_ask_what_the_lanes_ask(self):
        known, planned, _ = gate.read_lists()
        for level, extra in (("quick", ()), ("main", ()), ("land", ()), ("long", ()), ("final", ()), ("main", ("--mods", "short")),
                             ("final", ("--mods", "full", "--mod-parallel", "3")), ("main", ("--serial",))):
            flag = "--main" if level == "land" else "--" + level
            args = gate.parser().parse_args([flag, "--tablet", "T", "--work", str(self.t / "runs"), "--build-root", str(self.t / "b")] + list(extra))
            if level == "land":
                args.mode = gate.LAND_LEVEL
            dry = gate.run_checks(args, known, planned, dry=True)
            try:
                if "--serial" in extra:
                    self.assertEqual(gate.lanes_asked(dry), 1)
                    continue
                self.assertEqual(gate.lanes_asked(dry), dry.lanes_seen, "%s %s: what the lanes ask" % (level, extra))
                for name in dry.needs:
                    if name in planned:
                        continue      # not built yet: it is classified when it is
                    in_game, in_none = gate.starts_game(name), any(name.startswith(p) for p in gate.NO_GAME_CHECKS)
                    self.assertTrue(in_game != in_none, "%s (%s) is a game check or not, one of the two" % (name, level))
            finally:
                shutil.rmtree(dry.work, ignore_errors=True)
        tablet_args = gate.parser().parse_args(["--main", "--tablet", "T"])
        self.assertTrue(gate.run_has_tablet_part(tablet_args, "main", None))
        self.assertFalse(gate.run_has_tablet_part(tablet_args, "quick", None))
        self.assertFalse(gate.run_has_tablet_part(tablet_args, "main", {"tour/d3d12"}))
        self.assertTrue(gate.run_has_tablet_part(tablet_args, "main", {"tablet/prepare"}))
        self.assertFalse(gate.run_has_tablet_part(gate.parser().parse_args(["--main"]), "main", None), "no tablet named: no part")


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("machine_ready_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
