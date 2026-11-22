"""failures.py: the store behind "same failure three times, stop" (package T13, step 3).

A landing that fails stores one signature for its branch: the name of the check that failed and that check's first
error line, with the parts that change from run to run (commit hashes, times, run folders) cut out. When the same
signature comes back for the same branch three times in a row, the landing exits with code 3 and says to stop
retrying. Any other signature, and any pass, start the count again.

Only a real failure (kind FAIL) counts. A TOOL result (the runner or a script broke) and an ENV result (the machine,
not the change) never count; they come back with what to do instead.

plan.py land calls this module today. gate.py land (a later package) will call the same functions, so the rule is in
one place: `Store(path).record(...)` after a failed landing, `Store(path).clear(branch)` after a pass.

The store is a tab-separated file, one row per branch: branch, signature, count, date of the last failure.
"""
import datetime
import os
import pathlib
import re
import tempfile

STOP_AFTER = 3
STOP_CODE = 3

KINDS = ("PASS", "FAIL", "TOOL", "ENV", "FLAKE", "NEW")

STOP_TEXT = "Same failure three times. Stop retrying. Run plan.py blocked, then plan.py next."
TOOL_TEXT = ("this is a tool fault, not the change's: it does not count as a failure; give the landing once more. Not judged a "
             "second time: stop (an agent of the plan: plan.py blocked <ID> \"tool: <the fault's first line>\", then plan.py next). "
             "Do not debug the tool.")
ENV_TEXT = ("this is the machine's fault, not the change's (disk, graphics card, tablet or another program): "
            "it does not count as a failure; run the landing again when the machine is free")

_HEX = re.compile(r"\b(?=[0-9a-f]*\d)[0-9a-f]{7,40}\b")
_TIME = re.compile(r"\b\d+(?:\.\d+)?\s*(?:ms|s|sec|seconds|min)\b")
_STAMP = re.compile(r"\b\d{8}-\d{6}\b|\b\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}(?::\d{2})?\b")
_PATH = re.compile(r"[A-Za-z]:[\\/][^\s;,)]*")
_SPACE = re.compile(r"\s+")


def signature(check, line):
    """The signature of one failure: the check's name and its first error line, without what changes per run."""
    text = line.strip()
    for pattern, token in ((_STAMP, "<time>"), (_PATH, "<path>"), (_HEX, "<hash>"), (_TIME, "<t>")):
        text = pattern.sub(token, text)
    return "%s | %s" % (check.strip(), _SPACE.sub(" ", text)[:200])


def first_counted(failures):
    """From a landing's results, [(check, kind, first_error_line)]: the one failure that is the landing's signature
    (the first FAIL by check name, so that lanes finishing in another order give the same answer), or None."""
    fails = sorted((c, l) for c, k, l in failures if k == "FAIL")
    return signature(*fails[0]) if fails else None


class Outcome:
    """What one recorded landing means: `count` same failures in a row, `stop` once that reaches three, `exit_code`
    (3 to stop, else 1 for a failed landing), `message` (what to print, or "")."""

    def __init__(self, count, stop, message, exit_code, counted):
        self.count, self.stop, self.message, self.exit_code, self.counted = count, stop, message, exit_code, counted

    def __repr__(self):
        return "Outcome(count=%r, stop=%r, exit_code=%r)" % (self.count, self.stop, self.exit_code)


class Store:
    def __init__(self, path):
        self.path = pathlib.Path(path)

    # The file ----------------------------------------------------------------------------------------------

    def rows(self):
        out = {}
        try:
            text = self.path.read_text(encoding="utf-8")
        except FileNotFoundError:
            return out
        for line in text.splitlines():
            cells = line.split("\t")
            if len(cells) >= 3 and cells[0] != "branch" and cells[2].isdigit():
                out[cells[0]] = (cells[1], int(cells[2]), cells[3] if len(cells) > 3 else "")
        return out

    def _write(self, rows):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        lines = ["branch\tsignature\tcount\tlast"] + ["%s\t%s\t%d\t%s" % (b, s, n, d) for b, (s, n, d) in sorted(rows.items())]
        fd, tmp = tempfile.mkstemp(dir=str(self.path.parent), prefix=self.path.name + ".")
        try:
            with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(lines) + "\n")
            os.replace(tmp, self.path)
        except BaseException:
            if os.path.exists(tmp):
                os.unlink(tmp)
            raise

    # The rule ----------------------------------------------------------------------------------------------

    def count(self, branch):
        return self.rows().get(branch, ("", 0, ""))[1]

    def last_line(self, branch):
        """The stored signature of the branch's last failure ("" when none): plan.py blocked writes it down."""
        return self.rows().get(branch, ("", 0, ""))[0]

    def clear(self, branch):
        rows = self.rows()
        if branch in rows:
            del rows[branch]
            self._write(rows)

    def record(self, branch, failures, today=None):
        """One failed landing of `branch`. `failures`: [(check, kind, first_error_line)] for every check that was not
        a pass. Returns an Outcome. Kinds other than FAIL never count: a landing whose only failures are TOOL or ENV
        leaves the stored count alone and says what to do instead."""
        sig = first_counted(failures)
        if sig is None:
            kinds = {k for _, k, _ in failures}
            if "TOOL" in kinds:
                return Outcome(self.count(branch), False, TOOL_TEXT, 4, False)
            if "ENV" in kinds:
                return Outcome(self.count(branch), False, ENV_TEXT, 4, False)
            return Outcome(self.count(branch), False, "", 1, False)
        rows = self.rows()
        old_sig, old_count, _ = rows.get(branch, ("", 0, ""))
        count = old_count + 1 if old_sig == sig else 1
        rows[branch] = (sig, count, (today or datetime.date.today()).isoformat())
        self._write(rows)
        if count >= STOP_AFTER:
            return Outcome(count, True, STOP_TEXT, STOP_CODE, True)
        return Outcome(count, False, "", 1, True)
