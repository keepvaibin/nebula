"""main_push_guard.py: main moves only through gate.py land <branch> (docs/plan/04-GATE-AND-MAIN.md). The pre-push
hook passes it the push's lines on standard input (local ref, local commit, remote ref, remote commit):

    printf '%s\\n' "$input" | python -I tools/gate/main_push_guard.py

A push that changes refs/heads/main passes only when it moves main to the commit a passed landing allowed: the gate run of
gate.py land writes that commit into a file of its run and names the file in NEBULA_MAIN_ALLOWED for its own push.
Deleting main is refused. A push of a branch named plant/... is refused (a planted fault never leaves the PC). Pushes of other branches pass. Exit 0 to let the push go, 1 to refuse it.
"""
import os
import pathlib
import sys
import time

ZERO = "0" * 40
ALLOWED_ENV = "NEBULA_MAIN_ALLOWED"
FRESH_SECONDS = 600
PLANT_REF = "refs/heads/plant/"      # the branches of planted faults (docs/plan/native/TOOL-MEND-PLANTS.md)
REFUSAL = "main moves only through gate.py land <branch> (docs/plan/04-GATE-AND-MAIN.md)"


def judge(lines, allowed_path, now=None):
    """None when the push may go, else why not."""
    for line in lines:
        words = line.split()
        if len(words) == 4 and words[2].startswith(PLANT_REF):
            return "a planted fault never leaves this PC (docs/plan/native/TOOL-MEND-PLANTS.md)"
        if len(words) != 4 or words[2] != "refs/heads/main":
            continue
        local_sha = words[1]
        if local_sha == ZERO:
            return "main is never deleted"
        if not allowed_path:
            return REFUSAL
        path = pathlib.Path(allowed_path)
        try:
            allowed = path.read_text(encoding="utf-8").split()
            age = (now or time.time()) - path.stat().st_mtime
        except OSError:
            return "the landing's allowance %s is not there" % allowed_path
        if not allowed or allowed[0] != local_sha:
            return "the landing allowed %s, not %s" % (allowed[0][:9] if allowed else "nothing", local_sha[:9])
        if age > FRESH_SECONDS:
            return "the landing's allowance is %d s old" % age
    return None


def main():
    why = judge(sys.stdin.read().splitlines(), os.environ.get(ALLOWED_ENV, ""))
    if why is not None:
        print(("pre-push: refused: " if why.startswith("a planted") else "pre-push: refs/heads/main refused: ") + why, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
