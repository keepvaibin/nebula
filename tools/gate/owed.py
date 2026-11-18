"""owed.py: the GitHub jobs a move of main owed (07-BASE-FIRST, How the work flows 3), read after the move.

  owed.py --branch BRANCH --sha SHA --job NAME [--job NAME ...] --board FILE --who NAME [--hours 2]

gate.py starts it, detached, when main moved with GitHub's slow jobs (gate.OWED_JOBS) not ended. It asks GitHub once a
minute for those jobs of the candidate's run until each has ended or the hours are over, and writes the end on the board:
OWED-GREEN, or OWED-RED (the job, its end and the minute): fixed first, or the landing taken back. Exit 0 when green.
"""
import argparse
import pathlib
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gate  # noqa: E402


def owed_wait(branch, sha, jobs, hours, state_of=gate.candidate_state, sleep=time.sleep, clock=time.time):
    """(green, detail) of the owed jobs: red at the first that ends other than green, green when all are."""
    deadline = clock() + 3600.0 * hours
    detail = "not asked"
    while True:
        try:
            _status, all_jobs = state_of(branch, sha)
            mine = [j for j in all_jobs if (j.get("name") or "?") in jobs]
            missing = [name for name in jobs if name not in {j.get("name") for j in mine}]
            done, green, detail = gate.judge_run("completed" if not missing else None, mine)
            if missing and not done:
                detail = "%s; not in the run yet: %s" % (detail, ", ".join(missing))
        except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as e:
            done, green, detail = False, False, "GitHub did not answer: %s" % e
        if done:
            return green, detail
        if clock() >= deadline:
            return False, detail + " (not ended after %g hours)" % hours
        sleep(60.0)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--branch", required=True)
    p.add_argument("--sha", required=True)
    p.add_argument("--job", action="append", required=True)
    p.add_argument("--board", default="")
    p.add_argument("--who", default="Agent 1")
    p.add_argument("--hours", type=float, default=gate.OWED_HOURS)
    args = p.parse_args(argv)
    green, detail = owed_wait(args.branch, args.sha, args.job, args.hours)
    if green:
        gate.board_line(args.board, "%s OWED-GREEN: main %s: GitHub's owed jobs %s." % (args.who, args.sha[:9], detail))
    else:
        gate.board_line(args.board, "%s OWED-RED: main %s: GitHub's owed jobs: %s; fixed first, or the landing taken "
                        "back (07-BASE-FIRST, How the work flows 3)." % (args.who, args.sha[:9], detail))
    print(detail)
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
