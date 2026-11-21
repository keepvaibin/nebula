"""plan.py: the step tool of the native plan (package T13). The coding agent reads, codes and proves; this says what is next.

    python -X utf8 -I tools/plan/plan.py next | status | page | card ID | rows [ID] | start ID | land ID [--github-again] | blocked ID "WHY"
                                         | owed ID "TEXT"
                                         | claim PACKAGE [--take-stale] | tool "TEXT" | toolfault --last --could-not "TEXT"
                                         | repair add --source S --kind K --check C ... | repair list
    (next, start, land, blocked, claim, tool, toolfault and status take --agent 1|2; 1 is the default; ID is a step, or for
     start, land and blocked a repair such as R-003)
    (--no-push, before the command, or NEBULA_PLAN_NO_PUSH=1 in the environment: the progress branch is not pushed)
    Each agent runs the copy in its own work folder: `D:/NebulaWork/MarioGalaxyRecomp/Nebula-native/tools/plan/plan.py` for agent 1,
    `.../Nebula-native-2/tools/plan/plan.py` for agent 2. A command with an --agent that does not match the folder its file is in
    (when that folder is one of those two) is refused with one line that names the right file. From any other tree (the tests, a
    planner's look) it works as it does without this rule.

The plan is `docs/plan/native/`: `steps.tsv` (order, step, package, title, package_needs, card_file, state, commit),
the step cards in the three card files, the package row tables, `rows/<package>.tsv`. What is `landed` is the same for both
agents: `next`, `start`, `status` and `land` fetch origin first and read `steps.tsv` from `origin/main` as well as from the tree
the file is in; a step is `landed`, with its commit, when either says so (so agent 2's landing is seen by agent 1 at once). When
the fetch or the read fails, the tree's file alone is used and one line says so. Python standard library only.

next     The first step in file order whose state is `open`, whose needs are landed, and whose package-mates before it
         are landed (the cards stack a package's steps, and `start` cuts the branch from main, so the code of an
         earlier step must be on main first; packages with different needs go side by side). A package is done when all
         its steps are `landed`. In `package_needs`, `G2a` is G2.1 to G2.7, `G2b` G2.8, `G2c` G2.9 and `everything`
         every step that does not itself need everything (and the package's own earlier steps). Prints the step, its
         whole card, its package's row table, the checks its changed paths will run, and the commands to start and to
         land it. If an earlier step is blocked it says so and names the first later step that does not depend on it.
         Two agents (--agent): agent 1, the master, follows the order of steps.tsv; while agent 2 holds a claim that is
         not stale, agent 1 passes over the packages of agent 2's list that are not yet started and takes them only when
         everything else it may do is done. Agent 2, the helper, has the fixed list G6, G7, G8, G9, G1, R10, R9, G4, in
         that order, and is never given anything else; a package that is landed or claimed by agent 1 is skipped; when
         the list is done or nothing on it can start, `next --agent 2` says so and the helper stops. Neither agent is
         given a package whose needs are not landed, nor G2.1 to G2.7 while R7 is in work (or R7 while they are):
         both change the frame and present code. A claim with no commit on its branch for 48 hours is reported as stale.
start    One work folder for each agent, kept: agent 1 works in `D:/NebulaWork/MarioGalaxyRecomp/Nebula-native`, agent 2 in
         `Nebula-native-2`. `start` makes the folder the first time (git worktree add) and uses it every later time, so the
         build beside it is kept and only changed files are compiled again. It refuses, with one line saying what to do,
         when the folder has uncommitted changes or when the agent has something in work (a step or a repair that is
         `started`: one thing at a time); then it fetches, makes the branch `native/<id>` from `origin/main` in that
         folder, sets the step `started` and claims its package for the agent. Also refuses a step whose needs are not
         landed, a package another agent holds, and for agent 2 a package that is not on its list.
land     Can be run again at any time and never resets the work folder. The tool's own commit (`Plan: step X landed`, only
         steps.tsv) is made once: a tip that already is it is handed over as it is, and fix commits on top of one get a second
         that names the newer code commit. A landing record (live folder, `landing-<agent>.tsv`) stands while the gate has the
         branch: `land` then joins ("joined the landing of X, in the queue since HH:MM"), `status` and `next` begin with
         "LANDING RUNNING: ...", `start` refuses. When origin/main already holds the step: "LANDED (found on main)". A branch that
         changes tests/replays/ lands only if the card says the replay changes, and a commit message says "replay reference".
         The extra checks of docs/plan/native/step-checks.tsv go to the gate as --also. Otherwise:
         Refuses unless every ledger row the card closes whole is `fixed` with a proof that names a test, a search or a
         measure, and every row it only advances has a dated note written by this branch; checks the file and line
         references in the changed documents and the commit messages (tools/verify/doc_refs.py); then hands the branch
         to the gate: ONE call, `gate.py land native/<id>` (the landing queue of tools/gate/landing.py: it runs the checks
         the changed paths need, in a batch with whatever else waits, and moves main). Reads the gate's `KIND` and
         `VERDICT` lines and its exit code: 0 landed, 1 fail, 2 not judged (a tool or the machine, a branch that does
         not apply: never "refused"), 3 the same failure three times. A pass sets `landed` and the commit in the same
         landing. The gate stores a failure by signature (failures.py) and writes a TOOL or ENV result, at once, as a line
         of tool-faults.tsv (live.py); it prints the command that adds the agent's "could not" sentence. A `main` line is
         information, not a failure.
blocked  Refuses while the agent's work folder has uncommitted changes (commit the unfinished work first); pushes the
         branch to origin so nothing is lost; sets `blocked`; writes the reason and the failure's first line into
         BLOCKED.md. The folder is free for the next `start`; `next` skips to independent work. For a repair it sets
         `set-aside` and writes the same into BLOCKED.md under "repairs".
claim    Claims a package for the agent; a stale claim of the other agent is taken over with --take-stale (its branch is
         left as it is).
tool     Writes a tool fault into BLOCKED.md under "tools" and into tool-faults.tsv. The step is not blocked.
toolfault --last --could-not "TEXT": the sentence on what the agent's last tool fault stopped it from doing.
status   Writes STATUS.md and prints it: steps landed, started, blocked, claims, the last five landings, the next step.
         The agent runs it at every start and after every context compaction; it is the only memory it needs.
page     Writes the progress files into the live folder (page.py), from steps.tsv, the live state, the tool faults and
         the gate's flakes.tsv and set-aside.tsv (in the live folder): progress.html (the complete page, the data
         inside it: the file a person opens, it works from disk), STATUS.md (the same data as short plain text, any
         C:/Users/<name>/ written ~/), publish/index.html (the page for the web: it fetches progress-data.json from
         beside it) and publish/progress-data.json. The old names progress-local.html and progress-data.json (at the top
         level) are removed. Every command that changes the state (start, land, blocked, claim, tool, toolfault) makes
         them again at its end; a failure there prints one warning line and changes nothing of the command's result.
         Then, when STATUS.md changed, one commit that holds exactly STATUS.md goes to origin as `progress` (a branch
         with no history in common with main; plumbing commands in the main repository, no work folder touched;
         message `Progress: N of M steps landed`). A failed push is one warning line. Nothing else is ever pushed.
card     `card ID`: any step's card, its package's rows and the checks it needs (a repair's card); changes nothing, no --agent.
owed     `owed ID "TEXT"`: what only a person or other hardware can check, appended to owed.tsv (live folder) and listed by
         status, STATUS.md and the page; it blocks nothing.
rows     What a card's "Rows." line means (closes whole / advances), for one step or all of them.
repair   `repair add --source nightly|stress|measure --kind fail|stress|worse --check CHECK [--renderer R] [--step ID
         --commit SHA] --evidence FOLDER [--row ROW] --note "TEXT"` writes a line of repairs.tsv (the live folder) and prints
         its id; the same check and renderer already `open`, `started` or `set-aside` is not written twice: its `seen` count
         goes up and the date is added to its note. `repair list` prints them, open ones first. `next` gives the oldest
         open repair of kind `fail` before any step (stress and worse repairs never come first and never block);
         `start R-003` makes the branch `native/repair-003` in the agent's folder; `land R-003` hands it to
         `gate.py land --also <its check>` and on a pass sets the repair `landed` with the commit; `blocked R-003 "WHY"`
         sets it `set-aside` and `next` goes on with the steps. Agent 1's work only.

Exit codes: 0 done; 1 refused or failed; 2 the command was wrong; 3 the same failure three times; 4 a tool or
machine fault only, or a branch that does not apply (the gate's "not judged": it does not count as a failure of the change).

State. `landed` and the commit are in steps.tsv (the repository) and travel with the landing's own commit; after a pass
they are written into the plan tree too (and `origin/main` has them from the landing, which is what the other agent reads). Everything that more than one agent, or the progress page, must see is in the
live folder (LIVE below; `--live` for tests): started and blocked steps, claims, tool faults, the landings' times, the
page. It is never kept in a file inside one work folder. STATUS.md is the plan tree's own, untracked; BLOCKED.md is a
plain file the agent may commit.
"""
import argparse
import datetime
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "verify"))
sys.path.insert(0, str(TREE / "tools" / "gate"))

import failures  # noqa: E402
import doc_refs  # noqa: E402
import live as lv  # noqa: E402
import landing  # noqa: E402
import page  # noqa: E402
import scope  # noqa: E402

GIT = "C:/Program Files/Git/cmd/git.exe"
NATIVE = "docs/plan/native"
WORK_ROOT = "D:/NebulaWork/MarioGalaxyRecomp"
FAILURES = "D:/NebulaWork/plan/landing-failures.tsv"
TABLET = "R52WA0FXE5J"
TABLET_WAIT = 30
# One work folder for each agent, kept between steps (made by `start` the first time).
AGENT_FOLDERS = {"1": "Nebula-native", "2": "Nebula-native-2"}
LIVE = "D:/NebulaWork/plan/native"       # the live state: claims, started and blocked steps, tool faults, the page
AGENT2_LIST = ("G6", "G7", "G8", "G9", "G1", "R10", "R9", "G4")   # the helper's packages, in this order, and no others
# Two groups of steps that must not be in work at the same time (both change the renderers' frame and present code).
PAIRS = ((("G2a",), ("R7",)),)

QUEUE_FILE = str(landing.QUEUE)          # the gate's landing queue (a landing record is read against it); tests give their own
alive = landing.process_alive            # whether a process of this id runs; tests put their own in here
JOIN_POLL = 5.0                          # seconds between two looks while a landing that is no longer in the queue is finishing
STATE_COMMIT = "Plan: step %s landed"    # the subject of the tool's own commit: the step's row is `landed`, with the code's commit

# The extra checks a step's proof asks for (docs/plan/native/step-checks.tsv): the nightly's parts, which `gate.py land --also` takes.
STEP_CHECKS_FILE = "step-checks.tsv"
ALSO_CHECKS = ("stress", "sanitizer/tour", "soak", "replays/software")

# A replay reference (tests/replays/*.tsv) changes only where the card says so (T14 item 5).
REPLAY_DIR = "tests/replays/"
REPLAY_ALLOWED = re.compile(r"re-?record|recorded again|replay changes|new reference", re.I)
REPLAY_REFUSAL = "the replay differs and this step's card does not say it may: the change altered the game; find why"
REPLAY_WORDS = "replay reference"
# How a replay reference is recorded: step_hash.py's `record` takes the work folder of a luma_test.py run in which the tour played
# every recorded stop with the hash on, and writes one tests/replays/<stop>.tsv for each. T10 proves this command by a real run.
REPLAY_RECORD = "python -X utf8 -I tools/verify/step_hash.py record <work folder of the tour run> --out tests/replays"

STEP_COLUMNS = ["order", "step", "package", "title", "package_needs", "card_file", "state", "commit"]
STATES = ("open", "started", "blocked", "landed")

# `G2a` is the first seven landings of package G2, and so on.
SPLITS = {"G2a": ("G2", range(1, 8)), "G2b": ("G2", range(8, 9)), "G2c": ("G2", range(9, 10))}


def say(text=""):
    sys.stdout.write(text + "\n")


# ---- git -----------------------------------------------------------------------------------------------------

def git(folder, *args, check=False):
    exe = GIT if pathlib.Path(GIT).exists() else "git"
    r = subprocess.run([exe, "-C", str(folder), *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if check and r.returncode != 0:
        raise RuntimeError("git %s: %s" % (" ".join(args), (r.stderr or r.stdout).strip()))
    return r


def git_out(folder, *args):
    return git(folder, *args, check=True).stdout.strip()


# ---- the plan's files ----------------------------------------------------------------------------------------

def native(tree):
    return pathlib.Path(tree) / NATIVE


def read_base(tree):
    """The steps as `steps.tsv` has them, in file order: [dict]. Raises ValueError for a file that is not the plan's."""
    path = native(tree) / "steps.tsv"
    return parse_base(path.read_text(encoding="utf-8"), path)


def parse_base(text, path):
    lines = text.splitlines()
    if not lines or lines[0].split("\t") != STEP_COLUMNS:
        raise ValueError("%s: the first line must be %s" % (path, "\t".join(STEP_COLUMNS)))
    steps = []
    for n, line in enumerate(lines[1:], 2):
        if not line.strip():
            continue
        cells = line.split("\t")
        cells += [""] * (len(STEP_COLUMNS) - len(cells))
        if len(cells) != len(STEP_COLUMNS):
            raise ValueError("%s:%d: %d columns, want %d" % (path, n, len(cells), len(STEP_COLUMNS)))
        row = dict(zip(STEP_COLUMNS, cells))
        if row["state"] not in STATES:
            raise ValueError("%s:%d: state %r is none of %s" % (path, n, row["state"], ", ".join(STATES)))
        steps.append(row)
    if len({s["step"] for s in steps}) != len(steps):
        raise ValueError("%s: a step id is listed twice" % path)
    return steps


def live_dir(live=None):
    return pathlib.Path(live or LIVE)


def now():
    return datetime.datetime.now()


atomic_write = lv.atomic_write


# The steps `origin/main`'s steps.tsv has as landed (step -> commit), filled for the commands that read it. MAIN_READ is True when
# that file could be read (after the fetch, or from the last fetched copy): then it alone says what is landed. When it could
# not be read at all (a tree with no such ref) both are empty and the tree's own file is believed.
MAIN_LANDED = {}
MAIN_READ = []


def read_steps(tree, live=None):
    """The steps, in file order, with their state: `landed` (with the commit) when `origin/main`'s steps.tsv says so (MAIN_LANDED);
    a step that the tree's own steps.tsv has as landed and `origin/main` does not is one whose landing has not passed, so it is
    not landed: it is open, and the live state makes it `started`. Only when `origin/main` could not be read at all is the
    tree's file believed. For an open step the live state (`started`, `blocked`, with `agent` and `reason`). Every row has `agent`
    and `reason`."""
    steps = read_base(tree)
    local = lv.read_state(live_dir(live))
    for s in steps:
        if MAIN_READ:
            if s["step"] in MAIN_LANDED:
                s["state"], s["commit"] = "landed", MAIN_LANDED[s["step"]]
            elif s["state"] == "landed":
                s["state"], s["commit"] = "open", ""
        elif s["state"] == "open" and s["step"] in MAIN_LANDED:
            s["state"], s["commit"] = "landed", MAIN_LANDED[s["step"]]
    for s in steps:
        s["agent"], s["reason"] = "", ""
        if s["state"] == "open" and s["step"] in local:
            e = local[s["step"]]
            s["state"], s["agent"], s["reason"] = e["state"], e["agent"], e["reason"]
    return steps


def write_steps(tree, steps):
    text = "\t".join(STEP_COLUMNS) + "\n" + "".join("\t".join(s[c] for c in STEP_COLUMNS) + "\n" for s in steps)
    atomic_write(native(tree) / "steps.tsv", text)


def set_state(tree, step_id, state, commit=None, agent=1, reason="", live=None, track=True):
    """Sets a step's state. `landed` (with the commit) is written into steps.tsv: that is what a landing carries.
    `started` and `blocked` are the live folder's (state.tsv there, with the agent and the reason), so that both agents
    and the page see them and steps.tsv stays what main has; `open` takes a step out of it. `track` False leaves the live
    folder alone (the landing's own commit, in the work folder, before the gate has answered)."""
    base = read_base(tree)
    row = find(base, step_id)
    if row is None:
        raise KeyError(step_id)
    if state == "landed":
        row["state"] = "landed"
        if commit is not None:
            row["commit"] = commit
        write_steps(tree, base)
    if track:
        lv.set_state_entry(live_dir(live), step_id, state, agent, reason)


def find(steps, step_id):
    for s in steps:
        if s["step"] == step_id:
            return s
    return None


# ---- needs and order -----------------------------------------------------------------------------------------

def need_ids(step, steps):
    """The ids of the steps that must be landed before `step` may start: its package_needs, and the steps of its own
    package that come before it in the file."""
    ids = []
    by_pkg = {}
    for s in steps:
        by_pkg.setdefault(s["package"], []).append(s["step"])
    for token in [t.strip() for t in step["package_needs"].split(",") if t.strip()]:
        if token == "everything":
            ids += [s["step"] for s in steps if s["step"] != step["step"] and "everything" not in
                    [t.strip() for t in s["package_needs"].split(",")]]
        elif token in SPLITS:
            pkg, numbers = SPLITS[token]
            ids += ["%s.%d" % (pkg, n) for n in numbers]
        else:
            ids += by_pkg.get(token, [])
    for s in steps:                                  # the package's own earlier steps (stack order)
        if s["step"] == step["step"]:
            break
        if s["package"] == step["package"]:
            ids.append(s["step"])
    seen, out = set(), []
    for i in ids:
        if i != step["step"] and i not in seen:
            seen.add(i)
            out.append(i)
    return out


def unmet(step, steps):
    """[(id, state)] of the needs that are not landed. An id the file does not list is `missing`."""
    state = {s["step"]: s["state"] for s in steps}
    return [(i, state.get(i, "missing")) for i in need_ids(step, steps) if state.get(i) != "landed"]


class Choice:
    def __init__(self, step, blocked_before, notes=()):
        self.step, self.blocked_before, self.notes = step, blocked_before, list(notes)


def group_ids(steps, token):
    """The step ids of a package, or of a split (`G2a`)."""
    if token in SPLITS:
        pkg, numbers = SPLITS[token]
        return {"%s.%d" % (pkg, n) for n in numbers}
    return {s["step"] for s in steps if s["package"] == token}


def pair_conflict(step, steps):
    """Why `step` may not start now because its pair is in work ("" when it may): a started step in the other group."""
    for left, right in PAIRS:
        a = set().union(*[group_ids(steps, x) for x in left])
        b = set().union(*[group_ids(steps, x) for x in right])
        for mine, other, name in ((a, b, right), (b, a, left)):
            if step["step"] in mine and any(s["state"] == "started" and s["step"] in other for s in steps):
                return "%s is in work, and both change the renderers' frame and present code" % "/".join(name)
    return ""


def has_progress(steps, package):
    return any(s["package"] == package and s["state"] in ("landed", "started", "blocked") for s in steps)


def choose_next(steps, claims=(), stale=frozenset()):
    """The step agent 1 (the master) is offered: the first in file order that is open, has its needs landed, is not of a
    package another agent holds, and whose pair is not in work; with the blocked steps that came before it in the file.
    While agent 2 holds a claim that is not stale, packages of agent 2's list that have no step landed or started are
    left to the end. None when nothing can start."""
    helper_active = any(c["agent"] == "2" and c["package"] not in stale for c in claims)
    blocked, deferred = [], []
    for s in steps:
        if s["state"] == "blocked":
            blocked.append(s)
            continue
        if s["state"] != "open" or unmet(s, steps):
            continue
        c = lv.claim_of(claims, s["package"])
        if c and c["agent"] != "1":
            continue
        if pair_conflict(s, steps):
            continue
        if helper_active and s["package"] in AGENT2_LIST and not (c and c["agent"] == "1") and not has_progress(steps, s["package"]):
            deferred.append(s)
            continue
        return Choice(s, blocked)
    if deferred:
        return Choice(deferred[0], blocked, ["Everything else agent 1 may do is done, so it takes package %s from agent 2's list." %
                                             deferred[0]["package"]])
    return None


def choose_helper(steps, claims=(), stale=frozenset()):
    """What agent 2 (the helper) is offered: (Choice or None, [(package, why not)]). Its list, in its order; a package
    already landed or held by agent 1 is skipped; a package is done in order, so only its first step that is not landed
    can be offered."""
    report = []
    for pkg in AGENT2_LIST:
        mine = [s for s in steps if s["package"] == pkg]
        pending = [s for s in mine if s["state"] != "landed"]
        if not mine:
            report.append((pkg, "is not in the plan"))
            continue
        if not pending:
            report.append((pkg, "is landed"))
            continue
        c = lv.claim_of(claims, pkg)
        if c and c["agent"] != "2":
            report.append((pkg, "is claimed by agent %s%s" % (c["agent"], " (stale)" if pkg in stale else "")))
            continue
        s = pending[0]
        if s["state"] == "started":
            report.append((pkg, "has step %s started%s: land it first" % (s["step"], " by agent %s" % s["agent"] if s["agent"] else "")))
            continue
        if s["state"] == "blocked":
            report.append((pkg, "has step %s blocked: %s" % (s["step"], s["reason"] or "(no reason written)")))
            continue
        waiting = unmet(s, steps)
        if waiting:
            by_package = {}
            for sid, _state in waiting:
                owner = find(steps, sid)
                by_package.setdefault(owner["package"] if owner else sid, []).append(sid)
            shown = ", ".join("%s (%d not landed, first %s)" % (p, len(ids), ids[0]) for p, ids in by_package.items())
            report.append((pkg, "waits for %s" % shown))
            continue
        why = pair_conflict(s, steps)
        if why:
            report.append((pkg, "waits: " + why))
            continue
        return Choice(s, []), report
    return None, report


def branch_time(tree, branch):
    """The time of the newest commit of a branch, in seconds, or None when the branch is not there."""
    r = git(tree, "log", "-1", "--format=%ct", "refs/heads/" + branch)
    if r.returncode != 0 or not r.stdout.strip().isdigit():
        return None
    return int(r.stdout.strip())


def stale_of(tree, claims, at=None):
    return lv.stale_packages(claims, lambda b: branch_time(tree, b), at or now())


# ---- cards ---------------------------------------------------------------------------------------------------

def card_lines(tree, step):
    path = native(tree) / step["card_file"]
    return path.read_text(encoding="utf-8").splitlines()


def cut_card(lines, step_id):
    """The card of `### <id> ...`: its heading to the next heading, package heading or rule. None if there is none."""
    head = "### %s " % step_id
    for i, line in enumerate(lines):
        if line.startswith(head) or line.rstrip() == "### " + step_id:
            j = i + 1
            while j < len(lines) and not re.match(r"(### |## |---\s*$)", lines[j]):
                j += 1
            while j > i + 1 and not lines[j - 1].strip():
                j -= 1
            return "\n".join(lines[i:j])
    return None


def package_table(lines, package):
    """The `### Rows of this package` table under `## <package> ...`, or None."""
    start = None
    for i, line in enumerate(lines):
        if re.match(r"## %s(\s|$)" % re.escape(package), line):
            start = i
            break
    if start is None:
        return None
    end = len(lines)
    for j in range(start + 1, len(lines)):
        if lines[j].startswith("## "):
            end = j
            break
    for i in range(start, end):
        if lines[i].startswith("### Rows of this package"):
            j = i + 1
            while j < end and not re.match(r"(### |---\s*$)", lines[j]):
                j += 1
            return "\n".join(l for l in lines[i + 1:j]).strip("\n")
    return None


def card_text(tree, step):
    return cut_card(card_lines(tree, step), step["step"])


# ---- the card's "Rows." line --------------------------------------------------------------------------------

ROW_ID = re.compile(r"\b[PNMI]-\d{3,4}\b")
ROWS_LINE = re.compile(r"^- \*\*Rows[.:]\*\*\s*(.*)$", re.M)
CLOSE, ADVANCE, MENTION = "closes", "advances", "mentioned"


class RowsReading:
    """What a card's Rows line says: `closes` (row ids that must be fixed), `advances` (row ids that need a dated
    note), `mentioned` (named, neither), `ok` (False: the line is missing or says nothing this parser can read)."""

    def __init__(self, ok, closes=(), advances=(), mentioned=(), raw="", why=""):
        self.ok, self.closes, self.advances, self.mentioned, self.raw, self.why = (
            ok, list(closes), list(advances), list(mentioned), raw, why)

    def __repr__(self):
        return "RowsReading(ok=%r, closes=%r, advances=%r)" % (self.ok, self.closes, self.advances)


def _mask_parens(text):
    """`text` with what stands inside parentheses blanked (depth kept), and its first sentence's end."""
    out, depth = [], 0
    for ch in text:
        if ch == "(":
            depth += 1
            out.append(" ")
        elif ch == ")":
            depth = max(0, depth - 1)
            out.append(" ")
        else:
            out.append(" " if depth else ch)
    return "".join(out)


def _first_sentence(text):
    """The clause up to its first full stop or semicolon outside parentheses."""
    depth = 0
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth = max(0, depth - 1)
        elif depth == 0 and (ch == ";" or (ch == "." and (i + 1 == len(text) or text[i + 1] in " \n"))):
            return text[:i]
    return text


_CLOSES_HERE = re.compile(r"(?:^|[.;]\s+)\s*(?:the row\s+|it\s+|also\s+)?closes\s+here\b|\(closes\)|(?:^|[.;]\s+)\s*closes\b(?!\s+at\b)", re.I)
_CLOSES_LATER = re.compile(r"\bclos(?:es|ed|e)?\s+at\s+(?:the\s+later|[A-Z]\d)|\bstays\b", re.I)
_ADVANCE_WORDS = re.compile(r"\badvance[sd]?\b|\bpart\b|\bparts\b|\bhalf\b|\bclos(?:es|ed)?\s+at\b|\bclose\s+at\b", re.I)
_WHOLE = re.compile(r"\bwhole\b|\bconfirmed\b", re.I)
_PART_OF = re.compile(r"\b(?:part|parts|half|halves)\s+of\s*$", re.I)


def classify(clause, before=""):
    """(kind, explicit) for the words after one row id (`clause`, up to the next id) and the words before it: CLOSE,
    ADVANCE or MENTION (see the module's rules in the comments below). `explicit` is False when nothing in the words
    marked it and the default (an id with no qualifier closes) applied.

    Closes: "whole", "(closes)", "closes here" at the start of a sentence, "closes" right before the id, "confirmed".
    Advances: "advance(s|d)", "part", "half", "closes at <another step>", "stays", and a colon followed by words that say
    none of the above ("N-1053: finds what is left"). When the words could mean either, the row only advances: a refused
    landing nobody can satisfy is worse than a weaker check."""
    if re.search(r"\bcloses?\s*$", before, re.I):
        return CLOSE, True
    if re.search(r"\badvance[sd]?\s*$", before, re.I):
        return ADVANCE, True
    if _CLOSES_HERE.search(clause):
        return CLOSE, True
    if _CLOSES_LATER.search(clause) or _PART_OF.search(before):
        return ADVANCE, True
    sentence = _first_sentence(clause)
    outside = _mask_parens(sentence)
    if _ADVANCE_WORDS.search(outside):
        return ADVANCE, True
    if _WHOLE.search(outside) or re.match(r"\s*:?\s*\(\s*whole\b", sentence, re.I) or re.search(r"\bwhole\s*\)", sentence, re.I):
        return CLOSE, True
    if re.search(r"\(\s*(?:[^()]*?)\b(?:part|parts|half)\b", sentence, re.I):
        return ADVANCE, True
    if re.match(r"\s*:\s*\S", outside) and outside.strip(" :."):
        return ADVANCE, True
    return CLOSE, False


_JOIN = re.compile(r"^\s*(?:,|,?\s*and|,?\s*or|&)\s*$")


def _depth(text, pos):
    depth = 0
    for ch in text[:pos]:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth = max(0, depth - 1)
    return depth


def parse_rows(text):
    """The Rows line's text (after `- **Rows.**`) read as closes / advances. See RowsReading and classify."""
    raw = text.strip()
    none_line = bool(re.match(r"(?i)(?:none\b|no row\b)", raw))
    matches = list(ROW_ID.finditer(raw))
    if not matches:
        if none_line:
            return RowsReading(True, raw=raw)
        return RowsReading(False, raw=raw, why="the line names no row and does not say none")
    groups = []                                       # [[match, ...]]
    for m in matches:
        if groups and _JOIN.match(raw[groups[-1][-1].end():m.start()]):
            groups[-1].append(m)
        else:
            groups.append([m])
    verdict = {}
    order = []
    prev_end = 0
    for gi, group in enumerate(groups):
        end = groups[gi + 1][0].start() if gi + 1 < len(groups) else len(raw)
        clause = raw[group[-1].end():end]
        before = raw[prev_end:group[0].start()]
        kind, explicit = classify(clause, before)
        if none_line:
            # "none of this package's rows ... P-155 stays open": rows are named, not worked, unless the line says it
            # advances them or closes them ("No row of this package. Closes P-096, ...").
            if re.search(r"\bcloses?\s*$", before, re.I):
                kind = CLOSE
            elif re.search(r"\badvance[sd]?\b", clause, re.I) or re.search(r"\badvance[sd]?\s*$", before, re.I):
                kind = ADVANCE
            else:
                kind = MENTION
        elif _depth(raw, group[0].start()) and not explicit:
            kind = MENTION                            # a row named inside a bracket, with no word about it
        for m in group:
            rid = m.group(0)
            if rid not in verdict:
                order.append(rid)
                verdict[rid] = kind
            elif kind == CLOSE or (kind == ADVANCE and verdict[rid] == MENTION):
                verdict[rid] = kind
        prev_end = group[-1].end()
    return RowsReading(True, [r for r in order if verdict[r] == CLOSE], [r for r in order if verdict[r] == ADVANCE],
                       [r for r in order if verdict[r] == MENTION], raw)


def card_rows(card):
    """RowsReading of one card's text (its `- **Rows.**` line; the line may continue on indented lines)."""
    if card is None:
        return RowsReading(False, why="no card")
    lines = card.splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"^- \*\*Rows[.:]\*\*\s*(.*)$", line)
        if m:
            text = m.group(1)
            for follow in lines[i + 1:]:
                if follow.startswith("  ") and follow.strip() and not re.match(r"\s+\d+\.", follow):
                    text += " " + follow.strip()
                else:
                    break
            return parse_rows(text)
    return RowsReading(False, why="the card has no Rows line")


# ---- the ledger ----------------------------------------------------------------------------------------------

LEDGER = "docs/plan/ledger.tsv"
DATE = re.compile(r"\b(?:20\d\d-)?(?:0[1-9]|1[0-2])-(?:0[1-9]|[12]\d|3[01])\b")
PROOF_WORD = re.compile(r"\b(?:tests?|ctest|search(?:ed|es)?|grep|measure[sd]?|measurement|replays?|gate)\b|_tests?\b|tests/",
                        re.I)


def read_ledger(text):
    """{id: {column: text}} from the ledger's text (CRLF or LF, 7 columns). Raises ValueError for a wrong row."""
    rows, cols = {}, None
    for n, line in enumerate(text.replace("\r\n", "\n").split("\n"), 1):
        if not line:
            continue
        cells = line.split("\t")
        if cols is None:
            cols = cells
            if len(cols) != 7 or cols[0] != "id":
                raise ValueError("ledger line 1: want the 7 columns id, agent, package, state, proof, title, note")
            continue
        if len(cells) != len(cols):
            raise ValueError("ledger line %d: %d columns, want %d" % (n, len(cells), len(cols)))
        rows[cells[0]] = dict(zip(cols, cells))
    return rows


def ledger_problems(reading, ledger, base=None):
    """Why a step may not land by its ledger rows: [text]. `base`: the ledger on main (an advanced row's note must
    differ from main's, so that it is this landing's note)."""
    out = []
    for rid in reading.closes:
        row = ledger.get(rid)
        if row is None:
            out.append("%s: the card says it is closed here, and the ledger has no such row" % rid)
        elif row["state"] != "fixed":
            out.append("%s: the card closes it whole, and its state is %r (want fixed)" % (rid, row["state"]))
        elif not PROOF_WORD.search(row["proof"]):
            out.append("%s: fixed, but its proof names no test, search or measure: %r" % (rid, row["proof"][:80]))
    for rid in reading.advances:
        row = ledger.get(rid)
        if row is None:
            out.append("%s: the card says it advances, and the ledger has no such row" % rid)
        elif not DATE.search(row["note"]):
            out.append("%s: the card advances it, and its note has no date" % rid)
        elif base is not None and rid in base and base[rid]["note"] == row["note"]:
            out.append("%s: the card advances it, and its note is the one main already has" % rid)
    return out


# ---- which checks the changed paths run ----------------------------------------------------------------------

# tools/gate/scope.tsv (package T4) says it: every row a path matches adds its groups, the landing runs the union, and a path
# no row names makes it full. The groups and the checks each stands for are tools/gate/scope.py's.


def read_scope(tree):
    """The scope table of the plan tree (tools/gate/scope.tsv), else this file's tree's own."""
    for base in (pathlib.Path(tree), TREE):
        path = base / "tools" / "gate" / "scope.tsv"
        if path.is_file():
            return scope.read_table(path)
    raise scope.ScopeError(["tools/gate/scope.tsv is not in %s" % tree])


def checks_for(paths, table):
    """What the landing of a change of `paths` runs, as lines: the groups, each with what it stands for, and the names of
    the checks. A path no row names runs everything (the main gate's checks on the native game)."""
    found = scope.plan_for(paths, table)
    lines = scope.describe(found)
    if not paths:
        lines[0] += " (the card names no path of the tree)"
    if not found.full:
        lines.append("checks: " + ", ".join(scope.check_patterns(found.groups)))
    return lines


def card_paths(card, tree):
    """The paths of the tree that a card's Where line names in backticks (the first folder must exist in the tree,
    which leaves out branch names such as rows/p022c)."""
    where = ""
    for line in (card or "").splitlines():
        if line.startswith("- **Where.**"):
            where = line
            break
    out = []
    for span in doc_refs.SPAN.findall(where):
        m = doc_refs.REF.match(span.strip())
        if m and not doc_refs.OUTSIDE.match(span.strip()) and (pathlib.Path(tree) / m.group("path").split("/")[0]).is_dir():
            if m.group("path") not in out:
                out.append(m.group("path"))
    return out


# ---- the extra checks a step's proof asks for ----------------------------------------------------------------------

def read_step_checks(tree):
    """{step: [(check, serves)]} from docs/plan/native/step-checks.tsv (columns step, check, serves). A check that is not one of
    ALSO_CHECKS is refused (ValueError): the gate takes no other name as an `--also` of a step. A tree with no such file has none."""
    path = native(tree) / STEP_CHECKS_FILE
    out = {}
    if not path.is_file():
        return out
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0].split("\t") != ["step", "check", "serves"]:
        raise ValueError("%s: the first line must be step, check, serves separated by tabs" % path)
    for n, line in enumerate(lines[1:], 2):
        if not line.strip():
            continue
        cells = line.split("\t")
        if len(cells) != 3 or not all(c.strip() for c in cells):
            raise ValueError("%s:%d: want step, check and serves" % (path, n))
        if cells[1] not in ALSO_CHECKS:
            raise ValueError("%s:%d: check %r is none of %s" % (path, n, cells[1], ", ".join(ALSO_CHECKS)))
        out.setdefault(cells[0], []).append((cells[1], cells[2]))
    return out


def also_names(tree, step_id):
    """The extra checks of a step, each once, in the table's order."""
    names = []
    for check, _ in read_step_checks(tree).get(step_id, []):
        if check not in names:
            names.append(check)
    return names


def also_lines(tree, step_id):
    return ["and, because this step's proof asks for it: %s (%s)" % (check, serves)
            for check, serves in read_step_checks(tree).get(step_id, [])]


# ---- the gate ------------------------------------------------------------------------------------------------

GATE_ROW = re.compile(r"^(\S+)\s+(pass|known|planned|owed|FAIL|TOOL|ENV|FLAKE|NEW)\s+(\d+(?:\.\d+)?) s\s*(.*)$")
KIND_OF = {"pass": "PASS", "known": "PASS", "planned": "PASS", "owed": "ENV", "FAIL": "FAIL", "TOOL": "TOOL", "ENV": "ENV",
           "FLAKE": "FLAKE", "NEW": "NEW"}


class Verdict:
    """The landing's result: `passed`; `rows` [(check, kind, detail)] (every check the gate printed); `failures`
    [(check, kind, first_error_line)]: what did not pass; `lines`: the gate's output. Set by the gate's own protocol
    (`gate.py land`): `code` its exit code (0 landed, 1 fail, 2 not judged, 3 the same failure three times), `commit` the
    commit main has the branch as, and `gate_recorded`: the gate has stored the failure's signature and written any tool or
    machine fault itself, so the step tool does neither."""

    def __init__(self, passed, rows=(), failures_=(), lines=()):
        self.passed, self.rows, self.failures, self.lines = passed, list(rows), list(failures_), list(lines)
        self.code, self.commit, self.gate_recorded = None, "", False

    def counts(self):
        out = {}
        for _, kind, _ in self.rows:
            out[kind] = out.get(kind, 0) + 1
        return out


def read_gate_output(lines):
    """[(check, kind, detail, state as the gate printed it)] for every check row in the gate's output."""
    rows = []
    for line in lines:
        m = GATE_ROW.match(line.rstrip())
        if m:
            rows.append((m.group(1), KIND_OF[m.group(2)], m.group(4).strip(), m.group(2)))
    return rows


def read_land_output(lines, code, branch=""):
    """The Verdict of one `gate.py land` from its output and exit code. The gate's last lines are `KIND <kind> <check> <test> ::
    <message>` for each result of its runs that is not a pass and `VERDICT pass | fail | not-judged`; before them a `LAND
    <branch> <state> <detail>` line for each branch. Its exit code: 0 landed, 1 fail, 2 not judged (a tool or machine fault, a
    branch that does not apply: never "refused"), 3 the same failure three times. A `main` KIND line is information, not a
    failure; a flake is not one either."""
    lines = [l.rstrip("\r\n") for l in lines]
    rows = [(c, k, d) for c, k, d, _ in read_gate_output(lines)]
    kind_lines = [l for l in lines if l.startswith("KIND ")]
    counted = [(c, k.upper(), m) for c, k, m in landing.failures_of_lines(kind_lines)]
    verdict = next((l.split()[1] for l in reversed(lines) if l.startswith("VERDICT ")), None)
    mine = next((l.split(" ", 3) for l in lines if l.startswith("LAND %s " % branch)), None) if branch else None
    v = None
    if verdict is None or code not in (0, 1, 2, 3):
        why = next((l.split("gate.py:", 1)[1].strip() for l in lines if l.startswith("gate.py:")), "")
        v = Verdict(False, rows, counted or [("gate.py", "TOOL", why or "exited %s with no VERDICT line" % code)], lines)
        v.code = 2
        return v
    if code == 0:
        v = Verdict(True, rows, [], lines)
    elif code in (1, 3):
        v = Verdict(False, rows, [f for f in counted if f[1] == "FAIL"] or counted or
                    [("gate.py", "FAIL", (mine[3] if mine and len(mine) > 3 else "failed with no KIND line"))], lines)
    else:
        why = (mine[2] + " " + mine[3]) if mine and len(mine) > 3 else "not judged"
        v = Verdict(False, rows, counted or [("land", "NOT-JUDGED", why.strip())], lines)
    v.code, v.gate_recorded = code, True
    if mine and len(mine) > 3 and mine[2] == "landed":
        v.commit = mine[3].replace("main now has it as ", "").split()[0]
    return v


def run_gate(folder, args, out=None, branch=""):
    """One gate.py run from the step's work folder. Returns a Verdict. The gate's own words go to `out` as they come."""
    out = out or sys.stdout
    cmd = [sys.executable, "-X", "utf8", "-I", str(pathlib.Path(folder) / "tools" / "gate" / "gate.py"), *args]
    out.write("plan.py: %s\n" % " ".join(cmd[3:]))
    lines = []
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    try:
        with subprocess.Popen(cmd, cwd=str(folder), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                              text=True, encoding="utf-8", errors="replace") as p:
            for line in p.stdout:
                lines.append(line)
                out.write(line)
                out.flush()
            code = p.wait()
    except OSError as e:
        return Verdict(False, [], [("gate.py", "TOOL", "could not be started: %s" % e)], lines)
    return read_land_output(lines, code, branch)


def land_with_gate(folder, branch, tablet=TABLET, tablet_wait=TABLET_WAIT, extra=(), out=None, push=True):
    """THE LANDING STEP: the branch pushed (a backup on origin, and the one the gate's queue reads), then ONE call of
    `gate.py land` from the step's work folder, which queues it, runs the checks its changed paths need, and moves main.
    Reads the gate's KIND and VERDICT lines and its exit code (0 landed, 1 fail, 2 not judged, 3 the same failure three
    times). `extra`: more options for the gate (--agent, --live, --failures, --also). `push` False (a landing that is joined: the gate
    has the branch in its queue): nothing is pushed."""
    out = out or sys.stdout
    # --force-with-lease: the branch is this step's own, and a try that went before may have left origin's copy of it behind.
    pushed = git(folder, "push", "--force-with-lease", "origin", branch) if push else None
    if pushed is not None and pushed.returncode != 0:
        return Verdict(False, [], [("git/push", "FAIL", (pushed.stderr.strip().splitlines() or ["push refused"])[-1])])
    return run_gate(folder, ["land", branch, "--tablet", tablet, "--tablet-wait", str(tablet_wait), *extra], out, branch)


LAND = land_with_gate            # tests put their own in here


def gate_options(a, agent):
    """The options `gate.py land` gets from this tool: who asks, where the faults and the failures are kept."""
    options = ["--agent", agent, "--live", live_dir(a.live).as_posix(), "--failures", str(a.failures)]
    if str(getattr(a, "queue_file", QUEUE_FILE)) != QUEUE_FILE:
        options += ["--queue-file", str(a.queue_file)]      # a queue of its own (a test); the gate's own is the default
    if getattr(a, "github_again", False):
        options.append("--github-again")      # a red GitHub job is run again once before the landing judges (gate.py --github-again)
    return options


# ---- BLOCKED.md, STATUS.md, landings.tsv ---------------------------------------------------------------------

BLOCKED_HEAD = ("# Blocked steps and tool faults\n\nWritten by plan.py. A blocked step is skipped by `plan.py next`; a tool "
                "fault is not the step's.\n\n## tools\n\n## steps\n")


def append_under(path, heading, line):
    path = pathlib.Path(path)
    text = path.read_text(encoding="utf-8") if path.exists() else BLOCKED_HEAD
    lines = text.splitlines()
    try:
        i = lines.index("## " + heading)
    except ValueError:
        lines += ["", "## " + heading]
        i = len(lines) - 1
    j = i + 1
    while j < len(lines) and not lines[j].startswith("## "):
        j += 1
    while j > i + 1 and not lines[j - 1].strip():
        j -= 1
    lines.insert(j, line)
    atomic_write(path, "\n".join(lines) + "\n")


def blocked_reasons(tree):
    """{step id: reason line} from BLOCKED.md's steps section (the last entry of a step wins)."""
    path = native(tree) / "BLOCKED.md"
    out = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            m = re.match(r"^- (\d{4}-\d\d-\d\d) (\S+): (.*)$", line)
            if m:
                out[m.group(2)] = m.group(3)
    return out


def tool_notes(tree):
    path = native(tree) / "BLOCKED.md"
    if not path.exists():
        return []
    lines, out, on = path.read_text(encoding="utf-8").splitlines(), [], False
    for l in lines:
        if l.startswith("## "):
            on = l == "## tools"
        elif on and l.startswith("- "):
            out.append(l)
    return out


def work_folder(work_root, agent=1):
    """The agent's own work folder: the same one for every step and repair."""
    return pathlib.Path(work_root) / AGENT_FOLDERS[str(agent)]


def count_flakes(live):
    """The number of lines in the gate's flakes.tsv, which it writes in the live folder (D:/NebulaWork/plan/native: time,
    test, renderer, commit, the first failure's message); 0 when there is no file."""
    path = live_dir(live) / "flakes.tsv"
    if not path.exists():
        return 0
    lines = [l for l in path.read_text(encoding="utf-8").splitlines() if l.strip() and not l.startswith("#")]
    return len(lines) - (1 if lines and lines[0].lower().startswith(("date", "time\t")) else 0)


def read_set_aside(live):
    """[{test, renderer, since, why}] of the gate's set-aside.tsv (time, test, renderer, why): the tests that flaked three times
    in seven days. They still run and are reported; their failure does not count."""
    path = live_dir(live) / "set-aside.tsv"
    out = []
    for line in path.read_text(encoding="utf-8").splitlines() if path.exists() else []:
        c = line.split("\t")
        if len(c) >= 3 and line.strip() and not line.startswith("#"):
            out.append({"since": c[0], "test": c[1], "renderer": c[2], "why": c[3] if len(c) > 3 else ""})
    return out


def status_text(tree, work_root=WORK_ROOT, at=None, live=None, agent="1", queue_file=None):
    steps = read_steps(tree, live)
    by = {s: [x for x in steps if x["state"] == s] for s in STATES}
    reasons = blocked_reasons(tree)
    at = at or now()
    claims = lv.read_claims(live_dir(live))
    stale = stale_of(tree, claims, at)
    out = ["# Native plan: status", ""]
    running = active_landing(live, agent, queue_file)
    if running:
        out += [landing_running_line(running, agent), ""]
    out += ["Written %s by plan.py status. Run it at every start and after every context compaction." % at.isoformat(timespec="minutes"),
            "", "Steps landed: %d of %d." % (len(by["landed"]), len(steps)), ""]
    repairs = lv.read_repairs(live_dir(live))
    state = lv.read_state(live_dir(live))
    working = [r for r in repairs if r["state"] == "started"]
    out.append("## In work (%d, one thing for each agent)" % (len(by["started"]) + len(working)))
    out.append("")
    out += ["- %s %s: agent %s, branch native/%s in %s" % (s["step"], s["title"], s["agent"] or "1", s["step"],
                                                          work_folder(work_root, s["agent"] or "1").as_posix()) for s in by["started"]]
    out += ["- %s repair of %s: agent %s, branch native/repair-%s in %s" % (
        r["id"], lv.repair_what(r), (state.get(r["id"]) or {}).get("agent", "1"), r["id"][2:],
        work_folder(work_root, (state.get(r["id"]) or {}).get("agent", "1")).as_posix()) for r in working]
    if not by["started"] and not working:
        out.append("- none")
    out += ["", "## Blocked (%d)" % len(by["blocked"]), ""]
    out += ["- %s %s: %s" % (s["step"], s["title"], s["reason"] or reasons.get(s["step"], "(no reason written)"))
            for s in by["blocked"]] or ["- none"]
    out += ["", "## Claims (%d)" % len(claims), ""]
    out += ["- %s: agent %s since %s%s%s" % (c["package"], c["agent"], c["time"], ", branch " + c["branch"] if c["branch"] else "",
                                            " STALE (no commit for %d hours): plan.py claim %s --take-stale" % (lv.STALE_HOURS, c["package"])
                                            if c["package"] in stale else "") for c in claims] or ["- none"]
    out += ["", "## Repairs (%d open of kind fail, %d in all)" % (
        sum(1 for r in repairs if r["state"] == "open" and r["kind"] == "fail"), len(repairs)), ""]
    out += ["- %s %s %s: %s, %s, seen %sx" % (r["id"], r["state"], r["kind"], lv.repair_what(r), lv.repair_traced(r), r["seen"])
            for r in lv.repair_order(repairs)] or ["- none"]
    faults = lv.fault_groups(lv.read_faults(live_dir(live)))
    out += ["", "## Tool faults (%d kinds, %d events)" % (len(faults), sum(g["count"] for g in faults)), ""]
    out += ["- %dx %s %s: %s" % (g["count"], g["kind"], g["check"], g["first_error"][:100]) for g in faults[:5]] or ["- none"]
    out += ["", "## Last five landings", ""]
    last = lv.read_landings(live_dir(live))[-5:]
    out += ["- %s: commit %s, main %s, %s, %s s" % (c["step"], c["commit"], lv.main_of(c), c["finished"], c["seconds"])
            for c in reversed(last)] or ["- none yet"]
    owed = lv.read_owed(live_dir(live))
    out += ["", "## For the owner to check by hand (%d)" % len(owed), ""]
    out += ["- %s: %s (agent %s, %s)" % (o["step"], " ".join(o["text"].split()), o["agent"], page.minute(o["time"])) for o in owed] or ["- none"]
    choice = choose_next(steps, claims, stale)
    out += ["", "## Next (agent 1)", ""]
    if choice:
        out.append("- %s %s (plan.py next prints its card)" % (choice.step["step"], choice.step["title"]))
        if choice.blocked_before:
            out.append("- skipping blocked: %s" % ", ".join(b["step"] for b in choice.blocked_before))
    elif len(by["landed"]) == len(steps):
        out.append("- every step is landed")
    else:
        waiting = [s["step"] for s in by["started"]]
        out.append("- nothing can start now" + ("; steps in progress: " + ", ".join(waiting) if waiting else ""))
    helper, report = choose_helper(steps, claims, stale)
    out += ["", "## Next (agent 2)", ""]
    out.append("- %s %s" % (helper.step["step"], helper.step["title"]) if helper else "- nothing on agent 2's list can start now")
    return "\n".join(out) + "\n"


class PagePaths:
    """The files of the progress page in the live folder: `html` (complete, the data inside), `status` (STATUS.md),
    `index` and `data` (the pair for the web, in `publish`), `url` (the address it is published at, written by hand) and
    `old` (names an earlier tool wrote, removed now)."""

    def __init__(self, live=None):
        d = live_dir(live)
        self.html, self.status, self.url = d / "progress.html", d / "STATUS.md", d / "progress-url.txt"
        self.publish = d / "publish"
        self.index, self.data = self.publish / "index.html", self.publish / "progress-data.json"
        self.old = (d / "progress-local.html", d / "progress-data.json")


def page_paths(live=None):
    return PagePaths(live)


def page_data(tree, live=None, at=None):
    """(static, data, landings, faults): what never changes with progress (the step list), what does, and the landings and
    the tool-fault events as the live folder has them (STATUS.md lists the last of each)."""
    steps = read_steps(tree, live)
    reasons = blocked_reasons(tree)
    for s in steps:
        if s["state"] == "blocked" and not s["reason"]:
            s["reason"] = reasons.get(s["step"], "")
    d = live_dir(live)
    claims = lv.read_claims(d)
    at = at or now()
    landings, events = lv.read_landings(d), lv.read_faults(d)
    return (page.make_static(steps),
            page.make_data(steps, landings, lv.read_state(d), claims, stale_of(tree, claims, at), lv.fault_groups(events),
                           count_flakes(live), lv.stamp(at), read_set_aside(live), lv.repair_order(lv.read_repairs(d)),
                           lv.read_owed(d)),
            landings, events)


class Written:
    """What write_page made: the PagePaths, the `data` (page.make_data), the STATUS.md `text` and the numbers in it."""

    def __init__(self, paths, data, text, landed, total):
        self.paths, self.data, self.text, self.landed, self.total = paths, data, text, landed, total


def write_if_changed(path, text):
    """True when the file was written (it was not there, or held other text)."""
    path = pathlib.Path(path)
    try:
        if path.read_bytes().decode("utf-8") == text:
            return False
    except (OSError, UnicodeDecodeError):
        pass
    atomic_write(path, text)
    return True


def write_page(tree, live=None, at=None):
    """Writes every progress file into the live folder: progress.html (complete, the data inside), STATUS.md, and in
    `publish` the pair for the web (index.html, rewritten only when its text changed, which it does when this tool or the
    plan's step list changes and never because progress changed; progress-data.json). Removes the names an earlier tool
    wrote. Returns a Written."""
    paths = page_paths(live)
    static, data, landings, events = page_data(tree, live, at)
    status, landed, total = page.make_status(static, data, landings, events, data["generated"])
    with lv.locked(live_dir(live)):
        paths.publish.mkdir(parents=True, exist_ok=True)
        write_if_changed(paths.index, page.render(static))
        write_if_changed(paths.data, page.data_json(data))
        write_if_changed(paths.html, page.render(static, data))
        write_if_changed(paths.status, status)
        for old in paths.old:
            old.unlink(missing_ok=True)
    return Written(paths, data, status, landed, total)


# ---- the progress branch ---------------------------------------------------------------------------------------

PROGRESS_BRANCH = "refs/heads/progress"
PUSH_OFF = "NEBULA_PLAN_NO_PUSH"
GIT_WAIT = 60


def push_enabled(a):
    """False with --no-push, or when NEBULA_PLAN_NO_PUSH is set (and not 0 or empty) in the environment."""
    if getattr(a, "no_push", False):
        return False
    return os.environ.get(PUSH_OFF, "") in ("", "0")


def main_repo(tree):
    """The main repository of the plan tree: its folder, whether the tree is that or one of its work folders. None when the
    tree is no git tree."""
    r = git(tree, "rev-parse", "--git-common-dir")
    if r.returncode != 0 or not r.stdout.strip():
        return None
    common = pathlib.Path(r.stdout.strip())
    if not common.is_absolute():
        common = pathlib.Path(tree) / common
    common = common.resolve()
    return common.parent if common.name == ".git" else common


def progress_git(repo, *args, stdin=None):
    """git in the main repository (plumbing and the one push); the text in (`stdin`) goes in as it is, with its line ends,
    and the output comes back stripped. A failure raises RuntimeError with its message."""
    exe = GIT if pathlib.Path(GIT).exists() else "git"
    r = subprocess.run([exe, "-C", str(repo), *args], input=stdin.encode("utf-8") if stdin is not None else None,
                       capture_output=True, timeout=GIT_WAIT)
    out, err = r.stdout.decode("utf-8", "replace").strip(), r.stderr.decode("utf-8", "replace").strip()
    if r.returncode != 0:
        raise RuntimeError("git %s: %s" % (args[0], (err or out)[-200:] or "exit %d" % r.returncode))
    return out


def publish_progress(repo, text, landed, total, remote="origin"):
    """Puts STATUS.md's `text` on the branch `progress` of `remote`, as one commit holding that one file, made with git's
    plumbing commands in `repo` (no work tree, no index, no checkout is touched). The branch shares no history with main:
    its first commit has no parent, a later one has the branch's previous tip. Text the tip already has: nothing is made
    or pushed. Returns what was done, in words; raises RuntimeError when git fails (a push that is refused too)."""
    listed = progress_git(repo, "ls-remote", "--heads", remote, PROGRESS_BRANCH)
    tip = listed.split()[0] if listed else ""
    blob = progress_git(repo, "hash-object", "-w", "--no-filters", "--stdin", stdin=text)
    if tip:
        have = subprocess.run([GIT if pathlib.Path(GIT).exists() else "git", "-C", str(repo), "cat-file", "-e", tip + "^{commit}"],
                              capture_output=True, text=True, timeout=GIT_WAIT).returncode == 0
        if not have:
            progress_git(repo, "fetch", "--quiet", remote, PROGRESS_BRANCH)       # FETCH_HEAD only: no ref, no work tree
        try:
            if progress_git(repo, "rev-parse", "%s:STATUS.md" % tip) == blob:
                return "STATUS.md is what %s has: nothing pushed" % PROGRESS_BRANCH
        except RuntimeError:
            pass                                                                # the tip has no STATUS.md: it is new
    tree = progress_git(repo, "mktree", stdin="100644 blob %s\tSTATUS.md\n" % blob)
    commit = progress_git(repo, "commit-tree", tree, *(["-p", tip] if tip else []), "-m",
                          "Progress: %d of %d steps landed" % (landed, total))
    progress_git(repo, "push", "--quiet", remote, "%s:%s" % (commit, PROGRESS_BRANCH))
    return "%s %s on %s (%s)" % ("pushed" if tip else "pushed the first commit", commit[:9], PROGRESS_BRANCH, "after " + tip[:9] if tip else "no parent")


def regenerate(a, say_result=False):
    """After a change of state (and for `page`): every progress file is made again, then the progress branch is pushed when
    STATUS.md changed. Never a reason to fail the command: a failure is one warning line (a failed push is its own line)."""
    try:
        read_main_quietly(a)
        written = write_page(a.tree, a.live)
    except Exception as e:  # noqa: BLE001 - the files are a report; the command's own result stands without them
        say("plan.py: warning: the progress files were not made again: %s: %s" % (type(e).__name__, " ".join(str(e).split())[:200]))
        return None
    if push_enabled(a):
        try:
            repo = main_repo(a.tree)
            if repo is None:
                raise RuntimeError("%s is no git tree" % a.tree)
            result = publish_progress(repo, written.text, written.landed, written.total)
            if say_result:
                say("progress branch: " + result)
        except Exception as e:  # noqa: BLE001
            say("plan.py: warning: the progress branch was not pushed: %s: %s" % (type(e).__name__, " ".join(str(e).split())[:200]))
    return written


def refresh_page(a):
    regenerate(a)


# ---- commands ------------------------------------------------------------------------------------------------

def agent_of(a):
    return str(getattr(a, "agent", 1))


def own_copy_refusal(agent, tree=None, work_root=None):
    """The one line that refuses a command run from the other agent's copy of this tool, or None. Each agent runs the copy in
    its own work folder; from any other tree (the tests, a planner's look) there is nothing to refuse."""
    tree = os.path.normcase(os.path.abspath(str(tree or TREE)))
    root = pathlib.Path(work_root or WORK_ROOT)
    agent = str(agent)
    for number, name in AGENT_FOLDERS.items():
        if tree == os.path.normcase(os.path.abspath(str(root / name))):
            if number == agent:
                return None
            right = (root / AGENT_FOLDERS.get(agent, "?") / "tools" / "plan" / "plan.py").as_posix()
            return ("plan.py: this is agent %s's copy (%s); agent %s runs its own: python -X utf8 -I %s" % (
                number, (root / name).as_posix(), agent, right))
    return None


def load_main_landed(tree):
    """Reads `steps.tsv` of the last fetched `origin/main` (no network) into MAIN_LANDED and MAIN_READ. Returns "" or why not."""
    MAIN_LANDED.clear()
    del MAIN_READ[:]
    try:
        shown = git(tree, "show", "origin/main:%s/steps.tsv" % NATIVE)
        if shown.returncode != 0:
            return "origin/main has no %s/steps.tsv" % NATIVE
        for s in parse_base(shown.stdout, "origin/main:%s/steps.tsv" % NATIVE):
            if s["state"] == "landed":
                MAIN_LANDED[s["step"]] = s["commit"]
        MAIN_READ.append(True)
    except (OSError, ValueError, subprocess.SubprocessError) as e:
        MAIN_LANDED.clear()
        return "%s: %s" % (type(e).__name__, " ".join(str(e).split())[:150])
    return ""


def fetch_main_landed(a, say_failure=True):
    """Fetches origin and reads `steps.tsv` of `origin/main` into MAIN_LANDED (what landed there is landed for every agent,
    wherever it was landed from). When the fetch fails, the last fetched copy is read and one line says so; when `origin/main`
    cannot be read at all, the tree's file alone is used and one line says so. Done once for each call of the tool."""
    if getattr(a, "main_fetched", False):
        return
    a.main_fetched = True
    why, fetch_failed = "", ""
    try:
        exe = GIT if pathlib.Path(GIT).exists() else "git"
        got = subprocess.run([exe, "-C", str(a.tree), "fetch", "-q", "origin"], capture_output=True, text=True, encoding="utf-8",
                             errors="replace", timeout=GIT_WAIT)
        if got.returncode != 0:
            fetch_failed = ((got.stderr or got.stdout).strip().splitlines() or ["exit %d" % got.returncode])[-1]
    except (OSError, subprocess.SubprocessError) as e:
        fetch_failed = "%s: %s" % (type(e).__name__, " ".join(str(e).split())[:150])
    why = load_main_landed(a.tree)
    if not say_failure:
        return
    if why:
        say("plan.py: origin/main's steps.tsv could not be read (%s%s); what is landed is read from this folder's file alone." % (
            "the fetch of origin failed: %s; " % fetch_failed if fetch_failed else "", why))
    elif fetch_failed:
        say("plan.py: the fetch of origin failed (%s); what is landed is read from the last fetched copy of origin/main." % fetch_failed)


def read_main_quietly(a):
    """For the commands that only change the live state or the page: what is landed, from the last fetched origin/main, with no
    fetch and no line."""
    if not getattr(a, "main_fetched", False):
        a.main_fetched = True
        load_main_landed(a.tree)


def say_stale(a, claims, stale):
    at = now()
    for c in claims:
        if c["package"] in stale and c["agent"] != agent_of(a):
            hours = lv.claim_age_hours(c, lambda b: branch_time(a.tree, b), at)
            say("STALE: package %s is claimed by agent %s (no commit on %s for %d hours). Take it over with: "
                "plan.py claim %s --take-stale --agent %s" % (c["package"], c["agent"], c["branch"] or "its branch", hours,
                                                               c["package"], agent_of(a)))


REPAIR_ID = re.compile(r"^R-\d{3,}$")
REPAIR_RULES = ("Read the evidence first. Find the cause and fix the game, not the test. If the test tool is at fault, do not "
                "debug it: run plan.py toolfault and plan.py blocked for this repair.")


def is_repair(item_id):
    return bool(REPAIR_ID.match(item_id))


def item_branch(item_id):
    """The branch of a step (`native/<id>`) or of a repair (`native/repair-003` for R-003)."""
    return "native/repair-" + item_id[2:] if is_repair(item_id) else "native/" + item_id


def plan_path(a):
    return (pathlib.Path(a.tree) / "tools" / "plan" / "plan.py").as_posix()


def agent_flag(agent):
    return " --agent 2" if agent == "2" else ""


def in_work(a, steps):
    """What the agent has in work: [("step", row) or ("repair", row)], the steps and repairs that are `started` by it. One
    thing at a time: `start` gives nothing new while this is not empty."""
    agent = agent_of(a)
    out = [("step", s) for s in steps if s["state"] == "started" and (s["agent"] or "1") == agent]
    state = lv.read_state(live_dir(a.live))
    out += [("repair", r) for r in lv.read_repairs(live_dir(a.live))
            if r["state"] == "started" and ((state.get(r["id"]) or {}).get("agent") or "1") == agent]
    return out


def in_work_line(a, work):
    kind, first = work[0]
    ident = first["step"] if kind == "step" else first["id"]
    flag = agent_flag(agent_of(a))
    return "you have %s in work. Land it (plan.py land %s%s) or block it (plan.py blocked %s \"<why>\"%s) first." % (
        ident, ident, flag, ident, flag)


# ---- a landing in the gate's hands ---------------------------------------------------------------------------------

def hhmm(text):
    when = lv.parse_stamp(text)
    return when.strftime("%H:%M") if when else (text or "?")


def active_landing(live, agent, queue_file=None):
    """The landing of `agent` that is in the gate's hands, from its landing record (written just before the gate is called, removed
    when the gate has answered): the record, with `queue_time` when its queue row waits or runs, or `finishing` when the row has
    ended but the process that handed it over still runs. None when there is none. A record whose queue row has ended and whose
    process is gone is stale: it is dropped, with no line."""
    rec = lv.read_record(live_dir(live), agent)
    if rec is None:
        return None
    row = None
    try:
        row = landing.Queue(queue_file or QUEUE_FILE).row(rec["branch"])
    except (OSError, RuntimeError):
        row = None
    if row is not None and not landing.ended(row):
        rec["queue_time"] = row["time"]
        return rec
    if rec["pid"].isdigit() and alive(int(rec["pid"])):
        rec["finishing"] = "1"
        return rec
    lv.drop_record(live_dir(live), agent, rec["step"])
    return None


def landing_running_line(rec, agent):
    return "LANDING RUNNING: %s since %s. Run `plan.py land %s%s` to wait for its end." % (
        rec["step"], hhmm(rec["time"]), rec["step"], agent_flag(agent))


def landing_of(a):
    """The agent's landing that is in the gate's hands (active_landing), or None."""
    return active_landing(a.live, agent_of(a), getattr(a, "queue_file", None))


def say_landing_running(a):
    rec = landing_of(a)
    if rec:
        say(landing_running_line(rec, agent_of(a)))
    return rec


def print_step_body(a, s):
    """A step's card and what goes with it: the package's rows, what the landing needs in the ledger, the checks its changed
    paths run and the extra checks its proof asks for. Changes nothing."""
    lines = card_lines(a.tree, s)
    card = cut_card(lines, s["step"])
    say("=== THE CARD ===")
    say(card if card is not None else "(no card with the heading '### %s' in %s)" % (s["step"], s["card_file"]))
    say()
    say("=== THE PACKAGE'S ROWS (%s) ===" % s["package"])
    table = package_table(lines, s["package"])
    say(table if table else "(this package has no row table)")
    rows_file = native(a.tree) / "rows" / ("%s.tsv" % s["package"])
    if rows_file.exists():
        say()
        say("The full ledger text of each row: %s/rows/%s.tsv" % (NATIVE, s["package"]))
    reading = card_rows(card)
    say()
    say("=== WHAT THE LANDING NEEDS IN THE LEDGER ===")
    if reading.ok:
        say("Closes whole (state fixed, proof naming a test, a search or a measure): %s" % (", ".join(reading.closes) or "none"))
        say("Advances (a dated note): %s" % (", ".join(reading.advances) or "none"))
    else:
        say("The card's Rows line could not be read (%s). Read it yourself: %s" % (reading.why, reading.raw[:200]))
    say()
    say("=== THE CHECKS ITS CHANGED PATHS WILL RUN ===")
    paths = card_paths(card, a.tree)
    say("Paths on the card: %s" % (", ".join(paths) if paths else "none in the tree"))
    for c in checks_for(paths, read_scope(a.tree)):
        say("  " + c)
    for line in also_lines(a.tree, s["step"]):
        say("  " + line)


def following(a, steps, done):
    """The step the tool would give next once `done` (a step row, or None) is landed: what `next` names after the one it gives."""
    copy = [dict(x) for x in steps]
    for x in copy:
        if done is not None and x["step"] == done["step"]:
            x["state"] = "landed"
    claims = lv.read_claims(live_dir(a.live))
    stale = stale_of(a.tree, claims)
    choice = choose_helper(copy, claims, stale)[0] if agent_of(a) == "2" else choose_next(copy, claims, stale)
    return choice.step if choice else None


def say_after(a, steps, done):
    nxt = following(a, steps, done)
    if nxt is not None:
        say("after it: %s %s; read it with `plan.py card %s`" % (nxt["step"], nxt["title"], nxt["step"]))


def print_step(a, steps, s, notes=(), blocked_before=(), working=False):
    """A step's card and what goes with it (print_step_body), and the commands. `working`: the step is in work, so the commands
    are the ones that end it."""
    landed = sum(1 for x in steps if x["state"] == "landed")
    agent = agent_of(a)
    say("%s: %s  %s" % ("IN WORK" if working else "NEXT", s["step"], s["title"]))
    say("Package %s; step %s of %d; %d landed so far." % (s["package"], s["order"], len(steps), landed))
    if working:
        say("This is in work: go on with it, then land it or block it. Nothing new is given while it is.")
    for note in notes:
        say("NOTE: " + note)
    for b in blocked_before:
        why = b["reason"] or blocked_reasons(a.tree).get(b["step"], "(no reason written)")
        say("BLOCKED: %s %s: %s" % (b["step"], b["title"], why))
    if blocked_before:
        say("The step above is blocked, so the first later step that does not need its package is %s." % s["step"])
    say("Needs first: %s." % ("%s (all landed)" % s["package_needs"] if s["package_needs"] else "nothing outside its own package"))
    say()
    print_step_body(a, s)
    say()
    say("=== COMMANDS ===")
    plan = plan_path(a)
    who = agent_flag(agent)
    if not working:
        say("start: python -X utf8 -I %s start %s%s" % (plan, s["step"], who))
    say("land:  python -X utf8 -I %s land %s%s" % (plan, s["step"], who))
    if working:
        say("block: python -X utf8 -I %s blocked %s \"<why>\"%s" % (plan, s["step"], who))


def print_repair(a, r, working=False, commands=True):
    """A repair's card: what failed, where the evidence is, the landing it was traced to, the ledger row, the fixed lines, and
    the commands."""
    say("%s: %s  (%s, kind %s)" % ("IN WORK" if working else "REPAIR", r["id"], r["source"], r["kind"]))
    if working:
        say("This is in work: go on with it, then land it or block it. Nothing new is given while it is.")
    say("What failed: %s" % lv.repair_what(r))
    say("Seen: %s time%s; opened %s." % (r["seen"], "" if r["seen"] == "1" else "s", r["opened"]))
    say("Evidence: %s" % (r["evidence"] if r["evidence"] not in ("", "-") else "none recorded"))
    traced = lv.repair_traced(r)
    say("Traced to: %s" % (traced[len("traced to "):] if traced.startswith("traced to ") else "no landing (not traced)"))
    say("Ledger row: %s" % (r["row"] if r["row"] not in ("", "-") else "none"))
    if r["note"] not in ("", "-"):
        say("Note: %s" % r["note"])
    say()
    say(REPAIR_RULES)
    if not commands:
        return
    say()
    say("=== COMMANDS ===")
    plan = plan_path(a)
    who = agent_flag(agent_of(a))
    if not working:
        say("start: python -X utf8 -I %s start %s%s" % (plan, r["id"], who))
    say("land:  python -X utf8 -I %s land %s%s" % (plan, r["id"], who))
    say("block: python -X utf8 -I %s blocked %s \"<why>\"%s" % (plan, r["id"], who))


def cmd_next(a):
    fetch_main_landed(a)
    steps = read_steps(a.tree, a.live)
    claims = lv.read_claims(live_dir(a.live))
    stale = stale_of(a.tree, claims)
    landed = sum(1 for s in steps if s["state"] == "landed")
    agent = agent_of(a)
    say_landing_running(a)
    say_stale(a, claims, stale)
    work = in_work(a, steps)
    if work:                                         # one thing at a time: it is shown again, nothing new is given
        first_kind, first = work[0]
        if first_kind == "step":
            print_step(a, steps, first, working=True)
        else:
            print_repair(a, first, working=True)
        for kind, other in work[1:]:
            say("ALSO IN WORK: %s (land it or block it too)" % (other["step"] if kind == "step" else other["id"]))
        say_after(a, steps, first if first_kind == "step" else None)
        return 0
    if agent == "1":
        repair = lv.oldest_open_fail(lv.read_repairs(live_dir(a.live)))
        if repair:                                   # a fault found after a landing comes before any step
            print_repair(a, repair)
            say_after(a, steps, None)
            return 0
    if agent == "2":
        choice, report = choose_helper(steps, claims, stale)
        if choice is None:
            if all(why in ("is landed", "is not in the plan") for _, why in report):
                say("Agent 2's list is finished: %s are all landed. Stop and report to agent 1. Do not take any other package." %
                    ", ".join(AGENT2_LIST))
            else:
                say("Nothing on agent 2's list can start now. Stop and report to agent 1; do not take any other package.")
                for pkg, why in report:
                    say("  %s %s" % (pkg, why))
            return 1
    else:
        choice = choose_next(steps, claims, stale)
    if choice is None:
        if landed == len(steps):
            say("Every step is landed (%d of %d). Nothing is next." % (landed, len(steps)))
            return 0
        say("Nothing can start now. Landed %d of %d." % (landed, len(steps)))
        for s in steps:
            if s["state"] in ("started", "blocked"):
                say("  %s %s: %s" % (s["step"], s["state"], s["title"]))
        say("Land the started steps (plan.py land <id>) or take a blocked step up again (plan.py start <id>).")
        return 1
    print_step(a, steps, choice.step, choice.notes, choice.blocked_before)
    say_after(a, steps, choice.step)
    return 0


def cmd_card(a):
    """Any step's card, its package's rows and the checks it needs, or a repair's card. Changes nothing, needs no --agent."""
    if is_repair(a.id):
        rep = next((r for r in lv.read_repairs(live_dir(a.live)) if r["id"] == a.id), None)
        if rep is None:
            say("plan.py: no repair %s (plan.py repair list)" % a.id)
            return 1
        print_repair(a, rep, commands=False)
        return 0
    steps = read_base(a.tree)
    s = find(steps, a.id)
    if s is None:
        say("plan.py: no step %s" % a.id)
        return 1
    say("CARD: %s  %s" % (s["step"], s["title"]))
    say("Package %s; step %s of %d. (Only the card: what is landed, started or blocked is in plan.py status.)" % (
        s["package"], s["order"], len(steps)))
    say("Needs first: %s." % (s["package_needs"] if s["package_needs"] else "nothing outside its own package"))
    say()
    print_step_body(a, s)
    return 0


def cmd_owed(a):
    """Writes what only a person or other hardware can check. It never blocks a landing and never changes steps.tsv."""
    text = " ".join(a.text.split())
    if not text:
        say("plan.py: owed needs the words of what must be checked and how")
        return 1
    known = find(read_base(a.tree), a.id) is not None or any(r["id"] == a.id for r in lv.read_repairs(live_dir(a.live)))
    if not known:
        say("plan.py: no step or repair %s" % a.id)
        return 1
    lv.add_owed(live_dir(a.live), agent_of(a), a.id, text)
    refresh_page(a)
    say("owed: %s: %s" % (a.id, text))
    say("Written to %s/owed.tsv and listed under 'For the owner to check by hand'. It blocks nothing; say in the ledger row's note "
        "what was not done." % live_dir(a.live).as_posix())
    return 0


def cmd_rows(a):
    steps = read_steps(a.tree, a.live)
    if a.id:
        s = find(steps, a.id)
        if s is None:
            say("plan.py: no step %s" % a.id)
            return 1
        steps = [s]
    bad = 0
    for s in steps:
        r = card_rows(card_text(a.tree, s))
        if not r.ok:
            bad += 1
            say("%s: UNREADABLE (%s): %s" % (s["step"], r.why, r.raw[:160]))
        else:
            say("%s: closes %s | advances %s%s" % (s["step"], ", ".join(r.closes) or "-", ", ".join(r.advances) or "-",
                                                   " | named only %s" % ", ".join(r.mentioned) if r.mentioned else ""))
    say("%d of %d readable" % (len(steps) - bad, len(steps)))
    return 1 if bad else 0


def holds_refusal(a, package, claims, stale):
    """Why this agent may not work on `package` ("" when it may): the other agent holds it, or it is not on the helper's list."""
    agent = agent_of(a)
    if agent == "2" and package not in AGENT2_LIST:
        return "package %s is not on agent 2's list (%s); agent 2 is never given another package" % (package, ", ".join(AGENT2_LIST))
    c = lv.claim_of(claims, package)
    if c and c["agent"] != agent:
        return "package %s is held by agent %s%s" % (package, c["agent"], (
            " (the claim is stale: plan.py claim %s --take-stale --agent %s)" % (package, agent)) if package in stale else "")
    return ""


BLOCKED_FILE = NATIVE + "/BLOCKED.md"


def folder_changes(folder):
    """The uncommitted changes of a work folder, as git status lines, except BLOCKED.md: `blocked` and `tool` write it in the
    tree the tool runs in (a plain file the agent may commit), and when that is the agent's own folder it must not stop the next
    start. It stays as it is when the branch changes."""
    lines = git(folder, "status", "--porcelain").stdout.splitlines()
    return [l for l in lines if l[3:].strip('"') != BLOCKED_FILE]


def branch_exists(folder, ref):
    return git(folder, "rev-parse", "--verify", "--quiet", ref).returncode == 0


def begin_work(a, branch):
    """What `start` does after its own checks, for a step or a repair: the agent's folder is made the first time (git worktree
    add) and used every later time; refuses (a line saying what to do) when something is in work or the folder has
    uncommitted changes; then fetches and makes `branch` there (`git checkout -b`, from origin/main; a branch that exists
    already, a blocked step's, is checked out as it is). Returns the refusal text, or None when the folder is on the branch."""
    agent = agent_of(a)
    folder = work_folder(a.work_root, agent)
    work = in_work(a, read_steps(a.tree, a.live))
    if work:
        return in_work_line(a, work)
    if folder.exists():
        if not (folder / ".git").exists():
            return "%s exists and is not a work folder of this repository. Move it away, then run start again." % folder.as_posix()
        if folder_changes(folder):
            return ("the work folder %s has uncommitted changes. Commit them (or throw away what you do not want), then run "
                    "start again." % folder.as_posix())
    fetched = git(folder if folder.exists() else a.tree, "fetch", "origin")
    if fetched.returncode != 0:
        return "git fetch origin failed: %s" % fetched.stderr.strip()
    here = folder if folder.exists() else a.tree
    if branch_exists(here, "refs/heads/" + branch):
        local, base = True, ""
    else:
        local, base = False, "origin/" + branch if branch_exists(here, "refs/remotes/origin/" + branch) else "origin/main"
    if folder.exists():
        made = git(folder, "checkout", *([branch] if local else ["--no-track", "-b", branch, base]))
        what = "git checkout"
    else:
        made = git(a.tree, "worktree", "add", *([str(folder), branch] if local else ["--no-track", "-b", branch, str(folder), base]))
        what = "git worktree add"
    if made.returncode != 0:
        return "%s failed: %s" % (what, (made.stderr or made.stdout).strip())
    return None


def cmd_start(a):
    fetch_main_landed(a)
    rec = landing_of(a)
    if rec:                                          # a landing of this agent is in the gate's hands: nothing new meanwhile
        say("plan.py: " + landing_running_line(rec, agent_of(a)))
        return 1
    if is_repair(a.id):
        return start_repair(a)
    steps = read_steps(a.tree, a.live)
    s = find(steps, a.id)
    agent = agent_of(a)
    if s is None:
        say("plan.py: no step %s" % a.id)
        return 1
    if s["state"] in ("landed", "started"):
        say("plan.py: step %s is already %s%s" % (a.id, s["state"], " (agent %s)" % s["agent"] if s["state"] == "started" else ""))
        return 1
    claims = lv.read_claims(live_dir(a.live))
    stale = stale_of(a.tree, claims)
    why = holds_refusal(a, s["package"], claims, stale)
    if why:
        say("plan.py: " + why)
        return 1
    waiting = unmet(s, steps)
    if waiting:
        say("plan.py: step %s needs first: %s" % (a.id, ", ".join("%s (%s)" % w for w in waiting)))
        return 1
    why = pair_conflict(s, steps)
    if why:
        say("plan.py: step %s waits: %s" % (a.id, why))
        return 1
    folder = work_folder(a.work_root, agent)
    branch = "native/" + a.id
    why = begin_work(a, branch)
    if why:
        say("plan.py: " + why)
        return 1
    set_state(a.tree, a.id, "started", agent=agent, live=a.live)
    lv.put_claim(live_dir(a.live), s["package"], agent, branch)
    refresh_page(a)
    say("started %s: branch %s in %s (package %s is claimed by agent %s)" % (a.id, branch, folder.as_posix(), s["package"], agent))
    if replay_allowed(card_text(a.tree, s)):
        say("This step's card says its replay reference may change. Once the change is made and the tour has played every recorded "
            "stop, record the reference with: %s" % REPLAY_RECORD)
        say("A commit message of the landing must contain the words \"%s\"." % REPLAY_WORDS)
    return 0


def start_repair(a):
    agent = agent_of(a)
    if agent != "1":
        say("plan.py: repairs are agent 1's work")
        return 1
    rep = next((r for r in lv.read_repairs(live_dir(a.live)) if r["id"] == a.id), None)
    if rep is None:
        say("plan.py: no repair %s (plan.py repair list)" % a.id)
        return 1
    if rep["state"] in ("started", "landed"):
        say("plan.py: repair %s is already %s" % (a.id, rep["state"]))
        return 1
    branch = item_branch(a.id)
    why = begin_work(a, branch)
    if why:
        say("plan.py: " + why)
        return 1
    lv.set_repair(live_dir(a.live), a.id, "started")
    lv.set_state_entry(live_dir(a.live), a.id, "started", agent)
    refresh_page(a)
    say("started %s: branch %s in %s" % (a.id, branch, work_folder(a.work_root, agent).as_posix()))
    return 0


def cmd_claim(a):
    steps = read_steps(a.tree, a.live)
    if not any(s["package"] == a.package for s in steps):
        say("plan.py: no package %s" % a.package)
        return 1
    agent = agent_of(a)
    if agent == "2" and a.package not in AGENT2_LIST:
        say("plan.py: package %s is not on agent 2's list (%s); agent 2 is never given another package" % (a.package, ", ".join(AGENT2_LIST)))
        return 1
    if all(s["state"] == "landed" for s in steps if s["package"] == a.package):
        say("plan.py: package %s is landed" % a.package)
        return 1
    d = live_dir(a.live)
    claims = lv.read_claims(d)
    stale = stale_of(a.tree, claims)
    c = lv.claim_of(claims, a.package)
    if c and c["agent"] != agent:
        if a.package not in stale:
            say("plan.py: package %s is held by agent %s (claimed %s); the claim is not stale" % (a.package, c["agent"], c["time"]))
            return 1
        if not a.take_stale:
            say("plan.py: package %s is held by agent %s and the claim is stale; take it with --take-stale" % (a.package, c["agent"]))
            return 1
        for s in steps:                      # the stale branch is left as it is; the steps started on it become this agent's
            if s["package"] == a.package and s["state"] == "started":
                set_state(a.tree, s["step"], "started", agent=agent, live=a.live)
    lv.put_claim(d, a.package, agent)
    refresh_page(a)
    say("package %s is claimed by agent %s" % (a.package, agent))
    return 0


def changed_documents(folder, base="origin/main"):
    names = git_out(folder, "diff", "--name-only", "--diff-filter=AM", "%s...HEAD" % base).splitlines()
    return [n for n in names if n.lower().endswith((".md", ".txt", ".rst")) and (pathlib.Path(folder) / n).is_file()]


def commit_messages(folder, base="origin/main"):
    return git_out(folder, "log", "%s..HEAD" % base, "--format=%B")


RUN_FOLDER = re.compile(r"[A-Za-z]:[\\/][^\s;,)]*gate-runs[\\/][\w.-]+|gate-runs[\\/][\w.-]+")


def record_faults(a, step_id, verdict):
    """Every TOOL or ENV failure of a landing is one line of tool-faults.tsv, at once (never through a landing).
    Returns the rows written."""
    folder = next((m.group(0) for l in verdict.lines for m in [RUN_FOLDER.search(l)] if m), "")
    rows = []
    for check, kind, detail in verdict.failures:
        if kind in ("TOOL", "ENV"):
            rows.append(lv.append_fault(live_dir(a.live), agent_of(a), step_id, kind, check, detail, run_folder=folder))
    return rows


def work_folder_problem(a, folder, branch):
    """Why the agent's work folder cannot be landed from ("" when it can): it is missing, has uncommitted changes, or is not on
    the item's branch."""
    if not folder.is_dir():
        return "the work folder %s is missing" % folder.as_posix()
    if folder_changes(folder):
        return "the work folder has uncommitted changes; commit them first"
    on = git_out(folder, "rev-parse", "--abbrev-ref", "HEAD")
    if on != branch:
        return "the work folder %s is on branch %s, not %s" % (folder.as_posix(), on, branch)
    return ""


def refs_problems(folder):
    """The invented file and line references of the changed documents and the commit messages: [line]."""
    allow = doc_refs.read_allow(folder / "tools" / "verify" / "doc_refs_allow.txt")
    bad = doc_refs.check_files([folder / d for d in changed_documents(folder)], folder, allow)
    bad += doc_refs.check_text("the commit messages", commit_messages(folder), folder, allow, message=True)
    return bad


def hand_to_gate(a, folder, branch, label, agent, extra, on_pass, join=False):
    """ONE call of the gate (LAND) for a step or a repair, and what follows: a pass calls `on_pass(verdict, seconds, summary)`
    and returns its exit code; anything else prints the verdict and returns 1 (a failure), 3 (the same failure three times) or
    4 (a tool or the machine: not judged). Nothing is undone: a state commit made before stays on the branch.

    The landing record (live.write_record) is written just before the gate is called and removed when it has answered. `join`:
    the gate already has this branch in its queue (an earlier call, cut): its record is left as it is, nothing is pushed, and the
    gate's own call attaches to the row and waits for its end."""
    started = time.time()
    options = gate_options(a, agent) + list(extra)
    if join:
        run = lambda: LAND(folder, branch, a.tablet, a.tablet_wait, options, push=False)
    else:
        lv.write_record(live_dir(a.live), agent, label, branch, git_out(folder, "rev-parse", "HEAD"), os.getpid())
        run = lambda: LAND(folder, branch, a.tablet, a.tablet_wait, options)
    try:
        verdict = run()
    finally:
        lv.drop_record(live_dir(a.live), agent, label)
    seconds = int(time.time() - started)
    store = failures.Store(a.failures)
    say()
    summary = ", ".join("%d %s" % (n, k) for k, n in sorted(verdict.counts().items()))
    if verdict.passed:
        store.clear(branch)
        return on_pass(verdict, seconds, summary)
    say("NOT LANDED: %s. Verdict by kind: %s." % (label, summary or "no check rows read"))
    for check, kind, detail in verdict.failures:
        say("  %-5s %s: %s" % (kind, check, detail))
    if verdict.gate_recorded:
        # `gate.py land` stored the failure's signature and wrote any tool or machine fault itself, and said what to do.
        refresh_page(a)
        if verdict.code == failures.STOP_CODE:
            if not any(failures.STOP_TEXT in l for l in verdict.lines):
                say(failures.STOP_TEXT)
            return failures.STOP_CODE
        return 1 if verdict.code == 1 else 4          # 2: not judged (a tool or the machine, a branch that does not apply)
    written = record_faults(a, label, verdict)
    outcome = store.record(branch, verdict.failures)
    if written:
        say("Written to %s (%d line%s)." % (live_dir(a.live).as_posix() + "/tool-faults.tsv", len(written), "" if len(written) == 1 else "s"))
        say("Say what it stopped you from doing: plan.py toolfault --last --could-not \"<one sentence>\"" +
            (" --agent %s" % agent if agent != "1" else ""))
        refresh_page(a)
    if outcome.message:
        say(outcome.message)
    elif outcome.counted:
        say("This failure has now come %d time%s in a row (the third stops the retries)." % (outcome.count, "" if outcome.count == 1 else "s"))
    return outcome.exit_code


def is_state_commit(folder, tip, step_id):
    """Whether `tip` is the tool's own state commit for the step: its subject starts `Plan: step <ID> landed` and it changes
    steps.tsv and nothing else."""
    subject = git(folder, "log", "-1", "--format=%s", tip).stdout.strip()
    if not subject.startswith(STATE_COMMIT % step_id):
        return False
    changed = [l for l in git(folder, "show", "--name-only", "--format=", tip).stdout.splitlines() if l.strip()]
    return changed == ["%s/steps.tsv" % NATIVE]


def code_commit_at(folder, tip, step_id):
    """The code commit the step's row names in steps.tsv as `tip` has it ("" when it has none)."""
    shown = git(folder, "show", "%s:%s/steps.tsv" % (tip, NATIVE))
    if shown.returncode != 0:
        return ""
    try:
        row = find(parse_base(shown.stdout, tip), step_id)
    except ValueError:
        return ""
    return row["commit"] if row and row["state"] == "landed" else ""


def later_commits(folder, tip):
    """[`sha subject`] of the commits of the branch after `tip`."""
    return git(folder, "log", "--format=%h %s", "%s..HEAD" % tip).stdout.strip().splitlines()


def replay_allowed(card):
    return bool(card and REPLAY_ALLOWED.search(card))


def replay_problems(folder, allowed):
    """Why a branch that changes a reference under tests/replays/ may not land ([text], empty when it may): the card must say the
    replay changes (`allowed`), and a commit message of the landing must contain the words "replay reference"."""
    names = git(folder, "diff", "--name-only", "origin/main...HEAD").stdout.splitlines()
    files = [n for n in names if n.startswith(REPLAY_DIR)]
    if not files:
        return []
    if not allowed:
        return [REPLAY_REFUSAL] + ["  changed: " + f for f in files]
    if REPLAY_WORDS not in commit_messages(folder).lower():
        return ["the branch changes the replay reference (%s), and no commit message of the landing contains the words \"%s\"" % (
            ", ".join(files), REPLAY_WORDS)]
    return []


def settle_claim(a, steps, s, agent, renew):
    """After a pass: the package's claim goes when every step of it is landed; else, with `renew`, it is renewed."""
    if all(x["state"] == "landed" or x["step"] == s["step"] for x in steps if x["package"] == s["package"]):
        lv.drop_claim(live_dir(a.live), s["package"])
    elif renew:
        lv.put_claim(live_dir(a.live), s["package"], agent)


def found_on_main(a, steps, s):
    """`land` for a step `origin/main` already holds (the gate finished without the tool): what follows a pass is done now if it was
    not done (the live state, the landings list, the claim, the page: each once, by step id)."""
    agent = agent_of(a)
    code_commit = MAIN_LANDED.get(a.id) or "-"
    lv.set_state_entry(live_dir(a.live), a.id, "landed", agent)
    lv.add_landing(live_dir(a.live), a.id, code_commit, 0, agent)
    lv.drop_record(live_dir(a.live), agent, a.id)
    settle_claim(a, steps, s, agent, renew=False)
    refresh_page(a)
    say("LANDED (found on main): %s as %s. The gate finished without this tool; what follows a pass is done now." % (a.id, code_commit))
    say("The progress files are made again (the page to open: %s)." % page_paths(a.live).html.as_posix())
    return 0


def wait_for_process(pid, limit=1800.0):
    """Waits while process `pid` runs (up to `limit` seconds). True when it ended."""
    end = time.time() + limit
    while alive(pid):
        if time.time() > end:
            return False
        time.sleep(JOIN_POLL)
    return True


def cmd_land(a):
    fetch_main_landed(a)
    if is_repair(a.id):
        return land_repair(a)
    steps = read_steps(a.tree, a.live)
    s = find(steps, a.id)
    agent = agent_of(a)
    if s is None:
        say("plan.py: no step %s" % a.id)
        return 1
    if s["state"] == "landed" and a.id in MAIN_LANDED:
        return found_on_main(a, steps, s)
    if s["state"] != "started":
        say("plan.py: step %s is %s; only a started step lands (plan.py start %s)" % (a.id, s["state"], a.id))
        return 1
    claims = lv.read_claims(live_dir(a.live))
    why = holds_refusal(a, s["package"], claims, stale_of(a.tree, claims))
    if why:
        say("plan.py: " + why)
        return 1
    if (s["agent"] or "1") != agent:
        say("plan.py: step %s was started by agent %s, not agent %s" % (a.id, s["agent"] or "1", agent))
        return 1
    folder = work_folder(a.work_root, agent)
    branch = "native/" + a.id
    rec = landing_of(a)
    if rec and rec["step"] != a.id:
        say("plan.py: " + landing_running_line(rec, agent))
        return 1
    if rec:
        return join_landing(a, s, folder, branch, rec)
    why = work_folder_problem(a, folder, branch)
    if why:
        say("plan.py: " + why)
        return 1
    return begin_landing(a, s, folder, branch)


def join_landing(a, s, folder, branch, rec):
    """`land` called while this step's landing is in the gate's hands (the agent was compacted and no longer knows, or its command
    was cut): it joins, waits, and ends with the gate's verdict and exit code, the same as the call that started it. Nothing is
    checked again and nothing is committed or pushed. When the landing that ran judged an older commit than the folder's tip
    now: if it failed, the present tip is landed in the same call; if it passed, the later commits are named as not on main."""
    agent = agent_of(a)
    if not folder.is_dir() or git(folder, "rev-parse", "--abbrev-ref", "HEAD").stdout.strip() != branch:
        say("plan.py: the work folder %s is not on branch %s, so the landing of %s cannot be joined from it" % (folder.as_posix(), branch, a.id))
        return 1
    if rec.get("finishing"):
        # The queue row has ended and the process that handed it over still runs: it is making the bookkeeping. Wait for it.
        say("joined the landing of %s: its row in the queue has ended and process %s is finishing; waiting for it" % (a.id, rec["pid"]))
        if not wait_for_process(int(rec["pid"])):
            say("plan.py: process %s is still running after 30 minutes; run land again later" % rec["pid"])
            return 1
        a.main_fetched = False
        return cmd_land(a)
    tip = rec["tip"]
    if git(folder, "rev-parse", "--verify", "--quiet", tip + "^{commit}").returncode != 0:
        lv.drop_record(live_dir(a.live), agent, a.id)             # a record naming no commit of this repository is no landing
        return cmd_land(a)
    say("joined the landing of %s, in the queue since %s" % (a.id, hhmm(rec["queue_time"])))
    code_commit = code_commit_at(folder, tip, a.id) or tip[:9]

    def passed(verdict, seconds, summary):
        later = later_commits(folder, tip)
        code = finish_landing(a, s, code_commit, verdict, seconds, summary)
        if later:
            say("NOTE: the landing judged %s. These later commits of the branch are not on main: %s" % (tip[:9], "; ".join(later)))
        return code

    code = hand_to_gate(a, folder, branch, a.id, agent, also_options(a, a.id), passed, join=True)
    here = git_out(folder, "rev-parse", "HEAD")
    if code in (1, 4) and here != tip:
        say("the landing that was running judged an older commit (%s); landing the present one (%s) now" % (tip[:9], here[:9]))
        why = work_folder_problem(a, folder, branch)
        if why:
            say("plan.py: " + why)
            return 1
        return begin_landing(a, find(read_steps(a.tree, a.live), a.id) or s, folder, branch)
    return code


def also_options(a, step_id):
    """`--also` for the extra checks the step's proof asks for (docs/plan/native/step-checks.tsv): none, or two words."""
    names = also_names(a.tree, step_id)
    return ["--also", ",".join(names)] if names else []


def finish_landing(a, s, code_commit, verdict, seconds, summary):
    """What follows a pass: the step is landed in the plan tree's file and in what the tool reads, the landings list, the claim and
    the page. The landings list takes a step id once, so whoever does this second adds nothing."""
    agent = agent_of(a)
    MAIN_LANDED[a.id] = code_commit
    set_state(a.tree, a.id, "landed", code_commit, live=a.live)
    lv.add_landing(live_dir(a.live), a.id, code_commit, seconds, agent, main=verdict.commit or "-")
    settle_claim(a, read_steps(a.tree, a.live), s, agent, renew=True)
    refresh_page(a)
    say("LANDED %s as %s%s in %d s. Verdict by kind: %s." % (
        a.id, code_commit, (" (main has it as %s)" % verdict.commit) if verdict.commit else "", seconds,
        summary or "no check rows read"))
    say("The progress files are made again (the page to open: %s)." % page_paths(a.live).html.as_posix())
    return 0


def begin_landing(a, s, folder, branch):
    """The landing of the folder's present tip: the ledger, the references, the replay reference; the state commit (made once: a
    tip that is already the tool's state commit for this step is handed over as it is); then the gate."""
    agent = agent_of(a)
    # 1. The ledger.
    reading = card_rows(card_text(folder, s))
    if not reading.ok:
        say("plan.py: the card's Rows line could not be read (%s): %s" % (reading.why, reading.raw[:200]))
        say("It is the plan's fault, not the step's: write it in BLOCKED.md under tools (plan.py tool \"...\") and take the next step.")
        return 1
    git(folder, "fetch", "origin")
    ledger = read_ledger((folder / LEDGER).read_bytes().decode("utf-8"))
    base = git(folder, "show", "origin/main:" + LEDGER)
    base_ledger = read_ledger(base.stdout) if base.returncode == 0 else None
    problems = ledger_problems(reading, ledger, base_ledger)
    if problems:
        say("plan.py: step %s does not land yet. The ledger (%s) must say:" % (a.id, LEDGER))
        for p in problems:
            say("  " + p)
        return 1
    # 2. Invented references in the changed documents and the commit messages.
    bad = refs_problems(folder)
    if bad:
        say("plan.py: a file or line named in the documents or the commit messages does not exist:")
        for l in bad:
            say("  " + l)
        return 1
    # 3. A replay reference changes only where the card says so.
    bad = replay_problems(folder, replay_allowed(card_text(folder, s)))
    if bad:
        say("plan.py: " + bad[0])
        for l in bad[1:]:
            say(l)
        return 1
    # 4. The landing's own commit: the step is landed, with the code's commit. Made once: when the tip is already this step's
    # state commit (an earlier try made it) nothing is committed; fix commits on top of one get a second state commit, whose
    # code commit is the newer tip. Nothing is ever undone.
    tip = git_out(folder, "rev-parse", "HEAD")
    if is_state_commit(folder, tip, a.id) and code_commit_at(folder, tip, a.id):
        code_commit = code_commit_at(folder, tip, a.id)
    else:
        code_commit = tip[:9]
        set_state(folder, a.id, "landed", code_commit, track=False)
        with tempfile.TemporaryDirectory() as t:
            msg = pathlib.Path(t) / "message.txt"
            msg.write_text(STATE_COMMIT % a.id + "\n\nThe step's code is %s.\n" % code_commit, encoding="utf-8")
            git(folder, "add", "%s/steps.tsv" % NATIVE, check=True)
            committed = git(folder, "commit", "-F", str(msg))
        if committed.returncode != 0:
            git(folder, "checkout", "--", "%s/steps.tsv" % NATIVE)
            say("plan.py: the state commit failed (a hook?):\n%s" % (committed.stdout + committed.stderr).strip())
            return 1

    def passed(verdict, seconds, summary):
        return finish_landing(a, s, code_commit, verdict, seconds, summary)

    return hand_to_gate(a, folder, branch, a.id, agent, also_options(a, a.id), passed)


def repair_ledger_problem(row_id, ledger):
    """Why a repair that names a ledger row may not land ("" when it may): the row must be there, and be `fixed` with a proof
    that names a test, a search or a measure, or have a dated note."""
    row = ledger.get(row_id)
    if row is None:
        return "%s: the repair names this ledger row, and the ledger has no such row" % row_id
    if row["state"] == "fixed" and PROOF_WORD.search(row["proof"]):
        return ""
    if DATE.search(row["note"]):
        return ""
    return ("%s: the repair names this ledger row: it must be fixed with a proof that names a test, a search or a measure, or its "
            "note must have a date" % row_id)


def land_repair(a):
    agent = agent_of(a)
    if agent != "1":
        say("plan.py: repairs are agent 1's work")
        return 1
    rep = next((r for r in lv.read_repairs(live_dir(a.live)) if r["id"] == a.id), None)
    if rep is None:
        say("plan.py: no repair %s (plan.py repair list)" % a.id)
        return 1
    if rep["state"] != "started":
        say("plan.py: repair %s is %s; only a started repair lands (plan.py start %s)" % (a.id, rep["state"], a.id))
        return 1
    if ((lv.read_state(live_dir(a.live)).get(a.id) or {}).get("agent") or "1") != agent:
        say("plan.py: repair %s was started by another agent, not agent %s" % (a.id, agent))
        return 1
    folder = work_folder(a.work_root, agent)
    branch = item_branch(a.id)
    rec = landing_of(a)
    if rec and rec["step"] != a.id:
        say("plan.py: " + landing_running_line(rec, agent))
        return 1
    if rec and rec.get("finishing"):
        say("joined the landing of %s: its row in the queue has ended and process %s is finishing; waiting for it" % (a.id, rec["pid"]))
        if not wait_for_process(int(rec["pid"])):
            say("plan.py: process %s is still running after 30 minutes; run land again later" % rec["pid"])
            return 1
        a.main_fetched = False
        return cmd_land(a)
    if not rec:
        why = work_folder_problem(a, folder, branch)
        if why:
            say("plan.py: " + why)
            return 1
    # The ledger rules of a step do not apply; the row the repair names must have a dated note or be fixed with a proof.
    if rep["row"] not in ("", "-") and not rec:
        git(folder, "fetch", "origin")
        problem = repair_ledger_problem(rep["row"], read_ledger((folder / LEDGER).read_bytes().decode("utf-8")))
        if problem:
            say("plan.py: repair %s does not land yet. The ledger (%s) must say:" % (a.id, LEDGER))
            say("  " + problem)
            return 1
    if not rec:
        bad = refs_problems(folder)
        if bad:
            say("plan.py: a file or line named in the documents or the commit messages does not exist:")
            for l in bad:
                say("  " + l)
            return 1
        # A repair may change a replay reference only if its card names a step whose card says the replay changes.
        named = find(read_base(folder), rep["step"]) if rep["step"] not in ("", "-") else None
        bad = replay_problems(folder, bool(named and replay_allowed(card_text(folder, named))))
        if bad:
            say("plan.py: " + bad[0])
            for l in bad[1:]:
                say(l)
            return 1
    code_commit = (rec["tip"] if rec else git_out(folder, "rev-parse", "HEAD"))[:9]

    def passed(verdict, seconds, summary):
        closed = verdict.commit or code_commit
        lv.set_repair(live_dir(a.live), a.id, "landed", closed)
        lv.set_state_entry(live_dir(a.live), a.id, "landed", agent)
        lv.add_landing(live_dir(a.live), a.id, code_commit, seconds, agent, main=verdict.commit or "-")
        refresh_page(a)
        say("LANDED %s as %s%s in %d s. Verdict by kind: %s." % (
            a.id, code_commit, (" (main has it as %s)" % verdict.commit) if verdict.commit else "", seconds,
            summary or "no check rows read"))
        say("The repair is landed (closed by %s). The progress files are made again (the page to open: %s)." % (
            closed, page_paths(a.live).html.as_posix()))
        return 0

    if rec:
        say("joined the landing of %s, in the queue since %s" % (a.id, hhmm(rec["queue_time"])))
    return hand_to_gate(a, folder, branch, a.id, agent, ["--also", rep["check"]], passed, join=bool(rec))


def park_branch(a, folder, branch):
    """What `blocked` does for something in work: the folder must have no uncommitted change, and the branch goes to origin so
    that nothing is lost. Returns the refusal text, or None."""
    if folder.exists() and folder_changes(folder):
        return ("the work folder %s has uncommitted changes. Commit your unfinished work first (git add, git commit), then run "
                "blocked again." % folder.as_posix())
    here = folder if folder.exists() else a.tree
    if branch_exists(here, "refs/heads/" + branch):
        pushed = git(here, "push", "--force-with-lease", "origin", branch)
        if pushed.returncode != 0:
            return ("the branch %s could not be pushed to origin, so nothing is blocked yet: %s. Fix that and run blocked again." %
                    (branch, (pushed.stderr.strip().splitlines() or ["push refused"])[-1]))
    return None


def cmd_blocked(a):
    if is_repair(a.id):
        return blocked_repair(a)
    steps = read_steps(a.tree, a.live)
    s = find(steps, a.id)
    if s is None:
        say("plan.py: no step %s" % a.id)
        return 1
    if s["state"] == "landed":
        say("plan.py: step %s is landed" % a.id)
        return 1
    branch = "native/" + a.id
    if s["state"] == "started":                      # in work: its folder is left clean and its branch goes to origin
        why = park_branch(a, work_folder(a.work_root, s["agent"] or "1"), branch)
        if why:
            say("plan.py: " + why)
            return 1
    first = failures.Store(a.failures).last_line(branch) or "(no failure recorded)"
    reason = " ".join(a.why.split())
    set_state(a.tree, a.id, "blocked", agent=agent_of(a), reason=reason, live=a.live)
    append_under(native(a.tree) / "BLOCKED.md", "steps", "- %s %s: %s | first failure line: %s" % (
        datetime.date.today().isoformat(), a.id, reason, first))
    refresh_page(a)
    say("blocked %s. Run plan.py next." % a.id)
    return 0


def blocked_repair(a):
    rep = next((r for r in lv.read_repairs(live_dir(a.live)) if r["id"] == a.id), None)
    if rep is None:
        say("plan.py: no repair %s (plan.py repair list)" % a.id)
        return 1
    if rep["state"] == "landed":
        say("plan.py: repair %s is landed" % a.id)
        return 1
    branch = item_branch(a.id)
    if rep["state"] == "started":
        owner = (lv.read_state(live_dir(a.live)).get(a.id) or {}).get("agent") or "1"
        why = park_branch(a, work_folder(a.work_root, owner), branch)
        if why:
            say("plan.py: " + why)
            return 1
    first = failures.Store(a.failures).last_line(branch) or "(no failure recorded)"
    reason = " ".join(a.why.split())
    lv.set_repair(live_dir(a.live), a.id, "set-aside")
    lv.set_state_entry(live_dir(a.live), a.id, "set-aside")
    append_under(native(a.tree) / "BLOCKED.md", "repairs", "- %s %s: %s | first failure line: %s" % (
        datetime.date.today().isoformat(), a.id, reason, first))
    refresh_page(a)
    say("set aside %s. Run plan.py next." % a.id)
    return 0


def cmd_repair(a):
    live = live_dir(a.live)
    if a.repair_cmd == "list":
        rows = lv.repair_order(lv.read_repairs(live))
        if not rows:
            say("No repairs.")
            return 0
        say("%d repair%s; open ones of kind fail first (plan.py next gives the oldest of them):" % (len(rows), "" if len(rows) == 1 else "s"))
        for r in rows:
            say("%s  %-9s %-6s %s, %s, seen %s time%s, opened %s%s%s" % (
                r["id"], r["state"], r["kind"], lv.repair_what(r), lv.repair_traced(r), r["seen"], "" if r["seen"] == "1" else "s",
                r["opened"], ", ledger row " + r["row"] if r["row"] not in ("", "-") else "",
                ", closed by " + r["closed"] if r["closed"] not in ("", "-") else ""))
            say("      evidence: %s%s" % (r["evidence"], "; " + r["note"] if r["note"] not in ("", "-") else ""))
        return 0
    row, new = lv.add_repair(live, a.source, a.kind, a.check, a.renderer, a.step, a.commit, a.evidence, a.row, a.note)
    refresh_page(a)
    say(row["id"])                                   # the one line the nightly run reads: the repair's id, new or counted
    return 0


def cmd_tool(a):
    text = " ".join(a.text.split())
    append_under(native(a.tree) / "BLOCKED.md", "tools", "- %s: %s" % (datetime.date.today().isoformat(), text))
    lv.append_fault(live_dir(a.live), agent_of(a), "", "TOOL", "(by hand)", text)
    refresh_page(a)
    say("written under 'tools' in %s/BLOCKED.md and as a line of tool-faults.tsv. Take the next step (plan.py next); do not debug the tool." % NATIVE)
    return 0


def cmd_toolfault(a):
    if not a.last:
        say("plan.py: toolfault needs --last (it sets the sentence of the agent's last tool fault)")
        return 1
    row = lv.set_could_not_last(live_dir(a.live), agent_of(a), " ".join(a.could_not.split()))
    if row is None:
        say("plan.py: agent %s has no tool fault recorded" % agent_of(a))
        return 1
    refresh_page(a)
    say("written: %s %s %s (%s): could not: %s" % (row["time"], row["kind"], row["check"], row["step"] or "no step", row["could_not"]))
    return 0


def cmd_status(a):
    fetch_main_landed(a)
    text = status_text(a.tree, a.work_root, live=a.live, agent=agent_of(a), queue_file=getattr(a, "queue_file", None))
    atomic_write(native(a.tree) / "STATUS.md", text)
    sys.stdout.write(text)
    url = page_paths(a.live).url
    if url.exists():
        say("\nProgress page: %s" % url.read_text(encoding="utf-8").strip())
    return 0


def cmd_page(a):
    read_main_quietly(a)
    written = write_page(a.tree, a.live)
    p, data = written.paths, written.data
    steps = data["steps"]
    count = lambda state: sum(1 for e in steps.values() if e["state"] == state)
    say("progress.html:           %s (%d bytes)  the page to open, the data inside, works from disk" % (p.html.as_posix(), p.html.stat().st_size))
    say("STATUS.md:               %s (%d bytes)  the same data as plain text" % (p.status.as_posix(), p.status.stat().st_size))
    say("publish/index.html:      %s (%d bytes)  for the web" % (p.index.as_posix(), p.index.stat().st_size))
    say("publish/progress-data.json: %s (%d bytes)  for the web, beside index.html" % (p.data.as_posix(), p.data.stat().st_size))
    say("%d steps landed, %d started, %d blocked, %d tool fault kinds, %d flakes." % (
        count("landed"), count("started"), count("blocked"), len(data["tool_faults"]), data["flakes"]))
    say("Publish the two files of the publish folder together (the address is kept in %s); index.html changes only when the tool "
        "or the plan's step list changes." % p.url.as_posix())
    if push_enabled(a):
        try:
            repo = main_repo(a.tree)
            if repo is None:
                raise RuntimeError("%s is no git tree" % a.tree)
            say("progress branch: " + publish_progress(repo, written.text, written.landed, written.total))
        except Exception as e:  # noqa: BLE001 - a failed push is a warning, never the command's result
            say("plan.py: warning: the progress branch was not pushed: %s: %s" % (type(e).__name__, " ".join(str(e).split())[:200]))
    else:
        say("progress branch: not pushed (--no-push or %s)." % PUSH_OFF)
    return 0


def parser():
    p = argparse.ArgumentParser(prog="plan.py", description=__doc__.split("\n")[0])
    p.add_argument("--tree", default=str(TREE), help="the plan tree (default: the tree this file is in)")
    p.add_argument("--work-root", default=WORK_ROOT)
    p.add_argument("--live", default=LIVE, help="the live folder (claims, state, tool faults, the page)")
    p.add_argument("--failures", default=FAILURES, help="the three-failure store")
    p.add_argument("--queue-file", default=QUEUE_FILE, help="the gate's landing queue (a landing record is read against it)")
    p.add_argument("--no-push", action="store_true", help="do not push STATUS.md to the progress branch (also: %s=1)" % PUSH_OFF)
    p.add_argument("--tablet", default=TABLET)
    p.add_argument("--tablet-wait", type=float, default=TABLET_WAIT)
    who = argparse.ArgumentParser(add_help=False)
    who.add_argument("--agent", type=int, choices=(1, 2), default=1, help="1 is the master (the default), 2 the helper")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("next", parents=[who]).set_defaults(fn=cmd_next)
    sub.add_parser("status", parents=[who]).set_defaults(fn=cmd_status)
    sub.add_parser("page").set_defaults(fn=cmd_page)
    cd = sub.add_parser("card", help="any step's card, its package's rows and the checks it needs; changes nothing")
    cd.add_argument("id")
    cd.set_defaults(fn=cmd_card)
    ow = sub.add_parser("owed", parents=[who], help="what only a person or other hardware can check")
    ow.add_argument("id")
    ow.add_argument("text")
    ow.set_defaults(fn=cmd_owed)
    r = sub.add_parser("rows")
    r.add_argument("id", nargs="?")
    r.set_defaults(fn=cmd_rows)
    for name, fn in (("start", cmd_start), ("land", cmd_land)):
        q = sub.add_parser(name, parents=[who])
        q.add_argument("id")
        if name == "land":
            q.add_argument("--github-again", action="store_true",
                           help="a must job of GitHub that is red for the commit is run again once, then the landing judges by "
                                "the new result (when the log shows a download or an install failing, not the game)")
        q.set_defaults(fn=fn)
    c = sub.add_parser("claim", parents=[who])
    c.add_argument("package")
    c.add_argument("--take-stale", action="store_true")
    c.set_defaults(fn=cmd_claim)
    b = sub.add_parser("blocked", parents=[who])
    b.add_argument("id")
    b.add_argument("why")
    b.set_defaults(fn=cmd_blocked)
    t = sub.add_parser("tool", parents=[who])
    t.add_argument("text")
    t.set_defaults(fn=cmd_tool)
    tf = sub.add_parser("toolfault", parents=[who])
    tf.add_argument("--last", action="store_true")
    tf.add_argument("--could-not", required=True)
    tf.set_defaults(fn=cmd_toolfault)
    rp = sub.add_parser("repair", help="what is found wrong after a landing: add one, list them")
    rsub = rp.add_subparsers(dest="repair_cmd", required=True)
    ra = rsub.add_parser("add", help="write a repair (the nightly run calls this) and print its id")
    ra.add_argument("--source", required=True, choices=lv.REPAIR_SOURCES)
    ra.add_argument("--kind", required=True, choices=lv.REPAIR_KINDS)
    ra.add_argument("--check", required=True, help="the check as the gate names it, for example tour/d3d11")
    ra.add_argument("--renderer", default="-")
    ra.add_argument("--step", default="-", help="the step of the landing the fault was traced to")
    ra.add_argument("--commit", default="-", help="the commit of that landing")
    ra.add_argument("--evidence", required=True, help="the folder with the evidence")
    ra.add_argument("--row", default="-", help="the ledger row")
    ra.add_argument("--note", required=True)
    ra.set_defaults(fn=cmd_repair)
    rl = rsub.add_parser("list", help="the repairs, open ones first")
    rl.set_defaults(fn=cmd_repair)
    return p


def main(argv=None):
    # A self-test that starts this tool as a child process runs it under the live-state guard (tools/gate/selftest_guard.py).
    import pathlib as _pathlib
    import sys as _sys
    _gate = str(_pathlib.Path(__file__).resolve().parents[1] / "gate")
    _sys.path.insert(0, _gate)
    try:
        import selftest_guard
    finally:
        _sys.path.remove(_gate)
    selftest_guard.install_if_asked()
    a = parser().parse_args(argv)
    MAIN_LANDED.clear()
    del MAIN_READ[:]
    if getattr(a, "agent", None) is not None:
        why = own_copy_refusal(a.agent, TREE, a.work_root)
        if why:
            say(why)
            return 2
    if a.cmd not in ("next", "start", "land", "status", "card"):
        read_main_quietly(a)           # the others read what is landed from the last fetched origin/main, with no fetch and no line
    try:
        return a.fn(a)
    except (ValueError, OSError, RuntimeError, KeyError) as e:
        say("plan.py: %s: %s" % (type(e).__name__, e))
        return 2
    finally:
        MAIN_LANDED.clear()
        del MAIN_READ[:]


if __name__ == "__main__":
    sys.exit(main())
