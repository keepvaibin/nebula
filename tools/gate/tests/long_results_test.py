"""long_results_test.py: what follows from a long run's results (tools/gate/long_results.py, package B15, design d2), with fakes.

python -X utf8 -I tools/gate/tests/long_results_test.py

A scratch gate-runs folder (history.tsv, run folders with result.json and logs), a scratch live folder (repairs.tsv, tool-faults.tsv) and
a tracer that is a function. Nothing here starts the game or touches D:/NebulaWork/plan.
- each kind leads to the right action with the right fields (fail, stress, worse, tool, env, a part that did not run, planned, a row
  that only says another check did not run, the old measure row);
- halving finds a planted landing among eight in three runs, for each of the eight places;
- a check on the never-traced list and a check that is not a suite are not traced, and say so with the landings in between;
- the 15-minute run and the 90-minute night stop the tracing;
- a landed repair is confirmed by a passing run and a failing run opens a new repair that says "seen again after R-nnn";
- new pictures with no picture check failed are landed, with one failed they are not;
- how-to-run.txt is the gate's own command for the check with the failed tests alone and `<bin>` for the build's programs;
- the report, nightly-report.md; the run's line in history.tsv (and its columns, old files read as they are); the lines of level move;
- the step run twice changes nothing the second time; --dry changes nothing;
- plan.py: branch_item, a repair's card prints how-to-run.txt, `confirmed` is a state, the newest long run's line.
Exit 0 when every case holds.
"""
import contextlib
import hashlib
import io
import json
import os
import pathlib
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent.parent / "plan"))
import gate  # noqa: E402
import live as lv  # noqa: E402
import long_results as lr  # noqa: E402
import plan  # noqa: E402


def sha(n):
    return ("%09x" % (0x100000000 + n * 7919)) + "0" * 31


class FakeActions(lr.Actions):
    """The real repairs and faults of live.py in a scratch folder (what plan.py repair add does, without the process)."""

    def repair_add(self, kind, check, renderer, step, commit, evidence, note):
        row, _ = lv.add_repair(self.live, "nightly", kind, check, renderer, step, commit, str(evidence), "-", note)
        return row["id"]

    def branch_item(self, branch):
        return plan.branch_item(branch)


class World(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp())
        self.work = self.tmp / "gate-runs"
        self.live = self.tmp / "native"
        self.live.mkdir()
        self.work.mkdir()
        self.queue = self.tmp / "queue.tsv"
        self.lines = []
        self.played = []
        self.landed = []

    # --- the record ---
    def history(self, level, run, commit, started, result="pass", checks="", only="-", covers="-", moved="-", seconds=100):
        path = self.work / "history.tsv"
        new = not path.exists()
        cells = [started, run, level, commit, result, str(seconds), "0", "0", "0", "0", "0", only, checks, covers, moved]
        with open(path, "a", encoding="utf-8") as f:
            if new:
                f.write("\t".join(gate.HISTORY_COLUMNS) + "\n")
            f.write("\t".join(cells) + "\n")

    def move(self, n, started, branches):
        self.history("move", "land-%d" % n, sha(n)[:9], started, covers=",".join(branches), moved=sha(n)[:9])

    def long_run(self, name, commit_n, started, rows, seconds=7200, parts=None):
        """A long run's folder and its history line. rows: [(check, state, detail, records, log text or None)]."""
        folder = self.work / name
        folder.mkdir()
        out = []
        for check, state, detail, records, log in rows:
            logfile = None
            if log is not None:
                logfile = folder / (check.replace("/", "-") + ".log")
                logfile.write_text(log, encoding="utf-8")
            out.append({"check": check, "state": state, "passed": state in ("pass", "known", "planned"), "seconds": 60.0, "detail": detail,
                        "log": str(logfile) if logfile else None, "records": records})
        verdict = "fail" if any(r["state"] in ("fail", "tool", "env") for r in out) else "pass"
        (folder / "result.json").write_text(json.dumps({"gate": 2, "mode": "long", "commit": sha(commit_n)[:9], "run": name, "started": started,
                                                        "seconds": seconds, "verdict": verdict, "checks": out}), encoding="utf-8")
        if parts is not None:
            (folder / "parts.json").write_text(json.dumps(parts), encoding="utf-8")
        self.history("long", name, sha(commit_n)[:9], started, result=verdict,
                     checks=" ".join("%s=%s/%d" % (r["check"], r["state"], r["seconds"]) for r in out), seconds=seconds)
        return folder

    def passing(self, name, commit_n, started, *checks):
        return self.long_run(name, commit_n, started, [(c, "pass", "ok", [], None) for c in checks])

    def ctx(self, play=None, dry=False, land=None):
        actions = FakeActions(self.live)
        return lr.Context(self.work, actions, queue_file=self.queue, play=play, land_pictures=land, dry=dry, say=self.lines.append,
                          now=__import__("datetime").datetime(2026, 11, 22, 6, 0, 0))

    def tsv(self, name):
        path = self.live / name
        return path.read_text(encoding="utf-8") if path.exists() else ""

    def snapshot(self):
        files = {}
        for base in (self.work, self.live):
            for p in sorted(base.rglob("*")):
                if p.is_file():
                    files[str(p)] = hashlib.sha256(p.read_bytes()).hexdigest()
        return files


TOUR_LOG = ("$ C:\\Python\\python.exe -X utf8 -I D:\\Tree\\tools\\lumacore\\luma_test.py run D:\\runs\\x\\tour\\suite.json --bin D:\\build\\bins "
            "--work D:\\runs\\x\\runs-tour-d3d12 --who gate --lock D:\\runs\\x\\harness-tour-d3d12.lock --commit abc --renderer d3d12 "
            "--test 001-A-1 --test 002-B-1\n001-A-1 d3d12 fail boom\n")


class Kinds(World):
    def test_each_kind_leads_to_the_right_action(self):
        self.passing("20261121-100000-aaa-long", 1, "2026-11-21T10:00:00", "tour/d3d12", "stress/tour/d3d12/2", "repo/code")
        self.move(2, "2026-11-21T12:00:00", ["native/R1.1"])
        fail_rec = [{"test": "002-B-1", "kind": "fail", "message": "the stage did not load", "counts": True, "note": "", "final": False}]
        run_folder = self.long_run("20261122-050000-bbb-long", 2, "2026-11-22T05:00:00", [
            ("repo/code", "pass", "ok", [], None),
            ("tour/d3d12", "fail", "1 of 2 pass", fail_rec, TOUR_LOG),
            ("stress/tour/d3d12/2", "stress", "stress", [], None),
            ("measure/step/pc", "worse", "worse: render_ms", [], None),
            ("unit/windows", "tool", "the runner raised", [], None),
            ("tour-RMGP01/vulkan", "env", "the drive is full", [], None),
            ("part/tours", "env", "not run: the gate lock is held", [], None),
            ("tour-asan/d3d12", "fail", "not run: the sanitizer build failed", [], None),
            ("measure/route", "fail", "vulkan worse: memory_mb", [], None),
            ("fuzz", "planned", "not built yet", [], None)])
        out = lr.results(lr.Run(run_folder), self.ctx())
        repairs = lv.read_repairs(self.live)
        by = {r["check"]: r for r in repairs}
        self.assertEqual(set(by), {"tour/d3d12", "stress/tour/d3d12/2", "measure/step/pc"}, "tool, env, planned and the old row open no repair")
        fail = by["tour/d3d12"]
        self.assertEqual((fail["kind"], fail["source"], fail["renderer"], fail["step"], fail["state"]), ("fail", "nightly", "d3d12", "R1.1", "open"))
        self.assertEqual(fail["commit"], sha(2)[:9], "traced to the commit main moved to")
        self.assertIn("Traced to step R1.1", fail["note"])
        self.assertEqual(pathlib.Path(fail["evidence"]), run_folder / "evidence" / "tour-d3d12")
        self.assertEqual((by["stress/tour/d3d12/2"]["kind"], by["measure/step/pc"]["kind"]), ("stress", "worse"))
        faults = lv.read_faults(self.live)
        self.assertEqual({(f["check"], f["kind"], f["step"]) for f in faults},
                         {("unit/windows", "TOOL", "nightly"), ("tour-RMGP01/vulkan", "ENV", "nightly"), ("part/tours", "ENV", "nightly")})
        self.assertTrue(all(f["run_folder"] == str(run_folder) for f in faults))
        self.assertEqual(sorted(out["opened"]), sorted(r["id"] for r in repairs))
        text = "\n".join(self.lines)
        self.assertIn("no action: not run: the sanitizer build failed", text)
        self.assertIn("no speed part any more", text)

    def test_how_to_run_is_the_gates_own_command(self):
        text = lr.how_to_run(TOUR_LOG, ["002-B-1"])
        command = text.strip().splitlines()[-1]
        self.assertIn("luma_test.py run D:\\runs\\x\\tour\\suite.json --bin <bin> --work <new-empty-folder> --who gate --lock", command)
        self.assertIn("--renderer d3d12 --test 002-B-1", command)
        self.assertNotIn("001-A-1", command, "only the failed test")
        self.assertNotIn("D:\\build\\bins", command)
        self.assertIsNone(lr.how_to_run("$ cargo test\nok\n", ["x"]), "a check that is not a runner's suite has none")
        self.passing("20261121-100000-aaa-long", 1, "2026-11-21T10:00:00", "tour/d3d12")
        self.move(2, "2026-11-21T12:00:00", ["x/y"])
        rec = [{"test": "002-B-1", "kind": "fail", "message": "m", "counts": True, "note": "", "final": False}]
        folder = self.long_run("20261122-050000-bbb-long", 2, "2026-11-22T05:00:00", [("tour/d3d12", "fail", "x", rec, TOUR_LOG)])
        lr.results(lr.Run(folder), self.ctx())
        written = (folder / "evidence" / "tour-d3d12" / "how-to-run.txt").read_text(encoding="utf-8")
        self.assertEqual(written, text)


class Tracing(World):
    def moves(self, n):
        return [{"time": "2026-11-21T%02d:00:00" % (10 + i), "commit": sha(100 + i)[:9], "branches": ["native/S%d.1" % i]} for i in range(n)]

    def test_halving_finds_a_planted_landing_among_eight_in_three_runs(self):
        for planted in range(8):
            cands, played = self.moves(8), []

            def play(m, planted=planted):
                played.append(m["commit"])
                return ("fail" if cands.index(m) >= planted else "pass"), 60.0
            found = lr.halve(cands, play, say=lambda t: None)
            self.assertIs(found.landing, cands[planted], "planted at %d" % planted)
            self.assertLessEqual(len(played), 3, "planted at %d took %d runs" % (planted, len(played)))
            self.assertEqual(found.runs, len(played))

    def test_the_only_landing_is_named_without_a_run(self):
        t = lr.trace.__wrapped__ if hasattr(lr.trace, "__wrapped__") else lr.trace
        self.passing("20261121-100000-aaa-long", 1, "2026-11-21T10:00:00", "tour/d3d12")
        self.move(2, "2026-11-21T12:00:00", ["native/A1.1"])
        rows = lr.read_history(self.work)
        found = t("tour/d3d12", sha(2)[:9], "2026-11-22T05:00:00", rows, lr.moves_of(rows), lambda m: self.fail("a run was made"))
        self.assertEqual(found.landing["branches"], ["native/A1.1"])

    def test_never_traced_and_not_a_suite(self):
        for check in ("tour-asan/d3d12", "build/windows-asan", "unit/game-asan", "coverage", "soak", "stress/tour/d3d12/2", "tour/tablet", "replays/tablet",
                      "measure/step/pc", "github/Linux", "pictures/tablet"):
            found = lr.trace(check, sha(2)[:9], "x", [], [], lambda m: self.fail("a run was made for %s" % check))
            self.assertIsNone(found.landing)
            self.assertIn("never traced", found.why, check)
        for check in ("repo/code", "unit/windows", "replays/d3d12", "pictures/tour/d3d12", "validate/d3d12"):
            found = lr.trace(check, sha(2)[:9], "x", [], [], lambda m: self.fail("a run was made for %s" % check))
            self.assertIn("not one suite", found.why, check)

    def test_a_not_traced_repair_lists_the_landings_in_between(self):
        self.passing("20261121-100000-aaa-long", 1, "2026-11-21T10:00:00", "tour/d3d12")
        for n, br in ((2, ["native/A1.1"]), (3, ["tools/x"]), (4, ["native/repair-007"])):
            self.move(n, "2026-11-21T%02d:00:00" % (10 + n), br)
        rec = [{"test": "002-B-1", "kind": "fail", "message": "m", "counts": True, "note": "", "final": False}]
        folder = self.long_run("20261122-050000-bbb-long", 4, "2026-11-22T05:00:00", [("tour/d3d12", "fail", "x", rec, TOUR_LOG)])
        lr.results(lr.Run(folder), self.ctx(play=lambda *a: ("unknown", 0.0)))
        note = lv.read_repairs(self.live)[0]["note"]
        self.assertIn("Not traced: a traced run could not be made", note)
        for word in ("step A1.1", "a tools landing: tools/x", "repair R-007"):
            self.assertIn(word, note)

    def test_a_check_that_never_passed_in_a_long_run_is_not_traced(self):
        found = lr.trace("tour/d3d12", sha(2)[:9], "2026-11-22T05:00:00", [], [], lambda m: self.fail("a run"))
        self.assertIn("no earlier long run", found.why)

    def test_a_run_over_fifteen_minutes_and_the_nights_ninety_stop_the_tracing(self):
        cands = self.moves(8)
        found = lr.halve(cands, lambda m: ("pass", 16 * 60.0), say=lambda t: None)
        self.assertIsNone(found.landing)
        self.assertIn("took over 15 minutes", found.why)
        calls = []

        def slow(m):
            calls.append(m)
            return "pass", 40 * 60.0
        found = lr.halve(cands, slow, say=lambda t: None)
        self.assertIsNone(found.landing)
        self.assertLessEqual(len(calls), 3)
        found = lr.halve(self.moves(8), lambda m: ("pass", 10 * 60.0), spent=90 * 60.0, say=lambda t: None)
        self.assertIsNone(found.landing)
        self.assertIn("90 minutes", found.why)
        self.assertEqual(found.runs, 0, "no run is made once the night's 90 minutes are used")

    def test_the_commits_looked_at_are_the_moves_after_the_last_pass(self):
        self.passing("20261121-080000-aaa-long", 1, "2026-11-21T08:00:00", "tour/d3d12")
        self.long_run("20261121-120000-bbb-long", 3, "2026-11-21T12:00:00", [("tour/d3d12", "fail", "x", [], None)])
        for n in (2, 3, 4, 5):
            self.move(n, "2026-11-21T%02d:30:00" % (8 + n), ["native/S%d.1" % n])
        rows = lr.read_history(self.work)
        cands, why = lr.candidates(sha(5)[:9], "2026-11-22T05:00:00", "tour/d3d12", rows, lr.moves_of(rows))
        self.assertEqual([m["commit"] for m in cands], [sha(n)[:9] for n in (2, 3, 4, 5)], "the run of 12:00 failed: it is not a pass")


class Follow(World):
    def test_a_landed_repair_is_confirmed_and_a_failing_run_opens_another(self):
        lv.add_repair(self.live, "nightly", "fail", "tour/d3d12", "d3d12", "S1.1", "abc", "ev", "-", "n")
        lv.set_repair(self.live, "R-001", "landed", "def")
        folder = self.passing("20261122-050000-bbb-long", 2, "2026-11-22T05:00:00", "tour/d3d12")
        out = lr.results(lr.Run(folder), self.ctx())
        self.assertEqual(out["confirmed"], ["R-001"])
        self.assertEqual(lv.read_repairs(self.live)[0]["state"], "confirmed")
        # a second long run that fails the check again: a new repair that says so
        lv.add_repair(self.live, "nightly", "fail", "soak", "-", "-", "-", "ev", "-", "n")
        lv.set_repair(self.live, "R-002", "landed", "def")
        self.passing("20261121-100000-aaa-long", 1, "2026-11-21T10:00:00", "tour/d3d12")
        rec = [{"test": "002-B-1", "kind": "fail", "message": "m", "counts": True, "note": "", "final": False}]
        failing = self.long_run("20261123-050000-ccc-long", 3, "2026-11-23T05:00:00", [("soak", "fail", "d3d11 FAIL", [], None),
                                                                                       ("tour/d3d12", "fail", "x", rec, TOUR_LOG)])
        lr.results(lr.Run(failing), self.ctx())
        rows = {r["id"]: r for r in lv.read_repairs(self.live)}
        self.assertEqual(rows["R-002"]["state"], "landed", "a failing check does not confirm")
        again = [r for r in rows.values() if r["check"] == "soak" and r["state"] == "open"]
        self.assertEqual(len(again), 1)
        self.assertIn("Seen again after R-002", again[0]["note"])
        self.assertEqual([r["check"] for r in rows.values() if r["state"] == "confirmed"], ["tour/d3d12"], "only R-001 is confirmed")

    def test_new_pictures_are_landed_when_no_picture_check_failed(self):
        landed = []
        rows = [("pictures/tour/d3d12", "new", "3 new", [], None), ("tour/d3d12", "pass", "ok", [], None)]
        folder = self.long_run("20261122-050000-bbb-long", 2, "2026-11-22T05:00:00", rows)
        out = lr.results(lr.Run(folder), self.ctx(land=lambda run: landed.append(run.name) or "landed on nightly/pictures-20261122 (abc)"))
        self.assertEqual(landed, ["20261122-050000-bbb-long"])
        self.assertIn("landed on nightly/pictures-20261122", out["pictures"])
        self.setUp()
        landed.clear()
        rows = [("pictures/tour/d3d12", "new", "3 new", [], None), ("pictures/tour/vulkan", "fail", "differs", [], None)]
        folder = self.long_run("20261122-050000-bbb-long", 2, "2026-11-22T05:00:00", rows)
        out = lr.results(lr.Run(folder), self.ctx(land=lambda run: landed.append(1)))
        self.assertEqual(landed, [], "a picture that differs: the new ones are not landed")
        self.assertIn("a picture check failed", out["pictures"])
        self.assertEqual(lv.read_repairs(self.live)[0]["check"], "pictures/tour/vulkan", "and the picture that differs is a fail")

    def test_the_report_and_the_long_runs_line(self):
        parts = [{"part": "tours 1/2", "waited": 0.0, "held": 330.0}, {"part": "tours 2/2", "waited": 120.0, "held": 360.0},
                 {"part": "stress", "waited": 0.0, "held": 3000.0}, {"part": "tablet 1/8", "waited": 0.0, "held": 400.0, "beside": True}]
        folder = self.long_run("20261122-050000-bbb-long", 2, "2026-11-22T05:00:00", [("tour/d3d12", "pass", "ok", [], None),
                                                                                    ("soak", "fail", "d3d11 FAIL", [], None)], parts=parts, seconds=6000)
        lr.results(lr.Run(folder), self.ctx())
        report = (folder / "nightly-report.md").read_text(encoding="utf-8")
        self.assertIn(sha(2)[:9], report)
        self.assertIn("tours: 2 pieces, 11.5 minutes holding the machine, the longest piece 6.0 minutes", report)
        self.assertIn("stress: 1 piece, 50.0 minutes", report)
        self.assertIn("OVER 45 MINUTES", report)
        self.assertEqual(report.count("OVER 45 MINUTES"), 1, "only the part over 45 minutes is named")
        self.assertIn("The longest a landing waited for this run: 50.0 minutes (stress)", report, "the tablet's beside piece holds no landing")
        self.assertIn("Repairs opened: R-001", report)
        # the run's own line in history.tsv: start, commit, minutes, verdict and each check's kind
        row = lr.long_runs(lr.read_history(self.work))[-1]
        self.assertEqual((row["started"], row["commit"], row["result"], row["seconds"]), ("2026-11-22T05:00:00", sha(2)[:9], "fail", "6000"))
        self.assertEqual(lr.checks_of(row), {"tour/d3d12": ("pass", 60), "soak": ("fail", 60)})

    def test_the_history_record_gains_columns_and_old_files_are_read(self):
        out = {"started": "2026-11-22T05:00:00", "run": "r-land", "mode": "main", "commit": "abc", "dirty": False, "verdict": "pass", "seconds": 5.0,
               "counts": {"pass": 1, "known": 0, "planned": 0, "owed": 0, "fail": 0, "worse": 0}, "only": None, "checks": [{"check": "x", "state": "pass", "seconds": 1}]}
        path = self.work / "h2.tsv"
        gate.append_history(path, out, "native/A1.1, native/A1.2")
        gate.append_move(path, "r-land", "def4567890", "native/A1.1, native/A1.2")
        lines = path.read_text(encoding="utf-8").splitlines()
        self.assertEqual(lines[0].split("\t"), list(lr.HISTORY_COLUMNS))
        self.assertEqual(list(lr.HISTORY_COLUMNS), list(gate.HISTORY_COLUMNS), "the reader and the writer name the same columns")
        self.assertEqual(lines[0].split("\t")[-2:], ["covers", "moved"])
        self.assertTrue(all(len(l.split("\t")) == len(gate.HISTORY_COLUMNS) for l in lines))
        old = self.work / "history.tsv"
        old.write_text("started\trun\tlevel\tcommit\tresult\tseconds\tpass\tknown\tplanned\towed\tfailed\tonly\tchecks\n"
                       "2026-11-17T16:43:31\tr\tmain\te76e79c4f\tpass\t927\t10\t5\t0\t4\t0\t-\tbuild/windows=pass/60\n", encoding="utf-8")
        got = lr.read_history(self.work)
        self.assertEqual((got[0]["commit"], got[0]["covers"], got[0]["moved"]), ("e76e79c4f", "", ""))
        # the moves: history lines first, then the landing queue's rows for the landings before them
        self.history("move", "land-9", "9" * 9, "2026-11-22T01:00:00", covers="native/B1.1", moved="9" * 9)
        self.queue.write_text("time\tbranch\tagent\tstate\tcommit\trun folder\tnote\talso\n"
                              "2026-11-21T09:00:00\ttest/x\t1\tlanded\tabc\tD:\\runs\\a\tmain moved to 123456789\t\n"
                              "2026-11-21T09:00:00\ttest/y\t1\tlanded\tabd\tD:\\runs\\a\tmain moved to 123456789\t\n"
                              "2026-11-22T01:00:00\tnative/B1.1\t1\tlanded\tabe\tD:\\runs\\b\tmain moved to 999999999\t\n"
                              "2026-11-22T02:00:00\tnative/B2.1\t1\twaiting\t\t\t\t\n", encoding="utf-8")
        moves = lr.moves_of(lr.read_history(self.work), self.queue)
        self.assertEqual([(m["commit"], m["branches"]) for m in moves], [("123456789", ["test/x", "test/y"]), ("9" * 9, ["native/B1.1"])],
                         "the same landing is not counted twice, a waiting row is no landing")

    def test_run_twice_changes_nothing_the_second_time(self):
        self.passing("20261121-100000-aaa-long", 1, "2026-11-21T10:00:00", "tour/d3d12")
        self.move(2, "2026-11-21T12:00:00", ["native/A1.1"])
        rec = [{"test": "002-B-1", "kind": "fail", "message": "m", "counts": True, "note": "", "final": False}]
        folder = self.long_run("20261122-050000-bbb-long", 2, "2026-11-22T05:00:00", [
            ("tour/d3d12", "fail", "x", rec, TOUR_LOG), ("unit/windows", "tool", "raised", [], None), ("stress/e", "stress", "s", [], None)])
        lr.results(lr.Run(folder), self.ctx())
        first = self.snapshot()
        self.assertTrue(first)
        again = []
        lr.results(lr.Run(folder), lr.Context(self.work, FakeActions(self.live), queue_file=self.queue, say=again.append))
        self.assertEqual(self.snapshot(), first)
        self.assertIn("done already", again[0])
        # the same when the first run left no long-results.json (it broke before the end): each action finds its own record
        (folder / "long-results.json").unlink()
        (folder / "nightly-report.md").unlink()
        lr.results(lr.Run(folder), self.ctx())
        repairs = lv.read_repairs(self.live)
        self.assertEqual([(r["check"], r["seen"]) for r in repairs], [("tour/d3d12", "1"), ("stress/e", "1")], "no repair counted twice")
        self.assertEqual(len(lv.read_faults(self.live)), 1)

    def test_dry_changes_nothing(self):
        self.passing("20261121-100000-aaa-long", 1, "2026-11-21T10:00:00", "tour/d3d12")
        self.move(2, "2026-11-21T12:00:00", ["native/A1.1"])
        lv.add_repair(self.live, "nightly", "fail", "soak", "-", "-", "-", "ev", "-", "n")
        lv.set_repair(self.live, "R-001", "landed", "x")
        rows = [("tour/d3d12", "fail", "x", [{"test": "002-B-1", "kind": "fail", "message": "m", "counts": True, "note": "", "final": False}], TOUR_LOG),
                ("soak", "pass", "ok", [], None), ("unit/windows", "tool", "raised", [], None), ("pictures/tour/d3d12", "new", "n", [], None)]
        folder = self.long_run("20261122-050000-bbb-long", 2, "2026-11-22T05:00:00", rows)
        before = self.snapshot()
        out = lr.results(lr.Run(folder), self.ctx(dry=True, land=lambda run: self.fail("landed in a dry run")))
        self.assertEqual(self.snapshot(), before)
        text = "\n".join(self.lines)
        self.assertIn("would open a repair of kind fail", text)
        self.assertIn("would write one TOOL line", text)
        self.assertIn("would be confirmed", text)
        self.assertIn("would land the approval", out["pictures"])
        self.assertFalse((folder / "evidence").exists())


class Plan(World):
    def test_branch_item(self):
        self.assertEqual(plan.branch_item("native/S1.2"), ("step", "S1.2"))
        self.assertEqual(plan.branch_item("native/repair-007"), ("repair", "R-007"))
        self.assertIsNone(plan.branch_item("test/long-results"))
        self.assertIsNone(plan.branch_item("native/repair-x"))
        for item in ("S1.2", "R-007"):
            self.assertEqual(plan.branch_item(plan.item_branch(item))[1], item, "the two functions are each other's inverse")

    def test_a_repairs_card_prints_how_to_run(self):
        evidence = self.tmp / "ev"
        evidence.mkdir()
        (evidence / "how-to-run.txt").write_text(lr.how_to_run(TOUR_LOG, ["002-B-1"]), encoding="utf-8")
        row, _ = lv.add_repair(self.live, "nightly", "fail", "tour/d3d12", "d3d12", "S1.1", "abc", str(evidence), "-", "n")
        out = io.StringIO()
        a = type("A", (), {"tree": str(self.tmp), "live": str(self.live), "agent": "1"})()
        with contextlib.redirect_stdout(out):
            plan.print_repair(a, row)
        text = out.getvalue()
        self.assertIn("Run the failed test alone", text)
        self.assertIn("--bin <bin>", text)
        self.assertIn("--test 002-B-1", text)

    def test_a_test_live_folder_never_shows_the_real_gates_record(self):
        """The landing of this package failed once because `plan.py status` of a test's live folder read the real long lock: only the
        real live folder (plan.LIVE_REAL, which a test cannot replace) shows the record."""
        from unittest import mock
        self.assertEqual(plan.LIVE_REAL, "D:/NebulaWork/plan/native")
        with mock.patch.object(plan, "LIVE", str(self.tmp)):
            self.assertNotEqual(plan.live_dir(None).as_posix(), plan.LIVE_REAL)
        self.assertNotIn("newest long run", plan.status_text(pathlib.Path(__file__).resolve().parents[3], live=str(self.live)))

    def test_confirmed_is_a_state_that_sorts_last(self):
        self.assertIn("confirmed", lv.REPAIR_STATES)
        rows = [{"id": "R-001", "state": "confirmed", "kind": "fail"}, {"id": "R-002", "state": "landed", "kind": "fail"},
                {"id": "R-003", "state": "open", "kind": "fail"}]
        self.assertEqual([r["id"] for r in lv.repair_order(rows)], ["R-003", "R-002", "R-001"])

    def test_the_newest_long_run_line(self):
        folder = self.passing("20261122-050000-bbb-long", 2, "2026-11-22T05:00:00", "tour/d3d12")
        self.assertIn("results not read yet", lr.newest_long(self.work, lock=self.tmp / "none.lock"))
        (folder / "long-results.json").write_text(json.dumps({"opened": ["R-004", "R-005"], "done": True}), encoding="utf-8")
        line = lr.newest_long(self.work, lock=self.tmp / "none.lock")
        self.assertIn("2026-11-22 05:00", line)
        self.assertIn(sha(2)[:9], line)
        self.assertIn("repairs opened: R-004, R-005", line)
        self.assertNotIn("running", line)
        lock = self.tmp / "long.lock"
        lock.mkdir()
        (lock / "holder.txt").write_text("long gate of Nebula-long@abc pid %d; covers x 2026-11-22 06:30:12" % os.getpid(), encoding="utf-8")
        self.assertTrue(lr.newest_long(self.work, alive=lambda pid: True, lock=lock).startswith("running since 06:30; "))
        self.assertNotIn("running", lr.newest_long(self.work, alive=lambda pid: False, lock=lock))
        self.assertEqual(lr.newest_long(self.tmp / "empty", lock=self.tmp / "none.lock"), "none yet")

    def test_the_real_repair_add_command_is_called_and_its_last_line_is_the_id(self):
        real = lr.Actions(self.live)
        rid = real.repair_add("fail", "tour/d3d12", "d3d12", "S1.1", "abc", self.tmp / "ev", "a note")
        self.assertEqual(rid, "R-001")
        self.assertEqual(real.repair_add("fail", "tour/d3d12", "d3d12", "S1.1", "abc", self.tmp / "ev2", "a note"), "R-001", "counted, not written twice")
        self.assertEqual(lv.read_repairs(self.live)[0]["seen"], "2")


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=0).result
    print("long_results_test: %s" % ("pass (%d tests)" % result.testsRun if result.wasSuccessful() else "FAILED"))
    sys.exit(0 if result.wasSuccessful() else 1)
