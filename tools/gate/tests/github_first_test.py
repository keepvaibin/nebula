"""github_first_test.py: gate.py reads GitHub before a main gate starts and while it runs (section 8, N-1061; the rule of
package T8a, tools/gate/github_rule.py). A red `must` job of any run of the commit, on any branch, stops the start and is
named with its branch and minute; a cancelled run, a planned job and a job that is not `must` do not; GitHub not answering
does not stop it. While the checks run, the candidate's first red `must` job ends the commands running (their children
too) and no other starts."""
import datetime
import os
import pathlib
import shutil
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import gate  # noqa: E402


def job(name, conclusion="success", ended="2026-11-17T21:50:12Z", status="completed"):
    return {"name": name, "status": status, "conclusion": conclusion, "completedAt": ended}


def runs(*pairs):
    """The push runs of a commit as gate.commit_runs gives them: (branch, jobs) newest first."""
    return [{"id": 100 - i, "branch": branch, "jobs": jobs} for i, (branch, jobs) in enumerate(pairs)]


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    minute = datetime.datetime(2026, 11, 17, 21, 50, 12, tzinfo=datetime.timezone.utc).astimezone().strftime("%H:%M")

    # 1. Before the start: every run of the commit.
    green = [job("Linux"), job("Linux unit tests"), job("Android (x86_64)")]
    check(gate.red_on_github("abc", runs_of=lambda sha: runs(("engine/one", green))) is None, "all green: it starts")
    why = gate.red_on_github("abc", runs_of=lambda sha: runs(("engine/one", green + [job("Mac", "failure")]),
                                                                    ("gate/x", green)))
    check(why is not None and "Mac failure on engine/one at " + minute in why,
          "a red job on another branch stops the start, named with its branch and minute: %s" % why)
    check(gate.red_on_github("abc", runs_of=lambda sha: runs(("engine/one", green + [job("Mac", "cancelled")]))) is None,
          "a cancelled run (a newer push's) does not stop it")
    check(gate.red_on_github("abc", planned={"Mac"},
                             runs_of=lambda sha: runs(("engine/one", green + [job("Mac", "failure")]))) is None,
          "a job planned.tsv names does not stop it")
    check(gate.red_on_github("abc", runs_of=lambda sha: runs(("engine/one", green + [job("Mac", None, "0001-01-01T00:00:00Z",
                                                                                      "in_progress")]))) is None,
          "a job still running does not stop it")

    def away(sha):
        raise RuntimeError("gh run list: timeout")

    check(gate.red_on_github("abc", runs_of=away) is None, "GitHub not answering does not stop it")
    check(gate.red_minute(job("x", ended="0001-01-01T00:00:00Z")) == "", "no minute for a job not ended")
    check(gate.red_on_github("abc", runs_of=lambda sha: runs(("engine/one", [job("Windows game modules", "failure")]))) is None,
          "a red job that is not `must` (Windows is nightly) does not stop it")

    # 2. While the checks run: the candidate's first red job ends what runs.
    t = pathlib.Path(tempfile.mkdtemp())
    try:
        answers = iter([runs(("gate/x", green[:2])), runs(("gate/x", green[:2] + [job("Mac", "failure")]))])
        last = [None]

        def runs_of(sha):
            last[0] = next(answers, last[0])
            return last[0]

        said = []
        rule = gate.make_rule("abc", gate.github_rule.read_table())
        rule.runs_of = runs_of
        watch = gate.GithubWatch(rule, "gate/x", every=0.05,
                                 on_red=lambda why: (said.append(why), gate.stop_runs(why))).start()
        started = time.monotonic()
        code = gate.run([sys.executable, "-c", "import subprocess, sys, time; "
                         "subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)']); time.sleep(60)"],
                        t / "long.log")
        took = time.monotonic() - started
        watch.stop()
        check(code == 125 and took < 30, "the red job ends the command running: exit %s after %.1f s" % (code, took))
        check(said and "Mac failure on gate/x at " + minute in said[0] and watch.red == said[0],
              "the watch names the job and its minute: %s" % said)
        check(gate.run([sys.executable, "-c", "print(1)"], t / "after.log") == 125
              and "not started" in (t / "after.log").read_text(), "no command starts after it")
        log = (t / "long.log").read_text(encoding="utf-8", errors="replace")
        check("(stopped: " in log, "the log says why it stopped")

        gate.STOPPED.clear()
        rule2 = gate.make_rule("abc", gate.github_rule.read_table())
        rule2.runs_of = lambda sha: runs(("gate/x", green[:2] + [job("Mac", None, "0001-01-01T00:00:00Z", "in_progress")]))
        quiet = gate.GithubWatch(rule2, "gate/x", every=0.05,
                                 on_red=lambda why: failures.append("a run with no red job was stopped")).start()
        check(gate.run([sys.executable, "-c", "import time; time.sleep(0.3)"], t / "quiet.log") == 0,
              "with no red job the command ends by itself")
        quiet.stop()
        check(quiet.red is None, "no red job, nothing said")
    finally:
        gate.STOPPED.clear()
        shutil.rmtree(t, ignore_errors=True)

    for f in failures:
        print("FAIL:", f)
    print("github_first_test: %s" % ("FAIL" if failures else "pass"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
