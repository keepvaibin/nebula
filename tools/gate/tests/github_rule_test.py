"""github_rule_test.py: the GitHub rule of a landing or a move (package T8a, tools/gate/github_rule.py), without GitHub.

    python -X utf8 -I tools/gate/tests/github_rule_test.py [--work FOLDER]

Every reading of GitHub here is a fake: a function that returns the push runs of the commit. Nothing starts a process.

  every_must_green_passes            all `must` jobs green: pass at the first reading
  must_running_waits_then_passes     one `must` job still running: the wait asks again, and passes when it turns green
  nightly_changes_nothing            a `nightly` job red, cancelled or running changes nothing
  unknown_job_is_must_with_warning   a job the table does not name counts as must, and a warning line of kind tool names it once
  red_must_refuses_before_checks     a red `must` job of any run of the commit refuses the start; a cancelled one, an advisory
                                     one, a nightly one, a planned one and GitHub not answering do not
  red_must_is_fail_in_the_wait       a red `must` job in the wait is `fail`, and its log is saved under the right name
  newest_run_wins                    red then green on a newer run is green; green on the work branch's run counts for the
                                     candidate whose own job is still running; an older red is not forgotten
  cancelled_once_rerun_twice_env     a cancelled job is run again once; ending that way twice is env (GitHub could not run it)
  rerun_refused_is_asked_again       a request to run again that GitHub refuses is made again at the next reading
  advisory_red_rerun_once_never_blocks  a red advisory job is run again once; red again it is one line and blocks nothing
  time_limit_is_env                  the limit gives env and names the jobs not finished
  no_job_finished_is_waited_for      no run, a run with no job, a run whose jobs have not ended: waited for, not refused
  log_that_cannot_be_fetched         one line says so, no file is made, the verdict stays
  table_and_workflow_agree           github_jobs.tsv and the job names of check.yml (matrix legs written out) are the same set,
                                     and the names written out are those a real run of main showed (a fixture, below)
  table_errors                       a table with no header, an unknown class, a twice named job or a row with no class is refused
Exit 0 when every case holds.
"""
import pathlib
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import github_rule as rule  # noqa: E402

CLASSES = rule.read_table()
MUSTS = [n for n, c in CLASSES.items() if c == rule.MUST]
NIGHTLY = [n for n, c in CLASSES.items() if c == rule.NIGHTLY]
ADVISORY = [n for n, c in CLASSES.items() if c == rule.ADVISORY]

# The job names of a real push run of main (run 44884895139, 2026-11-21, gh run view --json jobs): the names GitHub gives a
# matrix leg cannot be told from a reading of the workflow file alone, so a real run's names are kept here and the names
# the parser writes out must be these.
REAL_RUN_NAMES = [
    "Linux", "Rust tests (windows-2022)", "Rust tests (ubuntu-24.04)", "Windows game modules",
    "Windows (three renderers, unit tests)", "Linux sanitizers (thread)", "Linux sanitizers (address,undefined)",
    "Linux unit tests", "Mac", "Android (arm64-v8a)", "Android (x86_64)", "Android start check (x86_64 emulator)",
]

COUNTER = [1000]


def job(name, status="completed", conclusion="success", ended="2026-11-21T08:30:00Z", jid=None):
    COUNTER[0] += 1
    return {"name": name, "status": status, "conclusion": conclusion if status == "completed" else None,
            "completedAt": ended if status == "completed" else "0001-01-01T00:00:00Z", "databaseId": jid or COUNTER[0]}


def running(name):
    return job(name, "in_progress")


def run(run_id, branch, jobs):
    return {"id": run_id, "branch": branch, "jobs": jobs}


def all_green(names=None):
    return [job(n) for n in (names or list(CLASSES))]


def rest(*named):
    """Every job of the table green except those named (the test gives them their own state)."""
    return [job(n) for n in CLASSES if n not in named]


class Clock:
    def __init__(self):
        self.now = 0.0
        self.slept = 0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds
        self.slept += 1


def make(runs_of, rerun=None, planned=(), clock=None, notes=None):
    clock = clock or Clock()
    return rule.Rule("abc", CLASSES, runs_of, rerun, planned, clock=clock, say=lambda text: None,
                     note=(notes.append if notes is not None else None)), clock


def sequence(*answers):
    """runs_of that gives each answer once, then the last again; an Exception is raised."""
    it = iter(answers)
    last = [None]

    def runs_of(sha):
        try:
            last[0] = next(it)
        except StopIteration:
            pass
        if isinstance(last[0], Exception):
            raise last[0]
        return last[0]
    return runs_of


class Rerunner:
    def __init__(self, ok=True):
        self.calls, self.ok = [], ok

    def __call__(self, run_id, job_id):
        self.calls.append((run_id, job_id))
        return self.ok, "asked" if self.ok else "refused"


class Rule(unittest.TestCase):
    def test_every_must_green_passes(self):
        r, c = make(lambda sha: [run(1, "gate/x", all_green())])
        kind, detail, reading = r.wait(120, sleep=c.sleep)
        self.assertEqual(kind, "pass", detail)
        self.assertEqual(c.slept, 0)
        self.assertEqual(sorted(reading.green), sorted(MUSTS))

    def test_must_running_waits_then_passes(self):
        before = [job(n) for n in CLASSES if n != "Mac"] + [running("Mac")]
        r, c = make(sequence([run(1, "gate/x", before)], [run(1, "gate/x", before)], [run(1, "gate/x", all_green())]))
        kind, detail, _ = r.wait(120, sleep=c.sleep)
        self.assertEqual((kind, c.slept), ("pass", 2), detail)

    def test_nightly_changes_nothing(self):
        for state in (job("x", conclusion="failure"), job("x", conclusion="cancelled"), job("x", "in_progress")):
            jobs = [dict(job(n), conclusion=state["conclusion"], status=state["status"]) if n in NIGHTLY else job(n)
                    for n in CLASSES]
            r, c = make(lambda sha, jobs=jobs: [run(1, "gate/x", jobs)], rerun=Rerunner())
            kind, detail, _ = r.wait(120, sleep=c.sleep)
            self.assertEqual(kind, "pass", detail)
            self.assertIsNone(r.red_before())
            self.assertEqual(r.reruns, {})

    def test_unknown_job_is_must_with_warning(self):
        jobs = all_green() + [running("Mac (Intel)")]
        r, c = make(sequence([run(1, "gate/x", jobs)], [run(1, "gate/x", all_green() + [job("Mac (Intel)")])]))
        kind, detail, _ = r.wait(120, sleep=c.sleep)
        self.assertEqual((kind, c.slept), ("pass", 1), "an unknown job is waited for like a must job: %s" % detail)
        self.assertEqual([n for n, _ in r.warnings()], ["Mac (Intel)"], "named once, whatever the readings")
        self.assertIn("github_jobs.tsv", r.warnings()[0][1])
        r2, _ = make(lambda sha: [run(1, "gate/x", all_green() + [job("Mac (Intel)", conclusion="failure")])])
        self.assertIn("Mac (Intel) failure", r2.red_before(), "a red unknown job refuses like a must job")

    def test_red_must_refuses_before_checks(self):
        ended = "2026-11-21T08:30:12Z"
        red = job("Mac", conclusion="failure", ended=ended)
        r, _ = make(lambda sha: [run(7, "engine/one", [red]), run(9, "gate/x", all_green()[1:])])
        why = r.red_before()
        self.assertIsNotNone(why)
        self.assertIn("Mac failure on engine/one at ", why)
        for other in (job("Mac", conclusion="cancelled"), job("Mac", conclusion="startup_failure"), job("Mac", "in_progress"),
                      job("Android start check (x86_64 emulator)", conclusion="failure"),
                      job("Windows game modules", conclusion="failure")):
            r, _ = make(lambda sha, other=other: [run(7, "engine/one", [other])])
            self.assertIsNone(r.red_before(), "%s %s does not refuse" % (other["name"], other["conclusion"]))
        r, _ = make(lambda sha: [run(7, "engine/one", [red])], planned={"Mac"})
        self.assertIsNone(r.red_before(), "a planned job does not refuse")

        def away(sha):
            raise RuntimeError("gh run list: timeout")
        r, _ = make(away)
        self.assertIsNone(r.red_before(), "GitHub not answering does not refuse")

    def test_red_must_is_fail_in_the_wait(self):
        jobs = [job(n) for n in CLASSES if n != "Linux unit tests"] + [job("Linux unit tests", conclusion="failure", jid=555)]
        r, c = make(lambda sha: [run(3, "gate/x", jobs)])
        kind, detail, reading = r.wait(120, sleep=c.sleep)
        self.assertEqual(kind, "fail", detail)
        self.assertEqual(c.slept, 0, "a red job ends the wait at once")
        self.assertEqual([(f.job, f.run_id, f.job_id) for f in reading.reds], [("Linux unit tests", 3, 555)])
        lines = ["line %d" % i for i in range(1, 301)]
        said = []
        with tempfile.TemporaryDirectory() as tmp:
            path = rule.save_log(reading.reds[0], tmp, lambda job_id: "\n".join(lines) if job_id == 555 else "", said.append)
            self.assertEqual(pathlib.Path(path).name, "github-Linux-unit-tests.log")
            saved = pathlib.Path(path).read_text(encoding="utf-8").splitlines()
        self.assertEqual((saved[0], saved[-1], len(saved)), ("line 101", "line 300", 200), "the last 200 lines")
        shown = [l for l in said if l.startswith("  line")]
        self.assertEqual(shown, ["  " + l for l in saved[:20]], "the first 20 of them are printed")
        self.assertIn("Linux unit tests failure", said[0], "the line that names the job comes first")
        self.assertIn("job 555", said[0])
        self.assertEqual(rule.file_name_of("Linux sanitizers (address,undefined)"), "github-Linux-sanitizers-(address,undefined).log")

    def test_newest_run_wins(self):
        mac_red, mac_green = job("Mac", conclusion="failure"), job("Mac")
        r, _ = make(lambda sha: [run(5, "work/a", [mac_red]), run(8, "gate/x", [mac_green])])
        self.assertEqual(r.read().green, ["Mac"], "red, then green on a newer run: green")
        self.assertIsNone(r.red_before())
        r, _ = make(lambda sha: [run(5, "work/a", [mac_green]), run(8, "gate/x", [running("Mac")])])
        reading = r.read()
        self.assertEqual(reading.green, ["Mac"], "green on the work branch's push counts for the candidate still running it")
        r, _ = make(lambda sha: [run(5, "work/a", [mac_green]), run(8, "gate/x", [mac_red])])
        self.assertEqual([f.job for f in r.read().reds], ["Mac"], "green, then red on a newer run: red")
        r, _ = make(lambda sha: [run(5, "work/a", [mac_red]), run(8, "gate/x", [running("Mac")])])
        self.assertEqual([f.job for f in r.read().reds], ["Mac"], "a red of the same commit stays while the newer run has no result")
        r, _ = make(lambda sha: [run(5, "work/a", [job("Mac", conclusion="skipped")]), run(8, "gate/x", [running("Mac")])])
        self.assertIn("Mac", r.read().pending, "a skipped job (a work branch has no Windows or Android) is no result")
        newer_cancelled = [run(5, "work/a", [mac_green]), run(8, "gate/x", [job("Mac", conclusion="cancelled")])]
        r, _ = make(lambda sha: newer_cancelled, rerun=Rerunner())
        self.assertEqual(r.read().green, ["Mac"], "a cancelled newer run does not hide a green one")
        order = [run(8, "gate/x", [mac_green]), run(5, "work/a", [mac_red])]
        r, _ = make(lambda sha: order)
        self.assertEqual(r.read().green, ["Mac"], "the order the runs come in does not matter")

    def test_cancelled_once_rerun_twice_env(self):
        clock = Clock()
        first = job("Mac", conclusion="cancelled", jid=11)
        again = job("Mac", conclusion="cancelled", jid=12)
        runs = [[run(4, "gate/x", rest("Mac") + [first])], [run(4, "gate/x", rest("Mac") + [running("Mac")])],
                [run(4, "gate/x", rest("Mac") + [again])]]
        rerunner, notes = Rerunner(), []
        r, c = make(sequence(*runs), rerunner, clock=clock, notes=notes)
        reading = r.read()
        self.assertEqual((rerunner.calls, reading.pending, reading.reds, reading.envs), ([(4, 11)], ["Mac"], [], []),
                         "the first end is run again, and is no red")
        self.assertTrue(any("run again once" in n for n in notes), "github.log notes it: %s" % notes)
        reading = r.read()
        self.assertEqual((len(rerunner.calls), reading.pending), (1, ["Mac"]), "running again: waited for")
        reading = r.read()
        self.assertEqual(len(rerunner.calls), 1, "never run again twice")
        self.assertEqual([f.text for f in reading.envs], ["GitHub could not run Mac"])
        # The same through the wait: env.
        r, c = make(sequence(*runs), Rerunner(), clock=Clock())
        kind, detail, _ = r.wait(120, sleep=c.sleep)
        self.assertEqual((kind, detail), ("env", "GitHub could not run Mac"))

    def test_the_old_end_after_a_rerun_is_not_a_second_end(self):
        clock = Clock()
        old = job("Mac", conclusion="cancelled", jid=11)
        rerunner = Rerunner()
        r, _ = make(lambda sha: [run(4, "gate/x", rest("Mac") + [old])], rerunner, clock=clock)
        self.assertEqual(r.read().pending, ["Mac"])
        clock.now += 60
        reading = r.read()
        self.assertEqual((reading.pending, reading.envs), (["Mac"], []), "the same job id, a minute later: still the old end")
        clock.now += rule.RERUN_GRACE_SECONDS
        self.assertEqual(len(r.read().envs), 1, "an end that stays is the second end")
        self.assertEqual(len(rerunner.calls), 1)

    def test_rerun_refused_is_asked_again(self):
        rerunner = Rerunner(ok=False)
        r, _ = make(lambda sha: [run(4, "gate/x", rest("Mac") + [job("Mac", conclusion="startup_failure", jid=11)])], rerunner)
        for _ in range(3):
            reading = r.read()
            self.assertEqual((reading.pending, reading.envs), (["Mac"], []))
        self.assertEqual(len(rerunner.calls), 3, "asked at each reading until GitHub takes it")
        rerunner.ok = True
        r.read()
        self.assertEqual(len(rerunner.calls), 4)
        r.read()
        self.assertEqual(len(rerunner.calls), 4)

    def test_advisory_red_rerun_once_never_blocks(self):
        start = ADVISORY[0]
        clock = Clock()
        rerunner = Rerunner()
        red1 = job(start, conclusion="failure", jid=21)
        red2 = job(start, conclusion="failure", jid=22)
        answers = [[run(2, "gate/x", all_green([n for n in CLASSES if n != start]) + [red1])],
                   [run(2, "gate/x", all_green([n for n in CLASSES if n != start]) + [running(start)])],
                   [run(2, "gate/x", all_green([n for n in CLASSES if n != start]) + [red2])]]
        r, c = make(sequence(*answers), rerunner, clock=clock)
        for expect_calls in (1, 1, 1):
            reading = r.read()
            self.assertTrue(reading.done, "an advisory job never blocks")
            self.assertEqual(len(rerunner.calls), expect_calls)
        self.assertEqual(r.advised, {start: "red twice on GitHub; advisory, blocks nothing"})
        r, c = make(lambda sha: [run(2, "gate/x", all_green([n for n in CLASSES if n != start]) + [red1])], Rerunner())
        self.assertIsNone(r.red_before(), "a red advisory job does not refuse the start")
        kind, _, _ = r.wait(120, sleep=c.sleep)
        self.assertEqual(kind, "pass")

    def test_time_limit_is_env(self):
        jobs = [job(n) for n in CLASSES if n not in ("Mac", "Android (x86_64)")] + [running("Mac"), running("Android (x86_64)")]
        r, c = make(lambda sha: [run(1, "gate/x", jobs)])
        kind, detail, reading = r.wait(120, sleep=c.sleep)
        self.assertEqual(kind, "env")
        self.assertIn("Mac", detail)
        self.assertIn("Android (x86_64)", detail)
        self.assertIn("120 minutes", detail)
        self.assertGreaterEqual(c.now, 7200.0)
        self.assertEqual(sorted(reading.pending), ["Android (x86_64)", "Mac"])
        r, c = make(lambda sha: (_ for _ in ()).throw(RuntimeError("gh run list: timeout")))
        kind, detail, reading = r.wait(2, sleep=c.sleep)
        self.assertEqual(kind, "env")
        self.assertIsNone(reading)
        self.assertIn("did not finish", detail)

    def test_no_job_finished_is_waited_for(self):
        queued = [job(n, "queued") for n in CLASSES]
        answers = (None, [], [run(1, "gate/x", [])], [run(1, "gate/x", queued)], [run(1, "gate/x", all_green())])
        for first in answers[:-1]:
            r, c = make(sequence(first, [run(1, "gate/x", all_green())]))
            kind, detail, _ = r.wait(120, sleep=c.sleep)
            self.assertEqual((kind, c.slept), ("pass", 1), "%s: waited for, then green (%s)" % (first, detail))
        r, c = make(lambda sha: [])
        kind, detail, _ = r.wait(3, sleep=c.sleep)
        self.assertEqual(kind, "env", "and not refused: only the limit ends it")
        self.assertNotIn("has no job", detail)

    def test_log_that_cannot_be_fetched(self):
        st = rule.State("Mac", rule.MUST, "red", 3, 9, "gate/x", job("Mac", conclusion="failure"))
        finding = rule.Finding(st, st.said())
        said = []

        def broken(job_id):
            raise RuntimeError("gh run view: HTTP 410")

        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(rule.save_log(finding, tmp, broken, said.append))
            self.assertEqual(list(pathlib.Path(tmp).iterdir()), [])
        self.assertEqual(len(said), 2)
        self.assertIn("could not be fetched", said[1])
        self.assertIsNone(finding.log)


class Table(unittest.TestCase):
    def test_table_and_workflow_agree(self):
        text = rule.WORKFLOW_FILE.read_text(encoding="utf-8")
        self.assertEqual(rule.table_problems(CLASSES, text), [],
                         "tools/gate/github_jobs.tsv and .github/workflows/check.yml must name the same jobs")
        derived = rule.workflow_job_names(text)
        self.assertEqual(sorted(derived), sorted(REAL_RUN_NAMES),
                         "the names written out from the workflow file are the names of a real run (update both the table "
                         "and REAL_RUN_NAMES here when a job is renamed, added or removed)")
        self.assertEqual(sorted(CLASSES), sorted(REAL_RUN_NAMES))
        self.assertEqual(len(derived), len(set(derived)))

    def test_the_parser_on_a_small_workflow(self):
        text = ("name: X\non:\n  push:\njobs:\n  a:\n    name: Plain\n    runs-on: x\n"
                "  b:\n    name: Leg (${{ matrix.os }})\n    strategy:\n      matrix:\n"
                "        os: ${{ fromJSON(cond && '[\"u\", \"w\"]' || '[\"u\"]') }}\n    steps:\n      - run: x\n"
                "  c:\n    name: Two (${{ matrix.s }}, ${{ matrix.t }})\n    strategy:\n      matrix:\n"
                "        s: ['a,b', thread]\n        t: [p, q]\n    steps:\n      - run: y\n")
        self.assertEqual(rule.workflow_job_names(text),
                         ["Plain", "Leg (u)", "Leg (w)", "Two (a,b, p)", "Two (a,b, q)", "Two (thread, p)", "Two (thread, q)"])

    def test_table_errors(self):
        for text, why in (("a\tmust\n", "header"), ("job\tclass\nMac\tmaybe\n", "not a class"), ("job\tclass\nMac\n", "tab"),
                          ("job\tclass\nMac\tmust\nMac\tmust\n", "twice"), ("# nothing\n", "no row")):
            with self.assertRaises(rule.TableError, msg=text) as e:
                rule.parse_table(text)
            self.assertIn(why, str(e.exception))
        self.assertEqual(rule.parse_table("# c\njob\tclass\nA b\tmust\nC\tnightly\n"), {"A b": "must", "C": "nightly"})
        missing = rule.table_problems({"Mac": "must"}, "jobs:\n  a:\n    name: Linux\n")
        self.assertEqual(len(missing), 2)


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("github_rule_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
