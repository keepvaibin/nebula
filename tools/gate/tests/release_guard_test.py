"""release_guard_test.py: a release is built only from a commit main holds whose Check run passed (P-023).

python -X utf8 -I tools/gate/tests/release_guard_test.py

tools/release_guard.judge with GitHub's answers made up:
- a commit main holds (compare: ahead or identical) with a green Check run may be released;
- a commit main does not hold (behind, diverged) is refused, whatever its runs;
- a commit with no Check run, or only red, cancelled or unfinished ones, is refused; one green run among red ones is
  enough (a run started again);
- a failed question to GitHub refuses, naming it.
And release.yml: its first job runs the guard, and every package job needs that job.
Exit 0 when every case holds.
"""
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(TREE / "tools"))
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


try:
    import release_guard
except ImportError:
    release_guard = None
check(release_guard is not None, "tools/release_guard.py exists")


def asker(status, runs, fail=None):
    def ask(*args):
        if fail and fail in args[1]:
            raise RuntimeError("HTTP 502")
        if "/compare/" in args[1]:
            return json.dumps({"status": status})
        return json.dumps({"workflow_runs": [{"status": s, "conclusion": c} for s, c in runs]})
    return ask


if release_guard is not None:
    judge = release_guard.judge
    green = [("completed", "success")]
    check(judge("o/r", "a" * 40, asker("ahead", green)) is None, "a commit main holds, green: released")
    check(judge("o/r", "a" * 40, asker("identical", green)) is None, "main's own commit, green: released")
    why = judge("o/r", "a" * 40, asker("behind", green))
    check(why is not None and "main does not hold" in why, "a commit main does not hold is refused: %s" % why)
    check(judge("o/r", "a" * 40, asker("diverged", green)) is not None, "a diverged commit is refused")
    why = judge("o/r", "a" * 40, asker("ahead", []))
    check(why is not None and "no Check run" in why, "no Check run is refused: %s" % why)
    why = judge("o/r", "a" * 40, asker("ahead", [("completed", "failure"), ("in_progress", None), ("completed", "cancelled")]))
    check(why is not None and "no Check run" in why and "completed/failure" in why, "red, unfinished, cancelled: refused: %s" % why)
    check(judge("o/r", "a" * 40, asker("ahead", [("completed", "failure"), ("completed", "success")])) is None,
          "one green run among red ones is enough")
    why = judge("o/r", "a" * 40, asker("ahead", green, fail="/compare/"))
    check(why is not None and "could not compare" in why, "a failed comparison refuses: %s" % why)
    why = judge("o/r", "a" * 40, asker("ahead", green, fail="/runs"))
    check(why is not None and "could not list" in why, "a failed run list refuses: %s" % why)

text = (TREE / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
jobs = text.split("\njobs:\n", 1)[1]
names = [l[2:-1] for l in jobs.splitlines() if l.startswith("  ") and not l.startswith("   ") and l.endswith(":")]
check("tools/release_guard.py" in jobs.split("\n  %s:\n" % names[1])[0] if len(names) > 1 else False,
      "release.yml's first job runs the guard")
for name in names[1:]:
    body = jobs.split("\n  %s:\n" % name, 1)[1]
    needs = next((l for l in body.splitlines() if l.startswith("    needs:")), "")
    check("release" in needs.replace("[", " ").replace("]", " ").replace(",", " ").split(),
          "the package job %s needs the guarded first job: %s" % (name, needs.strip()))

if failures:
    print("release_guard_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("release_guard_test.py: pass")
