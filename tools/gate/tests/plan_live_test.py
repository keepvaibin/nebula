"""plan_live_test.py: tools/plan/live.py, the live state the agents and the progress page share (package T13, items 5 to 7):
the lock folder, the state, claims and their staleness, the tool-fault record and its counts per signature, the landings.
A temporary folder each: no game, no build, no network, nothing of D:/NebulaWork.

    python -X utf8 -I tools/gate/tests/plan_live_test.py [--work <folder>]
"""
import datetime
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(TREE / "tools" / "plan"))
import live as lv  # noqa: E402

NOW = datetime.datetime(2026, 11, 20, 12, 0, 0)


class LiveTest(unittest.TestCase):
    def setUp(self):
        self.live = pathlib.Path(tempfile.mkdtemp(prefix="plan-live-")) / "native"
        self.addCleanup(shutil.rmtree, self.live.parent, True)


class LockTest(LiveTest):
    def test_the_lock_folder_is_made_and_removed(self):
        with lv.locked(self.live):
            self.assertTrue((self.live / "lock").is_dir())
        self.assertFalse((self.live / "lock").exists())
        self.assertTrue(self.live.is_dir(), "the live folder is made when it is not there")

    def test_the_lock_is_given_up_when_the_block_raises(self):
        with self.assertRaises(ValueError):
            with lv.locked(self.live):
                raise ValueError("x")
        self.assertFalse((self.live / "lock").exists())

    def test_a_held_lock_makes_the_next_wait_and_then_report_busy(self):
        with lv.locked(self.live):
            started = time.time()
            with self.assertRaises(lv.Busy):
                with lv.locked(self.live, timeout=0.3):
                    pass
            self.assertGreaterEqual(time.time() - started, 0.25)

    def test_a_lock_left_by_a_dead_process_is_removed_when_old(self):
        (self.live / "lock").mkdir(parents=True)
        old = time.time() - 3600
        os.utime(self.live / "lock", (old, old))
        with lv.locked(self.live, timeout=1):
            pass
        self.assertFalse((self.live / "lock").exists())

    def test_a_young_lock_is_not_taken(self):
        (self.live / "lock").mkdir(parents=True)
        with self.assertRaises(lv.Busy):
            with lv.locked(self.live, timeout=0.2):
                pass

    def test_threads_appending_at_once_lose_nothing(self):
        def work(i):
            for j in range(5):
                lv.append_fault(self.live, 1, "S%d" % i, "TOOL", "check", "error %d-%d" % (i, j))
        threads = [threading.Thread(target=work, args=(i,)) for i in range(6)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(len(lv.read_faults(self.live)), 30)

    def test_two_processes_appending_at_once_lose_nothing(self):
        code = ("import sys; sys.path.insert(0, %r); import live as lv\n"
                "for j in range(6): lv.append_fault(%r, int(sys.argv[1]), 'S', 'ENV', 'c', 'e %%s-%%d' %% (sys.argv[1], j))\n"
                % (str(TREE / "tools" / "plan"), str(self.live)))
        procs = [subprocess.Popen([sys.executable, "-X", "utf8", "-I", "-c", code, str(n)], stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE) for n in (1, 2, 1, 2)]
        for p in procs:
            out, err = p.communicate(timeout=60)
            self.assertEqual(p.returncode, 0, err.decode())
        self.assertEqual(len(lv.read_faults(self.live)), 24)
        self.assertFalse((self.live / "lock").exists())


class FilesTest(LiveTest):
    def test_a_cell_never_holds_a_tab_or_a_line_break(self):
        row = lv.append_fault(self.live, 1, "S", "TOOL", "c", "line one\nline\ttwo\r\nthree")
        self.assertEqual(row["first_error"], "line one line two three")
        text = (self.live / "tool-faults.tsv").read_text(encoding="utf-8")
        self.assertEqual(len(text.splitlines()), 2)
        self.assertEqual(len(text.splitlines()[1].split("\t")), len(lv.FAULT_COLUMNS))
        self.assertEqual(lv.read_faults(self.live)[0]["first_error"], "line one line two three")

    def test_a_missing_file_has_no_rows_and_a_short_row_is_padded(self):
        self.assertEqual(lv.read_faults(self.live), [])
        self.assertEqual(lv.read_claims(self.live), [])
        self.assertEqual(lv.read_state(self.live), {})
        self.assertEqual(lv.read_landings(self.live), [])
        self.live.mkdir(parents=True)
        (self.live / "claims.tsv").write_text("package\tagent\ttime\tbranch\nG6\t2\n", encoding="utf-8")
        self.assertEqual(lv.read_claims(self.live), [{"package": "G6", "agent": "2", "time": "", "branch": ""}])

    def test_the_headers_are_the_ones_the_plan_names(self):
        lv.put_claim(self.live, "G6", 2, "native/G6.1", NOW)
        lv.append_fault(self.live, 1, "S", "TOOL", "c", "e")
        lv.set_state_entry(self.live, "A.1", "started", 1, now=NOW)
        lv.add_landing(self.live, "A.1", "abc", 5, now=NOW)
        first = lambda name: (self.live / name).read_text(encoding="utf-8").splitlines()[0]
        self.assertEqual(first("claims.tsv"), "package\tagent\ttime\tbranch")
        self.assertEqual(first("tool-faults.tsv"), "time\tagent\tstep\tkind\tcheck\ttest\tfirst_error\trun_folder\tcould_not")
        self.assertEqual(first("state.tsv"), "step\tstate\tagent\treason\ttime")
        self.assertEqual(first("landings.tsv"), "step\tcommit\tfinished\tseconds\tagent\tmain\tfigures")

    def test_state_entries(self):
        lv.set_state_entry(self.live, "A.1", "started", 2, now=NOW)
        lv.set_state_entry(self.live, "B.1", "blocked", 1, "the tool broke", NOW)
        state = lv.read_state(self.live)
        self.assertEqual((state["A.1"]["agent"], state["A.1"]["time"]), ("2", "2026-11-20T12:00:00"))
        self.assertEqual(state["B.1"]["reason"], "the tool broke")
        lv.set_state_entry(self.live, "A.1", "landed")            # anything but started and blocked takes the step out
        lv.set_state_entry(self.live, "NEVER", "open")
        self.assertEqual(list(lv.read_state(self.live)), ["B.1"])

    def test_a_state_row_with_another_state_is_ignored(self):
        self.live.mkdir(parents=True)
        (self.live / "state.tsv").write_text("step\tstate\tagent\treason\ttime\nA.1\tdone\t1\t\t\nB.1\tstarted\t1\t\t\n", encoding="utf-8")
        self.assertEqual(list(lv.read_state(self.live)), ["B.1"])

    def test_landings(self):
        lv.add_landing(self.live, "A.1", "abc123", 61.7, 2, NOW)
        self.assertEqual(lv.read_landings(self.live), [{"step": "A.1", "commit": "abc123", "finished": "2026-11-20T12:00:00",
                                                         "seconds": "61", "agent": "2", "main": "-", "figures": "-"}])

    def test_a_landing_is_listed_once_for_each_step_and_main_is_filled_in_when_it_becomes_known(self):
        self.assertTrue(lv.add_landing(self.live, "A.1", "abc123", 61, 1, NOW))
        self.assertFalse(lv.add_landing(self.live, "A.1", "zzz999", 5, 1, NOW), "a second line for the step is not made")
        rows = lv.read_landings(self.live)
        self.assertEqual([(r["step"], r["commit"], r["main"]) for r in rows], [("A.1", "abc123", "-")])
        lv.add_landing(self.live, "A.1", "abc123", 5, 1, NOW, main="0123456789")
        self.assertEqual([r["main"] for r in lv.read_landings(self.live)], ["0123456789"])
        lv.add_landing(self.live, "A.1", "abc123", 5, 1, NOW, main="ffffffffff")
        self.assertEqual([r["main"] for r in lv.read_landings(self.live)], ["0123456789"], "a known main commit is kept")

    def test_a_landings_file_written_before_the_column_main_is_read_with_a_dash(self):
        self.live.mkdir(parents=True)
        (self.live / "landings.tsv").write_text("step\tcommit\tfinished\tseconds\tagent\nA.1\tabc\t2026-11-20T12:00:00\t5\t1\n", encoding="utf-8")
        self.assertEqual(lv.main_of(lv.read_landings(self.live)[0]), "-")

    def test_the_landing_record_is_one_file_for_each_agent_and_goes_when_dropped(self):
        lv.write_record(self.live, 1, "A.1", "native/A.1", "f" * 40, 4242, NOW)
        lv.write_record(self.live, 2, "G6.1", "native/G6.1", "e" * 40, 77, NOW)
        self.assertEqual(lv.read_record(self.live, 1), {"step": "A.1", "branch": "native/A.1", "tip": "f" * 40,
                                                        "time": "2026-11-20T12:00:00", "pid": "4242"})
        self.assertEqual(lv.read_record(self.live, 2)["step"], "G6.1")
        self.assertEqual(sorted(p.name for p in self.live.glob("landing-*.tsv")), ["landing-1.tsv", "landing-2.tsv"])
        lv.drop_record(self.live, 1, "OTHER")
        self.assertIsNotNone(lv.read_record(self.live, 1), "a record for another step is not dropped")
        lv.drop_record(self.live, 1, "A.1")
        self.assertIsNone(lv.read_record(self.live, 1))
        self.assertIsNotNone(lv.read_record(self.live, 2))

    def test_owed_lines_are_appended_in_order(self):
        self.assertEqual(lv.read_owed(self.live), [])
        lv.add_owed(self.live, 1, "G3.1", "film the clap frame", NOW)
        lv.add_owed(self.live, 2, "G6.2", "a Bluetooth pad", NOW)
        self.assertEqual([(o["agent"], o["step"], o["text"]) for o in lv.read_owed(self.live)],
                         [("1", "G3.1", "film the clap frame"), ("2", "G6.2", "a Bluetooth pad")])
        self.assertEqual((self.live / "owed.tsv").read_text(encoding="utf-8").splitlines()[0], "time\tagent\tstep\ttext")

    def test_no_temporary_file_is_left(self):
        lv.put_claim(self.live, "G6", 1, "b", NOW)
        lv.append_fault(self.live, 1, "S", "TOOL", "c", "e")
        self.assertEqual(sorted(p.name for p in self.live.iterdir()), ["claims.tsv", "tool-faults.tsv"])


class ClaimsTest(LiveTest):
    def test_a_claim_is_kept_renewed_and_dropped(self):
        lv.put_claim(self.live, "G6", 2, "native/G6.1", NOW)
        lv.put_claim(self.live, "G7", 1, "", NOW)
        later = NOW + datetime.timedelta(hours=5)
        lv.put_claim(self.live, "G6", 2, "", later)                  # renewed: the branch stays, the time is new
        claims = lv.read_claims(self.live)
        self.assertEqual({c["package"]: (c["agent"], c["branch"], c["time"]) for c in claims},
                         {"G6": ("2", "native/G6.1", "2026-11-20T17:00:00"), "G7": ("1", "", "2026-11-20T12:00:00")})
        self.assertEqual(lv.claim_of(claims, "G7")["agent"], "1")
        self.assertIsNone(lv.claim_of(claims, "G8"))
        lv.drop_claim(self.live, "G6")
        lv.drop_claim(self.live, "G9")
        self.assertEqual([c["package"] for c in lv.read_claims(self.live)], ["G7"])

    def claim(self, hours_ago, branch=""):
        return {"package": "G6", "agent": "2", "branch": branch, "time": lv.stamp(NOW - datetime.timedelta(hours=hours_ago))}

    def test_a_claim_with_no_commit_for_48_hours_is_stale(self):
        none = lambda b: None
        self.assertAlmostEqual(lv.claim_age_hours(self.claim(47), none, NOW), 47)
        self.assertEqual(lv.stale_packages([self.claim(47)], none, NOW), set())
        self.assertEqual(lv.stale_packages([self.claim(49)], none, NOW), {"G6"})

    def test_a_recent_commit_on_the_branch_keeps_a_claim_alive(self):
        commit = (NOW - datetime.timedelta(hours=3)).timestamp()
        claim = self.claim(100, "native/G6.1")
        self.assertAlmostEqual(lv.claim_age_hours(claim, lambda b: commit, NOW), 3, places=3)
        self.assertEqual(lv.stale_packages([claim], lambda b: commit, NOW), set())
        old = (NOW - datetime.timedelta(hours=200)).timestamp()
        self.assertAlmostEqual(lv.claim_age_hours(claim, lambda b: old, NOW), 100)       # the claim's own time is the later
        self.assertEqual(lv.stale_packages([claim], lambda b: None, NOW), {"G6"})

    def test_a_claim_with_no_readable_time_is_stale(self):
        self.assertEqual(lv.stale_packages([{"package": "G6", "agent": "2", "branch": "", "time": ""}], lambda b: None, NOW), {"G6"})


class FaultsTest(LiveTest):
    def add(self, check="repo/code", test="", error="boom", step="A.1", agent=1, kind="TOOL", when=0, **kw):
        return lv.append_fault(self.live, agent, step, kind, check, error, test=test,
                               now=NOW + datetime.timedelta(minutes=when), **kw)

    def test_a_line_has_the_nine_fields_of_the_plan(self):
        row = self.add(test="t1", run_folder="D:/runs/1", could_not="")
        self.assertEqual(list(row), ["time", "agent", "step", "kind", "check", "test", "first_error", "run_folder", "could_not"])
        self.assertEqual((row["time"], row["agent"], row["step"], row["kind"]), ("2026-11-20T12:00:00", "1", "A.1", "TOOL"))

    def test_the_same_check_test_and_first_error_are_one_signature_with_a_count(self):
        for i in range(3):
            self.add("harness/self", "t1", "result.json is missing", when=i, run_folder="run%d" % i)
        self.add("harness/self", "t2", "result.json is missing")             # another test
        self.add("harness/self", "t1", "result.json is unreadable")           # another first line
        self.add("unit", "t1", "result.json is missing")                      # another check
        groups = lv.fault_groups(lv.read_faults(self.live))
        self.assertEqual(len(groups), 4)
        top = groups[0]
        self.assertEqual((top["count"], top["check"], top["test"], top["first_error"]), (3, "harness/self", "t1", "result.json is missing"))
        self.assertEqual((top["first_seen"], top["last_seen"]), ("2026-11-20T12:00:00", "2026-11-20T12:02:00"))
        self.assertEqual(top["run_folder"], "run2", "the latest run folder")
        self.assertEqual([g["count"] for g in groups], [3, 1, 1, 1])

    def test_what_changes_from_run_to_run_is_not_part_of_the_signature(self):
        self.add("build", "", "exit 1 at 4a67ed3e6 in C:/x/build-20261120-101500/a.obj 12.3 s")
        self.add("build", "", "exit 1 at 790626cb5 in D:/y/build-20261121-090000/a.obj 98.7 s")
        groups = lv.fault_groups(lv.read_faults(self.live))
        self.assertEqual([g["count"] for g in groups], [2])

    def test_the_most_frequent_first_then_the_most_recent(self):
        self.add("a", "", "e", when=0)
        self.add("b", "", "e", when=5)
        self.add("c", "", "e", when=1)
        self.add("c", "", "e", when=2)
        self.assertEqual([g["check"] for g in lv.fault_groups(lv.read_faults(self.live))], ["c", "b", "a"])

    def test_could_not_is_set_on_the_last_fault_of_that_agent(self):
        self.add(agent=1, error="one")
        self.add(agent=2, error="two")
        self.add(agent=1, error="three")
        row = lv.set_could_not_last(self.live, 1, "run the tour on Direct3D 11")
        self.assertEqual(row["first_error"], "three")
        rows = lv.read_faults(self.live)
        self.assertEqual([r["could_not"] for r in rows], ["", "", "run the tour on Direct3D 11"])
        self.assertEqual(lv.set_could_not_last(self.live, 2, "x")["first_error"], "two")
        self.assertEqual(lv.set_could_not_last(self.live, None, "last of all")["first_error"], "three")
        self.assertIsNone(lv.set_could_not_last(self.live, 3, "nobody"))

    def test_could_not_with_no_fault_file_is_none(self):
        self.assertIsNone(lv.set_could_not_last(self.live, 1, "x"))

    def test_a_group_keeps_the_latest_could_not(self):
        self.add("c", "", "e", when=0)
        self.add("c", "", "e", when=1)
        lv.set_could_not_last(self.live, 1, "land the second step")
        self.add("c", "", "e", when=2)
        group = lv.fault_groups(lv.read_faults(self.live))[0]
        self.assertEqual((group["count"], group["could_not"]), (3, "land the second step"))


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_live_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
