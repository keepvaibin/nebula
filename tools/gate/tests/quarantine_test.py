"""quarantine_test.py: a new row of pace.py or a new check of gate.py is report only until it has passed on main five times
(tools/gate/quarantine.py, docs/plan/04-GATE-AND-MAIN.md "Quarantine of new rows"; the planner's order of 11-27, M-015).

    python -X utf8 -I tools/gate/tests/quarantine_test.py

  the_live_file               every row and check on main at the mend is listed (and a name that is not: a new one)
  a_new_name_is_quarantined   0 passes; 4 passes still quarantined; 5 live; a name a plant run makes live is live; an unreadable live file
                              makes everything live
  the_counters                a pass counts once per main commit (a retry of a commit counts once); a failure counts for nothing and takes
                              nothing away; a new name is written with 0 passes; the passes stop at 5
  a_new_row_in_pace           a new row that would FAIL refuses nothing (its verdict says quarantined and the digest names it); with --live-row it
                              refuses; an existing row refuses at once; pace.py keep counts the pass of a new row once and never counts a FAIL
  a_new_check_in_the_gate     a new check that fails passes with the text in front; its failure is a fail again with --live-row; an existing
                              check, a planned check that runs, a fault of the tool and a check owed are judged as before; the pass
                              of a quarantined check is kept after a move (keep_quarantine), once for a main commit, a failure never
  the_commands                gate.py hands the counters file and the live rows to pace.py's run and keep
"""
import contextlib
import io
import json
import pathlib
import subprocess
import sys
import tempfile
import types
import unittest
import unittest.mock

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402
import pace  # noqa: E402
import quarantine  # noqa: E402

LINE = ("[pace] seconds=9.983333 steps=600 steps_per_second=60.0000 pictures=1200 displayed=1200 generated=600 refresh_ms=8.333 "
        "on_time=1199 missed=0 doubled=0 missed_system=0 jumps={jumps} jump_max_us=120.0 animation_within_1ms=1.0 animation_p99_us=80.0 "
        "delay_avg_us=20000.0 delay_p99_us=25000.0 render_avg_us=3000.0 render_p99_us=4000.0 generated_render_avg_us=500.0 "
        "generated_render_p99_us=700.0 step_work_avg_us=3000.0 step_work_p99_us=4000.0 step_work_max_us=5000.0")
NEW_ROW = "a row a step adds"


def write_run(work, jumps=0):
    folder = pathlib.Path(work) / "match" / "d3d12"
    folder.mkdir(parents=True, exist_ok=True)
    lines = ["harness: frame 0 is game frame 900"] + [LINE.format(jumps=jumps)] * 3
    (folder / "run.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (folder / "test.json").write_text(json.dumps({"name": "match", "settings": []}), encoding="utf-8")


def with_new_row(verdict):
    """pace.judge as it is, and one more row of a name no one has listed."""
    real = pace.judge

    def judge(run, history=None, base=None, base_name=""):
        return real(run, history, base, base_name) + [(NEW_ROW, "a value", "a target", verdict)]
    return judge


@contextlib.contextmanager
def quiet():
    with contextlib.redirect_stdout(io.StringIO()) as out:
        yield out


class Counting(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.t = pathlib.Path(self.tmp.name)
        self.counters = self.t / "quarantine.tsv"

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_live_file(self):
        self.assertTrue(quarantine.read_live(quarantine.LIVE_FILE), "the live file reads and has lines")
        q = quarantine.Quarantine(self.counters)
        for kind, name in (("row", "game speed"), ("row", "even pictures"), ("row", "jumps"), ("row", "display lead in play"),
                           ("row", "loads"), ("row", "delay, in-between pictures on"), ("row", "miss: Unknown"),
                           ("row", pace.BASELINE_ROW), ("row", pace.PLATFORM_ROW), ("row", "pictures shown, from the start"),
                           ("check", "pace/pc"), ("check", "pace/tablet"), ("check", "repo/strictness"), ("check", "build/windows"),
                           ("check", "tour/d3d12"), ("check", "pictures/tour-RMGP01/vulkan"), ("check", "stress/tour/d3d11/4"),
                           ("check", "measure/step/tablet"), ("check", "harness/self"), ("check", "replays/vulkan")):
            self.assertIsNone(q.passes(kind, name), "%s %s is live from the start" % (kind, name))
        for kind, name in (("row", NEW_ROW), ("check", "pace/new-check"), ("check", "mods/tablet-quick"), ("check", "hostile-files")):
            self.assertEqual(q.passes(kind, name), 0, "%s %s is new" % (kind, name))
        self.assertEqual(q.passes("check", "jumps"), 0, "a row's name is not a check's")

    def test_a_new_name_is_quarantined(self):
        q = quarantine.Quarantine(self.counters)
        self.assertEqual(q.passes("row", NEW_ROW), 0)
        for n in range(1, quarantine.NEEDED):
            quarantine.keep(self.counters, "row", {NEW_ROW: True}, "commit%d" % n)
            self.assertEqual(quarantine.Quarantine(self.counters).passes("row", NEW_ROW), n, "%d passes: still quarantined" % n)
        quarantine.keep(self.counters, "row", {NEW_ROW: True}, "commit5")
        self.assertIsNone(quarantine.Quarantine(self.counters).passes("row", NEW_ROW), "the 5th pass: it refuses from now on")
        self.assertEqual(quarantine.Quarantine(self.counters, forced=[NEW_ROW]).passes("row", NEW_ROW), None)
        self.assertEqual(quarantine.Quarantine(self.t / "none.tsv", forced=[NEW_ROW]).passes("row", NEW_ROW), None,
                         "a plant run's --live-row makes a name with no count live")
        broken = self.t / "live.tsv"
        broken.write_text("", encoding="utf-8")
        self.assertIsNone(quarantine.Quarantine(self.counters, live_file=broken).passes("row", "another new row"),
                          "a live file with no line: nothing is quarantined")
        self.assertIsNone(quarantine.Quarantine(self.counters, live_file=self.t / "absent.tsv").passes("check", "another new check"),
                          "a live file that cannot be read: nothing is quarantined")

    def test_the_counters(self):
        said = quarantine.keep(self.counters, "row", {NEW_ROW: False}, "c1")
        self.assertEqual(said, [])
        self.assertEqual(quarantine.read_counters(self.counters)[("row", NEW_ROW)]["passes"], 0, "first seen: 0 passes")
        quarantine.keep(self.counters, "row", {NEW_ROW: True}, "c2")
        quarantine.keep(self.counters, "row", {NEW_ROW: True}, "c2")
        self.assertEqual(quarantine.read_counters(self.counters)[("row", NEW_ROW)]["passes"], 1, "a retry of the same main commit counts once")
        quarantine.keep(self.counters, "row", {NEW_ROW: False}, "c3")
        self.assertEqual(quarantine.read_counters(self.counters)[("row", NEW_ROW)]["passes"], 1, "a failure counts nothing and takes nothing")
        for n in range(4, 12):
            quarantine.keep(self.counters, "row", {NEW_ROW: True}, "c%d" % n)
        entry = quarantine.read_counters(self.counters)[("row", NEW_ROW)]
        self.assertEqual((entry["passes"], len(entry["commits"])), (quarantine.NEEDED, quarantine.NEEDED), "the passes stop at 5")
        quarantine.keep(self.counters, "check", {NEW_ROW: True}, "c2")
        self.assertEqual(quarantine.read_counters(self.counters)[("check", NEW_ROW)]["passes"], 1, "a check and a row of one name count apart")

    def test_a_new_row_in_pace(self):
        saved = pace.judge
        write_run(self.t / "work")
        try:
            pace.judge = with_new_row("FAIL")
            with quiet() as out:
                code = pace.digest(self.t / "work", None, pace.reading_of(counters=str(self.counters)))
            self.assertEqual(code, 0, "a new row that fails refuses nothing: " + out.getvalue())
            self.assertIn("quarantined: report only (0 of 5 passes on main)", out.getvalue())
            self.assertIn("quarantined row %s would have failed" % NEW_ROW, out.getvalue())
            rows = json.loads((self.t / "work" / "pace.json").read_text(encoding="utf-8"))[0]
            self.assertEqual(rows["quarantine"], {NEW_ROW: {"passes": 0, "verdict": "FAIL"}})
            self.assertEqual({r[0]: r[3] for r in rows["rows"]}[NEW_ROW], "quarantined: report only (0 of 5 passes on main)")
            with quiet():
                live = pace.digest(self.t / "work", None, pace.reading_of(counters=str(self.counters), live_rows=[NEW_ROW]))
            self.assertEqual(live, 1, "a plant run's --live-row: the new row refuses")
            with quiet():
                self.assertEqual(pace.digest(self.t / "work", None, pace.reading_of()), 0, "no counters file: 0 passes, quarantined")
            for n in range(5):
                quarantine.keep(self.counters, "row", {NEW_ROW: True}, "main%d" % n)
            with quiet():
                self.assertEqual(pace.digest(self.t / "work", None, pace.reading_of(counters=str(self.counters))), 1,
                                 "after 5 passes on main the row refuses")
        finally:
            pace.judge = saved

    def test_an_existing_row_refuses_at_once(self):
        write_run(self.t / "work", jumps=3)
        with quiet() as out:
            code = pace.digest(self.t / "work", None, pace.reading_of(counters=str(self.counters)))
        self.assertEqual(code, 1, out.getvalue())
        rows = {r[0]: r[3] for r in json.loads((self.t / "work" / "pace.json").read_text(encoding="utf-8"))[0]["rows"]}
        self.assertEqual(rows["jumps"], "FAIL")
        self.assertNotIn("quarantine", out.getvalue())

    def test_pace_keep_counts_a_pass_once(self):
        saved = pace.judge
        write_run(self.t / "work")
        history = self.t / "history.tsv"
        try:
            pace.judge = with_new_row("pass")
            with quiet():
                pace.digest(self.t / "work", None, pace.reading_of(counters=str(self.counters)))
                self.assertEqual(pace.keep(self.t / "work", history, "aaaaaaaaaaaa", str(self.counters)), 0)
                pace.keep(self.t / "work", history, "aaaaaaaaaaaa", str(self.counters))
            self.assertEqual(quarantine.read_counters(self.counters)[("row", NEW_ROW)]["passes"], 1, "a pass of one main commit counts once")
            pace.judge = with_new_row("FAIL")
            with quiet():
                pace.digest(self.t / "work", None, pace.reading_of(counters=str(self.counters)))
                pace.keep(self.t / "work", history, "bbbbbbbbbbbb", str(self.counters))
            self.assertEqual(quarantine.read_counters(self.counters)[("row", NEW_ROW)]["passes"], 1, "a FAIL of it counts for nothing")
            self.assertNotIn(("row", "jumps"), quarantine.read_counters(self.counters), "an existing row is never written")
        finally:
            pace.judge = saved


def gate_args(work, *more):
    return gate.parser().parse_args(["--main", "--tablet", "T", "--work", str(work), "--board", "", "--status", ""] + list(more))


def new_gate(args, **more):
    """A Gate as run_checks makes it: with the quarantine of its arguments."""
    return gate.Gate(args, quarantined=gate.quarantine_of(args), **more)


class GateChecks(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.t = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def states(self, g):
        return {r["check"]: r["state"] for r in g.results}

    def test_a_new_check_that_fails_refuses_nothing(self):
        g = new_gate(gate_args(self.t / "runs"))
        with quiet():
            g.check("pace/new-check", lambda log: (False, "3 of 9 pass"))
            g.check("rust", lambda log: (False, "1 failed"))
            g.check("repo/new-lint", lambda log: (True, "clean"))
        states = self.states(g)
        self.assertEqual(states, {"pace/new-check": "pass", "rust": "fail", "repo/new-lint": "pass"}, "an existing check still fails")
        row = {r["check"]: r for r in g.results}["pace/new-check"]
        self.assertTrue(row["detail"].startswith("quarantined: report only (0 of 5 passes on main); it failed: 3 of 9 pass"), row["detail"])
        self.assertEqual(row["quarantine"], {"passes": 0, "needed": 5, "ran": "fail"})
        self.assertEqual(row["records"], [], "the failure leaves no record that fails the verdict")
        self.assertIsNone({r["check"]: r for r in g.results}["rust"].get("quarantine"))
        self.assertEqual(g.counts()["fail"], 1)

    def test_run_checks_makes_its_gate_with_the_quarantine(self):
        seen = []

        class Stop(Exception):
            pass

        def fake(*a, **kw):
            seen.append(kw)
            raise Stop
        args = gate_args(self.t / "runs", "--live-row", "pace/new-check")
        with unittest.mock.patch.object(gate, "Gate", fake), self.assertRaises(Stop):
            gate.run_checks(args, dry=True)
        q = seen[0]["quarantined"]
        self.assertEqual((q.counters, q.forced), (str(self.t / "runs" / gate.QUARANTINE_FILE), ("pace/new-check",)))
        self.assertEqual(q.passes("check", "pace/other-new-check"), 0)

    def test_a_gate_made_without_it_holds_nothing_back(self):
        g = gate.Gate(gate_args(self.t / "runs"))
        with quiet():
            g.check("pace/new-check", lambda log: (False, "3 of 9 pass"))
        self.assertEqual(self.states(g), {"pace/new-check": "fail"})

    def test_a_plant_run_makes_the_check_live(self):
        g = new_gate(gate_args(self.t / "runs", "--live-row", "pace/new-check"))
        with quiet():
            g.check("pace/new-check", lambda log: (False, "3 of 9 pass"))
        self.assertEqual(self.states(g), {"pace/new-check": "fail"})

    def test_five_passes_make_it_refuse(self):
        counters = self.t / "runs" / gate.QUARANTINE_FILE
        for n in range(4):
            quarantine.keep(counters, "check", {"pace/new-check": True}, "m%d" % n)
        g = new_gate(gate_args(self.t / "runs"))
        with quiet():
            g.check("pace/new-check", lambda log: (False, "x"))
        self.assertEqual(self.states(g), {"pace/new-check": "pass"}, "4 passes: still quarantined")
        quarantine.keep(counters, "check", {"pace/new-check": True}, "m4")
        g = new_gate(gate_args(self.t / "runs"))
        with quiet():
            g.check("pace/new-check", lambda log: (False, "x"))
        self.assertEqual(self.states(g), {"pace/new-check": "fail"}, "the 5th pass on main: it refuses")

    def test_what_is_not_the_changes_is_judged_as_before(self):
        g = new_gate(gate_args(self.t / "runs"), planned={"built": {"level": "main", "plan step or ledger row": "N-3", "owner": "A"}})
        with quiet():
            g.check("built", lambda log: (True, "it ran"))
            g.check("new/tool", lambda log: (False, "the tool broke", None, "tool"))
            g.check("new/env", lambda log: (False, "the drive is full", None, "env"))
            g.check("new/boom", lambda log: 1 / 0)
            g.owe("new/owed", "the tablet is away")
        self.assertEqual(self.states(g), {"built": "fail", "new/tool": "tool", "new/env": "env", "new/boom": "tool", "new/owed": "owed"})

    def landed(self, g, commit, args):
        with quiet():
            gate.keep_quarantine(g, commit, args)

    def test_the_pass_is_kept_after_a_move(self):
        args = gate_args(self.t / "runs")
        g = new_gate(args)
        with quiet():
            g.check("pace/new-check", lambda log: (True, "ok"))
            g.check("pace/failing-check", lambda log: (False, "bad"))
            g.check("rust", lambda log: (True, "468 passed"))
        self.landed(g, "1" * 40, args)
        self.landed(g, "1" * 40, args)
        counts = quarantine.read_counters(self.t / "runs" / gate.QUARANTINE_FILE)
        self.assertEqual({k: v["passes"] for k, v in counts.items()}, {("check", "pace/new-check"): 1, ("check", "pace/failing-check"): 0},
                         "one pass for one main commit, a failure none, an existing check nothing")
        self.landed(g, "2" * 40, args)
        self.assertEqual(quarantine.read_counters(self.t / "runs" / gate.QUARANTINE_FILE)[("check", "pace/new-check")]["passes"], 2)
        self.assertEqual(quarantine.read_counters(self.t / "runs" / gate.QUARANTINE_FILE)[("check", "pace/failing-check")]["passes"], 0)

    def test_the_commands(self):
        args = types.SimpleNamespace(work=str(self.t), pace_fault=False, live_row=["new row, with a comma"])
        self.assertEqual(gate.pace_quarantine_args(args), ["--quarantine", str(self.t / gate.QUARANTINE_FILE),
                                                           "--live-row", "new row, with a comma"])
        g = types.SimpleNamespace(bin=self.t / "bins", work=self.t / "run", args=args, paks_text=lambda: "")
        parsed = pace.parser().parse_args(gate.pace_pc_command(g, self.t / "run" / "pace-pc")[5:])
        self.assertEqual((parsed.quarantine, parsed.live_row), (str(self.t / gate.QUARANTINE_FILE), ["new row, with a comma"]))
        digest = pace.parser().parse_args(["digest", str(self.t), "--quarantine", "q.tsv", "--live-row", "a", "--live-row", "b"])
        self.assertEqual((digest.quarantine, digest.live_row), ("q.tsv", ["a", "b"]))
        self.assertEqual(gate.parser().parse_args(["--main", "--live-row", "x"]).live_row, ["x"])

    def test_the_keep_command_gets_the_counters(self):
        work = self.t / "pace-pc"
        write_run(work)
        saved = pace.judge
        calls = []
        real_run = gate.subprocess.run
        try:
            pace.judge = with_new_row("pass")
            with quiet():
                pace.digest(work, None, pace.reading_of())
            args = gate_args(self.t / "runs")
            g = types.SimpleNamespace(results=[{"check": gate.PACE_PC_CHECK, "passed": True, "state": "pass"}],
                                      pace_work=[(gate.PACE_PC_CHECK, work)], measure_faulted=set())

            def fake(cmd, **kw):
                calls.append(cmd)
                return real_run(cmd, **kw)
            gate.subprocess.run = fake
            with quiet():
                gate.keep_pace(g, "c" * 40, args)
            self.assertIn("--quarantine", calls[0])
            self.assertEqual(quarantine.read_counters(self.t / "runs" / gate.QUARANTINE_FILE)[("row", NEW_ROW)]["passes"], 1)
        finally:
            gate.subprocess.run = real_run
            pace.judge = saved


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("quarantine_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
