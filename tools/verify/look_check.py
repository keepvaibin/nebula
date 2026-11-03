"""look_check.py FLAGGED.tsv REVIEW.txt: a gate is not passed until every stop look_audit.py flagged has a line written by whoever looked at its sheet.

A line reads "<stop> <galaxy> <scenario>: cause: <why it differs>" or "... : bug: <what is wrong>". Exit 0 only with no stop unread and no bug line.
"""

import csv
import os
import re
import sys

LINE = re.compile(r"^(\d+) (\S+) (\S+): (cause|bug): \S")


def main():
    flagged_path, review_path = sys.argv[1], sys.argv[2]
    with open(flagged_path, encoding="utf-8", newline="") as f:
        flagged = {(r["stop"], r["galaxy"], r["scenario"]) for r in csv.DictReader(f, delimiter="\t")}
    lines = {}
    if os.path.exists(review_path):
        with open(review_path, encoding="utf-8") as f:
            for text in f:
                m = LINE.match(text)
                if m:
                    lines[m.group(1, 2, 3)] = m.group(4)
    unread = sorted(flagged - set(lines), key=lambda k: int(k[0]))
    bugs = sorted((k for k in flagged if lines.get(k) == "bug"), key=lambda k: int(k[0]))
    print("look: %d flagged, %d read, %d unread, %d bugs" % (len(flagged), len(flagged) - len(unread), len(unread), len(bugs)))
    for k in unread:
        print("  unread: %s %s %s" % k)
    for k in bugs:
        print("  bug: %s %s %s" % k)
    if unread or bugs:
        print("look NOT PASSED: %s" % review_path)
        return 1
    print("look passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
