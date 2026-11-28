"""quarantine.py: a new row of pace.py or a new check of gate.py is report only until it has passed on main NEEDED times.

The rule (docs/plan/04-GATE-AND-MAIN.md, "Quarantine of new rows"): a name is live when a line of quarantine-live.tsv names it (every
row and check on main when this landed), or when its line in the counters file has NEEDED passes. Every other name is quarantined:
the row or check runs, is shown, and is judged, and a failure of it is printed and refuses nothing. The verdict of a quarantined
row is `quarantined: report only (n of 5 passes on main)`; a quarantined check that fails passes with that text in front of its detail.

The counters file (the gate's work folder, quarantine.tsv; columns COLUMNS) has one line for each name seen: the kind (`row` or
`check`), the name, the time it was first seen, its passes and the main commits that gave them. `keep` runs only after a landing
moved main (gate.py keep_quarantine, which calls it as pace.py keep is called): a name that passed in that landing, with every run
of it passing, gets one pass for that main commit, and a commit counted already gives none, so a landing that is retried counts
once. A failure counts for nothing and takes nothing away. A plant run, a tool's self-test and a run that moved nothing never call
`keep`. A name in a plant run's `--live-row` is live there whatever its count: the plant branch's planted fault in a new row is
refused, and main's counters are never touched.

An unreadable live file makes every name live: a fault of the tool never makes a check see less.
"""
import datetime
import fnmatch
import os
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
LIVE_FILE = HERE / "quarantine-live.tsv"
NEEDED = 5
COLUMNS = ("kind", "name", "first_seen", "passes", "commits")
KINDS = ("row", "check")
VERDICT = "quarantined: report only (%d of %d passes on main)"


class Quarantine:
    """What is live and what each quarantined name has passed. `counters`: the counters file ("" for none: every unlisted name has 0
    passes); `forced`: names that are live whatever their count (the plant run's --live-row); `live`: the live file's lines
    (kind, pattern), None when it cannot be read (everything is live)."""

    def __init__(self, counters="", forced=(), live_file=None):
        self.counters = str(counters) if counters else ""
        self.forced = tuple(forced or ())
        self.live = read_live(live_file or LIVE_FILE)
        self.counts = read_counters(self.counters)

    @classmethod
    def inert(cls):
        """A quarantine that holds nothing back: every name is live (a Gate made by a test, not by the gate's run_checks)."""
        q = cls.__new__(cls)
        q.counters, q.forced, q.live, q.counts = "", (), None, {}
        return q

    def passes(self, kind, name):
        """None when the name is live; else its passes on main so far (0 for a name seen for the first time)."""
        if self.live is None or name in self.forced:
            return None
        if any(k == kind and fnmatch.fnmatchcase(name, pattern) for k, pattern in self.live):
            return None
        n = self.counts.get((kind, name), {}).get("passes", 0)
        return None if n >= NEEDED else n


def read_live(path):
    """[(kind, pattern)] of the live file, None when it cannot be read or holds no line (then nothing is quarantined)."""
    try:
        lines = pathlib.Path(path).read_text(encoding="utf-8").splitlines()
    except OSError:
        return None
    out = []
    for line in lines:
        if not line.strip() or line.startswith("#") or line.startswith(COLUMNS[0] + "\t"):
            continue
        cells = line.split("\t")
        if len(cells) == 2 and cells[0] in KINDS:
            out.append((cells[0], cells[1]))
    return out or None


def read_counters(path):
    """{(kind, name): {first_seen, passes, commits}} of the counters file; {} where there is none."""
    out = {}
    if not path or not pathlib.Path(path).is_file():
        return out
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines()[1:]:
        cells = line.split("\t")
        if len(cells) != len(COLUMNS) or cells[0] not in KINDS:
            continue
        try:
            passes = int(cells[3])
        except ValueError:
            continue
        out[(cells[0], cells[1])] = {"first_seen": cells[2], "passes": passes, "commits": [c for c in cells[4].split(",") if c]}
    return out


def apply_rows(rows, quarantine):
    """(the rows with each quarantined row's verdict replaced, {name: {passes, verdict}} of those rows). `verdict` is what the row said
    before: only the pass of a run counts, and a FAIL is shown beside by the digest."""
    out, info = [], {}
    for row in rows:
        measure, value, target, verdict = row
        n = quarantine.passes("row", measure)
        if n is None:
            out.append(row)
            continue
        info[measure] = {"passes": n, "verdict": verdict}
        out.append((measure, value, target, VERDICT % (n, NEEDED)))
    return out, info


def outcomes_of_runs(runs):
    """{name: passed} of the quarantined rows of pace.py's runs (pace.json): a name passed when every run that has it judged it pass."""
    out = {}
    for run in runs:
        for name, said in (run.get("quarantine") or {}).items():
            out[name] = out.get(name, True) and said.get("verdict") == "pass"
    return out


def keep(path, kind, outcomes, commit, now=None):
    """Writes the counters of a landing that moved main to `commit`. `outcomes`: {name: passed} of the names that were quarantined in it.
    A new name is added with 0 passes (first seen); one that passed gets the commit once. Returns the lines to print."""
    counts = read_counters(path)
    said = []
    when = (now or datetime.datetime.now()).strftime("%Y-%m-%d %H:%M:%S")
    for name, passed in sorted(outcomes.items()):
        entry = counts.setdefault((kind, name), {"first_seen": when, "passes": 0, "commits": []})
        if passed and entry["passes"] < NEEDED and commit not in entry["commits"]:
            entry["passes"] += 1
            entry["commits"].append(commit)
            said.append("%s %s: %d of %d passes on main%s" % (kind, name, entry["passes"], NEEDED,
                                                                 " (live from now on)" if entry["passes"] >= NEEDED else ""))
    if outcomes:
        folder = pathlib.Path(path).parent
        folder.mkdir(parents=True, exist_ok=True)
        lines = ["\t".join(COLUMNS)] + ["\t".join((k, n, e["first_seen"], str(e["passes"]), ",".join(e["commits"])))
                                       for (k, n), e in sorted(counts.items())]
        temp = pathlib.Path(str(path) + ".tmp")
        temp.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        os.replace(temp, path)
    return said
