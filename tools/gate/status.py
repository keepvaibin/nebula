"""status.py: the owner's one page, D:/NebulaWork/plan/STATUS.md (docs/plan/04-GATE-AND-MAIN.md section 12). Written by
gate.py after every run and every move of main; never by hand.

    python -X utf8 -I tools/gate/status.py [--out STATUS.md] [--work gate-runs] [--board BOARD.md]

What it says, and where it reads it:
  main's commit and when it last moved, green or red   git (origin/main), the board's MAIN-MOVED, MAIN-RED, MAIN-GREEN
  the last main gate and the last long run              <work>/history.tsv
  ledger rows closed and open, per agent and package    docs/plan/ledger.tsv as main has it (this tree's when main has none)
  known failures and planned checks left                tools/gate/known-failures.tsv and planned.tsv, the same way
  commits each agent has off main                       branches engine/* (Agent 1) and mods/* (Agent 2), here and on origin
"""
import argparse
import datetime
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
GIT = next((p for p in ("C:/Program Files/Git/cmd/git.exe",) if pathlib.Path(p).exists()), "git")
CLOSED = ("fixed", "not-a-fault", "decided", "out-of-scope")
AGENT_BRANCHES = (("Agent 1", "engine"), ("Agent 2", "mods"))


def git(*args, tree=TREE):
    r = subprocess.run([GIT, "-C", str(tree), *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.stdout.strip() if r.returncode == 0 else None


def on_main(rel, tree=TREE):
    """(text, where) of a tracked file as main has it, else as this tree has it."""
    text = git("show", "origin/main:" + rel, tree=tree)
    if text is not None:
        return text, "main"
    path = pathlib.Path(tree) / rel
    return (path.read_text(encoding="utf-8") if path.exists() else ""), "this tree (%s; main has none)" % (
        git("rev-parse", "--abbrev-ref", "HEAD", tree=tree) or "?")


def ledger_counts(text):
    """{(agent, package): [closed, open]} of a ledger."""
    out = {}
    for line in text.splitlines()[1:]:
        cells = line.split("\t")
        if len(cells) < 4 or not cells[0].strip():
            continue
        key = ("Agent " + cells[1], cells[2])
        out.setdefault(key, [0, 0])[0 if cells[3] in CLOSED else 1] += 1
    return out


def list_rows(text):
    """Lines of a gate list that are rows: not comments, not the header."""
    rows = [l for l in text.splitlines() if l.strip() and not l.startswith("#")]
    return rows[1:]


# A board line of an event: its date and time, who (no colon), the event word and a colon. A line that only mentions
# the word (a decision, an ACK) is not one.
EVENT = re.compile(r"^\d{4}-\d\d-\d\d \d\d:\d\d [^:]*?\bMAIN-(MOVED|RED|GREEN):")


def board_events(board):
    """(last MAIN-MOVED line, last MAIN-RED or MAIN-GREEN line) of the board."""
    moved = colour = None
    lines = pathlib.Path(board).read_text(encoding="utf-8", errors="replace").splitlines() if board and pathlib.Path(board).exists() else []
    for line in lines:
        event = EVENT.match(line)
        if event and event.group(1) == "MOVED":
            moved = line
        elif event:
            colour = line
    return moved, colour


def last_runs(work):
    """{level: newest history row as a dict} of <work>/history.tsv."""
    path = pathlib.Path(work) / "history.tsv"
    if not path.exists():
        return {}
    lines = path.read_text(encoding="utf-8").splitlines()
    head = lines[0].split("\t") if lines else []
    out = {}
    for line in lines[1:]:
        row = dict(zip(head, line.split("\t")))
        if row.get("only", "-") == "-":
            out[row.get("level")] = row
    return out


def commits_off_main(prefix, tree=TREE):
    """How many commits the agent's branches (here and on origin) hold that main does not. Only branches built on
    main's history count: one from before it (an old line of work) is not on its way to main."""
    refs = [r for r in (git("for-each-ref", "--format=%(refname)", "refs/heads/%s/" % prefix,
                            "refs/remotes/origin/%s/" % prefix, tree=tree) or "").split()
            if git("merge-base", "--is-ancestor", "origin/main", r, tree=tree) is not None]
    if not refs:
        return 0
    return int(git("rev-list", "--count", *refs, "--not", "origin/main", tree=tree) or 0)


def run_line(row):
    if not row:
        return "none yet"
    return "%s on %s, %s, %s s (pass %s, known %s, planned %s, owed %s, failed %s): %s" % (
        row["started"], row["commit"], row["result"].upper(), row["seconds"], row["pass"], row["known"], row["planned"],
        row["owed"], row["failed"], row["run"])


def long_line(work, row, lock):
    """The newest long run: its counts, and (tools/gate/long_results.py) the repairs it opened and `running since HH:MM` while one runs."""
    import long_results
    import landing
    extra = long_results.newest_long(work, alive=landing.process_alive, lock=lock)
    return run_line(row) + ("; " + extra if row or extra != "none yet" else "")


def build(work, board, tree=TREE, now=None):
    now = now or datetime.datetime.now()
    main = git("rev-parse", "--short", "origin/main", tree=tree) or "?"
    moved, colour = board_events(board)
    when = moved[:16] if moved else "never by the gate (main's commit is from %s)" % (
        git("log", "-1", "--format=%cd", "--date=format:%Y-%m-%d %H:%M", "origin/main", tree=tree) or "?")
    state = "RED" if colour and EVENT.match(colour).group(1) == "RED" else "GREEN"
    runs = last_runs(work)
    ledger, ledger_from = on_main("docs/plan/ledger.tsv", tree)
    known, known_from = on_main("tools/gate/known-failures.tsv", tree)
    planned, planned_from = on_main("tools/gate/planned.tsv", tree)
    out = ["# Status", "",
           "Written by tools/gate/status.py at %s. Never edit by hand." % now.strftime("%Y-%m-%d %H:%M"), "",
           "## Main", "",
           "- Commit: %s; last moved: %s" % (main, when),
           "- %s%s" % (state, (" (" + colour.strip() + ")") if colour else " (no MAIN-RED on the board)"),
           "- Last main gate: " + run_line(runs.get("main")),
           "- Last landing: " + run_line(runs.get("land")),
           "- Last long run: " + long_line(work, runs.get("long"), pathlib.Path(board).parent / "long.lock" if board else "none"),
           "- Last quick gate: " + run_line(runs.get("quick")), "",
           "## Ledger (from %s)" % ledger_from, "",
           "| Agent | Package | Closed | Open |", "|---|---|---|---|"]
    counts = ledger_counts(ledger)
    totals = {}
    for (agent, package), (closed, open_) in sorted(counts.items()):
        out.append("| %s | %s | %d | %d |" % (agent, package, closed, open_))
        t = totals.setdefault(agent, [0, 0])
        t[0] += closed
        t[1] += open_
    for agent, (closed, open_) in sorted(totals.items()):
        out.append("| %s | all | %d | %d |" % (agent, closed, open_))
    out += ["", "## The gate's lists", "",
            "- Known failures: %d (from %s)" % (len(list_rows(known)), known_from),
            "- Planned checks: %d (from %s)" % (len(list_rows(planned)), planned_from), "",
            "## Off main", ""]
    for agent, prefix in AGENT_BRANCHES:
        out.append("- %s (%s/*): %d commits" % (agent, prefix, commits_off_main(prefix, tree)))
    return "\n".join(out) + "\n"


def write(path, work, board, tree=TREE):
    pathlib.Path(path).write_text(build(work, board, tree), encoding="utf-8")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--out", default="D:/NebulaWork/plan/STATUS.md")
    p.add_argument("--work", default="D:/NebulaWork/build/gate-runs")
    p.add_argument("--board", default="D:/NebulaWork/plan/BOARD.md")
    args = p.parse_args()
    write(args.out, args.work, args.board)
    print("status:", args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
