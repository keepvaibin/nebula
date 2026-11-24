"""plan.py: the step tool of the native plan (package T13). The coding agent reads, codes and proves; this says what is next.

    python -X utf8 -I tools/plan/plan.py next | status | page | card ID | rows [ID] | start ID | land ID [--github-again] | blocked ID "WHY"
                                         | ahead | stack [--list] | wait ID [--minutes N] [--log FILE] | handoff ID | hard ID "LINE"
                                         | owed ID "TEXT" | commands | note "TEXT" | note --clear
                                         | claim PACKAGE [--take-stale] | tool "TEXT" | toolfault --last --could-not "TEXT"
                                         | mend open|land|close|failed|drop|list ... | decide ID "WHAT" --because "WHY" [--replaces N] | decide ID --list
                                         | alive | check [--no-units] [--no-build]
                                         | repair add --source S --kind K --check C ... | repair list | repair end R-00N --mend M-00N
    (next, ahead, start, land, blocked, claim, tool, toolfault, status and commands take --agent 1|2; 1 is the default; ID is a step, or for
     start, land and blocked a repair such as R-003)
    (--no-push, before the command, or NEBULA_PLAN_NO_PUSH=1 in the environment: the progress branch is not pushed; --build-root FOLDER, before the
     command: where the build folders are, for the self-tests and the plants)
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
         `next`, `status` and `land` first read GitHub's jobs of the landings that moved main before they ended (live folder,
         github-pending.tsv; one question for each line, no waiting): green is one line, a red job opens a repair (source
         github, check github/<job>) that `next` gives before any step, even when a step is in work: it says the step is kept on
         its branch; `start R-nnn` then keeps the step (its work must be committed: the branch goes to origin, the step is blocked
         with the reason "kept on its branch for repair R-nnn") and starts the repair, which `land` hands to the gate with
         `--github first` (main moves only after GitHub's jobs are green); once the repair has landed `next` offers the kept step
         and `start <step>` puts its branch on top of the new main (a rebase; a conflict leaves the branch as it was).
start    One work folder for each agent, kept: agent 1 works in `D:/NebulaWork/MarioGalaxyRecomp/Nebula-native`, agent 2 in
         `Nebula-native-2`. `start` makes the folder the first time (git worktree add) and uses it every later time, so the
         build beside it is kept and only changed files are compiled again. It refuses, with one line saying what to do,
         when the folder has uncommitted changes or when the agent has something in work (a step or a repair that is
         `started`: one thing at a time); then it fetches, makes the branch `native/<id>` from `origin/main` in that
         folder, sets the step `started` and claims its package for the agent. Also refuses a step whose needs are not
         landed, a package another agent holds, and for agent 2 a package that is not on its list.
land     Can be run again at any time and never resets the work folder. The tool's own commit (`Plan: step X landed`, only
         steps.tsv) is made for the landing, and taken off the branch again when the landing does not pass (failed, stopped or not
         judged: only a landing not judged while the folder is on a step written ahead keeps it, to be handed over again from there),
         so that no commit says `landed` for a step that is not on main. One left by a landing that was cut off: a tip that already is it is
         handed over as it is, and fix commits on top of one get it taken off and a new one made, naming the newer code commit. A landing record (live folder, `landing-<agent>.tsv`) stands while the gate has the
         branch: `land` then joins ("joined the landing of X, in the queue since HH:MM"), `status` and `next` begin with
         "LANDING RUNNING: ...", `start` refuses. When origin/main already holds the step: "LANDED (found on main)". A branch that
         changes tests/replays/ lands only if the card says the replay changes, and a commit message says "replay reference".
         The extra checks of docs/plan/native/step-checks.tsv go to the gate as --also. Otherwise:
         Refuses unless every ledger row the card closes whole is `fixed` with a proof that names a test, a search or a
         measure, and every row it only advances has a dated note written by this branch; checks the file and line
         references in the changed documents and the commit messages (tools/verify/doc_refs.py), the repository's code checks, the
         strictness check (tools/gate/strictness.py) and the rule that a step changes a test tool, a GitHub job, approved.tsv, steps.tsv
         or step-checks.tsv only where its card names the file; a new landing takes the tool's own earlier state commit off the branch
         first; after a pass it prints the speed checks' lines and keeps them with the step; then hands the branch
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
ahead    While the landing of the item N in work (a step or a repair) is in the gate's hands: makes the branch `native/<M>` of the step
         M that follows N on top of the tip the landing was given, in the work folder (clean, on N's branch), and prints M's card, its
         rows, what the ledger must say, the checks and the commands; M is written there, never landed. It changes no step's state, makes
         no claim and pushes nothing; given again for the same M (N was mended and landed once more) it moves M's own commits onto N's
         new tip (`git rebase --onto`; a conflict is aborted, the branch is as it was and the command to do by hand is printed). One step
         ahead at a time. While M is written ahead, `land N` joins or hands over from M's branch (its own commits stay, the working tree
         is not written to when the landing ends), `next` and `status` name M, and `start M` puts M's branch on top of main: nothing
         moves when origin/main is in it, else M's own commits are moved onto origin/main. Nothing ever deletes a branch.
claim    Claims a package for the agent; a stale claim of the other agent is taken over with --take-stale (its branch is
         left as it is).
tool     Writes a tool fault into BLOCKED.md under "tools" and into tool-faults.tsv. The step is not blocked.
toolfault --last --could-not "TEXT": the sentence on what the agent's last tool fault stopped it from doing.
mend     A tool at fault is mended by one helper while the agent waits (docs/plan/native/TOOL-MEND.md, AGENT.md section 9). `mend open
         [ID] "TITLE" [--last] [--check C]` writes the fault sheet (live folder, mends/M-00N.md) and puts the step in work into the
         state `waits for M-00N` (still started, never blocked); one mend is open at a time, and while it is, `next`, `start` and `land`
         answer `WAIT: mend M-00N is open`; `ahead` writes the step after the one in work on top of its branch (it refuses when the mend's
         sheet or its branch mend/M-00N names a file that step's card names, or when the mend waits for no step). The helper's prompt is
         four lines: the last asks for one report, at its end. `mend land M-00N` is the helper's: from its own folder on the branch mend/M-00N, the
         references check, the code checks and the strictness check, then the gate with `--who "mend M-00N"` (its landing record is
         landing-mend.tsv). `mend close M-00N COMMIT`: the commit is on origin/main and touches tools/, docs/ or .github/; the step goes on,
         the steps blocked `tool: M-00N` are open again. `mend failed M-00N "WHY"` counts the attempts on the sheet (the helper, a second
         helper, the agent itself) and prints what comes next; the third blocks the step `tool: M-00N`. `mend drop M-00N "FILES"`: the
         fault needs game code, no tool fault, the step is the agent's again. `mend open ... --landed COMMIT` records a mend landed by
         hand (plan.py itself was at fault). `mend list`. State: mends.tsv in the live folder.
decide   `decide ID "WHAT" --because "WHY" [--doc "FILE, SECTION"] [--replaces N]` appends a row to docs/plan/native/decisions.tsv of the plan
         tree; `--replaces N` names an earlier decision of the step (`ID#N`, as `decide ID --list` prints them) that this one corrects: the file
         keeps both, the page and `--list` show the newest.
alive    One word: `working` (a command in the last three hours, or a landing or mend landing in the gate's line), `ended` (a note that
         begins Done or Stopped, or every step landed) or `stale`. Every other command writes the time into heartbeat.txt (live folder).
check    What `land` checks before it queues, on the agent's work folder as it is, every refusal at once; nothing is committed or queued.
         Whole, it also builds (the agent's own build command) and runs the unit tests GitHub's Check runs that can run on this PC: ctest over
         the Windows build and the game's, and the Rust tests (units.py); a failing one is refused by name with its own lines, and what only
         GitHub's other jobs run is named as `not proven here`. `--no-units` skips all of it, `--no-build` skips the build.
         On a step written ahead it counts that step's own commits, not those of the step below it.
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
commands The agent's five commands filled in for its folder (agent 1: Nebula-native, agent 2: Nebula-native-2) and its build folder
         (D:/NebulaWork/build/<the folder's name>): build the game for Windows, run one unit-test program by name, run one engine
         test by name on Direct3D 12, play the tour's 12 stops (tools/gate/agent_tour.py), record the replay references. Each is the
         form the gate uses, with the game data, the hidden window and the quiet sound carried by the command. `start` prints the same
         block after its own text.
note     `note "TEXT"` writes the line `Now: TEXT` under the count of the progress page and of STATUS.md (note.txt in the live
         folder: its first line, at most 200 characters, shown as text); a longer text is refused. `note --clear` empties it. With no
         note nothing is printed.
card     `card ID`: any step's card, its package's rows and the checks it needs (a repair's card); changes nothing, no --agent.
owed     `owed ID "TEXT"`: what only a person or other hardware can check, appended to owed.tsv (live folder) and listed by
         status, STATUS.md and the page; it blocks nothing.
rows     What a card's "Rows." line means (closes whole / advances), for one step or all of them.
repair   `repair add --source nightly|stress|measure|github --kind fail|stress|worse --check CHECK [--renderer R] [--step ID
         --commit SHA] --evidence FOLDER [--row ROW] --note "TEXT"` writes a line of repairs.tsv (the live folder) and prints
         its id; the same check and renderer already `open`, `started` or `set-aside` is not written twice: its `seen` count
         goes up and the date is added to its note. `repair list` prints them, open ones first. `next` gives the oldest
         open repair of kind `fail` before any step (stress and worse repairs never come first and never block);
         `start R-003` makes the branch `native/repair-003` in the agent's folder; `land R-003` hands it to
         `gate.py land --also <its check>` and on a pass sets the repair `landed` with the commit; `blocked R-003 "WHY"`
         sets it `set-aside` and `next` goes on with the steps. Agent 1's work only. A repair whose cause was a test tool is ended by its
         mend on every route, by one function: `mend close M-00N COMMIT` (the repair is `started`), `mend open R-00N "TITLE" --landed COMMIT`
         (a mend landed by hand because plan.py itself was at fault) and `repair end R-00N --mend M-00N` (the mend is recorded closed and the
         repair is still open). The repair becomes `landed`, closed by the mend's commit, with the note `ended by mend M-00N (TITLE)`; `repair
         end` refuses a mend that is not closed, a mend of another step, a repair that is landed already and a repair that waits for another,
         open mend.

Exit codes: 0 done; 1 refused or failed; 2 the command was wrong; 3 the same failure three times; 4 a tool or
machine fault only, or a branch that does not apply (the gate's "not judged": it does not count as a failure of the change).

State. `landed` and the commit are in steps.tsv (the repository) and travel with the landing's own commit; after a pass
they are written into the plan tree too (and `origin/main` has them from the landing, which is what the other agent reads). Everything that more than one agent, or the progress page, must see is in the
live folder (LIVE below; `--live` for tests): started and blocked steps, claims, tool faults, the landings' times, the
page. It is never kept in a file inside one work folder. STATUS.md is the plan tree's own, untracked; BLOCKED.md (the blocked
steps' reasons and the tool notes) is in the live folder too (an older one in docs/plan/native is read as well).
"""
import argparse
import contextlib
import datetime
import io
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
import lockholder  # noqa: E402
import page  # noqa: E402
import noprompt  # noqa: E402
import scope  # noqa: E402
import strictness  # noqa: E402
import units  # noqa: E402

GIT = "C:/Program Files/Git/cmd/git.exe"
NATIVE = "docs/plan/native"
WORK_ROOT = "D:/NebulaWork/MarioGalaxyRecomp"
FAILURES = "D:/NebulaWork/plan/landing-failures.tsv"
TABLET = "R52WA0FXE5J"
TABLET_WAIT = 30
# One work folder for each agent, kept between steps (made by `start` the first time).
AGENT_FOLDERS = {"1": "Nebula-native", "2": "Nebula-native-2"}
LIVE = "D:/NebulaWork/plan/native"       # the live state: claims, started and blocked steps, tool faults, the page
LIVE_REAL = LIVE                          # the live folder as the machine has it (LIVE is replaced by a test: only the real one shows the gate's record)
GATE_RUNS = "D:/NebulaWork/build/gate-runs"   # the gate's record of its runs (history.tsv): `status` shows the newest long run from it
AGENT2_LIST = ("G6", "G7", "G8", "G9", "G1", "R10", "R9", "G4")   # the helper's packages, in this order, and no others
# Two groups of steps that must not be in work at the same time (both change the renderers' frame and present code).
PAIRS = ((("G2a",), ("R7",)),)

QUEUE_FILE = str(landing.QUEUE)          # the gate's landing queue (a landing record is read against it); tests give their own
alive = landing.process_alive            # whether a process of this id runs; tests put their own in here
JOIN_POLL = 5.0                          # seconds between two looks while a landing that is no longer in the queue is finishing
SIGN_IN = "GitHub sign-in needed"
SIGN_IN_TEXTS = ("authentication failed", "could not read username", "could not read password", "terminal prompts disabled", "invalid username",
                 "bad credentials", "permission denied (publickey)", "returned error: 401", "returned error: 403")
STATE_COMMIT = "Plan: step %s landed"    # the subject of the tool's own commit: the step's row is `landed`, with the code's commit

# The extra checks a step's proof asks for (docs/plan/native/step-checks.tsv): the nightly's parts, which `gate.py land --also` takes.
STEP_CHECKS_FILE = "step-checks.tsv"
ALSO_CHECKS = ("stress", "sanitizer/tour", "soak", "soak/long", "replays/software", "speed/anchor", "replays/rates")

# A replay reference (tests/replays/*.tsv) changes only where the card says so (T14 item 5).
REPLAY_DIR = "tests/replays/"
REPLAY_ALLOWED = re.compile(r"re-?record|recorded again|replay changes|new reference", re.I)
REPLAY_REFUSAL = "the replay differs and this step's card does not say it may: the change altered the game; find why"
REPLAY_WORDS = "replay reference"
# What `start` tells the agent about the words is what `replay_problems` refuses, and nothing more: it refuses a branch that changes a file
# under tests/replays/ when no commit message of the landing has the words. A step whose card allows the change and that records nothing is
# not asked for them.
REPLAY_WORDS_RULE = ("A branch that changes a file under %s needs a commit message with the words \"%s\"; `check` and `land` refuse it "
                     "without them. A step that records no replay reference needs no such words." % (REPLAY_DIR, REPLAY_WORDS))
# How a replay reference is recorded: step_hash.py's `record` takes the work folder of a luma_test.py run in which the tour played
# every recorded stop with the hash on, and writes one tests/replays/<stop>.tsv for each. T10 proves this command by a real run.
REPLAY_RECORD = "python -X utf8 -I tools/verify/step_hash.py record <work folder of the tour run> --out tests/replays"

# The commands the coding agent runs in its work folder (brief T10 part B), each the form the gate itself uses: it never composes one.
# `start` prints them filled in after its own text, `commands` prints them again at any time. The places: {folder} the agent's work
# folder, {build} its build folder (BUILD_ROOT/<the folder's name>), {pak} the US game.pak (the game data by key), {name} a test's name,
# which the agent writes in place of <name>. Every path a command names under {folder} is a file of the tree (a self-test reads them).
BUILD_ROOT = "D:/NebulaWork/build"
US_DISC = "RMGE01"
AGENT_COMMANDS = (
    ("build", "Build the game for Windows: the three programs and both game modules. Again after a change: only what changed is built.",
     "powershell.exe -NoProfile -ExecutionPolicy Bypass -File {folder}/tools/gate/build_windows.ps1 -Tree {folder} -Root {build} "
     "-Bin {build}/bins", ""),
    ("unit", "Run one unit-test program by name (the programs are the galaxy_*_tests files in {build}/msvc).",
     "powershell.exe -NoProfile -ExecutionPolicy Bypass -File {folder}/tools/gate/ctest.ps1 -Dir {build}/msvc -Name <name>", ""),
    ("engine", "Run one engine test by name on Direct3D 12 (window hidden, no sound; the names are in tests/engine/suite.json).",
     "python -X utf8 -I {folder}/tools/lumacore/luma_test.py run {folder}/tests/engine/suite.json --bin {build}/bins --pak {pak} "
     "--work {build}/runs/engine --who agent --lock {build}/engine.lock --renderer d3d12 --test <name>", ""),
    ("tour", "Play the tour's 12 stops on Direct3D 12 with the step hash on (window hidden, no sound).",
     "python -X utf8 -I {folder}/tools/gate/agent_tour.py --bin {build}/bins --pak {pak} --work {build}/runs/tour --renderer d3d12",
     "For the next command play the 20 stops whose hashes are recorded: the same line with --stops recorded added."),
    ("record", "Record the replay references from the tour run, only where the step's card says they may change.",
     "python -X utf8 -I {folder}/tools/verify/step_hash.py record {build}/runs/tour --out {folder}/tests/replays", ""),
)

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


def set_state(tree, step_id, state, commit=None, agent=1, reason="", live=None, track=True, write_file=True):
    """Sets a step's state. `landed` (with the commit) is written into steps.tsv: that is what a landing carries.
    `started` and `blocked` are the live folder's (state.tsv there, with the agent and the reason), so that both agents
    and the page see them and steps.tsv stays what main has; `open` takes a step out of it. `track` False leaves the live
    folder alone (the landing's own commit, in the work folder, before the gate has answered). `write_file` False leaves the
    tree's steps.tsv alone (a landing that ends while the work folder is on a step written ahead: its working tree stays as it is)."""
    base = read_base(tree)
    row = find(base, step_id)
    if row is None:
        raise KeyError(step_id)
    if state == "landed" and write_file:
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

# The label a check's row carries in the gate's output (gate.py LABELS: the kinds that need a person's eye in capitals, the others as
# they are) and what the step tool counts it as. Every state of tools/gate/kinds.py is here (a self-test reads them from there). A row
# `again` is a check waiting for its second run: its final row comes later and is the one counted.
KIND_OF = {"pass": "PASS", "known": "PASS", "planned": "PASS", "owed": "ENV", "FAIL": "FAIL", "WORSE": "WORSE", "TOOL": "TOOL",
           "ENV": "ENV", "FLAKE": "FLAKE", "main": "MAIN", "STRESS": "STRESS", "NEW": "NEW"}
GATE_ROW = re.compile(r"^(\S+)\s+(%s)\s+(\d+(?:\.\d+)?) s\s*(.*)$" % "|".join(re.escape(label) for label in KIND_OF))


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
        said = (pushed.stderr.strip().splitlines() or ["push refused"])[-1]
        if any(k in pushed.stderr.lower() for k in SIGN_IN_TEXTS):
            # GitHub wants a sign-in nobody can give: the environment, not the step. Nothing is landed; the page says so.
            return Verdict(False, [], [("github/sign-in", "ENV", "%s: %s" % (SIGN_IN, said))])
        return Verdict(False, [], [("git/push", "FAIL", said)])
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


def blocked_path(live=None):
    """BLOCKED.md: in the live folder, with the rest of the state every command reads and writes (T11-B: it was a file of the work
    folder, untracked, which `git add -A` could take into a step)."""
    return live_dir(live) / "BLOCKED.md"


def blocked_files(tree, live=None):
    """The files that hold the blocked steps' reasons and the tool notes: the live folder's, and the older one of the plan tree
    (docs/plan/native/BLOCKED.md, where an earlier version wrote it) when there is one; an entry of the live file wins."""
    return [f for f in (native(tree) / "BLOCKED.md", blocked_path(live)) if f.exists()]


def blocked_reasons(tree, live=None):
    """{step id: reason line} from BLOCKED.md's steps section (the last entry of a step wins)."""
    out = {}
    for path in blocked_files(tree, live):
        for line in path.read_text(encoding="utf-8").splitlines():
            m = re.match(r"^- (\d{4}-\d\d-\d\d) (\S+): (.*)$", line)
            if m:
                out[m.group(2)] = m.group(3)
    return out


def tool_notes(tree, live=None):
    out = []
    for path in blocked_files(tree, live):
        on = False
        for l in path.read_text(encoding="utf-8").splitlines():
            if l.startswith("## "):
                on = l == "## tools"
            elif on and l.startswith("- "):
                out.append(l)
    return out


def work_folder(work_root, agent=1):
    """The agent's own work folder: the same one for every step and repair."""
    return pathlib.Path(work_root) / AGENT_FOLDERS[str(agent)]


def game_pak(tree):
    """The US game.pak `tree`'s build runs on, by key (tools/gate/gamedata.py: disc, store version, importer's tree hash), or None
    when that data has not been made."""
    import gamedata
    try:
        key = gamedata.key_of(US_DISC, gamedata.tree_store_version(tree), gamedata.importer_tree_hash(GIT, tree))
        pak = gamedata.GameData().lookup(key)
    except (OSError, ValueError, RuntimeError):
        return None
    return pak.as_posix() if pak else None


PAK_OF = game_pak                        # tests put their own in here


def build_folder(a, agent):
    """The agent's build folder: <the build root>/<the work folder's name> (`--build-root`, default BUILD_ROOT)."""
    return "%s/%s" % (getattr(a, "build_root", None) or BUILD_ROOT, AGENT_FOLDERS[str(agent)])


def commands_text(a, agent):
    """The block of the agent's commands filled in for its folder (one text, the same for `start` and `commands`)."""
    folder = work_folder(a.work_root, agent).as_posix()
    build = build_folder(a, agent)
    pak = PAK_OF(a.tree)
    if pak is None:
        pak = ("<the US game.pak is not made: python -X utf8 -I %s/tools/gate/gamedata.py make %s --importer %s/bins/nebula-import.exe>"
               % (folder, US_DISC, build))
    out = ["=== YOUR COMMANDS (agent %s; folder %s; build folder %s) ===" % (agent, folder, build),
           "Run exactly these; write the test's name in place of <name>. `plan.py commands` prints this block again."]
    for number, (_, what, command, note) in enumerate(AGENT_COMMANDS, 1):
        fill = lambda text: text.format(folder=folder, build=build, pak=pak)  # noqa: E731
        out += ["%d. %s" % (number, fill(what)), "   " + fill(command)]
        if note:
            out.append("   " + fill(note))
    return "\n".join(out)


def cmd_commands(a):
    say(commands_text(a, agent_of(a)))
    return 0


# The note under the count of the progress page and of STATUS.md (`Now: <the line>`): the first line of note.txt in the live folder.
NOTE_FILE = "note.txt"
NOTE_MAX = 200


def read_note(live=None):
    """The note: the first line with words in it of note.txt in the live folder, in one line of printable characters, at most NOTE_MAX
    of them; "" when there is no file or it holds nothing. It is text, never markup: the page sets it as text."""
    try:
        text = (live_dir(live) / NOTE_FILE).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""
    for line in text.splitlines():
        line = " ".join("".join(c if c.isprintable() else " " for c in line).split())
        if line:
            return line[:NOTE_MAX]
    return ""


def cmd_note(a):
    """`note "TEXT"` writes the note (one line, at most NOTE_MAX characters; a longer one is refused); `note --clear` empties the file."""
    path = live_dir(a.live) / NOTE_FILE
    if a.clear == (a.text is not None):
        say("plan.py: give the text of the note, or --clear (not both)")
        return 2
    text = "" if a.clear else " ".join(a.text.split())
    if len(text) > NOTE_MAX:
        say("plan.py: the note is %d characters; %d at most. Nothing was written." % (len(text), NOTE_MAX))
        return 1
    path.parent.mkdir(parents=True, exist_ok=True)
    atomic_write(path, text + "\n" if text else "")
    refresh_page(a)
    say("note cleared" if a.clear else "note: " + text)
    return 0


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


def status_text(tree, work_root=WORK_ROOT, at=None, live=None, agent="1", queue_file=None, gate_runs=None):
    steps = read_steps(tree, live)
    by = {s: [x for x in steps if x["state"] == s] for s in STATES}
    reasons = blocked_reasons(tree, live)
    at = at or now()
    claims = lv.read_claims(live_dir(live))
    stale = stale_of(tree, claims, at)
    out = ["# Native plan: status", ""]
    running = active_landing(live, agent, queue_file)
    if running:
        out += [landing_running_line(running, agent), ""]
    out += ["Written %s by plan.py status. Run it at every start and after every context compaction." % at.isoformat(timespec="minutes"),
            "", "Steps landed: %d of %d." % (len(by["landed"]), len(steps)), ""]
    note = read_note(live)
    if note:
        out += ["Now: " + note, ""]
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
    written = lv.read_ahead(live_dir(live), agent)
    held = find(steps, written["step"]) if written else None
    if held is not None:
        count = None
        folder = work_folder(work_root, agent)
        r = git(folder, "rev-list", "--count", "%s..refs/heads/native/%s" % (written["base"], written["step"])) if folder.is_dir() else None
        if r is not None and r.returncode == 0 and r.stdout.strip().isdigit():
            count = int(r.stdout.strip())
        out += ["", "## Written ahead (1)", "",
                "- %s %s: branch native/%s, %s, on top of %s (base %s); plan.py start %s takes it up once %s has landed" % (
                    held["step"], held["title"], held["step"], commits_word(count), written["after"], written["base"][:9],
                    held["step"], written["after"])]
    stacked = lv.read_stack(live_dir(live), agent)
    if stacked:
        out += ["", "## Stack (%d)" % len(stacked), ""]
        out += ["- %s: branch native/%s on native/%s (base %s); plan.py start %s takes it up once %s has landed" % (
            r["step"], r["step"], r["after"], r["base"][:9], r["step"], r["after"]) for r in stacked]
    out += ["", "## Blocked (%d)" % len(by["blocked"]), ""]
    out += ["- %s %s: %s" % (s["step"], s["title"], s["reason"] or reasons.get(s["step"], "(no reason written)"))
            for s in by["blocked"]] or ["- none"]
    mends = lv.read_mends(live_dir(live))
    questions = owner_questions(steps, mends)
    out += ["", "## Waiting for the owner (%d)" % len(questions), ""]
    out += ["- %s %s: %s" % (q["step"], q["title"], q["question"]) for q in questions] or ["- none"]
    out += ["", "## Tool mends (%d, %d open)" % (len(mends), sum(1 for m in mends if m["state"] == "open")), ""]
    out += ["- %s %s: step %s, %s%s" % (m["id"], m["state"], m["step"], m["title"], "" if m["commit"] in ("", "-") else ", commit " + m["commit"])
            for m in mends] or ["- none"]
    away = tablet_away(live_dir(live))
    if away:
        out += ["", "## Tablet", "", "- away since %s: %s checks owed" % (away["since"], away["owed"])]
    out += ["", "## Claims (%d)" % len(claims), ""]
    out += ["- %s: agent %s since %s%s%s" % (c["package"], c["agent"], c["time"], ", branch " + c["branch"] if c["branch"] else "",
                                            " STALE (no commit for %d hours): plan.py claim %s --take-stale" % (lv.STALE_HOURS, c["package"])
                                            if c["package"] in stale else "") for c in claims] or ["- none"]
    out += ["", "## Repairs (%d open of kind fail, %d in all)" % (
        sum(1 for r in repairs if r["state"] == "open" and r["kind"] == "fail"), len(repairs)), ""]
    out += ["- %s %s %s: %s, %s, seen %sx" % (r["id"], r["state"], r["kind"], lv.repair_what(r), lv.repair_traced(r), r["seen"])
            for r in lv.repair_order(repairs)] or ["- none"]
    if gate_runs:
        import long_results
        out += ["", "## The newest long run", "", "- " + long_results.newest_long(gate_runs, alive=alive)]
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
    # The helper's section only where there is a helper: an agent 2 that has never claimed, started or landed anything is not shown
    # (a lone agent 1 would read "nothing on agent 2's list can start now" as a second agent waiting).
    helper_seen = (agent == "2" or any(c["agent"] == "2" for c in claims) or any(e["agent"] == "2" for e in state.values())
                   or any(l["agent"] == "2" for l in lv.read_landings(live_dir(live))))
    if helper_seen:
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
    reasons = blocked_reasons(tree, live)
    for s in steps:
        if s["state"] == "blocked" and not s["reason"]:
            s["reason"] = reasons.get(s["step"], "")
    d = live_dir(live)
    claims = lv.read_claims(d)
    at = at or now()
    landings, events = lv.read_landings(d), lv.read_faults(d)
    signin = ""
    for e in reversed(events):
        if e["check"] == "github/sign-in" or SIGN_IN in e["first_error"]:
            if not any(l["finished"] > e["time"] for l in landings):
                signin = e["time"]
            break
    mends = []
    for m in lv.read_mends(d):
        opened, closed = lv.parse_stamp(m["opened"]), lv.parse_stamp(m["closed"])
        mends.append(dict(m, minutes=int(((closed or at) - opened).total_seconds() // 60) if opened else 0))
    return (page.make_static(steps),
            page.make_data(steps, landings, lv.read_state(d), claims, stale_of(tree, claims, at), lv.fault_groups(events),
                           count_flakes(live), lv.stamp(at), read_set_aside(live), lv.repair_order(lv.read_repairs(d)),
                           lv.read_owed(d), read_note(live), mends, read_decisions(tree), read_strictness_log(tree),
                           tablet_away(d), lock_notes(d.parent, at), signin=signin),
            landings, events)


HAND_LOCK_MINUTES = 30
LOCK_NAMES = ("gate.lock", "tablet.lock", "measure.lock", "long.lock")


def tablet_away(live):
    """{since, owed} when the gate wrote tablet-owed.tsv (a landing owed the tablet's checks and none has played them since), else None."""
    path = live_dir(live) / "tablet-owed.tsv"
    rows = [l.split("\t") for l in path.read_text(encoding="utf-8").splitlines() if l.strip()] if path.is_file() else []
    if not rows:
        return None
    return {"since": rows[0][0], "owed": sum(len([c for c in (r[2] if len(r) > 2 else "").split(",") if c]) for r in rows)}


def lock_notes(plan_dir, at=None, alive_fn=None):
    """[text] for each lock of the machine (the gate's, the tablet's, the measure's, the long run's) that has been held for more than
    30 minutes by a holder line that names no process (taken by hand: nothing takes it; the gate waits for it), or whose process has ended
    (the next run takes it over). The page names them; nothing here frees a lock."""
    at = at or now()
    alive_fn = alive_fn or alive
    out = []
    for name in LOCK_NAMES:
        folder = pathlib.Path(plan_dir) / name
        if not folder.is_dir():
            continue
        try:
            text = (folder / "holder.txt").read_text(encoding="utf-8", errors="replace").strip()
            since = datetime.datetime.fromtimestamp(folder.stat().st_mtime)
        except OSError:
            text, since = "", at
        if (at - since).total_seconds() < 60 * HAND_LOCK_MINUTES:
            continue
        pid, _ = lockholder.parse(text)
        if pid is None:
            out.append("%s is held by hand since %s (%s): nothing takes it, and every run that needs it waits" % (name, since.strftime("%H:%M"), text or "no holder line"))
        elif lockholder.gone(text, alive_fn) is not None:
            out.append("%s is held since %s by a process that has ended (%s): the next run takes it over" % (name, since.strftime("%H:%M"), text))
    return out


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
        write_if_changed(paths.index, page.render(static, live_url=live_data_url(tree)))
        write_if_changed(paths.data, page.data_json(data))
        write_if_changed(paths.html, page.render(static, data))
        write_if_changed(paths.status, status)
        for old in paths.old:
            old.unlink(missing_ok=True)
    return Written(paths, data, status, landed, total)


# ---- the progress branch ---------------------------------------------------------------------------------------

PROGRESS_BRANCH = "refs/heads/progress"
# The live data the published page fetches first (mend M-007): progress-data.json on the progress branch, read through GitHub's raw
# file host, which a claude.ai page may fetch (tried by the owner on 11-23). Its answers may be up to five minutes old (its cache).
LIVE_DATA = "https://raw.githubusercontent.com/%s/%s/progress/progress-data.json"
GITHUB_ORIGIN = re.compile(r"^(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\.git)?/?$")
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


def live_data_url(tree):
    """The address of the live progress data (LIVE_DATA) for the plan tree's origin when that is on GitHub; "" when it is not."""
    r = git(tree, "remote", "get-url", "origin")
    m = GITHUB_ORIGIN.match(r.stdout.strip()) if r.returncode == 0 else None
    return LIVE_DATA % (m.group(1), m.group(2)) if m else ""


def publish_progress(repo, text, landed, total, remote="origin", data=None):
    """Puts STATUS.md's `text` (and `data`, progress-data.json's text, when given) on the branch `progress` of `remote`, as one
    commit holding those files, made with git's plumbing commands in `repo` (no work tree, no index, no checkout is touched). The
    branch shares no history with main: its first commit has no parent, a later one has the branch's previous tip. Files the tip
    already has as they are: nothing is made or pushed. Returns what was done, in words; raises RuntimeError when git fails (a
    push that is refused too)."""
    listed = progress_git(repo, "ls-remote", "--heads", remote, PROGRESS_BRANCH)
    tip = listed.split()[0] if listed else ""
    blob = progress_git(repo, "hash-object", "-w", "--no-filters", "--stdin", stdin=text)
    data_blob = progress_git(repo, "hash-object", "-w", "--no-filters", "--stdin", stdin=data) if data is not None else ""
    if tip:
        have = subprocess.run([GIT if pathlib.Path(GIT).exists() else "git", "-C", str(repo), "cat-file", "-e", tip + "^{commit}"],
                              capture_output=True, text=True, timeout=GIT_WAIT).returncode == 0
        if not have:
            progress_git(repo, "fetch", "--quiet", remote, PROGRESS_BRANCH)       # FETCH_HEAD only: no ref, no work tree
        try:
            if progress_git(repo, "rev-parse", "%s:STATUS.md" % tip) == blob and (
                    not data_blob or progress_git(repo, "rev-parse", "%s:progress-data.json" % tip) == data_blob):
                return "%s is what %s has: nothing pushed" % ("STATUS.md and progress-data.json" if data_blob else "STATUS.md",
                                                             PROGRESS_BRANCH)
        except RuntimeError:
            pass                                                                # the tip lacks a file: it is new
    tree = progress_git(repo, "mktree", stdin="100644 blob %s\tSTATUS.md\n" % blob + (
        "100644 blob %s\tprogress-data.json\n" % data_blob if data_blob else ""))
    commit = progress_git(repo, "commit-tree", tree, *(["-p", tip] if tip else []), "-m",
                          "Progress: %d of %d steps landed" % (landed, total))
    progress_git(repo, "push", "--quiet", remote, "%s:%s" % (commit, PROGRESS_BRANCH))
    return "%s %s on %s (%s)" % ("pushed" if tip else "pushed the first commit", commit[:9], PROGRESS_BRANCH, "after " + tip[:9] if tip else "no parent")


def push_progress(repo, written):
    """publish_progress with what write_page made: STATUS.md and the live data, the page's data with a person's home folder
    written ~/ as in STATUS.md (the branch is public)."""
    return publish_progress(repo, written.text, written.landed, written.total, data=page.home_short(page.data_json(written.data)))


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
            result = push_progress(repo, written)
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


# A step kept on its branch while a repair of a red job of GitHub is landed: `blocked` in the live state with this reason (`next` offers
# it again once nothing else comes first, `start` puts its branch on top of the new main).
KEPT_PREFIX = "kept on its branch for repair"


def kept_reason(repair_id):
    return "%s %s" % (KEPT_PREFIX, repair_id)


def is_kept(s):
    return s["state"] == "blocked" and (s["reason"] or "").startswith(KEPT_PREFIX)


def settle_github(a):
    """The GitHub jobs of the landings that moved main before they ended, read now (tools/gate/github_rule.py settle_pending): one
    question for each, no waiting; a red job opens a repair. It never fails the command. Tests put their own in SETTLE."""
    try:
        events = SETTLE(live_dir(a.live).as_posix())
    except Exception as e:  # noqa: BLE001 - a report after a move: the command goes on
        say("plan.py: GitHub's pending results could not be read (%s: %s)" % (type(e).__name__, e))
        return []
    for commit, what, text in events:
        if what == "kept":
            say("GitHub: %s %s" % (commit[:9], text))
    if any(what == "red" for _, what, _ in events):
        refresh_page(a)
    return events


def _gate_settle(live):
    import gate  # noqa: E402 - tools/gate/gate.py (on the path): its readers of GitHub
    return gate.settle_github_pending(live, say=say)


SETTLE = _gate_settle                    # tests put their own in here


def github_repair_open(a):
    """The oldest open repair of kind fail that GitHub's red job opened (source github), or None."""
    return next((r for r in lv.read_repairs(live_dir(a.live))
                 if r["state"] == "open" and r["kind"] == "fail" and r["source"] == "github"), None)


REPAIR_ID = re.compile(r"^R-\d{3,}$")
REPAIR_RULES = ("Read the evidence first. Find the cause and fix the game, not the test. If a test tool is at fault (AGENT.md section 9: "
                "evidence of one of its kinds), do not debug it: plan.py mend open %s \"<title>\", fill the sheet, and the helper mends it.")
FLAKE_RULES = ("This test is set aside: it failed once and passed alone three times in seven days, so its failure no longer counts. Find "
               "out whether the test or the game is at fault. The test (a tool): a mend, as above; the game: the fix is this repair's work, "
               "with a ledger row (docs/plan/ledger.tsv). When this repair has landed the test counts again.")


def is_repair(item_id):
    return bool(REPAIR_ID.match(item_id))


def item_branch(item_id):
    """The branch of a step (`native/<id>`) or of a repair (`native/repair-003` for R-003)."""
    return "native/repair-" + item_id[2:] if is_repair(item_id) else "native/" + item_id


def branch_item(branch):
    """The step or repair a branch is for (the other way round from item_branch): ("step", "<ID>") for `native/<ID>`, ("repair", "R-00N")
    for `native/repair-00N`, None for any other branch (a tools landing)."""
    if not branch.startswith("native/"):
        return None
    name = branch[len("native/"):]
    if name.startswith("repair-"):
        item = "R-" + name[len("repair-"):]
        return ("repair", item) if is_repair(item) else None
    return ("step", name) if name else None


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


# ---- a step written ahead --------------------------------------------------------------------------------------------
# While a landing of N (a step or a repair) plays, `plan.py ahead` makes the branch of the step M that follows N on top of the tip
# the landing was given, in the agent's work folder, and the live folder keeps one line about it (live.py, ahead-<agent>.tsv: M, N,
# the base M's own commits stand on, the tip of N the last landing was given). `start M` takes it up once N is on main. Nothing here
# deletes a branch, a commit or an uncommitted change, and a move of M's commits that does not apply is aborted.

AHEAD_REFUSAL = "ahead is for the time a landing plays: nothing of yours is being landed"


def head_branch(folder):
    return git(folder, "rev-parse", "--abbrev-ref", "HEAD").stdout.strip()


def commit_of(folder, ref):
    """The commit a name or an id stands for in the folder's repository, or ""."""
    r = git(folder, "rev-parse", "--verify", "--quiet", ref + "^{commit}")
    return r.stdout.strip() if r.returncode == 0 else ""


def is_ancestor(folder, old, new):
    return git(folder, "merge-base", "--is-ancestor", old, new).returncode == 0


def tracked_changes(folder):
    """The uncommitted changes of tracked files (what stops a rebase); a file git does not track does not."""
    return git(folder, "status", "--porcelain", "--untracked-files=no").stdout.splitlines()


def dirty_line(folder, again):
    """The line `start` gives for a work folder with uncommitted changes; `again` is the command to run once they are committed."""
    return ("the work folder %s has uncommitted changes. Commit them (or throw away what you do not want), then run %s again."
            % (folder.as_posix(), again))


def move_commits(folder, onto, base, branch):
    """`git rebase --onto <onto> <base> <branch>` in the folder, which is on `branch` and has no uncommitted change to a tracked
    file. "" when it worked; else the reason, with the rebase aborted when it had begun (the branch is as it was)."""
    before = commit_of(folder, "refs/heads/" + branch)
    r = git(folder, "rebase", "--onto", onto, base, branch)
    if r.returncode == 0:
        return ""
    said = (r.stdout + r.stderr).strip().splitlines()
    for name in ("rebase-merge", "rebase-apply"):
        place = pathlib.Path(git(folder, "rev-parse", "--git-path", name).stdout.strip())
        if not place.is_absolute():
            place = pathlib.Path(folder) / place
        if place.exists():
            git(folder, "rebase", "--abort")
            break
    if commit_of(folder, "refs/heads/" + branch) != before:
        raise RuntimeError("%s was changed by a rebase that failed, and could not be put back (it was %s)" % (branch, before))
    return said[-1] if said else "conflict"


def ahead_entry(a):
    """The agent's ahead entry (live.read_ahead), or None."""
    return lv.read_ahead(live_dir(a.live), agent_of(a))


def ahead_here(a, entry=None):
    """The agent's ahead entry when its work folder is on that step's branch now, else None."""
    entry = entry or ahead_entry(a)
    folder = work_folder(a.work_root, agent_of(a))
    if entry and folder.is_dir() and head_branch(folder) == "native/" + entry["step"]:
        return entry
    return None


def ahead_count(a, entry):
    """How many commits the step written ahead has of its own (the ones after its base); None when that cannot be told."""
    folder = work_folder(a.work_root, agent_of(a))
    r = git(folder, "rev-list", "--count", "%s..refs/heads/native/%s" % (entry["base"], entry["step"]))
    return int(r.stdout.strip()) if r.returncode == 0 and r.stdout.strip().isdigit() else None


def commits_word(count):
    return "%s commits" % ("?" if count is None else count)


def ahead_item(a, steps, rec):
    """(kind, row, id) of the one item the agent has in work when `rec` is the landing in the gate's hands for it; else None."""
    work = in_work(a, steps)
    if rec and len(work) == 1:
        kind, row = work[0]
        ident = row["step"] if kind == "step" else row["id"]
        if ident == rec["step"]:
            return kind, row, ident
    return None


def ahead_why_none(steps, done):
    """Why no step can follow `done` (a step row or None): the words after `nothing can be prepared ahead:`."""
    copy = [dict(x) for x in steps]
    for x in copy:
        if done is not None and x["step"] == done["step"]:
            x["state"] = "landed"
    if all(x["state"] == "landed" for x in copy):
        return "the list is at its end: every step is landed or is the one being landed"
    for x in copy:
        if x["state"] == "open":
            waiting = unmet(x, copy)
            if waiting:
                return "the step after it, %s, needs %s, not landed yet" % (x["step"], ", ".join("%s (%s)" % w for w in waiting[:3]))
    return "no open step can start after it (the steps left are blocked, held by another agent or wait for a pair)"


def say_what_next_says(a):
    """The second line of a refusal: the first line `plan.py next` prints now (what to do)."""
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            cmd_next(a)
    except Exception:  # noqa: BLE001 - a hint only
        pass
    first = next((l for l in out.getvalue().splitlines() if l.strip()), "")
    say("plan.py next says: " + first if first else "plan.py next says what to do.")


def say_ahead_in_next(a, steps, rec):
    """`next` while a landing runs: the line about the step written ahead, or the step that could be."""
    entry = ahead_entry(a)
    if entry:
        row = find(steps, entry["step"])
        if row is not None:
            say("AHEAD: %s  %s (branch native/%s, %s)" % (row["step"], row["title"], row["step"], commits_word(ahead_count(a, entry))))
        return
    item = ahead_item(a, steps, rec)
    if item is None:
        return
    nxt = following(a, steps, item[1] if item[0] == "step" else None)
    if nxt is not None and not (item[0] == "repair" and any(is_kept(s) for s in steps)):
        say("AHEAD POSSIBLE: %s %s; plan.py ahead%s" % (nxt["step"], nxt["title"], agent_flag(agent_of(a))))


MEND_FILES_HEADING = "## The files I believe are at fault"
FILE_WORD = re.compile(r"[\w.@+-]+(?:/[\w.@+-]+)+/?|[\w+-]+\.[A-Za-z][\w]*")


def mend_record(a, steps, mend, folder):
    """The landing record `ahead` works from while a mend is open: the item the mend waits for (a step or a repair in work) and the tip of its
    branch. None when the mend waits for no item or the branch is not there."""
    ident = mend["step"]
    if ident in ("", "-"):
        return None
    tip = commit_of(folder, "refs/heads/" + item_branch(ident)) if folder.is_dir() else ""
    return {"step": ident, "tip": tip, "branch": item_branch(ident), "time": mend["opened"]} if tip else None


def mend_files(a, mend, folder):
    """The files a mend touches, as far as is known: the paths its fault sheet lists under `The files I believe are at fault`, and the files
    its branch mend/M-00N changes against origin/main once the helper has committed on it."""
    found = []
    sheet = lv.sheet_path(live_dir(a.live), mend["id"])
    if sheet.is_file():
        text = sheet.read_text(encoding="utf-8", errors="replace")
        if MEND_FILES_HEADING in text:
            part = text.split(MEND_FILES_HEADING, 1)[1].split("\n## ", 1)[0]
            part = "\n".join(l for l in part.splitlines() if not l.strip().startswith("(path and function"))
            found += [m.group(0).rstrip("/") for m in FILE_WORD.finditer(part)]
    branch = "refs/heads/mend/" + mend["id"]
    for repo in (folder, pathlib.Path(a.tree)):
        if repo.is_dir() and commit_of(repo, branch):
            found += git(repo, "diff", "--name-only", "origin/main...%s" % branch).stdout.split()
            break
    return sorted(set(found))


def touches(mend_path, card_path):
    """Whether a path a mend touches and a path a card names are the same file, one holds the other (a folder), or the mend names the card's file by
    its name alone."""
    a, b = mend_path.strip("/"), card_path.strip("/")
    return a == b or a.startswith(b + "/") or b.startswith(a + "/") or ("/" not in a and b.rsplit("/", 1)[-1] == a)


def mend_ahead_refusal(a, steps, mend, rec):
    """Why `ahead` may not write while mend `mend` is open ("" when it may): the mend waits for no item, or it touches a file that the step to be
    written touches."""
    if rec is None:
        return ("The mend waits for no step in work, so there is nothing to write the next step on top of: nothing is written, started or landed "
                "while it is open. `python -X utf8 -I %s next` says what to do." % plan_path(a))
    folder = work_folder(a.work_root, agent_of(a))
    item = ahead_item(a, steps, rec)
    done = item[1] if item and item[0] == "step" else None
    nxt = following(a, steps, done)
    if nxt is None:
        return ""
    mine = mend_files(a, mend, folder)
    theirs = card_paths(card_text(a.tree, nxt), a.tree)
    clash = sorted({m for m in mine for c in theirs if touches(m, c)})
    if clash:
        return ("The mend touches %s, which step %s touches (its card's Where): nothing is written for it until main holds the mend. "
                "`python -X utf8 -I %s next` says what to do." % (", ".join(clash[:6]), nxt["step"], plan_path(a)))
    return ""


def cmd_ahead(a):
    """`ahead`: while the landing of N plays, the branch native/<M> of the step that follows N, on top of the tip the landing was given
    (made at the first call; at a later one moved onto the new tip of N when N was mended and landed again), and what `next` prints
    for a step. It changes no step's state, makes no claim and pushes nothing."""
    fetch_main_landed(a)
    agent = agent_of(a)
    steps = read_steps(a.tree, a.live)
    folder = work_folder(a.work_root, agent)
    rec = landing_of(a)
    mend = lv.open_mend(live_dir(a.live))
    if mend is not None:
        # A helper mends a tool: the step in work waits and nothing is landed, but the step after it is written meanwhile, on top of the
        # tip of the step in work, unless the mend touches a file that step touches (AGENT.md section 9).
        rec = rec or mend_record(a, steps, mend, folder)
        why = mend_ahead_refusal(a, steps, mend, rec)
        if why:
            say("WAIT: mend %s is open: %s" % (mend["id"], mend["title"]))
            say(why)
            return 1
    item = ahead_item(a, steps, rec)
    if item is None:
        say(AHEAD_REFUSAL)
        say_what_next_says(a)
        return 1
    kind, row, n_id = item
    done = row if kind == "step" else None
    n_branch = item_branch(n_id)
    live = live_dir(a.live)
    entry = ahead_entry(a)
    if entry:
        held = find(steps, entry["step"])
        if held is None or held["state"] == "landed":
            lv.drop_ahead(live, agent, entry["step"])
            say("the ahead entry of %s is dropped (%s); native/%s is left as it is." % (
                entry["step"], "it is landed" if held else "the plan has no such step", entry["step"]))
            entry = None
    if kind == "repair":
        kept = next((s for s in steps if is_kept(s)), None)
        if kept:
            say("nothing can be prepared ahead: step %s is kept on its branch and comes first after the repair" % kept["step"])
            return 1
    nxt = following(a, steps, done)
    if nxt is not None and done is not None and nxt["package"] != done["package"] and is_section_check(a, steps, done):
        # The section's barrier (mend M-006): the next package is not written before its section's full check has passed.
        say("nothing can be prepared ahead: the landing of %s is the full check of package %s, and the next step, %s, is of "
            "package %s: it is written when the check has passed" % (done["step"], done["package"], nxt["step"], nxt["package"]))
        if entry:
            say("native/%s, written ahead, is kept as it is." % entry["step"])
        return 1
    if nxt is None:
        say("nothing can be prepared ahead: " + ahead_why_none(steps, done))
        if entry:
            say("native/%s, written ahead, is kept as it is." % entry["step"])
        return 1
    m_id = nxt["step"]
    branch = "native/" + m_id
    if entry and entry["step"] != m_id:
        say("ahead has native/%s written, and the step that follows now is %s: native/%s is kept as it is (nothing is deleted); "
            "one step ahead at a time. `plan.py next` says which step comes first." % (entry["step"], m_id, entry["step"]))
        return 1
    if not folder.is_dir() or not (folder / ".git").exists():
        say("plan.py: the work folder %s is missing" % folder.as_posix())
        return 1
    exists = branch_exists(folder, "refs/heads/" + branch)
    if entry and not exists:
        lv.drop_ahead(live, agent, m_id)                  # an entry whose branch is gone is no entry
        entry = None
    moved = 0
    if entry is None:
        if exists:
            say("plan.py: the branch %s exists already and ahead has no entry for it. Nothing is changed and nothing is deleted. "
                "Nothing is written ahead this time: wait for the landing." % branch)
            return 1
        if folder_changes(folder):
            say("plan.py: " + dirty_line(folder, "ahead"))
            return 1
        on = head_branch(folder)
        if on != n_branch:
            say("plan.py: the work folder %s is on branch %s, not %s: ahead makes %s on top of the landing of %s from there. "
                "git checkout %s first." % (folder.as_posix(), on, n_branch, branch, n_id, n_branch))
            return 1
        base = commit_of(folder, rec["tip"])
        if not base:
            say("plan.py: the tip %s of the landing of %s is not a commit of this repository" % (rec["tip"][:9], n_id))
            return 1
        made = git(folder, "checkout", "--no-track", "-b", branch, base)
        if made.returncode != 0:
            say("plan.py: git checkout failed: %s" % (made.stderr or made.stdout).strip())
            return 1
    else:
        if head_branch(folder) != branch:
            if folder_changes(folder):
                say("plan.py: " + dirty_line(folder, "ahead"))
                return 1
            made = git(folder, "checkout", branch)
            if made.returncode != 0:
                say("plan.py: git checkout failed: %s" % (made.stderr or made.stdout).strip())
                return 1
        n_tip = commit_of(folder, "refs/heads/" + n_branch) or commit_of(folder, rec["tip"])
        base = entry["base"]
        # N's tip is where M's commits must stand; they stand on `base`. They move when N's tip is not under M, and also when the commit
        # between N's tip and `base` was taken off N (a landing that did not pass takes its state commit off): then N's tip is under `base`.
        dropped = (n_tip != base and commit_of(folder, base) and is_ancestor(folder, n_tip, base)
                   and is_ancestor(folder, base, "refs/heads/" + branch))
        if n_tip != base and (dropped or not is_ancestor(folder, n_tip, "refs/heads/" + branch)):
            if not commit_of(folder, base):
                say("plan.py: the commit %s that %s was written on is no longer in this repository, so its commits cannot be moved. "
                    "%s is kept as it is; nothing is deleted." % (base[:9], branch, branch))
                return 1
            if tracked_changes(folder):
                say("plan.py: the work folder %s has uncommitted changes, and the commits of %s have to be moved onto %s: commit your "
                    "work on %s first, then run ahead again." % (folder.as_posix(), branch, n_branch, branch))
                return 1
            moved = ahead_count(a, entry) or 0
            why = move_commits(folder, n_tip, base, branch)
            if why:
                say("plan.py: the commits of %s do not apply on top of %s (%s). The branch is as it was. Move it by hand: git rebase "
                    "--onto %s %s %s (resolve, git rebase --continue), then run ahead again." % (branch, n_branch, why, n_tip, base, branch))
                return 1
        base = n_tip
    if not entry or (entry["after"], entry["base"], entry["handed"]) != (n_id, base, rec["tip"]):    # a second call that changes nothing writes nothing
        lv.write_ahead(live, agent, m_id, n_id, base, rec["tip"])
    say("AHEAD: %s  %s" % (m_id, nxt["title"]))
    say("on %s, the landing of %s" % (base[:9], n_id))
    print_step_body(a, nxt)
    say()
    say(commands_text(a, agent))
    if mend is not None:
        say("Mend %s is open: nothing is landed and nothing is handed in until main holds it. `python -X utf8 -I %s next` says when." % (mend["id"], plan_path(a)))
    say("Do not land it. When %s has landed: plan.py start %s%s" % (n_id, m_id, agent_flag(agent)))
    if moved:
        say("moved the %d commits of %s onto %s" % (moved, branch, base[:9]))
    return 0


# ---- a section written ahead, the hand-off sheet, a hard failure, waiting for a landing (mend M-007) -----------------------
# The usage design (docs/plan/native/AGENT.md): a writer writes every step of a section before any lands, each on the one before;
# a lander lands them in order and waits with one call per landing; a failure it may not fix goes to a fixer with a report. `stack`
# makes the branch of the step after the last one written on that one's tip; the live folder keeps a row for it (live.py
# stack-<agent>.tsv: the step, the one before it, the commit its own commits stand on). `start <ID>` takes a stacked step up once
# the one it stands on has landed: its own commits are moved onto origin/main (take_up_prepared). Nothing here deletes a branch,
# a commit or a file.

HANDOFF_HEADINGS = ("Changes", "Pictures declared", "Expected test effects", "Hard if")
HANDOFF_REQUIRED = True       # `land` refuses a step without a complete sheet; plan_fixture turns it off for the other rules' tests
HARD_PREFIX = "hard: "
HARD_HEADINGS = ("Check", "Test", "First error line", "Run folder", "What was tried")
WAIT_POLL = 20.0              # seconds between two looks of `wait`
WAIT_APPEAR = 120.0           # seconds a landing begun in the background has to reach the gate (its pre-checks run first)
WAIT_MAX = 9                  # minutes of one `wait` call at most


def stack_rows(a, steps=None):
    """The agent's stack rows in the order written; the rows of steps that are landed or not in the plan are dropped (their
    branches are left as they are)."""
    live = live_dir(a.live)
    agent = agent_of(a)
    steps = steps if steps is not None else read_steps(a.tree, a.live)
    rows = []
    for r in lv.read_stack(live, agent):
        s = find(steps, r["step"])
        if s is None or s["state"] == "landed":
            lv.drop_stack(live, agent, r["step"])
        else:
            rows.append(r)
    return rows


def stack_row(a, step_id, steps=None):
    return next((r for r in stack_rows(a, steps) if r["step"] == step_id), None)


def say_stack(a, steps, rows):
    work = [r["step"] for k, r in in_work(a, steps) if k == "step"]
    if not rows:
        say("the stack is empty%s" % ("; in work: " + ", ".join(work) if work else ""))
        return
    folder = work_folder(a.work_root, agent_of(a))
    say("the stack (start takes each up in this order once the one it stands on has landed):")
    for r in rows:
        n = git(folder, "rev-list", "--count", "%s..refs/heads/native/%s" % (r["base"], r["step"]))
        count = int(n.stdout.strip()) if n.returncode == 0 and n.stdout.strip().isdigit() else None
        say("  %s on native/%s (%s), %s of its own" % (r["step"], r["after"], r["base"][:9], commits_word(count)))


def cmd_stack(a):
    """`stack`: the writer of a section writes every step of it before any lands. The step that follows the last one written (the
    step in work, then each stacked one) gets its branch native/<ID> on the tip of that one, in the agent's work folder, and a row in
    the live folder. Refused while a landing of the agent plays, while a step written ahead waits, when nothing is in work, when the
    folder is not on the last step's branch or has uncommitted changes, and for a step of another package (the section's barrier:
    the next package is written after this one's landings, the last of them its full check). `stack --list` changes nothing."""
    if mend_blocks(a):
        return 1
    fetch_main_landed(a)
    agent = agent_of(a)
    live = live_dir(a.live)
    steps = read_steps(a.tree, a.live)
    rows = stack_rows(a, steps)
    if getattr(a, "list", False):
        say_stack(a, steps, rows)
        return 0
    rec = landing_of(a)
    if rec:
        say("plan.py: %s is in the gate's hands. stack writes a section before its landings; while a landing plays, `plan.py ahead` "
            "writes the step after it." % rec["step"])
        return 1
    entry = ahead_entry(a)
    if entry:
        say("plan.py: native/%s was written ahead of the landing of %s and waits for `plan.py start %s%s`: stack begins after it." % (
            entry["step"], entry["after"], entry["step"], agent_flag(agent)))
        return 1
    work = in_work(a, steps)
    if len(work) != 1 or work[0][0] != "step":
        say("plan.py: stack writes the steps after the one in work, and %s. Start the section's first step (plan.py start <ID>%s), "
            "commit its work, then run stack." % ("nothing is in work" if not work else "a repair is in work", agent_flag(agent)))
        return 1
    first = work[0][1]
    chain = [first["step"]] + [r["step"] for r in rows]
    last = chain[-1]
    folder = work_folder(a.work_root, agent)
    if not (folder / ".git").exists():
        say("plan.py: the work folder %s is missing" % folder.as_posix())
        return 1
    last_branch = "native/" + last
    tip = commit_of(folder, "refs/heads/" + last_branch)
    if not tip:
        say("plan.py: the branch %s of the last step written is missing; nothing is changed." % last_branch)
        return 1
    on = head_branch(folder)
    if on != last_branch:
        say("plan.py: the work folder %s is on %s, not %s: stack makes the next step on top of the last one written. git checkout "
            "%s first." % (folder.as_posix(), on or "no branch", last_branch, last_branch))
        return 1
    if folder_changes(folder):
        say("plan.py: " + dirty_line(folder, "stack"))
        return 1
    marked = [dict(x, state="landed") if x["step"] in chain else dict(x) for x in steps]
    nxt = following(a, marked, None)
    if nxt is None:
        say("STACK END: %s is the last step written: %s." % (last, ahead_why_none(marked, None)))
        return 1
    if nxt["package"] != first["package"]:
        say("STACK END: package %s is written up to %s. The next step, %s, is of package %s: it is written after the landings of "
            "package %s, the last of them its full check, have passed." % (first["package"], last, nxt["step"], nxt["package"],
                                                                           first["package"]))
        return 1
    branch = "native/" + nxt["step"]
    if branch_exists(folder, "refs/heads/" + branch):
        say("plan.py: the branch %s exists already and the stack has no row for it. Nothing is changed and nothing is deleted." % branch)
        return 1
    made = git(folder, "checkout", "--no-track", "-b", branch, tip)
    if made.returncode != 0:
        say("plan.py: git checkout failed: %s" % (made.stderr or made.stdout).strip())
        return 1
    lv.push_stack(live, agent, nxt["step"], last, tip)
    say("STACKED: %s  %s" % (nxt["step"], nxt["title"]))
    say("on %s, the tip of native/%s. Written after %s (in work): %s" % (tip[:9], last, first["step"],
                                                                         ", ".join(chain[1:] + [nxt["step"]])))
    print_step_body(a, nxt)
    say()
    say(commands_text(a, agent))
    say("Write it and commit it on %s, fill its hand-off sheet (plan.py handoff %s), then run stack again. Land none of them: the "
        "lander lands %s, then takes each stacked step up in order with plan.py start <ID>." % (branch, nxt["step"], first["step"]))
    return 0


def handoff_path(live, step_id):
    return live_dir(live) / "handoff" / ("%s.md" % step_id)


def handoff_template(step_id):
    lines = ["# Hand-off sheet of %s" % step_id, "",
             "Written by the step's writer for its lander and its fixer (docs/plan/native/WRITER.md). Each heading needs at least one line.", ""]
    for h in HANDOFF_HEADINGS:
        lines += ["## " + h, "", ""]
    return "\n".join(lines)


def sheet_parts(text):
    """{heading: [its non-empty lines]} of the `## ` headings of a sheet (a `### ` line is text under the heading before it)."""
    parts, current = {}, None
    for line in text.splitlines():
        m = re.match(r"## +(\S.*?)\s*$", line)
        if m:
            current = m.group(1)
            parts.setdefault(current, [])
        elif current is not None and line.strip():
            parts[current].append(line)
    return parts


def handoff_problems(live, step_id):
    """[] when the step's hand-off sheet is there with text under each of its headings; else one line for each thing missing."""
    path = handoff_path(live, step_id)
    if not path.exists():
        return ["the sheet %s is missing (plan.py handoff %s makes it with its headings)" % (path.as_posix(), step_id)]
    parts = sheet_parts(path.read_text(encoding="utf-8", errors="replace"))
    out = []
    for h in HANDOFF_HEADINGS:
        if h not in parts:
            out.append("the heading '## %s' is missing in %s" % (h, path.as_posix()))
        elif not parts[h]:
            out.append("nothing is written under '## %s' in %s" % (h, path.as_posix()))
    return out


def cmd_handoff(a):
    """`handoff <ID>`: the path of the step's hand-off sheet (made with its headings when it is not there) and what it still lacks.
    0 when it is complete."""
    if find(read_steps(a.tree, a.live), a.id) is None:
        say("plan.py: no step %s" % a.id)
        return 1
    path = handoff_path(a.live, a.id)
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(handoff_template(a.id), encoding="utf-8")
    say(path.as_posix())
    bad = handoff_problems(a.live, a.id)
    for b in bad:
        say("to fill: " + b)
    if not bad:
        say("the sheet is complete")
    return 1 if bad else 0


def hard_path(live, step_id):
    return live_dir(live) / "hard" / ("%s.md" % step_id)


def is_hard(s):
    return s["state"] == "blocked" and (s["reason"] or "").startswith(HARD_PREFIX)


def cmd_hard(a):
    """`hard <ID> "<line>"`: the lander met a failure it may not fix (docs/plan/native/LANDER.md). The step in work, or a stacked
    step whose commits do not apply on main, is blocked with the reason `hard: <line>` (as `blocked` does: a branch in work kept and
    pushed, the page lists it), and the report file for the fixer is made with its headings when it is not there (one that is
    there is never written over); its path is printed. `start <ID>` takes the step up again after the fixer, on top of main (a
    stacked one from its stack row)."""
    steps = read_steps(a.tree, a.live)
    s = find(steps, a.id)
    if s is None:
        say("plan.py: no step %s" % a.id)
        return 1
    if s["state"] != "started" and not (s["state"] == "open" and stack_row(a, a.id, steps)):
        say("plan.py: step %s is %s; hard is for the step in work whose landing failed, or a stacked step that `start` could not "
            "move onto main" % (a.id, s["state"]))
        return 1
    line = " ".join((a.line or "").split())
    if not line:
        say("plan.py: hard needs one line: what failed")
        return 1
    path = hard_path(a.live, a.id)
    a.why = "%s%s (report: %s)" % (HARD_PREFIX, line, path.as_posix())
    code = cmd_blocked(a)
    if code != 0:
        return code
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        body = ["# Hard failure of %s" % a.id, "", "The lander's line: %s" % line, "",
                "Written by the lander for the fixer (docs/plan/native/LANDER.md): at most 30 lines in all.", ""]
        for h in HARD_HEADINGS:
            body += ["## " + h, "", ""]
        path.write_text("\n".join(body), encoding="utf-8")
    say("HARD: %s is blocked for the fixer. Fill the report: %s" % (a.id, path.as_posix()))
    return 0


def last_text_line(path):
    try:
        lines = [l for l in pathlib.Path(path).read_text(encoding="utf-8", errors="replace").splitlines() if l.strip()]
    except OSError:
        return ""
    return lines[-1].strip()[:300] if lines else ""


def item_state(a, item_id):
    """The state of a step or a repair, or None when there is no such item."""
    if is_repair(item_id):
        rep = next((r for r in lv.read_repairs(live_dir(a.live)) if r["id"] == item_id), None)
        return rep["state"] if rep else None
    s = find(read_steps(a.tree, a.live), item_id)
    return s["state"] if s else None


def cmd_wait(a):
    """`wait <ID> [--minutes N] [--log FILE]`: for the lander, which runs `plan.py land <ID>` in the background: blocks until the
    landing of ID is out of the gate's hands or N minutes (at most 9) pass, then prints one line. Exit 0 LANDED; 1 ENDED (not
    landed: read the land command's output); 3 STILL PLAYING (call wait again). FILE is the land command's output: its last line is
    shown. A landing begun a moment ago has WAIT_APPEAR seconds to reach the gate."""
    minutes = a.minutes
    if not 0 <= minutes <= WAIT_MAX:
        say("plan.py: --minutes is 0 to %d: one wait call for %d minutes at most" % (WAIT_MAX, WAIT_MAX))
        return 2
    if item_state(a, a.id) is None:
        say("plan.py: no step or repair %s" % a.id)
        return 1
    log = getattr(a, "log", None)

    def tail():
        last = last_text_line(log) if log else ""
        return "; the last line of %s: %s" % (pathlib.Path(log).name, last) if last else ""

    began = time.monotonic()
    deadline, appear_by = began + minutes * 60, began + WAIT_APPEAR
    seen = False
    while True:
        rec = landing_of(a)
        now = time.monotonic()
        if rec is not None and rec["step"] != a.id:
            say("ENDED: %s is not in the gate's hands; %s is%s" % (a.id, rec["step"], tail()))
            return 1
        if rec is not None:
            seen = True
            if now >= deadline:
                say("STILL PLAYING: %s, in the gate's hands since %s%s" % (a.id, hhmm(rec.get("queue_time") or rec["time"]), tail()))
                return 3
        else:
            fetch_main_landed(a, say_failure=False)
            state = item_state(a, a.id)
            if state == "landed":
                say("LANDED: %s%s" % (a.id, tail()))
                return 0
            if seen or now >= appear_by or now >= deadline:
                say("ENDED: %s is not landed (it is %s)%s" % (a.id, state, tail()))
                return 1
        time.sleep(WAIT_POLL)


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
    entry = None if working else ahead_entry(a)
    if entry and entry["step"] == s["step"]:
        say("PREPARED AHEAD: branch native/%s, %s; plan.py start %s%s takes it up" % (
            s["step"], commits_word(ahead_count(a, entry)), s["step"], agent_flag(agent)))
    row = None if working else stack_row(a, s["step"], steps)
    if row:
        say("STACKED: branch native/%s, written on native/%s; plan.py start %s%s takes it up" % (
            s["step"], row["after"], s["step"], agent_flag(agent)))
    say("Package %s; step %s of %d; %d landed so far." % (s["package"], s["order"], len(steps), landed))
    if working:
        say("This is in work: go on with it, then land it or block it. Nothing new is given while it is.")
    for note in notes:
        say("NOTE: " + note)
    for b in blocked_before:
        why = b["reason"] or blocked_reasons(a.tree, a.live).get(b["step"], "(no reason written)")
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
    howto = pathlib.Path(r["evidence"]) / "how-to-run.txt" if r["evidence"] not in ("", "-") else None
    if howto is not None and howto.is_file():
        say("Run the failed test alone (the command the gate ran for it):")
        for line in howto.read_text(encoding="utf-8", errors="replace").strip().splitlines():
            say("  " + line)
    traced = lv.repair_traced(r)
    say("Traced to: %s" % (traced[len("traced to "):] if traced.startswith("traced to ") else "no landing (not traced)"))
    say("Ledger row: %s" % (r["row"] if r["row"] not in ("", "-") else "none"))
    if r["note"] not in ("", "-"):
        say("Note: %s" % r["note"])
    say()
    say(REPAIR_RULES % r["id"])
    if r["source"] == "flake":
        say(FLAKE_RULES)
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
    settle_github(a)
    waiting = lv.open_mend(live_dir(a.live))
    if waiting:                                      # a helper mends a tool: nothing else is given until it has ended (AGENT.md section 9)
        say_mend_wait(a, waiting)
        return 0
    steps = read_steps(a.tree, a.live)
    claims = lv.read_claims(live_dir(a.live))
    stale = stale_of(a.tree, claims)
    landed = sum(1 for s in steps if s["state"] == "landed")
    agent = agent_of(a)
    running = say_landing_running(a)
    if running:
        say_ahead_in_next(a, steps, running)
    say_stale(a, claims, stale)
    work = in_work(a, steps)
    if work and agent == "1" and all(k == "step" for k, _ in work):
        repair = github_repair_open(a)
        if repair:                                   # a red job of GitHub on a landed commit comes before the step in work
            print_repair(a, repair)
            say()
            for _, s in work:
                say("KEPT: step %s is in work on branch native/%s. `plan.py start %s` keeps it on that branch (commit its work first; "
                    "nothing is lost) and starts the repair. When the repair has landed, `plan.py start %s` goes on with the step on its "
                    "own branch, on top of the new main." % (s["step"], s["step"], repair["id"], s["step"]))
            return 0
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
    if agent == "1":
        kept = next((s for s in steps if is_kept(s)), None)
        if kept:                                     # its repair has landed (or been set aside): it goes on first
            print_step(a, steps, kept, ["it is kept on its branch native/%s (%s); `start` puts the branch on top of main's newest "
                                        "commit and goes on with it" % (kept["step"], kept["reason"])])
            say_after(a, steps, kept)
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


def begin_work(a, branch, on_main=False):
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
            return dirty_line(folder, "start")
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
    if on_main and local:
        # A step that was kept on its branch while other landings moved main: its commits go on top of origin/main (nothing is lost;
        # when they do not apply the branch is left as it is and one line says so).
        where = work_folder(a.work_root, agent)
        if git(where, "merge-base", "--is-ancestor", "origin/main", "HEAD").returncode != 0:
            rebased = git(where, "rebase", "origin/main")
            if rebased.returncode != 0:
                git(where, "rebase", "--abort")
                said = (rebased.stdout + rebased.stderr).strip().splitlines()
                return ("the commits of %s do not apply on top of origin/main (%s). The branch is as it was; put it on main by hand "
                        "(git rebase origin/main, resolve, git rebase --continue), then run start again." % (
                            branch, said[-1] if said else "conflict"))
    return None


def take_up_prepared(a, entry):
    """What `start` does for a step written ahead (its branch native/<id> stands on the tip of the item that was being landed):
    the folder goes onto the branch; when origin/main is not in it yet the step's own commits (the ones after the entry's base) are
    moved onto origin/main (`git rebase --onto origin/main <base> native/<id>`, a clean folder; a conflict is aborted and the
    branch is as it was). When origin/main is in the branch nothing moves and uncommitted changes stay. Returns the refusal text, or
    None when the folder is on the branch, on top of main."""
    agent = agent_of(a)
    folder = work_folder(a.work_root, agent)
    branch = "native/" + entry["step"]
    work = in_work(a, read_steps(a.tree, a.live))
    if work:
        return in_work_line(a, work)
    if not (folder / ".git").exists():
        return "the work folder %s is missing" % folder.as_posix()
    fetched = git(folder, "fetch", "origin")
    if fetched.returncode != 0:
        return "git fetch origin failed: %s" % fetched.stderr.strip()
    if head_branch(folder) != branch:
        if folder_changes(folder):
            return dirty_line(folder, "start")
        made = git(folder, "checkout", branch)
        if made.returncode != 0:
            return "git checkout failed: %s" % (made.stderr or made.stdout).strip()
    base = entry["base"]
    have = commit_of(folder, base)
    # Nothing moves when main is in the branch and the commit the step stands on is in main (N is on main as it was written). When N
    # is not on main (given up), or main has it under other ids, or main has gone further, the step's own commits are moved.
    if have and is_ancestor(folder, "origin/main", "refs/heads/" + branch) and is_ancestor(folder, base, "origin/main"):
        return None
    if not have:
        return ("the commit %s that %s was written on is no longer in this repository, so its commits cannot be moved onto main. %s "
                "is kept as it is; nothing is deleted." % (base[:9], branch, branch))
    if tracked_changes(folder):
        return ("the work folder %s has uncommitted changes, and the commits of %s have to be moved onto origin/main: commit them "
                "first (git add, git commit), then run start again." % (folder.as_posix(), branch))
    why = move_commits(folder, "origin/main", base, branch)
    if why:
        return ("the commits of %s do not apply on top of origin/main (%s). The branch is as it was. Move it by hand: git rebase "
                "--onto origin/main %s %s (resolve, git rebase --continue), then run start again." % (branch, why, base, branch))
    return None


def cmd_start(a):
    if mend_blocks(a):
        return 1
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
    entry = ahead_entry(a)
    prepared = entry if entry and entry["step"] == a.id and folder.is_dir() and branch_exists(folder, "refs/heads/" + branch) else None
    stacked = None
    if prepared is None:
        row = stack_row(a, a.id, steps)
        if row and folder.is_dir() and branch_exists(folder, "refs/heads/" + branch):
            prepared = stacked = row          # the step it stands on is of its package, so `unmet` above held it until that landed
    why = take_up_prepared(a, prepared) if prepared else begin_work(a, branch, on_main=is_kept(s) or is_hard(s))
    if why:
        say("plan.py: " + why)
        return 1
    set_state(a.tree, a.id, "started", agent=agent, live=a.live)
    lv.put_claim(live_dir(a.live), s["package"], agent, branch)
    refresh_page(a)
    say("started %s: branch %s in %s (package %s is claimed by agent %s)" % (a.id, branch, folder.as_posix(), s["package"], agent))
    if prepared:
        count = git(folder, "rev-list", "--count", "origin/main..refs/heads/" + branch).stdout.strip()
        say("%s: %s commits of %s are on top of main" % ("from the stack" if stacked else "prepared ahead", count, a.id))
        if stacked:
            lv.drop_stack(live_dir(a.live), agent, a.id)
        else:
            lv.drop_ahead(live_dir(a.live), agent, a.id)
    if replay_allowed(card_text(a.tree, s)):
        say("This step's card says its replay reference may change. Once the change is made and the tour has played every recorded "
            "stop, record the reference with: %s" % REPLAY_RECORD)
        say(REPLAY_WORDS_RULE)
    say()
    say(commands_text(a, agent))
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
    work = in_work(a, read_steps(a.tree, a.live))
    if work and rep["source"] == "github" and all(k == "step" for k, _ in work):
        for _, s in work:                            # the step is kept on its branch, its work committed there
            why = park_branch(a, work_folder(a.work_root, s["agent"] or "1"), "native/" + s["step"])
            if why:
                say("plan.py: " + why)
                return 1
        for _, s in work:
            set_state(a.tree, s["step"], "blocked", agent=agent, reason=kept_reason(a.id), live=a.live)
            say("kept %s on its branch native/%s (pushed to origin). `plan.py start %s` goes on with it after repair %s has landed." % (
                s["step"], s["step"], s["step"], a.id))
    why = begin_work(a, branch)
    if why:
        say("plan.py: " + why)
        return 1
    lv.set_repair(live_dir(a.live), a.id, "started")
    lv.set_state_entry(live_dir(a.live), a.id, "started", agent)
    refresh_page(a)
    say("started %s: branch %s in %s" % (a.id, branch, work_folder(a.work_root, agent).as_posix()))
    say()
    say(commands_text(a, agent))
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


def refs_problems(folder, base="origin/main"):
    """The invented file and line references of the changed documents and the commit messages after `base`: [line]."""
    allow = doc_refs.read_allow(folder / "tools" / "verify" / "doc_refs_allow.txt")
    bad = doc_refs.check_files([folder / d for d in changed_documents(folder, base)], folder, allow)
    bad += doc_refs.check_text("the commit messages", commit_messages(folder, base), folder, allow, message=True)
    return bad


CODE_CHECKS = ("console_census.py", "cast_check.py", "byte_order_check.py")


def code_check_problems(folder):
    """The repository's code checks (the ones the pre-push hook's census and the gate's repo/code run: tools/verify, each a few seconds)
    on the folder's own tree: [line] of what refuses it, with each check's own lines (the census: the groups not as they must be and the
    files with their counts). Empty when all pass. Run before anything is committed or queued, so that a rule of the repository
    refuses a step in seconds, as a refusal, and not at main's move as "not judged" (T11-B, step S0.1.3)."""
    out = []
    for name in CODE_CHECKS:
        script = pathlib.Path(folder) / "tools" / "verify" / name
        if not script.is_file():
            continue
        r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(script), str(folder)], capture_output=True, text=True, cwd=str(folder))
        if r.returncode == 0:
            continue
        lines = [l.rstrip() for l in (r.stdout + r.stderr).splitlines() if l.strip()]
        if name == "console_census.py":
            lines = [l for l in lines if l.startswith("    ") or re.search(r"\s[1-9]\d*\s+closed|OVER THE CEILING", l)]
        out.append("%s refuses it:" % name)
        out.extend("  " + l for l in lines[:25])
    return out


def hand_to_gate(a, folder, branch, label, agent, extra, on_pass, join=False, tip=None):
    """ONE call of the gate (LAND) for a step or a repair, and what follows: a pass calls `on_pass(verdict, seconds, summary)`
    and returns its exit code; anything else prints the verdict and returns 1 (a failure), 3 (the same failure three times) or
    4 (a tool or the machine: not judged). The callers take the tool's own state commit off the branch again when the landing did not
    pass (state_commit_after): no commit says `landed` for a step that is not on main.

    The landing record (live.write_record) is written just before the gate is called and removed when it has answered. `join`:
    the gate already has this branch in its queue (an earlier call, cut): its record is left as it is, nothing is pushed, and the
    gate's own call attaches to the row and waits for its end. `tip`: the commit handed over when the folder's HEAD is not the branch
    (the folder is on a step written ahead); it is the folder's HEAD otherwise."""
    started = time.time()
    options = gate_options(a, agent) + list(extra)
    if join:
        run = lambda: LAND(folder, branch, a.tablet, a.tablet_wait, options, push=False)
    else:
        tip = tip or git_out(folder, "rev-parse", "HEAD")
        lv.write_record(live_dir(a.live), agent, label, branch, tip, os.getpid())
        lv.set_ahead_handed(live_dir(a.live), agent, label, tip)       # a step written ahead remembers which tip was given
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


def later_commits(folder, tip, ref="HEAD"):
    """[`sha subject`] of the commits of `ref` (the folder's HEAD) after `tip`."""
    return git(folder, "log", "--format=%h %s", "%s..%s" % (tip, ref)).stdout.strip().splitlines()


def take_state_commit_off(folder, branch, tip):
    """The tool's own commit `Plan: step X landed` (`tip`) off `branch`, after a landing that did not pass: no commit says `landed` for a
    step that is not on main. Returns (done, line). When the commit is the branch's tip the branch ref moves to its parent (`update-ref`, only
    while the branch still is `tip`: nothing is reset, no file of the folder is written but steps.tsv, which the commit changed and nothing
    else, and which is put back from the new HEAD when the folder is on the branch; uncommitted changes of other files stay as they are).
    Commits after it (made while the landing played): the folder on the branch with no uncommitted change moves them onto the parent;
    else the commit stays, and the line says so (the next `land` takes it off, drop_state_commits)."""
    parent = commit_of(folder, tip + "^")
    here = commit_of(folder, "refs/heads/" + branch)
    if not parent or not here:
        return False, ""
    on = head_branch(folder) == branch
    if here == tip:
        moved = git(folder, "update-ref", "refs/heads/" + branch, parent, tip).returncode == 0
        if moved and on:
            git(folder, "checkout", "HEAD", "--", "%s/steps.tsv" % NATIVE)
        return moved, ""
    if on and not tracked_changes(folder) and not move_commits(folder, parent, tip, branch):
        return True, ""
    return False, ("the commit `%s` (%s) is still on %s under later commits; the next `land` takes it off" % (
        git_out(folder, "log", "-1", "--format=%s", tip), tip[:9], branch))



def state_commit_after(a, folder, branch, step_id, tip, code):
    """What follows a landing that did not pass (`code` is not 0): the tool's own state commit that was handed to the gate (`tip`) goes
    off the branch, so that no commit says `landed` for a step that is not on main (the next landing makes it again). One case keeps it:
    not judged (4) while the folder is on the step written ahead, where `land` hands the same tip over again from there (land_from_ahead)."""
    if code == 0 or not tip or not is_state_commit(folder, tip, step_id):
        return
    if code == 4 and ahead_here(a):
        return
    parent = commit_of(folder, tip + "^")
    done, line = take_state_commit_off(folder, branch, tip)
    if done:
        say("The commit `%s` is off %s: %s is not on main." % (STATE_COMMIT % step_id, branch, step_id))
        move_ahead_off(a, folder, step_id, tip, parent)
    elif line:
        say("NOTE: " + line)


def move_ahead_off(a, folder, step_id, tip, parent):
    """The step M written ahead of `step_id` was cut from `tip`, the commit just taken off: its own commits move onto `parent` (the folder is on
    M with no uncommitted change; else a line says `ahead` does it), so that no branch of the agent holds a commit that says `landed` for a step
    that is not on main."""
    entry = ahead_entry(a)
    if not entry or entry["after"] != step_id or entry["base"] != tip:
        return
    branch = "native/" + entry["step"]
    if not branch_exists(folder, "refs/heads/" + branch) or not is_ancestor(folder, tip, "refs/heads/" + branch):
        return
    if head_branch(folder) != branch or tracked_changes(folder):
        say("NOTE: %s stands on the commit that was taken off; `plan.py ahead` moves it onto %s's tip once your changes are committed." % (branch, step_id))
        return
    why = move_commits(folder, parent, tip, branch)
    if why:
        say("NOTE: the commits of %s could not be moved off it (%s); `plan.py ahead` says what to do." % (branch, why))
        return
    lv.write_ahead(live_dir(a.live), agent_of(a), entry["step"], step_id, parent, entry["handed"])
    say("The commits of %s stand on %s's tip again." % (branch, step_id))


def replay_allowed(card):
    return bool(card and REPLAY_ALLOWED.search(card))


def own_base(a, folder, s):
    """Where the commits of step `s` begin on the folder's HEAD: `origin/main`, or for a step written ahead (ahead's entry names this step)
    the commit it was written on, so that the commits of the step below it, which are on that branch until it has landed, are not counted
    as this step's (`check` on native/<M> while N plays)."""
    entry = ahead_entry(a)
    if entry and entry["step"] == s["step"] and commit_of(folder, entry["base"]) and is_ancestor(folder, entry["base"], "HEAD"):
        return entry["base"]
    return "origin/main"


def replay_problems(folder, allowed, base="origin/main"):
    """Why a branch that changes a reference under tests/replays/ may not land ([text], empty when it may): the card must say the
    replay changes (`allowed`), and a commit message of the landing must contain the words "replay reference". Only the branch's own
    commits count: those after `base` (origin/main, or the step below a step written ahead: own_base)."""
    names = git(folder, "diff", "--name-only", "%s...HEAD" % base).stdout.splitlines()
    files = [n for n in names if n.startswith(REPLAY_DIR)]
    if not files:
        return []
    if not allowed:
        return [REPLAY_REFUSAL] + ["  changed: " + f for f in files]
    if REPLAY_WORDS not in commit_messages(folder, base).lower():
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
    """`land` with the pending GitHub results read at its start and again after its landing ended (settle_github)."""
    if mend_blocks(a):
        return 1
    settle_github(a)
    code = land_item(a)
    settle_github(a)
    return code


def land_item(a):
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
        held = ahead_entry(a)
        if held and held["step"] == a.id:
            say("step %s is prepared ahead, not started: plan.py start %s%s" % (a.id, a.id, agent_flag(agent)))
            return 1
        if stack_row(a, a.id, steps):
            say("step %s is written in the stack, not started: plan.py start %s%s" % (a.id, a.id, agent_flag(agent)))
            return 1
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
    if HANDOFF_REQUIRED:
        bad = handoff_problems(a.live, a.id)
        if bad:
            say("plan.py: step %s has no complete hand-off sheet, which its lander and its fixer read (docs/plan/native/WRITER.md):" % a.id)
            for b in bad:
                say("  " + b)
            return 1
    here = ahead_here(a)
    if here and here["after"] == a.id and head_branch(folder) != branch:
        return land_from_ahead(a, s, folder, branch, here)
    why = work_folder_problem(a, folder, branch)
    if why:
        say("plan.py: " + why)
        return 1
    return begin_landing(a, s, folder, branch)


def land_from_ahead(a, s, folder, branch, entry):
    """A new landing of the step `entry` was written ahead of, given while the work folder is on the step written ahead (the landing
    before was not judged, and nothing has changed on the step's branch since). The branch's tip is handed over as it is: nothing is
    committed, the folder stays where it is with its uncommitted changes. Any other new landing (the branch has commits the last
    landing did not hold, or the tool's own state commit is still to be made) needs the folder on the step's branch, clean."""
    agent = agent_of(a)
    tip = commit_of(folder, "refs/heads/" + branch)
    if tip and tip == entry["handed"] and is_state_commit(folder, tip, a.id) and code_commit_at(folder, tip, a.id):
        say("landing %s again from native/%s: its tip %s is the one the last landing was given; nothing is committed and the folder "
            "stays on native/%s." % (a.id, entry["step"], tip[:9], entry["step"]))
        code_commit = code_commit_at(folder, tip, a.id)

        def passed(verdict, seconds, summary):
            return finish_landing(a, s, code_commit, verdict, seconds, summary)

        code = hand_to_gate(a, folder, branch, a.id, agent, also_options(a, a.id), passed, tip=tip)
        state_commit_after(a, folder, branch, a.id, tip, code)
        return code
    say("plan.py: " + ahead_branch_line(folder, entry, branch, a.id))
    return 1


def ahead_branch_line(folder, entry, branch, item):
    """Why a landing that needs the step's own branch cannot be given from the step written ahead: both branches are named."""
    return ("the work folder %s is on native/%s (written ahead), and %s needs a commit of its own or a new landing commit first: "
            "commit your work on native/%s, then git checkout %s, then run land %s again." % (
                folder.as_posix(), entry["step"], branch, entry["step"], branch, item))


def join_landing(a, s, folder, branch, rec):
    """`land` called while this step's landing is in the gate's hands (the agent was compacted and no longer knows, or its command
    was cut): it joins, waits, and ends with the gate's verdict and exit code, the same as the call that started it. Nothing is
    checked again and nothing is committed or pushed. When the landing that ran judged an older commit than the folder's tip
    now: if it failed, the present tip is landed in the same call; if it passed, the later commits are named as not on main."""
    agent = agent_of(a)
    # The folder is on the step's branch, or on the step written ahead: the join reads the record's tip and the step's own branch
    # (refs/heads/<branch>), never HEAD, and needs neither the step's branch checked out nor a clean folder.
    if not folder.is_dir() or (head_branch(folder) != branch and not ahead_here(a)):
        say("plan.py: the work folder %s is not on branch %s, so the landing of %s cannot be joined from it" % (folder.as_posix(), branch, a.id))
        return 1
    if rec.get("finishing"):
        # The queue row has ended and the process that handed it over still runs: it is making the bookkeeping. Wait for it.
        say("joined the landing of %s: its row in the queue has ended and process %s is finishing; waiting for it" % (a.id, rec["pid"]))
        if not wait_for_process(int(rec["pid"])):
            say("plan.py: process %s is still running after 30 minutes; run land again later" % rec["pid"])
            return 1
        a.main_fetched = False
        return land_item(a)
    tip = rec["tip"]
    if git(folder, "rev-parse", "--verify", "--quiet", tip + "^{commit}").returncode != 0:
        lv.drop_record(live_dir(a.live), agent, a.id)             # a record naming no commit of this repository is no landing
        return land_item(a)
    say("joined the landing of %s, in the queue since %s" % (a.id, hhmm(rec["queue_time"])))
    code_commit = code_commit_at(folder, tip, a.id) or tip[:9]

    def passed(verdict, seconds, summary):
        later = later_commits(folder, tip, "refs/heads/" + branch)
        code = finish_landing(a, s, code_commit, verdict, seconds, summary)
        if later:
            say("NOTE: the landing judged %s. These later commits of the branch are not on main: %s" % (tip[:9], "; ".join(later)))
        return code

    code = hand_to_gate(a, folder, branch, a.id, agent, also_options(a, a.id), passed, join=True)
    here = commit_of(folder, "refs/heads/" + branch) or git_out(folder, "rev-parse", "HEAD")
    if not (code in (1, 4) and here != tip):
        state_commit_after(a, folder, branch, a.id, tip, code)
    if code in (1, 4) and here != tip:
        say("the landing that was running judged an older commit (%s); landing the present one (%s) now" % (tip[:9], here[:9]))
        on_ahead = ahead_here(a)
        if on_ahead and head_branch(folder) != branch:
            say("plan.py: " + ahead_branch_line(folder, on_ahead, branch, a.id))
            return 1
        why = work_folder_problem(a, folder, branch)
        if why:
            say("plan.py: " + why)
            return 1
        return begin_landing(a, find(read_steps(a.tree, a.live), a.id) or s, folder, branch)
    return code


def also_options(a, step_id):
    """`--also` for the extra checks the step's proof asks for (docs/plan/native/step-checks.tsv): none, or two words; and `--section`
    when the landing is its section's full check (is_section_check)."""
    names = also_names(a.tree, step_id)
    steps = read_steps(a.tree, a.live)
    s = find(steps, step_id)
    section = ["--section"] if s is not None and is_section_check(a, steps, s) else []
    return (["--also", ",".join(names)] if names else []) + section


# The two tiers of a landing (mend M-006, docs/plan/04-GATE-AND-MAIN.md "Two tiers"): a step's landing plays the 12 quick stops on
# the three renderers and the tablet; the section's full check (gate.py land --section: every picture the main level compares, the
# debug layers, both speed measures) is the landing of a package's last step, or of the SECTION_EVERY-th step landed since the last
# one. The count is kept in the live folder (SECTION_SINCE), written at each pass of a step.
SECTION_EVERY = 4
SECTION_SINCE = "section-since.txt"


def section_since(a):
    """The steps landed since the last section's full check (0 when nothing is written)."""
    try:
        return max(0, int((live_dir(a.live) / SECTION_SINCE).read_text(encoding="utf-8").split()[0]))
    except (OSError, ValueError, IndexError):
        return 0


def is_section_check(a, steps, s):
    """Whether the landing of step `s` is its section's full check: every other step of its package is landed, or SECTION_EVERY - 1
    steps have landed since the last check."""
    if all(x["state"] == "landed" for x in steps if x["package"] == s["package"] and x["step"] != s["step"]):
        return True
    return section_since(a) + 1 >= SECTION_EVERY


def section_after_pass(a, section):
    """The count after a step's pass: 0 after a section's full check, one more after a step's landing."""
    lv.atomic_write(live_dir(a.live) / SECTION_SINCE, "%d\n" % (0 if section else section_since(a) + 1))


SPEED_CHECKS = ("measure/", "speed/")


def figures_line(verdict):
    """The speed checks' lines of a passed landing, one line: `<check>: <what the check said>; ...` (`-` when it had none). The speed
    figures exist only after the landing, so the tool carries them to the landings list and the page (AGENT.md section 11)."""
    parts = ["%s: %s" % (check, detail) for check, kind, detail in verdict.rows if check.startswith(SPEED_CHECKS) and detail]
    return " | ".join(parts) or "-"


def say_figures(verdict):
    line = figures_line(verdict)
    if line != "-":
        say("Speed figures of this landing (kept with the step in the landings list and on the page; copy none by hand):")
        for part in line.split(" | "):
            say("  " + part)


def end_aside(a, rep):
    """A landed repair of the source `flake` ends the time its test was set aside: the line of tests/set-aside.tsv goes (the test counts
    again). The repair's check is `<check> <test> <renderer>` in its note's first words; the gate's list is the live folder's."""
    if rep.get("source") != "flake":
        return
    import kinds
    path = live_dir(a.live) / "set-aside.tsv"
    removed = kinds.end_set_aside(path, rep.get("check", ""), rep.get("renderer", ""), rep.get("note", ""))
    if removed:
        say("The test set aside by repair %s counts again (its line is out of %s)." % (rep["id"], path.as_posix()))


def finish_landing(a, s, code_commit, verdict, seconds, summary):
    """What follows a pass: the step is landed in the plan tree's file and in what the tool reads, the landings list, the claim and
    the page. The landings list takes a step id once, so whoever does this second adds nothing."""
    agent = agent_of(a)
    section = is_section_check(a, read_steps(a.tree, a.live), s)       # before the step is written landed
    MAIN_LANDED[a.id] = code_commit
    # A landing that ends while the work folder is on a step written ahead writes no file of the working tree (steps.tsv there is
    # that step's own); what is landed is read from origin/main, and the live folder has the rest.
    set_state(a.tree, a.id, "landed", code_commit, live=a.live, write_file=ahead_here(a) is None)
    lv.add_landing(live_dir(a.live), a.id, code_commit, seconds, agent, main=verdict.commit or "-", figures=figures_line(verdict))
    section_after_pass(a, section)
    settle_claim(a, read_steps(a.tree, a.live), s, agent, renew=True)
    refresh_page(a)
    say("LANDED %s as %s%s in %d s. Verdict by kind: %s." % (
        a.id, code_commit, (" (main has it as %s)" % verdict.commit) if verdict.commit else "", seconds,
        summary or "no check rows read"))
    say_figures(verdict)
    say("The progress files are made again (the page to open: %s)." % page_paths(a.live).html.as_posix())
    return 0


PROTECTED_TOOLS = ("tools/gate/", "tools/plan/", "tools/measure/", "tools/verify/", "tools/lumacore/luma_test.py",
                    "tools/lumacore/luma_test_android.py", "tools/lumacore/tests/", "tools/test_driver/tests/", ".github/",
                    "tests/pictures/approved.tsv", "docs/plan/native/steps.tsv", "docs/plan/native/step-checks.tsv")
# AGENT.md section 9: the agent does not edit these (the test tools, their self-tests, GitHub's jobs, the approved pictures, the plan's
# step list and extra checks) unless the step's card names the file.


def branch_changes(folder, step_id=None, base="origin/main"):
    """[path] changed by the commits of the folder's HEAD that are not on `base` (origin/main), without the tool's own state commit of a
    step (it changes steps.tsv and nothing else)."""
    out = git(folder, "log", "--no-merges", "--format=%x01%s", "--name-only", "%s..HEAD" % base)
    paths, skip = [], False
    for line in out.stdout.splitlines():
        if line.startswith("\x01"):
            skip = line[1:].startswith("Plan: step ")
        elif line.strip() and not skip and line not in paths:
            paths.append(line.strip())
    return paths


def protected_problems(folder, card, what, base="origin/main"):
    """[line] for the files of PROTECTED_TOOLS the branch changes after `base` and the card does not name ("" card: no card names
    anything)."""
    out = []
    for path in branch_changes(folder, base=base):
        if not any(path == p or (p.endswith("/") and path.startswith(p)) for p in PROTECTED_TOOLS):
            continue
        if card and (path in card or path.rsplit("/", 1)[-1] in card):
            continue
        out.append(path)
    if not out:
        return []
    return ["plan.py: %s changes files of the test tools, their self-tests, GitHub's jobs, the approved pictures or the plan's own lists, "
            "and its card does not name them: %s" % (what, ", ".join(out[:8]) + (" and %d more" % (len(out) - 8) if len(out) > 8 else "")),
            "A tool at fault is mended by a helper (plan.py mend open ...), never in a step. Take the change out of the step's branch, or "
            "name the file in the card if the card is wrong."]


def strictness_problems(folder, branch, what, base="origin/main"):
    """[line] of what the strictness check refuses on the folder's branch after `base` (tools/gate/strictness.py): empty when nothing."""
    try:
        lines = strictness.problems_for(folder, base, "HEAD", branch)
    except strictness.StrictnessError as error:
        return ["plan.py: strictness.py could not judge %s: %s" % (what, error)]
    if not lines:
        return []
    return ["plan.py: %s changes a place that can make a check see less, and the log has no line for it (the gate's repo/strictness "
            "refuses the same). Nothing was committed or queued:" % what] + ["  " + l for l in lines]


def prechecks(a, s, folder, branch, first_only=True):
    """What `land` checks before it commits or queues anything, in order: [[line, ...]], one list of lines for each thing that refuses
    the step (only the first when `first_only`; `plan.py check` asks for all). Nothing is written, committed or queued."""
    groups = []

    def refused(lines):
        groups.append(lines)
        return first_only

    # 1. The ledger.
    reading = card_rows(card_text(folder, s))
    if not reading.ok:
        if refused(["plan.py: the card's Rows line could not be read (%s): %s" % (reading.why, reading.raw[:200]),
                    "It is the plan's fault, not the step's: plan.py blocked %s \"tool: the card's Rows line cannot be read\", then plan.py next." % s["step"]]):
            return groups
    else:
        git(folder, "fetch", "origin")
        ledger = read_ledger((folder / LEDGER).read_bytes().decode("utf-8"))
        base = git(folder, "show", "origin/main:" + LEDGER)
        base_ledger = read_ledger(base.stdout) if base.returncode == 0 else None
        problems = ledger_problems(reading, ledger, base_ledger)
        if problems and refused(["plan.py: step %s does not land yet. The ledger (%s) must say:" % (s["step"], LEDGER)] + ["  " + p for p in problems]):
            return groups
    # 2. Invented references in the changed documents and the commit messages.
    base = own_base(a, folder, s)
    bad = refs_problems(folder, base)
    if bad and refused(["plan.py: a file or line named in the documents or the commit messages does not exist:"] + ["  " + l for l in bad]):
        return groups
    # 3. A replay reference changes only where the card says so.
    bad = replay_problems(folder, replay_allowed(card_text(folder, s)), base)
    if bad and refused(["plan.py: " + bad[0]] + list(bad[1:])):
        return groups
    # 3b. The repository's code checks (the console census, the cast check, the byte-order check) on the folder's tree: a refusal here is
    # a refusal, in seconds, with the check's own lines; nothing is committed or queued.
    bad = code_check_problems(folder)
    if bad and refused(["plan.py: step %s does not land: a rule of the repository refuses it (the gate's repo/code and the pre-push hook run the same "
                        "checks). Nothing was committed or queued. Change the code, commit, and land again:" % s["step"]] + list(bad)):
        return groups
    # 3c. A place that can make a check see less changes only with its line in the strictness log.
    bad = strictness_problems(folder, branch, "step %s" % s["step"], base)
    if bad and refused(bad):
        return groups
    # 3d. The test tools, GitHub's jobs and the plan's lists change only where the card names the file.
    bad = protected_problems(folder, card_text(folder, s), "step %s" % s["step"], base)
    if bad and refused(bad):
        return groups
    return groups


def earlier_state_commits(folder, step_id):
    """[sha], newest first: the tool's own `Plan: step <ID> landed` commits on the folder's HEAD that are not on origin/main."""
    out = git(folder, "log", "--format=%H %s", "origin/main..HEAD").stdout.splitlines()
    found = []
    for line in out:
        sha, _, subject = line.partition(" ")
        if subject.startswith(STATE_COMMIT % step_id) and is_state_commit(folder, sha, step_id):
            found.append(sha)
    return found


def drop_state_commits(folder, step_id):
    """A new landing of a step takes the tool's own earlier state commits off its branch before it makes the new one (a landing that
    failed left one, and the code commit it names is no longer the step's). Only those commits go; the commits after them are put on their
    parents. Returns "" or why it could not (the rebase is aborted, the branch is as it was)."""
    for sha in earlier_state_commits(folder, step_id):
        r = git(folder, "rebase", "--onto", sha + "^", sha)
        if r.returncode != 0:
            git(folder, "rebase", "--abort")
            return "taking the earlier state commit %s off the branch did not apply: %s" % (sha[:9], (r.stderr or r.stdout).strip()[-200:])
    return ""


def begin_landing(a, s, folder, branch):
    """The landing of the folder's present tip: the ledger, the references, the replay reference, the repository's rules; the state commit
    (made once: a tip that is already the tool's state commit for this step is handed over as it is; an earlier one under fix commits is
    taken off first); then the gate."""
    agent = agent_of(a)
    groups = prechecks(a, s, folder, branch)
    if groups:
        for l in groups[0]:
            say(l)
        return 1
    # 4. The landing's own commit: the step is landed, with the code's commit. Made once: when the tip is already this step's
    # state commit (an earlier try made it) nothing is committed; fix commits on top of an earlier state commit take it off the branch,
    # and the new state commit names the newer tip (E1: no commit on the branch says "landed" for code that was not the step's).
    tip = git_out(folder, "rev-parse", "HEAD")
    if is_state_commit(folder, tip, a.id) and code_commit_at(folder, tip, a.id):
        code_commit = code_commit_at(folder, tip, a.id)
    else:
        why = drop_state_commits(folder, a.id)
        if why:
            say("plan.py: " + why)
            return 1
        tip = git_out(folder, "rev-parse", "HEAD")
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

    handed = git_out(folder, "rev-parse", "HEAD")
    code = hand_to_gate(a, folder, branch, a.id, agent, also_options(a, a.id), passed)
    state_commit_after(a, folder, branch, a.id, handed, code)
    return code


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
        return land_item(a)
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
        bad = code_check_problems(folder)
        if bad:
            say("plan.py: repair %s does not land: a rule of the repository refuses it (the gate's repo/code and the pre-push hook run the "
                "same checks). Nothing was queued. Change the code, commit, and land again:" % a.id)
            for l in bad:
                say(l)
            return 1
        bad = strictness_problems(folder, branch, "repair %s" % a.id) or protected_problems(folder, "", "repair %s" % a.id)
        if bad:
            for l in bad:
                say(l)
            return 1
    code_commit = (rec["tip"] if rec else git_out(folder, "rev-parse", "HEAD"))[:9]

    def passed(verdict, seconds, summary):
        closed = verdict.commit or code_commit
        lv.set_repair(live_dir(a.live), a.id, "landed", closed)
        lv.set_state_entry(live_dir(a.live), a.id, "landed", agent)
        lv.add_landing(live_dir(a.live), a.id, code_commit, seconds, agent, main=verdict.commit or "-", figures=figures_line(verdict))
        end_aside(a, rep)
        refresh_page(a)
        say("LANDED %s as %s%s in %d s. Verdict by kind: %s." % (
            a.id, code_commit, (" (main has it as %s)" % verdict.commit) if verdict.commit else "", seconds,
            summary or "no check rows read"))
        say_figures(verdict)
        say("The repair is landed (closed by %s). The progress files are made again (the page to open: %s)." % (
            closed, page_paths(a.live).html.as_posix()))
        return 0

    if rec:
        say("joined the landing of %s, in the queue since %s" % (a.id, hhmm(rec["queue_time"])))
    # A repair of a red job of GitHub is proven there before main moves (`--github first`); another repair runs its own check.
    extra = ["--github", "first"] if rep["source"] == "github" else ([] if rep["source"] == "flake" else ["--also", rep["check"]])
    return hand_to_gate(a, folder, branch, a.id, agent, extra, passed, join=bool(rec))


def park_branch(a, folder, branch):
    """What `blocked` does for something in work: the folder must have no uncommitted change, and the branch goes to origin so
    that nothing is lost. Returns the refusal text, or None."""
    if folder.exists() and folder_changes(folder):
        here = ahead_here(a)
        if here:                                     # the folder is on the step written ahead: its work is what has to be committed
            return ("the work folder %s has uncommitted changes. Commit your work on native/%s first (git add, git commit), then run "
                    "blocked again." % (folder.as_posix(), here["step"]))
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
    append_under(blocked_path(a.live), "steps", "- %s %s: %s | first failure line: %s" % (
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
    append_under(blocked_path(a.live), "repairs", "- %s %s: %s | first failure line: %s" % (
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
    if a.repair_cmd == "end":
        return repair_end(a)
    row, new = lv.add_repair(live, a.source, a.kind, a.check, a.renderer, a.step, a.commit, a.evidence, a.row, a.note)
    refresh_page(a)
    say(row["id"])                                   # the one line the nightly run reads: the repair's id, new or counted
    return 0


def repair_end(a):
    """`repair end R-00N --mend M-00N`: the repair's cause was a test tool and the mend is closed (it was recorded without ending the repair)."""
    m = mend_row(a, a.mend)
    if m is None:
        bad = "no mend %s (plan.py mend list)" % a.mend
    else:
        bad = repair_end_problem(a, a.id, m) or (mend_commit_problem(a, m["commit"]) if m["commit"] not in ("", "-") else "mend %s has no commit" % m["id"])
    if bad:
        say("plan.py: " + bad)
        return 1
    end_repair_by_mend(a, a.id, m["id"], m["title"], m["commit"])
    refresh_page(a)
    say("Repair %s is ended by mend %s (commit %s): its cause was a test tool, and nothing of the game was left to change." % (
        a.id, m["id"], m["commit"][:9]))
    say("Go on: `python -X utf8 -I %s next`." % plan_path(a))
    return 0


def cmd_tool(a):
    text = " ".join(a.text.split())
    append_under(blocked_path(a.live), "tools", "- %s: %s" % (datetime.date.today().isoformat(), text))
    lv.append_fault(live_dir(a.live), agent_of(a), "", "TOOL", "(by hand)", text)
    refresh_page(a)
    say("written under 'tools' in %s and as a line of tool-faults.tsv. Nothing is blocked by it: go on as your rules say; do not debug the tool." % blocked_path(a.live).as_posix())
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


# ---- tool mends, decisions, the heartbeat (docs/plan/native/TOOL-MEND.md, AGENT.md sections 3 and 9) ----------------------------------------

MEND_WAIT_HOURS = 3          # no result of the helper this long after its start: the wake-up looks (AGENT.md section 9)
MEND_PER_CHECK = 2           # per step at most two mends for the same check
MEND_TOOL_PATHS = ("tools/", "docs/", ".github/")      # a mend's commit touches a tool or a document
WAIT_REASON = "waits for %s"
BLOCKED_BY_MEND = "tool: %s"


def minutes_since(stamp_text, at=None):
    when = lv.parse_stamp(stamp_text)
    return int(((at or now()) - when).total_seconds() // 60) if when else 0


def mend_prompt(a, mend_id):
    return ["Read %s/%s/TOOL-MEND.md whole." % (pathlib.Path(a.tree).as_posix(), NATIVE),
            "Read the fault sheet at %s." % lv.sheet_path(live_dir(a.live), mend_id).as_posix(),
            "Your mend is %s." % mend_id,
            "Report once, at your end: `MENDED %s <main's commit>` or `NOT MENDED %s`. Send no progress note before it." % (mend_id, mend_id)]


def mend_blocks(a):
    """While a mend is open the agent starts no step and lands nothing (AGENT.md section 9): `start` and `land` answer WAIT, as `next` does.
    The step after the one in work may be written meanwhile (`ahead`: cmd_ahead). True when a mend is open."""
    m = lv.open_mend(live_dir(a.live))
    if m is None:
        return False
    say("WAIT: mend %s is open: %s" % (m["id"], m["title"]))
    say("Nothing is started or landed while a mend is open: the step goes on only when main holds the mend. `python -X utf8 -I %s ahead` writes the "
        "step after it meanwhile; `python -X utf8 -I %s next` says what to do now." % (plan_path(a), plan_path(a)))
    return True


def say_mend_wait(a, m):
    """What `next` prints while a mend is open: nothing else."""
    sheet = lv.sheet_path(live_dir(a.live), m["id"])
    plan = plan_path(a)
    say("WAIT: mend %s is open: %s" % (m["id"], m["title"]))
    say("Step in work: %s. Opened %s, %d minutes ago. Nothing is started or landed while a mend is open." % (
        m["step"] if m["step"] != "-" else "none", lv.parse_stamp(m["opened"]).strftime("%H:%M") if lv.parse_stamp(m["opened"]) else m["opened"],
        minutes_since(m["opened"])))
    say()
    say("What to do (AGENT.md section 9):")
    say("1. The sheet is %s; its five headings are filled in." % sheet.as_posix())
    say("2. A helper (the Agent tool, model sonnet, one at a time) is started with these lines; it reports once, at its end:")
    for l in mend_prompt(a, m["id"]):
        say("     " + l)
    say("   Meanwhile you write the step after the one in work: `python -X utf8 -I %s ahead` (it hands nothing in; it refuses when the mend touches a file "
        "that step touches). Nothing is landed until main holds the mend." % plan)
    say("3. MENDED <commit>: read the commit against TOOL-MEND.md section 5, then `python -X utf8 -I %s mend close %s <commit>`." % (plan, m["id"]))
    say("   NOT MENDED, or no result %d hours after its start: look at the gate's line for a landing of `mend/%s` (never two writers); then "
        "`python -X utf8 -I %s mend failed %s \"<why>\"`: it counts the attempts on the sheet (now %s of %d) and says what comes next: "
        "a second helper, then your own attempt, and only after the third the step is blocked." % (
            MEND_WAIT_HOURS, m["id"], plan, m["id"], m.get("attempts") or "0", lv.MEND_ATTEMPTS))
    say("   NOT MENDED, needs game code: `python -X utf8 -I %s mend drop %s \"<the files>\"`: that is no tool fault; the step is yours again." % (plan, m["id"]))


def cmd_mend(a):
    return {"open": mend_open, "land": mend_land, "close": mend_close, "failed": mend_failed, "drop": mend_drop, "list": mend_list}[a.mend_cmd](a)


def last_fault_text(a):
    rows = [r for r in lv.read_faults(live_dir(a.live)) if r["agent"] == agent_of(a)]
    if not rows:
        return None
    r = rows[-1]
    return ["Check: %s (kind %s)%s" % (r["check"], r["kind"], (", test " + r["test"]) if r["test"] else ""),
            "The tool said: %s" % r["first_error"],
            "Run folder: %s" % (r["run_folder"] or "none recorded"), "Recorded %s." % r["time"]]


def sheet_text(m, fault):
    out = ["# %s: %s" % (m["id"], m["title"]), "",
           "Step in work: %s. Opened %s. Written by plan.py mend open; the agent fills in the five headings below, "
           "then starts the helper (docs/plan/native/TOOL-MEND.md)." % (m["step"] if m["step"] != "-" else "none", m["opened"]), ""]
    out += ["## What I ran", "", "(the command, whole, and the folder it ran in)", ""]
    out += ["## What the tool said", ""] + (fault or ["(the command's output, or the run folder of the landing)"]) + [""]
    out += ["## What it should have said, and which document and section says so", "", "(quote the line of the document)", ""]
    out += ["## The files I believe are at fault", "", "(path and function, or `none known`)", ""]
    out += ["## What must stay refused after the mend", "",
            "(what the tool refused rightly before and must refuse after; the helper proves it)", ""]
    return "\n".join(out)


def commit_on_main(a, commit):
    """(True, [paths]) when `commit` is on origin/main (after a fetch): its changed paths; else (False, why)."""
    folder = pathlib.Path(a.tree)
    git(folder, "fetch", "origin")
    if not re.match(r"^[0-9a-fA-F]{7,40}$", commit or ""):
        return False, "%r is no commit id" % commit
    if git(folder, "cat-file", "-e", commit + "^{commit}").returncode != 0:
        return False, "commit %s is not in this repository" % commit
    if git(folder, "merge-base", "--is-ancestor", commit, "origin/main").returncode != 0:
        return False, "commit %s is not on origin/main (a mend is closed only when main has it)" % commit
    names = [l for l in git(folder, "show", "--name-only", "--format=", commit).stdout.splitlines() if l.strip()]
    return True, names


def mend_commit_problem(a, commit, what=": it is not a mend"):
    """None when `commit` is on origin/main and changes a tool or document path (a mend's commit); else the plain line that says why."""
    ok, found = commit_on_main(a, commit)
    if not ok:
        return found
    if not any(p.startswith(MEND_TOOL_PATHS) for p in found):
        return "commit %s changes no tool or document path (%s)%s" % (commit, ", ".join(MEND_TOOL_PATHS), what)
    return None


def repair_end_problem(a, repair_id, mend):
    """None when the repair may be ended by the mend, else the plain line that says why not. `mend`: its row in mends.tsv, or None for a mend
    that is being recorded closed for this repair (mend open --landed). The repair is `open` or `started`; a started one that waits for
    another, open mend is not ended by this one."""
    live = live_dir(a.live)
    rep = next((r for r in lv.read_repairs(live) if r["id"] == repair_id), None)
    if rep is None:
        return "no repair %s (plan.py repair list)" % repair_id
    if mend is not None:
        if mend["state"] != "closed":
            return "mend %s is %s, not closed: a repair is ended only by a mend whose commit is on main (plan.py mend close %s <commit>)" % (
                mend["id"], mend["state"], mend["id"])
        if mend["step"] != repair_id:
            return "mend %s was opened for %s, not for %s" % (mend["id"], "no step" if mend["step"] in ("", "-") else mend["step"], repair_id)
    if rep["state"] not in ("open", "started"):
        return "repair %s is %s already%s: only an open or a started repair is ended by a mend" % (
            repair_id, rep["state"], (" (closed by %s)" % rep["closed"]) if rep["closed"] not in ("", "-") else "")
    reason = (lv.read_state(live).get(repair_id) or {}).get("reason", "")
    for other in lv.read_mends(live):
        if other["state"] == "open" and reason == WAIT_REASON % other["id"] and (mend is None or other["id"] != mend["id"]):
            return "repair %s waits for mend %s, which is open: finish that mend first (mend close, or mend failed)" % (repair_id, other["id"])
    return None


def end_repair_by_mend(a, repair_id, mend_id, title, commit):
    """The one way a repair ends by a mend (mend close, mend open --landed, repair end): the repair is `landed`, closed by the mend's commit,
    with the note `ended by mend M-00N (<title>)`, and it leaves state.tsv (a repair that was never started has no line there). The page,
    `status`, `next` and `repair list` read repairs.tsv and state.tsv, so they show it closed by the mend. A repair of source flake also ends
    the time its test was set aside, as a landing does. Returns the repair's row."""
    live = live_dir(a.live)
    state = lv.read_state(live)
    lv.set_repair(live, repair_id, "landed", commit, note="ended by mend %s (%s)" % (mend_id, title))
    lv.set_state_entry(live, repair_id, "landed", (state.get(repair_id) or {}).get("agent") or "1")
    rep = next(r for r in lv.read_repairs(live) if r["id"] == repair_id)
    end_aside(a, rep)
    return rep


def mend_open(a):
    live = live_dir(a.live)
    words = a.words
    if len(words) > 2:
        say("plan.py: mend open takes [<step ID>] \"<title>\"")
        return 1
    step, title = (words[0], words[1]) if len(words) == 2 else ("", words[0])
    title = " ".join(title.split())
    if not title:
        say("plan.py: a mend needs a title")
        return 1
    if a.landed:
        bad = mend_commit_problem(a, a.landed, "")
        if bad is None and step and is_repair(step):
            bad = repair_end_problem(a, step, None)          # nothing is recorded when the repair cannot be ended
        if bad:
            say("plan.py: " + bad)
            return 1
        row = lv.add_mend(live, step or "-", title, a.check or "-", "closed", a.landed, "recorded after a landing by hand (plan.py itself was at fault)")
        if step and is_repair(step):
            end_repair_by_mend(a, step, row["id"], title, a.landed)
        refresh_page(a)
        say("recorded %s as closed by %s (the sheet, if you wrote one: %s)" % (row["id"], a.landed, lv.sheet_path(live, row["id"]).as_posix()))
        if step and is_repair(step):
            say("Repair %s is ended by the mend: its cause was a test tool, and nothing of the game was left to change. Go on: `python -X utf8 -I %s next`." % (
                step, plan_path(a)))
        return 0
    open_now = lv.open_mend(live)
    if open_now:
        say("plan.py: mend %s is open (%s): finish it first (mend close, or mend failed)" % (open_now["id"], open_now["title"]))
        return 1
    if step:
        s = find(read_steps(a.tree, a.live), step)
        entry = lv.read_state(live).get(step)
        if s is None and not is_repair(step):
            say("plan.py: no step %s" % step)
            return 1
        if is_repair(step) and not any(r["id"] == step for r in lv.read_repairs(live)):
            say("plan.py: no repair %s (plan.py repair list)" % step)
            return 1
        if not entry or entry["state"] != "started":
            say("plan.py: %s is not in work (a mend waits for the step in work: plan.py start %s first), or open the mend with no step" % (step, step))
            return 1
        if a.check:
            same = [m for m in lv.read_mends(live) if m["step"] == step and m["check"] == a.check]
            if len(same) >= MEND_PER_CHECK:
                say("plan.py: step %s has had %d mends for %s (%s): the step is blocked, not mended a third time: plan.py blocked %s \"tool: <the fault>\"" % (
                    step, len(same), a.check, ", ".join(m["id"] for m in same), step))
                return 1
    fault = last_fault_text(a) if a.last else None
    if a.last and fault is None:
        say("plan.py: agent %s has no tool fault recorded (--last)" % agent_of(a))
        return 1
    row = lv.add_mend(live, step or "-", title, a.check or "-")
    sheet = lv.sheet_path(live, row["id"])
    if not sheet.exists():
        atomic_write(sheet, sheet_text(row, fault))
    if step:
        lv.set_state_entry(live, step, "started", (lv.read_state(live).get(step) or {}).get("agent") or "1", WAIT_REASON % row["id"])
    refresh_page(a)
    say("opened %s: %s" % (row["id"], title))
    say("The fault sheet is %s. Fill in its five headings, then start ONE helper (the Agent tool, model sonnet) with these lines and wait (it reports once, at its end):" % sheet.as_posix())
    for l in mend_prompt(a, row["id"]):
        say("  " + l)
    say("%s is not blocked and its time is not counted against the three-failures and four-hours rules while the mend is open." % (step or "No step"))
    return 0


def mend_row(a, mend_id):
    return next((m for m in lv.read_mends(live_dir(a.live)) if m["id"] == mend_id), None)


def mend_close(a):
    live = live_dir(a.live)
    m = mend_row(a, a.id)
    if m is None:
        say("plan.py: no mend %s (plan.py mend list)" % a.id)
        return 1
    if m["state"] == "closed":
        say("plan.py: mend %s is closed already (commit %s)" % (a.id, m["commit"]))
        return 1
    bad = mend_commit_problem(a, a.commit)
    if bad:
        say("plan.py: " + bad)
        return 1
    lv.set_mend(live, a.id, "closed", a.commit)
    state = lv.read_state(live)
    waiting = m["step"] if m["step"] != "-" else ""
    ended = None
    if waiting and is_repair(waiting):
        # A repair whose cause was a test tool has nothing of the game left to change: the mend ends it (the page shows it closed by the mend).
        why_not = repair_end_problem(a, waiting, mend_row(a, a.id))
        if why_not is None:
            ended = end_repair_by_mend(a, waiting, a.id, m["title"], a.commit)
        else:
            say("plan.py: mend %s is closed, but repair %s is not ended by it: %s" % (a.id, waiting, why_not))
    elif waiting and (state.get(waiting) or {}).get("state") == "started":
        lv.set_state_entry(live, waiting, "started", state[waiting]["agent"] or "1", "")
    reopened = []
    for step_id, e in state.items():
        if e["state"] == "blocked" and e["reason"].startswith(BLOCKED_BY_MEND % a.id):
            lv.set_state_entry(live, step_id, "open")
            reopened.append(step_id)
    refresh_page(a)
    plan = plan_path(a)
    if ended is not None:
        say("closed %s (commit %s). Repair %s is ended by the mend: its cause was a test tool, and nothing of the game was left to change." % (
            a.id, a.commit[:9], waiting))
        say("Go on: `python -X utf8 -I %s next`." % plan)
    else:
        say("closed %s (commit %s). %s" % (a.id, a.commit[:9], ("%s is in work again." % waiting) if waiting else "No step was waiting."))
        say("Now, in your work folder: `git fetch origin`; commit what you have (the folder must be clean); `git rebase origin/main` on the step's "
            "branch, so that it is built on the mend (a conflict: AGENT.md section 9, Other code 4, point 1); then `python -X utf8 -I %s land %s`." % (
                plan, waiting or "<the step>"))
    for step_id in reopened:
        say("Open again, blocked by this mend before: %s. `python -X utf8 -I %s next` gives it in its turn; `start %s` takes its branch up "
            "(rebase it onto origin/main, then land)." % (step_id, plan, step_id))
    return 0


ATTEMPT_WHO = {1: "the helper", 2: "the second helper", 3: "the agent itself"}


def add_to_sheet(a, mend_id, lines):
    """Appends lines to the fault sheet (the next helper reads them there)."""
    sheet = lv.sheet_path(live_dir(a.live), mend_id)
    text = sheet.read_text(encoding="utf-8") if sheet.exists() else "# %s\n" % mend_id
    atomic_write(sheet, text.rstrip("\n") + "\n\n" + "\n".join(lines) + "\n")


def mend_failed(a):
    """`mend failed M-00N "why"`: one attempt ended without a mend. The attempts are counted on the sheet: the first helper, a second helper
    with the first one's notes, the agent itself by TOOL-MEND.md; only the third failure blocks the step."""
    live = live_dir(a.live)
    m = mend_row(a, a.id)
    if m is None:
        say("plan.py: no mend %s (plan.py mend list)" % a.id)
        return 1
    if m["state"] != "open":
        say("plan.py: mend %s is %s: nothing is open to fail" % (a.id, m["state"]))
        return 1
    why = " ".join(a.why.split())
    attempts = int(m.get("attempts") or 0) + 1
    plan = plan_path(a)
    add_to_sheet(a, a.id, ["## Attempt %d (%s) ended: NOT MENDED" % (attempts, ATTEMPT_WHO.get(attempts, "attempt %d" % attempts)), "", why])
    if attempts < lv.MEND_ATTEMPTS:
        lv.set_mend(live, a.id, "open", note="attempt %d failed: %s" % (attempts, why), attempts=attempts)
        refresh_page(a)
        say("attempt %d of %d of %s failed; the step keeps waiting." % (attempts, lv.MEND_ATTEMPTS, a.id))
        if attempts == 1:
            say("SECOND HELPER: start one new Sonnet helper with the same lines; the first helper's notes are on the sheet under "
                "\"Attempt 1\" and it reads them there:")
            for l in mend_prompt(a, a.id):
                say("  " + l)
        else:
            say("YOUR OWN ATTEMPT: you mend %s yourself, by docs/plan/native/TOOL-MEND.md to the letter (read it whole first; nothing in it is "
                "relaxed for you): the folder Nebula-mend, the branch mend/%s (an earlier attempt's branch is renamed there first), the sheet "
                "with the two attempts' notes, three hours of your own work, the proofs, and `python -X utf8 -I Nebula-mend/tools/plan/plan.py "
                "mend land %s`. When it fails too: `python -X utf8 -I %s mend failed %s \"<why>\"` again." % (a.id, a.id, a.id, plan, a.id))
        return 0
    step = m["step"] if m["step"] != "-" else ""
    entry = lv.read_state(live).get(step) if step else None
    if entry and entry["state"] == "started":
        b = argparse.Namespace(**vars(a))
        b.id, b.why = step, (BLOCKED_BY_MEND % a.id) + " " + why
        lv.set_state_entry(live, step, "started", entry["agent"] or "1", "")      # the wait is over; `blocked` takes the step from here
        code = cmd_blocked(b)
        if code != 0:
            lv.set_state_entry(live, step, "started", entry["agent"] or "1", WAIT_REASON % a.id)
            return code
    lv.set_mend(live, a.id, "failed", note="attempt %d failed: %s" % (attempts, why), attempts=attempts)
    refresh_page(a)
    say("BLOCKED: %s failed its third attempt%s. The page lists it under \"Waiting for the owner\"." % (a.id, ("; step %s is blocked `%s`" % (step, BLOCKED_BY_MEND % a.id)) if step else ""))
    say("NEXT: `python -X utf8 -I %s next` gives the steps that do not need it. `mend close %s <commit>` opens the step again when a commit on "
        "main mends the tool (the owner's message in this session names it, or `git log origin/main` shows it)." % (plan, a.id))
    return 0


def mend_drop(a):
    """`mend drop M-00N "<the files>"`: the helper reported `NOT MENDED, needs game code`. That is no tool fault: the mend ends, the step
    is the agent's again (it records a ledger row for the fault in the game, as AGENT.md section 10 says)."""
    live = live_dir(a.live)
    m = mend_row(a, a.id)
    if m is None:
        say("plan.py: no mend %s (plan.py mend list)" % a.id)
        return 1
    if m["state"] != "open":
        say("plan.py: mend %s is %s" % (a.id, m["state"]))
        return 1
    why = " ".join(a.why.split())
    add_to_sheet(a, a.id, ["## Dropped: needs game code", "", why])
    step = m["step"] if m["step"] != "-" else ""
    state = lv.read_state(live)
    if step and (state.get(step) or {}).get("state") == "started":
        lv.set_state_entry(live, step, "started", state[step]["agent"] or "1", "")
    lv.set_mend(live, a.id, "dropped", note="needs game code: " + why)
    refresh_page(a)
    say("dropped %s: the fault is in the game's code (%s), not in a tool. %s is yours again: add a ledger row for the fault in the game "
        "(AGENT.md section 10) and go on." % (a.id, why, step or "The step"))
    return 0


def mend_list(a):
    rows = lv.read_mends(live_dir(a.live))
    if not rows:
        say("No mends.")
        return 0
    for m in rows:
        opened = lv.parse_stamp(m["opened"])
        closed = lv.parse_stamp(m["closed"])
        minutes = int(((closed or now()) - opened).total_seconds() // 60) if opened else 0
        say("%s  %-7s step %s, %d min, commit %s: %s%s" % (m["id"], m["state"], m["step"], minutes, m["commit"], m["title"],
                                                        ("  [" + m["note"] + "]") if m["note"] not in ("", "-") else ""))
    return 0


def mend_land(a):
    """`mend land M-00N`, for the helper, from its own folder on its branch mend/M-00N: the pre-checks of `land` that apply to a tool
    (the references, the repository's code checks, the strictness check with the rule for `mend/*` branches), each refused in seconds,
    then the branch goes to the gate with the mend's name. A landing that was cut is joined."""
    live = live_dir(a.live)
    m = mend_row(a, a.id)
    if m is None:
        say("plan.py: no mend %s (plan.py mend list)" % a.id)
        return 1
    if m["state"] == "closed":
        say("plan.py: mend %s is closed already" % a.id)
        return 1
    folder = pathlib.Path(a.tree)
    branch = "mend/" + a.id
    agent_folder = work_folder(a.work_root, 1)
    if folder.resolve() == agent_folder.resolve():
        say("plan.py: run the plan.py of your own folder (Nebula-mend), never the agent's: this one is %s" % folder.as_posix())
        return 1
    why = work_folder_problem(a, folder, branch)
    if why:
        say("plan.py: " + why)
        return 1
    rec = active_landing(a.live, "mend", getattr(a, "queue_file", None))
    if rec and rec["branch"] == branch:
        say("joined the landing of %s, in the queue since %s" % (branch, hhmm(rec.get("queue_time", rec["time"]))))
        join = True
    else:
        join = False
        git(folder, "fetch", "origin")
        groups = []
        bad = refs_problems(folder)
        if bad:
            groups.append(["plan.py: a file or line named in the documents or the commit messages does not exist:"] + ["  " + l for l in bad])
        bad = code_check_problems(folder)
        if bad:
            groups.append(["plan.py: a rule of the repository refuses the mend (the gate's repo/code runs the same checks):"] + list(bad))
        bad = strictness_problems(folder, branch, "mend %s" % a.id)
        if bad:
            groups.append(bad)
        if groups:
            for g in groups:
                for l in g:
                    say(l)
            return 1

    def passed(verdict, seconds, summary):
        lv.set_mend(live, a.id, m["state"], note="landed as %s" % (verdict.commit or "?"))
        refresh_page(a)
        say("LANDED %s as %s in %d s. Verdict by kind: %s." % (branch, verdict.commit or "(commit not read)", seconds, summary or "no check rows read"))
        say("Report `MENDED %s %s`." % (a.id, verdict.commit or "<main's commit>"))
        return 0

    extra = ["--who", "mend %s" % a.id]
    rep = next((r for r in lv.read_repairs(live) if r["id"] == m["step"]), None) if is_repair(m["step"]) else None
    also = also_for_check(rep["check"]) if rep else []
    if also:
        # The mend was opened for a repair: the check that failed runs in this landing, as a repair's own landing has it (--also).
        extra += ["--also", ",".join(also)]
        if not join:
            say("the repair %s failed in %s: this landing runs it as an extra check (%s)." % (rep["id"], rep["check"], ", ".join(also)))
    elif rep and not join:
        say("the repair %s failed in %s, which the gate cannot add to a landing: the landing runs the checks its files ask for, and the "
            "mend's own self-test is the proof." % (rep["id"], rep["check"]))
    return hand_to_gate(a, folder, branch, branch, "mend", extra, passed, join=join)


ALSO_OF_CHECK = (("build/windows-asan", "sanitizer/tour"), ("unit/game-asan", "sanitizer/tour"), ("tour-asan/", "sanitizer/tour"),
                 ("mods-asan/", "sanitizer/tour"), ("stress/", "stress"), ("soak/long", "soak/long"), ("soak", "soak"),
                 ("replays/software", "replays/software"), ("speed/anchor", "speed/anchor"), ("replays/rates", "replays/rates"))


def also_for_check(check):
    """The `--also` names that make the gate run a failed check of a repair in a landing ([] when it has none: the gate's list of what
    a landing can add is ALSO_CHECKS and the main level's own checks; the nightly's sanitizer checks come with `sanitizer/tour`)."""
    for prefix, name in ALSO_OF_CHECK:
        if check == prefix or (prefix.endswith("/") and check.startswith(prefix)):
            return [name]
    return []


def cmd_check(a):
    """The pre-checks of `land` alone on the work folder's tree: every thing that would refuse the step, all at once; nothing is written,
    committed or queued."""
    agent = agent_of(a)
    folder = work_folder(a.work_root, agent)
    own_folder = folder.is_dir()
    if not own_folder:
        folder = pathlib.Path(a.tree)
    on = head_branch(folder)
    item = branch_item(on) if on else None
    steps = read_steps(a.tree, a.live)
    s = find(steps, item[1]) if item and item[0] == "step" else None
    if s is None:
        say("plan.py: the folder %s is on %s, which is no step's branch (native/<ID>); check is for a step in work" % (folder.as_posix(), on or "no branch"))
        return 1
    groups = prechecks(a, s, folder, on, first_only=False)
    info = []
    if getattr(a, "no_units", False):
        info.append("unit tests: not run (--no-units); run `check` whole before `land`")
    elif not own_folder:
        info.append("unit tests: not run (the agent's work folder %s is not there; they are run in it)" % work_folder(a.work_root, agent).as_posix())
    else:
        # GitHub's Check runs the unit tests: so does this, every one that can run on this PC (units.py); the rest is named as not proven here.
        failed, info = units.check(folder, build_folder(a, agent), build_first=not getattr(a, "no_build", False))
        groups = groups + failed
    if not groups:
        say("check: nothing of what `land %s` checks before it queues refuses %s as it stands (the gate's own checks are still to come)." % (s["step"], on))
    else:
        say("check: %d thing%s would refuse `land %s`:" % (len(groups), "" if len(groups) == 1 else "s", s["step"]))
        for g in groups:
            say()
            for l in g:
                say(l)
    if info:
        say()
        for l in info:
            say(l)
    return 1 if groups else 0


DECISION_COLUMNS = ["date", "step", "what", "because", "doc", "replaces"]


def numbered_decisions(tree):
    """[row] of every row of decisions.tsv, each with `number` (`<step>#<n>`: n counts the step's rows in the file's order, from 1) and
    `replaced_by` (the number of the later row that names it in its `replaces`, or "")."""
    rows = lv.read_rows(native(tree) / "decisions.tsv", DECISION_COLUMNS)
    count = {}
    for r in rows:
        count[r["step"]] = count.get(r["step"], 0) + 1
        r["number"] = "%s#%d" % (r["step"], count[r["step"]])
        r["replaced_by"] = ""
    by = {r["number"]: r for r in rows}
    for r in rows:
        old = by.get(r["replaces"])
        if old is not None:
            old["replaced_by"] = r["number"]
    return rows


def decision_number(text, step_id):
    """`3` or `S0.2.1#3` as the number of a decision: `<step>#<n>` (a bare n is the step's own)."""
    text = (text or "").strip()
    return text if "#" in text else "%s#%s" % (step_id, text)


def cmd_decide(a):
    """A decision of the agent (AGENT.md section 7): one line of docs/plan/native/decisions.tsv, which lands with the step. A later decision
    replaces an earlier one by naming its number (`--replaces`): the file keeps both, the page and `--list` show the newest. `--list`: the
    step's decisions as they stand, with their numbers."""
    steps = read_steps(a.tree, a.live)
    if find(steps, a.id) is None and not is_repair(a.id):
        say("plan.py: no step %s" % a.id)
        return 1
    rows = numbered_decisions(a.tree)
    if a.list:
        shown = [r for r in rows if r["step"] == a.id and not r["replaced_by"]]
        for r in shown:
            say("%s  %s: %s (because %s)%s" % (r["number"], r["date"], r["what"], r["because"], ("  [replaces %s]" % r["replaces"]) if r["replaces"] else ""))
        if not shown:
            say("%s has no decision." % a.id)
        return 0
    what = " ".join((a.what or "").split())
    because = " ".join((a.because or "").split())
    if not what or not because:
        say("plan.py: a decision has a text and a reason")
        return 1
    replaces = ""
    if a.replaces:
        replaces = decision_number(a.replaces, a.id)
        old = next((r for r in rows if r["number"] == replaces), None)
        if old is None:
            say("plan.py: no decision %s to replace (plan.py decide %s --list shows the numbers)" % (replaces, a.id))
            return 1
        if old["replaced_by"]:
            say("plan.py: decision %s is replaced already, by %s: replace that one" % (replaces, old["replaced_by"]))
            return 1
    row = [datetime.date.today().isoformat(), a.id, what, because, " ".join((a.doc or "-").split()), replaces]
    path = native(a.tree) / "decisions.tsv"
    text = path.read_text(encoding="utf-8") if path.exists() else "\t".join(DECISION_COLUMNS) + "\n"
    first, _, rest = text.partition("\n")
    if replaces and "replaces" not in first.split("\t"):
        text = "\t".join(DECISION_COLUMNS) + "\n" + rest             # the file's old rows have five cells: a short row reads as no replaces
    line = "\t".join(row if replaces else row[:5])
    atomic_write(path, text.rstrip("\n") + "\n" + line + "\n")
    number = "%s#%d" % (a.id, sum(1 for r in rows if r["step"] == a.id) + 1)
    say("decided %s: %s: %s (because %s) -> %s%s; it lands with the step: commit the file." % (
        number, a.id, what, because, path.as_posix(), (", replacing %s" % replaces) if replaces else ""))
    return 0


def read_decisions(tree):
    """[{date, step, what, because, doc, replaces, number}] of the plan tree's decisions.tsv that stand: a decision a later one replaces is not
    among them (a missing file has none)."""
    return [r for r in numbered_decisions(tree) if not r["replaced_by"]]


def read_strictness_log(tree):
    """[{date, file, what changed, why, allowed by, id, removed by}] of docs/plan/strictness-log.tsv as the tree has it."""
    path = pathlib.Path(tree) / "docs" / "plan" / "strictness-log.tsv"
    return lv.read_rows(path, list(strictness.LOG_COLUMNS))


def owner_questions(steps, mends=()):
    """[{step, title, question}] of the steps blocked with `owner: <the question>` and the mends that failed three times: they wait for the
    owner, and the page lists them apart."""
    return ([{"step": s["step"], "title": s["title"], "question": s["reason"][len("owner:"):].strip()}
             for s in steps if s["state"] == "blocked" and (s["reason"] or "").startswith("owner:")]
            + [{"step": "-", "title": m["title"], "question": page.mend_owner_text(m)} for m in mends if m["state"] == "failed"])


def run_state(a, at=None):
    """`working`, `ended` or `stale` (AGENT.md section 14): done or stopped by section 13 is `ended`; a command of the last three hours, or
    a landing or a mend landing of the agent in the gate's line, is `working`."""
    note = read_note(a.live) or ""
    steps = read_steps(a.tree, a.live)
    if re.match(r"^(Done|Stopped)\b", note) or (steps and all(s["state"] == "landed" for s in steps)):
        return "ended"
    age = lv.heartbeat_age_hours(live_dir(a.live), at)
    if age is not None and age <= lv.HEARTBEAT_HOURS:
        return "working"
    qf = getattr(a, "queue_file", None)
    if active_landing(a.live, "1", qf) or active_landing(a.live, "mend", qf):
        return "working"
    return "stale"


def cmd_alive(a):
    say(run_state(a))
    return 0


def cmd_status(a):
    fetch_main_landed(a)
    settle_github(a)
    real = live_dir(a.live).as_posix() == pathlib.Path(LIVE_REAL).as_posix()      # a test's own live folder shows no record of the real gate
    text = status_text(a.tree, a.work_root, live=a.live, agent=agent_of(a), queue_file=getattr(a, "queue_file", None),
                       gate_runs=GATE_RUNS if real else None)
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
            say("progress branch: " + push_progress(repo, written))
        except Exception as e:  # noqa: BLE001 - a failed push is a warning, never the command's result
            say("plan.py: warning: the progress branch was not pushed: %s: %s" % (type(e).__name__, " ".join(str(e).split())[:200]))
    else:
        say("progress branch: not pushed (--no-push or %s)." % PUSH_OFF)
    return 0


def parser():
    p = argparse.ArgumentParser(prog="plan.py", description=__doc__.split("\n")[0])
    p.add_argument("--tree", default=str(TREE), help="the plan tree (default: the tree this file is in)")
    p.add_argument("--work-root", default=WORK_ROOT)
    p.add_argument("--build-root", default=BUILD_ROOT, help="the folder the agents' build folders are in (the self-tests and plants use their own)")
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
    sub.add_parser("ahead", parents=[who], help="while a landing plays: the branch and the card of the step that follows it, on top of "
                   "the commit being tested").set_defaults(fn=cmd_ahead)
    sub.add_parser("page").set_defaults(fn=cmd_page)
    sk = sub.add_parser("stack", parents=[who], help="the writer of a section: the branch and the card of the step after the last one "
                        "written, on its tip; nothing lands (mend M-007)")
    sk.add_argument("--list", action="store_true", help="the stack's rows; changes nothing")
    sk.set_defaults(fn=cmd_stack)
    wt = sub.add_parser("wait", parents=[who], help="the lander: one line when the landing of ID ends, or after N minutes")
    wt.add_argument("id")
    wt.add_argument("--minutes", type=float, default=WAIT_MAX, help="0 to %d (default %d)" % (WAIT_MAX, WAIT_MAX))
    wt.add_argument("--log", help="the land command's output file: its last line is shown")
    wt.set_defaults(fn=cmd_wait)
    ho = sub.add_parser("handoff", help="the path of a step's hand-off sheet (made with its headings) and what it lacks")
    ho.add_argument("id")
    ho.set_defaults(fn=cmd_handoff)
    hd = sub.add_parser("hard", parents=[who], help="the lander: a failure it may not fix; the step is blocked for the fixer")
    hd.add_argument("id")
    hd.add_argument("line")
    hd.set_defaults(fn=cmd_hard)
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
    sub.add_parser("commands", parents=[who], help="the agent's commands for its folder: build, one unit test, one engine test, "
                   "the tour, the replay references").set_defaults(fn=cmd_commands)
    nt = sub.add_parser("note", help="the line `Now: ...` under the count of the progress page and STATUS.md")
    nt.add_argument("text", nargs="?", help="the line (%d characters at most)" % NOTE_MAX)
    nt.add_argument("--clear", action="store_true", help="empty the note")
    nt.set_defaults(fn=cmd_note)
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
    md = sub.add_parser("mend", help="a tool at fault is mended by one helper while the agent waits (docs/plan/native/TOOL-MEND.md)")
    msub = md.add_subparsers(dest="mend_cmd", required=True)
    mo = msub.add_parser("open", parents=[who], help="open a mend: the fault sheet is written and the step in work waits for it")
    mo.add_argument("words", nargs="+", help="[<step ID>] \"<title>\"")
    mo.add_argument("--last", action="store_true", help="the sheet starts from the agent's last tool fault")
    mo.add_argument("--check", default="", help="the check at fault (at most %d mends of one step for one check)" % MEND_PER_CHECK)
    mo.add_argument("--landed", default="", metavar="COMMIT", help="record a mend that was landed by hand (plan.py itself was at fault)")
    ml = msub.add_parser("land", parents=[who], help="for the helper: the mend's branch goes to the gate after the pre-checks")
    ml.add_argument("id")
    mc = msub.add_parser("close", parents=[who], help="the mend is on main: the waiting step goes on, the steps it blocked are open again")
    mc.add_argument("id")
    mc.add_argument("commit")
    mf = msub.add_parser("failed", parents=[who], help="one attempt ended without a mend: counts it and says what comes next; the third "
                         "blocks the waiting step `tool: M-00N`")
    mf.add_argument("id")
    mf.add_argument("why")
    mdp = msub.add_parser("drop", parents=[who], help="the fault needs game code: no tool fault, the waiting step is the agent's again")
    mdp.add_argument("id")
    mdp.add_argument("why")
    msub.add_parser("list", parents=[who], help="every mend with its state, step, minutes and commit")
    md.set_defaults(fn=cmd_mend)
    dc = sub.add_parser("decide", parents=[who], help="a decision of the agent: one line of docs/plan/native/decisions.tsv")
    dc.add_argument("id")
    dc.add_argument("what", nargs="?", help="the decision, in one sentence")
    dc.add_argument("--because", help="the reason (required with a decision)")
    dc.add_argument("--doc", default="", help="the file and section that settle it")
    dc.add_argument("--replaces", default="", metavar="N", help="the number of an earlier decision this one corrects (N, or <step>#N): "
                    "the file keeps both, the page shows this one")
    dc.add_argument("--list", action="store_true", help="the step's decisions that stand, with their numbers; changes nothing")
    dc.set_defaults(fn=cmd_decide)
    sub.add_parser("alive", parents=[who], help="one word: working, ended or stale").set_defaults(fn=cmd_alive)
    ck = sub.add_parser("check", parents=[who], help="what `land` checks before it queues, and the unit tests GitHub's Check runs that this PC "
                        "can run, on the folder as it is: nothing is queued")
    ck.add_argument("--no-units", action="store_true", help="only what `land` checks before it queues (quick, while you write); run `check` whole before `land`")
    ck.add_argument("--no-build", action="store_true", help="run the unit tests of the build folder as it stands, without building first (the self-tests use it)")
    ck.set_defaults(fn=cmd_check)
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
    rn = rsub.add_parser("end", help="a repair whose cause was a test tool is ended by its closed mend")
    rn.add_argument("id", help="the repair, R-00N")
    rn.add_argument("--mend", required=True, help="the closed mend that was opened for this repair, M-00N")
    rn.set_defaults(fn=cmd_repair)
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
    noprompt.install()                 # no git prompt and no crash box for anything this tool starts (tools/gate/noprompt.py)
    a = parser().parse_args(argv)
    if a.cmd != "alive":
        lv.write_heartbeat(live_dir(getattr(a, "live", None)))      # every command writes its time; `alive` reads it
    MAIN_LANDED.clear()
    del MAIN_READ[:]
    if getattr(a, "agent", None) is not None:
        why = own_copy_refusal(a.agent, TREE, a.work_root)
        if why:
            say(why)
            return 2
    if a.cmd not in ("next", "start", "land", "status", "card", "ahead"):
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
