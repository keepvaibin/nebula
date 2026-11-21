"""plan.py: the step tool of the native plan (package T13). The coding agent reads, codes and proves; this says what is next.

    python -X utf8 -I tools/plan/plan.py next | status | page | rows [ID] | start ID | land ID | blocked ID "WHY"
                                         | claim PACKAGE [--take-stale] | tool "TEXT" | toolfault --last --could-not "TEXT"
    (next, start, land, blocked, claim, tool, toolfault and status take --agent 1|2; 1 is the default)

The plan is `docs/plan/native/`: `steps.tsv` (order, step, package, title, package_needs, card_file, state, commit),
the step cards in the three card files, the package row tables, `rows/<package>.tsv`. The repository's copy is the
only one read. Python standard library only.

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
start    Branch `native/<id>` from main in `D:/NebulaWork/MarioGalaxyRecomp/N-<id>`; the step becomes `started` and its
         package is claimed by the agent. Refuses while three steps of the agent are started and not landed, a step whose
         needs are not landed, a package another agent holds, and for agent 2 a package that is not on its list.
land     Refuses unless every ledger row the card closes whole is `fixed` with a proof that names a test, a search or a
         measure, and every row it only advances has a dated note written by this branch; checks the file and line
         references in the changed documents and the commit messages (tools/verify/doc_refs.py); then hands the branch
         to the gate (`land_with_gate`, below). A pass sets `landed` and the commit in the same landing. A failure is
         stored by signature (failures.py): the third time in a row the same one comes back it exits with code 3.
         A TOOL or ENV result is also written, at once, as a line of tool-faults.tsv (live.py), and the command that adds
         the agent's "could not" sentence is printed.
blocked  Sets `blocked`; writes the reason and the failure's first line into BLOCKED.md. `next` then skips to
         independent work.
claim    Claims a package for the agent; a stale claim of the other agent is taken over with --take-stale (its branch is
         left as it is).
tool     Writes a tool fault into BLOCKED.md under "tools" and into tool-faults.tsv. The step is not blocked.
toolfault --last --could-not "TEXT": the sentence on what the agent's last tool fault stopped it from doing.
status   Writes STATUS.md and prints it: steps landed, started, blocked, claims, the last five landings, the next step.
         The agent runs it at every start and after every context compaction; it is the only memory it needs.
page     Writes three files into the live folder (page.py), from steps.tsv, the live state, the tool faults and
         tests/flakes.tsv: progress.html (static, rewritten only when the tool or the step list changes),
         progress-data.json (only what changes) and progress-local.html (the data embedded, for opening from disk).
         Publish the first two. land, start, blocked and toolfault do it too, so the page is never behind.
rows     What a card's "Rows." line means (closes whole / advances), for one step or all of them.

Exit codes: 0 done; 1 refused or failed; 2 the command was wrong; 3 the same failure three times; 4 a tool or
machine fault only (it does not count as a failure of the change).

State. `landed` and the commit are in steps.tsv (the repository) and travel with the landing's own commit; after a pass
they are written into the plan tree too. Everything that more than one agent, or the progress page, must see is in the
live folder (LIVE below; `--live` for tests): started and blocked steps, claims, tool faults, the landings' times, the
page. It is never kept in a file inside one work folder. STATUS.md is the plan tree's own, untracked; BLOCKED.md is a
plain file the agent may commit.
"""
import argparse
import datetime
import fnmatch
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

import failures  # noqa: E402
import doc_refs  # noqa: E402
import live as lv  # noqa: E402
import page  # noqa: E402

GIT = "C:/Program Files/Git/cmd/git.exe"
NATIVE = "docs/plan/native"
WORK_ROOT = "D:/NebulaWork/MarioGalaxyRecomp"
FAILURES = "D:/NebulaWork/plan/landing-failures.tsv"
TABLET = "R52WA0FXE5J"
TABLET_WAIT = 30
MAX_STARTED = 3
LIVE = "D:/NebulaWork/plan/native"       # the live state: claims, started and blocked steps, tool faults, the page
AGENT2_LIST = ("G6", "G7", "G8", "G9", "G1", "R10", "R9", "G4")   # the helper's packages, in this order, and no others
# Two groups of steps that must not be in work at the same time (both change the renderers' frame and present code).
PAIRS = ((("G2a",), ("R7",)),)

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
    lines = path.read_text(encoding="utf-8").splitlines()
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


def read_steps(tree, live=None):
    """The steps, in file order, with their state: what `steps.tsv` says (`open` or `landed`, with the commit), and for an
    open step the live state (`started`, `blocked`, with `agent` and `reason`). Every row has `agent` and `reason`."""
    steps = read_base(tree)
    local = lv.read_state(live_dir(live))
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

# tools/gate/scope.tsv (package T4) is read when it exists; this is the table the plan was written with. Kinds: `only`
# (the row applies to a path no other row names), `first` (the first of these rows that names the path), `add` (every one
# that names it, on top).
SCOPE_BUILTIN = [
    ("docs/**,*.md,docs/plan/ledger.tsv", "only", "repo/*"),
    ("tools/**", "only", "repo/*, harness/self, description/self"),
    ("native/**", "first", "build, unit, start on three renderers, tour (12 stops) on Direct3D 12 with its pictures and replays"),
    ("runtime/src/render/**,runtime/include/galaxy/render/**", "first",
     "build, unit, start, engine and tour (12 stops) on three renderers with pictures and replays"),
    ("runtime/**,platform/sdl/**", "first", "build, unit, start, engine and tour (12 stops) on Direct3D 12"),
    ("crates/nebula-import/**", "first",
     "rust, collision proof on the US disc, the US data made again, tour (12 stops) on Direct3D 12 with pictures"),
    ("platform/android/**,runtime/src/render/vk/**", "add",
     "the Android build and its unit tests, the tablet's short set (12 stops) if the tablet answers, else owed"),
]
SCOPE_EVERYTHING = "everything the main gate runs today"


def glob_regex(pattern):
    out, i = [], 0
    while i < len(pattern):
        if pattern.startswith("**", i):
            out.append(".*")
            i += 2
        elif pattern[i] == "*":
            out.append("[^/]*")
            i += 1
        elif pattern[i] == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(pattern[i]))
            i += 1
    return re.compile("^" + "".join(out) + "$")


def path_matches(path, patterns):
    """True when `path` matches one of the shell patterns (`**` any folders, `*` within a name; a pattern with no
    folder, `*.md`, matches the file's name anywhere)."""
    for p in patterns:
        p = p.strip()
        if not p:
            continue
        if "/" not in p:
            if fnmatch.fnmatchcase(path.rsplit("/", 1)[-1], p):
                return True
        elif glob_regex(p).match(path):
            return True
    return False


def read_scope(tree):
    """The scope table: tools/gate/scope.tsv when it is there and readable (columns: patterns, checks, kind), else the
    built-in one."""
    path = pathlib.Path(tree) / "tools" / "gate" / "scope.tsv"
    rows = []
    if path.is_file():
        for line in path.read_text(encoding="utf-8").splitlines():
            cells = line.split("\t")
            if len(cells) >= 2 and not line.startswith("#") and cells[0].strip().lower() not in (
                    "paths", "pattern", "patterns", "changed paths"):
                kind = (cells[2].strip().lower() if len(cells) > 2 else "first")
                rows.append((cells[0], kind if kind in ("only", "first", "add") else "first", cells[1]))
    return rows if len(rows) >= 2 else list(SCOPE_BUILTIN)


def checks_for(paths, scope):
    """The checks that the union of the rows `paths` match runs: [text], in table order. A path takes the first
    `first` row that names it and every `add` row; with none of those, the `only` rows that name it; a path no row
    names runs everything."""
    paths = [p for p in paths if p]
    if not paths:
        return [SCOPE_EVERYTHING + " (the card names no path of the tree)"]
    chosen, only_chosen, everything = set(), set(), False
    for path in paths:
        hit = False
        first_done = False
        for i, (pats, kind, _) in enumerate(scope):
            if kind == "first" and not first_done and path_matches(path, pats.split(",")):
                chosen.add(i)
                first_done = hit = True
            elif kind == "add" and path_matches(path, pats.split(",")):
                chosen.add(i)
                hit = True
        if not hit:
            for i, (pats, kind, _) in enumerate(scope):
                if kind == "only" and path_matches(path, pats.split(",")):
                    only_chosen.add(i)
                    hit = True
        everything = everything or not hit
    out = []
    if not chosen:                        # an `only` row is for a change that is only that: code beside it wins
        chosen = only_chosen
    for i in sorted(chosen):
        if scope[i][2] not in out:
            out.append(scope[i][2])
    if everything:
        out.append(SCOPE_EVERYTHING + " (for a path no row names)")
    return out


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


# ---- the gate ------------------------------------------------------------------------------------------------

GATE_ROW = re.compile(r"^(\S+)\s+(pass|known|planned|owed|FAIL|TOOL|ENV|FLAKE|NEW)\s+(\d+(?:\.\d+)?) s\s*(.*)$")
KIND_OF = {"pass": "PASS", "known": "PASS", "planned": "PASS", "owed": "ENV", "FAIL": "FAIL", "TOOL": "TOOL", "ENV": "ENV",
           "FLAKE": "FLAKE", "NEW": "NEW"}


class Verdict:
    """The landing's result: `passed`; `rows` [(check, kind, detail)] (every check the gate printed); `failures`
    [(check, kind, first_error_line)]: what did not pass; `lines`: the gate's output."""

    def __init__(self, passed, rows=(), failures_=(), lines=()):
        self.passed, self.rows, self.failures, self.lines = passed, list(rows), list(failures_), list(lines)

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


def run_gate(folder, args, out=None):
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
    read = read_gate_output(lines)
    rows = [(c, k, d) for c, k, d, _ in read]
    # An `owed` check (the tablet is away) is a pass for the gate: it is a kind ENV in the counts and not a failure.
    bad = [(c, k, d) for c, k, d, state in read if state in ("FAIL", "TOOL", "ENV")]
    if code == 0:
        return Verdict(True, rows, [], lines)
    if code == 2:
        why = next((l.split("gate.py:", 1)[1].strip() for l in lines if l.startswith("gate.py:")), "refused")
        return Verdict(False, rows, bad or [("gate.py", "FAIL", why)], lines)
    if not bad:
        return Verdict(False, rows, [("gate.py", "TOOL", "exited %d with no failing check in its output" % code)], lines)
    return Verdict(False, rows, bad, lines)


def land_with_gate(folder, branch, tablet=TABLET, tablet_wait=TABLET_WAIT, out=None):
    """THE LANDING STEP. Today: the quick gate, then the main gate with the move, from the step's work folder, the
    branch pushed first (the gate wants it on origin). When `gate.py land` exists (a later package), this body becomes
    one call, `run_gate(folder, ["land", branch], out)`, and nothing else in this file changes: it returns the same
    Verdict, and the three-failure store (failures.py) is called the same way by both."""
    out = out or sys.stdout
    # --force-with-lease: a failed try took its state commit off the branch again (cmd_land), so the next push is not a
    # fast-forward of what origin has. The branch is this step's own.
    pushed = git(folder, "push", "--force-with-lease", "origin", branch)
    if pushed.returncode != 0:
        return Verdict(False, [], [("git/push", "FAIL", (pushed.stderr.strip().splitlines() or ["push refused"])[-1])])
    quick = run_gate(folder, ["--quick"], out)
    if not quick.passed:
        return quick
    main = run_gate(folder, ["--main", "--move", "--tablet", tablet, "--tablet-wait", str(tablet_wait)], out)
    main.rows = quick.rows + main.rows
    return main


LAND = land_with_gate            # tests put their own in here


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


def work_folder(work_root, step_id):
    return pathlib.Path(work_root) / ("N-" + step_id)


def count_flakes(tree):
    """The number of lines in tests/flakes.tsv (the header, if it has one, not counted); 0 when there is no file."""
    path = pathlib.Path(tree) / "tests" / "flakes.tsv"
    if not path.exists():
        return 0
    lines = [l for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
    return len(lines) - (1 if lines and lines[0].lower().startswith(("date", "time")) else 0)


def status_text(tree, work_root=WORK_ROOT, at=None, live=None):
    steps = read_steps(tree, live)
    by = {s: [x for x in steps if x["state"] == s] for s in STATES}
    reasons = blocked_reasons(tree)
    at = at or now()
    claims = lv.read_claims(live_dir(live))
    stale = stale_of(tree, claims, at)
    out = ["# Native plan: status", "", "Written %s by plan.py status. Run it at every start and after every context "
           "compaction." % at.isoformat(timespec="minutes"), "",
           "Steps landed: %d of %d." % (len(by["landed"]), len(steps)), ""]
    out.append("## Started (%d, at most %d for one agent)" % (len(by["started"]), MAX_STARTED))
    out.append("")
    out += ["- %s %s: agent %s, branch native/%s in %s" % (s["step"], s["title"], s["agent"] or "1", s["step"],
                                                          work_folder(work_root, s["step"]).as_posix()) for s in by["started"]] or ["- none"]
    out += ["", "## Blocked (%d)" % len(by["blocked"]), ""]
    out += ["- %s %s: %s" % (s["step"], s["title"], s["reason"] or reasons.get(s["step"], "(no reason written)"))
            for s in by["blocked"]] or ["- none"]
    out += ["", "## Claims (%d)" % len(claims), ""]
    out += ["- %s: agent %s since %s%s%s" % (c["package"], c["agent"], c["time"], ", branch " + c["branch"] if c["branch"] else "",
                                            " STALE (no commit for %d hours): plan.py claim %s --take-stale" % (lv.STALE_HOURS, c["package"])
                                            if c["package"] in stale else "") for c in claims] or ["- none"]
    faults = lv.fault_groups(lv.read_faults(live_dir(live)))
    out += ["", "## Tool faults (%d kinds, %d events)" % (len(faults), sum(g["count"] for g in faults)), ""]
    out += ["- %dx %s %s: %s" % (g["count"], g["kind"], g["check"], g["first_error"][:100]) for g in faults[:5]] or ["- none"]
    out += ["", "## Last five landings", ""]
    last = lv.read_landings(live_dir(live))[-5:]
    out += ["- %s: commit %s, %s, %s s" % (c["step"], c["commit"], c["finished"], c["seconds"]) for c in reversed(last)] or ["- none yet"]
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


def page_paths(live=None):
    """(progress.html, progress-data.json, progress-url.txt, progress-local.html)."""
    d = live_dir(live)
    return d / "progress.html", d / "progress-data.json", d / "progress-url.txt", d / "progress-local.html"


def page_data(tree, live=None, at=None):
    """(static, data): what never changes with progress (the step list), and what does."""
    steps = read_steps(tree, live)
    reasons = blocked_reasons(tree)
    for s in steps:
        if s["state"] == "blocked" and not s["reason"]:
            s["reason"] = reasons.get(s["step"], "")
    d = live_dir(live)
    claims = lv.read_claims(d)
    at = at or now()
    return page.make_static(steps), page.make_data(steps, lv.read_landings(d), lv.read_state(d), claims, stale_of(tree, claims, at),
                                                   lv.fault_groups(lv.read_faults(d)), count_flakes(tree), lv.stamp(at))


def write_page(tree, live=None, at=None):
    """Writes the three files into the live folder: progress-data.json and progress-local.html (the data embedded, for
    opening from disk) each time, progress.html only when its text changed, which it does when this tool or the plan's
    step list changes and never because progress changed. Returns (html path, json path, local path, data)."""
    html_path, json_path, _, local_path = page_paths(live)
    static, data = page_data(tree, live, at)
    with lv.locked(live_dir(live)):
        html = page.render(static)
        if not html_path.exists() or html_path.read_text(encoding="utf-8") != html:
            atomic_write(html_path, html)
        atomic_write(json_path, page.data_json(data))
        atomic_write(local_path, page.render(static, data))
    return html_path, json_path, local_path, data


def refresh_page(a):
    """After a change of state: the page's files are made again. Never a reason to fail the command."""
    try:
        write_page(a.tree, a.live)
    except (OSError, ValueError, lv.Busy):
        pass


# ---- commands ------------------------------------------------------------------------------------------------

def agent_of(a):
    return str(getattr(a, "agent", 1))


def say_stale(a, claims, stale):
    at = now()
    for c in claims:
        if c["package"] in stale and c["agent"] != agent_of(a):
            hours = lv.claim_age_hours(c, lambda b: branch_time(a.tree, b), at)
            say("STALE: package %s is claimed by agent %s (no commit on %s for %d hours). Take it over with: "
                "plan.py claim %s --take-stale --agent %s" % (c["package"], c["agent"], c["branch"] or "its branch", hours,
                                                               c["package"], agent_of(a)))


def cmd_next(a):
    steps = read_steps(a.tree, a.live)
    claims = lv.read_claims(live_dir(a.live))
    stale = stale_of(a.tree, claims)
    landed = sum(1 for s in steps if s["state"] == "landed")
    agent = agent_of(a)
    say_stale(a, claims, stale)
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
    s = choice.step
    say("NEXT: %s  %s" % (s["step"], s["title"]))
    say("Package %s; step %s of %d; %d landed so far." % (s["package"], s["order"], len(steps), landed))
    for note in choice.notes:
        say("NOTE: " + note)
    for b in choice.blocked_before:
        why = b["reason"] or blocked_reasons(a.tree).get(b["step"], "(no reason written)")
        say("BLOCKED: %s %s: %s" % (b["step"], b["title"], why))
    if choice.blocked_before:
        say("The step above is blocked, so the first later step that does not need its package is %s." % s["step"])
    say("Needs first: %s." % ("%s (all landed)" % s["package_needs"] if s["package_needs"] else "nothing outside its own package"))
    say()
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
    say()
    say("=== COMMANDS ===")
    plan = (pathlib.Path(a.tree) / "tools" / "plan" / "plan.py").as_posix()
    who = " --agent 2" if agent == "2" else ""
    say("start: python -X utf8 -I %s start %s%s" % (plan, s["step"], who))
    say("land:  python -X utf8 -I %s land %s%s" % (plan, s["step"], who))
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


def cmd_start(a):
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
    mine = [x["step"] for x in steps if x["state"] == "started" and (x["agent"] or "1") == agent]
    if len(mine) >= MAX_STARTED:
        say("plan.py: %d steps are started and not landed (%s). Land one first (plan.py land <id>)." % (len(mine), ", ".join(mine)))
        return 1
    waiting = unmet(s, steps)
    if waiting:
        say("plan.py: step %s needs first: %s" % (a.id, ", ".join("%s (%s)" % w for w in waiting)))
        return 1
    why = pair_conflict(s, steps)
    if why:
        say("plan.py: step %s waits: %s" % (a.id, why))
        return 1
    folder = work_folder(a.work_root, a.id)
    branch = "native/" + a.id
    if folder.exists():
        say("plan.py: the work folder %s exists already" % folder.as_posix())
        return 1
    fetched = git(a.tree, "fetch", "origin")
    if fetched.returncode != 0:
        say("plan.py: git fetch origin failed: %s" % fetched.stderr.strip())
        return 1
    have = git(a.tree, "rev-parse", "--verify", "--quiet", "refs/heads/" + branch).returncode == 0
    made = git(a.tree, "worktree", "add", str(folder), *(([branch]) if have else ["-b", branch, "origin/main"]))
    if made.returncode != 0:
        say("plan.py: git worktree add failed: %s" % made.stderr.strip())
        return 1
    set_state(a.tree, a.id, "started", agent=agent, live=a.live)
    lv.put_claim(live_dir(a.live), s["package"], agent, branch)
    refresh_page(a)
    say("started %s: branch %s in %s (package %s is claimed by agent %s)" % (a.id, branch, folder.as_posix(), s["package"], agent))
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


def cmd_land(a):
    steps = read_steps(a.tree, a.live)
    s = find(steps, a.id)
    agent = agent_of(a)
    if s is None:
        say("plan.py: no step %s" % a.id)
        return 1
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
    folder = work_folder(a.work_root, a.id)
    branch = "native/" + a.id
    if not folder.is_dir():
        say("plan.py: the work folder %s is missing" % folder.as_posix())
        return 1
    if git_out(folder, "status", "--porcelain"):
        say("plan.py: the work folder has uncommitted changes; commit them first")
        return 1
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
    allow = doc_refs.read_allow(folder / "tools" / "verify" / "doc_refs_allow.txt")
    bad = doc_refs.check_files([folder / d for d in changed_documents(folder)], folder, allow)
    bad += doc_refs.check_text("the commit messages", commit_messages(folder), folder, allow, message=True)
    if bad:
        say("plan.py: a file or line named in the documents or the commit messages does not exist:")
        for l in bad:
            say("  " + l)
        return 1
    # 3. The landing's own commit: the step is landed, with the code's commit.
    code_commit = git_out(folder, "rev-parse", "--short=9", "HEAD")
    set_state(folder, a.id, "landed", code_commit, track=False)
    with tempfile.TemporaryDirectory() as t:
        msg = pathlib.Path(t) / "message.txt"
        msg.write_text("Plan: step %s landed\n\nThe step's code is %s.\n" % (a.id, code_commit), encoding="utf-8")
        git(folder, "add", "%s/steps.tsv" % NATIVE, check=True)
        committed = git(folder, "commit", "-F", str(msg))
    if committed.returncode != 0:
        git(folder, "checkout", "--", "%s/steps.tsv" % NATIVE)
        say("plan.py: the state commit failed (a hook?):\n%s" % (committed.stdout + committed.stderr).strip())
        return 1
    started = time.time()
    verdict = LAND(folder, branch, a.tablet, a.tablet_wait)
    seconds = int(time.time() - started)
    store = failures.Store(a.failures)
    say()
    summary = ", ".join("%d %s" % (n, k) for k, n in sorted(verdict.counts().items()))
    if verdict.passed:
        store.clear(branch)
        set_state(a.tree, a.id, "landed", code_commit, live=a.live)
        lv.add_landing(live_dir(a.live), a.id, code_commit, seconds, agent)
        if all(x["state"] == "landed" or x["step"] == a.id for x in steps if x["package"] == s["package"]):
            lv.drop_claim(live_dir(a.live), s["package"])
        else:
            lv.put_claim(live_dir(a.live), s["package"], agent)
        refresh_page(a)
        say("LANDED %s as %s in %d s. Verdict by kind: %s." % (a.id, code_commit, seconds, summary or "no check rows read"))
        say("Publish the page again: progress.html with progress-data.json, to the address in %s." % page_paths(a.live)[2].as_posix())
        return 0
    git(folder, "reset", "--hard", "HEAD~1")        # the state commit goes; the next try makes it again
    say("NOT LANDED: %s. Verdict by kind: %s." % (a.id, summary or "no check rows read"))
    for check, kind, detail in verdict.failures:
        say("  %-5s %s: %s" % (kind, check, detail))
    written = record_faults(a, a.id, verdict)
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


def cmd_blocked(a):
    steps = read_steps(a.tree, a.live)
    s = find(steps, a.id)
    if s is None:
        say("plan.py: no step %s" % a.id)
        return 1
    if s["state"] == "landed":
        say("plan.py: step %s is landed" % a.id)
        return 1
    first = failures.Store(a.failures).last_line("native/" + a.id) or "(no failure recorded)"
    reason = " ".join(a.why.split())
    set_state(a.tree, a.id, "blocked", agent=agent_of(a), reason=reason, live=a.live)
    append_under(native(a.tree) / "BLOCKED.md", "steps", "- %s %s: %s | first failure line: %s" % (
        datetime.date.today().isoformat(), a.id, reason, first))
    refresh_page(a)
    say("blocked %s. Run plan.py next." % a.id)
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
    text = status_text(a.tree, a.work_root, live=a.live)
    atomic_write(native(a.tree) / "STATUS.md", text)
    sys.stdout.write(text)
    url = page_paths(a.live)[2]
    if url.exists():
        say("\nProgress page: %s" % url.read_text(encoding="utf-8").strip())
    return 0


def cmd_page(a):
    html_path, json_path, local_path, data = write_page(a.tree, a.live)
    steps = data["steps"]
    count = lambda state: sum(1 for e in steps.values() if e["state"] == state)
    say("progress.html:        %s (%d bytes)  publish" % (html_path.as_posix(), html_path.stat().st_size))
    say("progress-data.json:   %s (%d bytes)  publish" % (json_path.as_posix(), json_path.stat().st_size))
    say("progress-local.html:  %s (%d bytes)  for opening from disk; do not publish" % (local_path.as_posix(), local_path.stat().st_size))
    say("%d steps landed, %d started, %d blocked, %d tool fault kinds, %d flakes." % (
        count("landed"), count("started"), count("blocked"), len(data["tool_faults"]), data["flakes"]))
    say("Publish progress.html with progress-data.json as its supporting file; after a landing publish again to the same address "
        "(the address is kept in %s). progress.html changes only when the tool or the plan's step list changes." % page_paths(a.live)[2].as_posix())
    return 0


def parser():
    p = argparse.ArgumentParser(prog="plan.py", description=__doc__.split("\n")[0])
    p.add_argument("--tree", default=str(TREE), help="the plan tree (default: the tree this file is in)")
    p.add_argument("--work-root", default=WORK_ROOT)
    p.add_argument("--live", default=LIVE, help="the live folder (claims, state, tool faults, the page)")
    p.add_argument("--failures", default=FAILURES, help="the three-failure store")
    p.add_argument("--tablet", default=TABLET)
    p.add_argument("--tablet-wait", type=float, default=TABLET_WAIT)
    who = argparse.ArgumentParser(add_help=False)
    who.add_argument("--agent", type=int, choices=(1, 2), default=1, help="1 is the master (the default), 2 the helper")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("next", parents=[who]).set_defaults(fn=cmd_next)
    sub.add_parser("status", parents=[who]).set_defaults(fn=cmd_status)
    sub.add_parser("page").set_defaults(fn=cmd_page)
    r = sub.add_parser("rows")
    r.add_argument("id", nargs="?")
    r.set_defaults(fn=cmd_rows)
    for name, fn in (("start", cmd_start), ("land", cmd_land)):
        q = sub.add_parser(name, parents=[who])
        q.add_argument("id")
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
    return p


def main(argv=None):
    a = parser().parse_args(argv)
    try:
        return a.fn(a)
    except (ValueError, OSError, RuntimeError, KeyError) as e:
        say("plan.py: %s: %s" % (type(e).__name__, e))
        return 2


if __name__ == "__main__":
    sys.exit(main())
