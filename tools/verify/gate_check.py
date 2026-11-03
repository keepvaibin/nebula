"""gate_check.py GATE.out [SECTIONS...]: a gate is not passed when a tool failed or a comparison gave no result: a Traceback or Python error in the output, a comparison line with nothing after its crash count, a crash, or a look that did not pass."""

import re
import sys

ERROR = re.compile(r"Traceback|^\s*File \"|^\w*Error: |^\s+\^+\s*$")
NO_RESULT = re.compile(r"^(\S.*?): crash \d+\s*$")
CRASH = re.compile(r": crash ([1-9]\d*)")
LOOK = re.compile(r"look NOT PASSED")
FAILED_RUNS = re.compile(r"^\d+ runs, [1-9]\d* failed")


def main():
    path = sys.argv[1]
    errors, empty, crashes, look, failed = [], [], [], [], []
    with open(path, encoding="utf-8", errors="replace") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if ERROR.search(line):
                errors.append("%d: %s" % (n, line[:110]))
            if NO_RESULT.match(line):
                empty.append("%d: %s" % (n, line[:110]))
            if CRASH.search(line):
                crashes.append("%d: %s" % (n, line[:110]))
            if LOOK.search(line):
                look.append("%d: %s" % (n, line[:110]))
            if FAILED_RUNS.match(line):
                failed.append("%d: %s" % (n, line[:110]))
    print("gate check: %d tool errors, %d comparisons with no result, %d crashes, %d look failures, %d failed sections" % (len(errors), len(empty), len(crashes), len(look), len(failed)))
    for title, rows in (("tool error", errors), ("no result", empty), ("crash", crashes), ("look", look), ("failed section", failed)):
        for row in rows[:12]:
            print("  %s %s" % (title, row))
    if errors or empty or crashes or look or failed:
        print("gate NOT PASSED")
        return 1
    print("gate check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
