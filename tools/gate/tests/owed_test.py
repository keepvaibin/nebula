"""owed_test.py: GitHub's owed jobs at a move (07-BASE-FIRST, How the work flows 3), without GitHub.

python -X utf8 -I tools/gate/tests/owed_test.py

- judge_run: with the slow jobs owed, a run whose other jobs are green is done and green, and its detail names the
  owed ones; without owed jobs the same run is not done; an owed job that ended red is red;
- github_wait passes the owed jobs on;
- start_owed starts owed.py detached with the candidate, the commit, the board and each owed job;
- owed.py: waits for an owed job not yet in the run (a job that needs others), is green when all ended green, red at
  the first that did not, and red when the hours are over; it writes OWED-GREEN or OWED-RED on the board.
Exit 0 when every case holds.
"""
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402
import owed  # noqa: E402

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def job(name, status, conclusion=None):
    return {"name": name, "status": status, "conclusion": conclusion}


ARM, X86, START = gate.OWED_JOBS
fast = [job("Linux", "completed", "success"), job("Mac", "completed", "success")]
slow = [job(ARM, "in_progress"), job(X86, "in_progress")]

done, green, detail = gate.judge_run("in_progress", fast + slow, owed_jobs=gate.OWED_JOBS)
check(done and green and gate.owed_of(detail) == [ARM, X86], "the slow jobs owed: done, green: %s" % detail)
done, green, detail = gate.judge_run("in_progress", fast + slow)
check(not done, "without owed jobs the run is not done: %s" % detail)
done, green, detail = gate.judge_run("in_progress", fast + [job(ARM, "completed", "failure"), job(X86, "in_progress")],
                                     owed_jobs=gate.OWED_JOBS)
check(done and not green and ARM in detail, "an owed job already red stops the move: %s" % detail)
done, green, detail = gate.judge_run("in_progress", [job("Linux", "in_progress")] + slow, owed_jobs=gate.OWED_JOBS)
check(not done, "a job not owed and not ended: not done: %s" % detail)
done, green, detail = gate.judge_run("completed", fast, owed_jobs=gate.OWED_JOBS)
check(done and green and gate.owed_of(detail) == [], "nothing owed when all ended: %s" % detail)

asked = []
green, detail = gate.github_wait("gate/x", "abc", owed=gate.OWED_JOBS, sleep=lambda s: None,
                                 state_of=lambda b, c: asked.append(b) or ("in_progress", fast + slow))
check(green and gate.owed_of(detail) == [ARM, X86] and asked == ["gate/x"], "github_wait passes the owed jobs: %s" % detail)

with tempfile.TemporaryDirectory() as t:
    t = pathlib.Path(t)
    args = type("A", (), {"work": str(t), "board": str(t / "board.md"), "who": "Agent 1"})()
    started = []
    what = gate.start_owed("gate/x", "0123456789", [ARM, START], args, popen=lambda cmd, **k: started.append(cmd))
    cmd = started[0] if started else []
    check(len(started) == 1 and cmd[3:5] == ["-I", str(gate.HERE / "owed.py")] and "gate/x" in cmd
          and "0123456789" in cmd and cmd.count("--job") == 2 and START in cmd and str(t / "board.md") in cmd,
          "start_owed: owed.py with the candidate, the commit, the board and each job: %s %s" % (what, cmd))

    # owed.py: the start check is not in the run until the builds end.
    answers = iter([("in_progress", fast + slow),
                    ("in_progress", fast + [job(ARM, "completed", "success"), job(X86, "completed", "success")]),
                    ("completed", fast + [job(ARM, "completed", "success"), job(X86, "completed", "success"),
                                          job(START, "completed", "success")])])
    green, detail = owed.owed_wait("gate/x", "abc", [ARM, X86, START], 2.0, state_of=lambda b, c: next(answers),
                                   sleep=lambda s: None)
    check(green and detail == "3 of 3 jobs green", "owed jobs all green, the late one waited for: %s" % detail)
    green, detail = owed.owed_wait("gate/x", "abc", [ARM, X86, START], 2.0, sleep=lambda s: None,
                                   state_of=lambda b, c: ("in_progress", fast + [job(ARM, "completed", "failure"), slow[1]]))
    check(not green and ARM in detail, "an owed job red: red: %s" % detail)
    now = [0.0]

    def tick(seconds):
        now[0] += seconds
    green, detail = owed.owed_wait("gate/x", "abc", [ARM], 1.0, state_of=lambda b, c: ("in_progress", slow), sleep=tick,
                                   clock=lambda: now[0])
    check(not green and "not ended after 1 hours" in detail and now[0] >= 3600.0, "past the hours: red: %s" % detail)

    board = t / "board.md"
    real = owed.owed_wait
    owed.owed_wait = lambda *a, **k: (False, "red: %s failure at 14:00" % X86)
    code = owed.main(["--branch", "gate/x", "--sha", "0123456789", "--job", X86, "--board", str(board)])
    owed.owed_wait = lambda *a, **k: (True, "1 of 1 jobs green")
    code_green = owed.main(["--branch", "gate/x", "--sha", "0123456789", "--job", X86, "--board", str(board)])
    owed.owed_wait = real
    text = board.read_text(encoding="utf-8") if board.exists() else ""
    check(code == 1 and "OWED-RED: main 012345678" in text and "taken back" in text, "a red owed job on the board: %s" % text)
    check(code_green == 0 and "OWED-GREEN: main 012345678" in text, "green owed jobs on the board: %s" % text)

if failures:
    print("owed_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("owed_test.py: pass")
