"""gate.py: the one check of a Nebula tree, at the four levels of docs/plan/04-GATE-AND-MAIN.md. Every check that
starts the game for a test goes through here.

    python -X utf8 -I tools/gate/gate.py --quick | --main [--move] | --long | --final  [options]

--quick  level 1: the repository checks, the Windows build, the unit and Rust tests, a start on each renderer, the
         self-tests, the mod tests and the engine tests on Direct3D 12, 12 tour stops with pictures and step hashes,
         two stops of each other disc. Before asking for a main gate.
--main   level 2, what moves main: everything deterministic. Every renderer's engine tests and 102-stop tour with
         pictures and step hashes, the other discs, the mod tests on every renderer, the Android build, the tablet's
         short part (the 12 stops, their step hashes against the PC's at the tablet's screen size) and every `must` job of
         GitHub's Check run of the same commit (tools/gate/github_jobs.tsv: Mac, Linux, Linux unit tests, both sanitizer
         jobs, Rust on both systems; a job the table does not name counts as must; the Android builds are nightly: a plan with
         the group `android` builds the app here), planned.tsv's
         github/<job> lines aside.
         --move: main moves to this commit when it passes (section 5). It is the landing runner's own call (gate.py land) and carries
         the runner's mark; given by hand it is refused: main moves only through gate.py land <branch>. It needs a whole quick gate of the
         commit passed, the branch pushed and main an ancestor of it; it takes the gate lock (or waits --wait minutes),
         and on a pass fast-forwards main and pushes it itself, writes MAIN-MOVED on the board and frees the lock.
         With --move, when the plan has the group `github` or `github-first` (tools/gate/scope.tsv; a full run has `github-first`), the commit
         is pushed first as gate/<run> (section 5): GitHub runs every job on it while the gate runs (a red `must` job stops the run),
         and the gate frees gate.lock when its own checks pass. With `github-first` (files this machine cannot build: the Metal renderer,
         the Apple and Linux parts, the CMake files, the workflow) it waits up to 120 minutes for every `must` job to be green for the
         commit (any push run of it; the newest result wins), then moves main and deletes gate/<run>. With `github` alone it reads GitHub
         once: a job with its end is judged now (red: main does not move); when some still run it prints "GitHub's jobs for <sha> are read
         after the move", moves main, and leaves a line in <live>/github-pending.tsv; gate/<run> stays until settle_github_pending (called by
         gate.py land and by the step tool's next, status and land) has read the jobs: green removes the line, a red `must` job opens a
         repair, 120 minutes with no result is a line of tool-faults.tsv. A cancelled job, or one red at a set-up (`uses:`) step, is run
         again once, and the limit gives env (not judged). --github-again: every red `must` job is run again once before the start check,
         each a tool-fault line (ENV). `land --github first` waits for GitHub in a landing whose plan has only `github` (the step tool
         does it for a repair of a red job). A plan with neither pushes no candidate, reads nothing from GitHub and moves main when its own
         checks pass (tools/gate/github_rule.py).
         A picture with no approved copy (kind new), or one the branch declares in PICTURES-CHANGE.txt, never fails a run:
         when the checks have passed and GitHub is green, the gate approves them before it moves main, as one commit on
         top of the tested one that touches only tests/pictures/approved.tsv (and removes PICTURES-CHANGE.txt), runs the
         picture checks again on the same runs (all must pass, else nothing moves: tool), and moves main to that commit.
         After the move the long run starts if none started in the last 20 hours. No speed measure starts at a move: a landing that
         plays the game has measured itself: measure/step/pc is the pair of the landing's build against the kept base (tools/gate/speedbase.py,
         section 10.2; the candidate becomes the base after a move that judged it), measure/step/tablet the single measure (its trusted
         lines are kept in the history after the move); the nightly run measures too.
--long   level 3, on main's newest commit, under the long lock, at below-normal priority: the tablet's 102 stops
         against the PC's, a sanitizer build's tour, the soak, every disc on every renderer. Its parts are pieces of at most seven
         minutes that give way to landings (tools/gate/long_parts.py); no speed part. At its end: tools/gate/long_results.py.
long-start --why TEXT [--sha COMMIT] [options]: the long run of a commit (default origin/main), started by hand the way a move starts it, without
         the 20-hour rule; the reason is written in <work>/long-started.txt. A long lock held by a process that runs, or a start of the last 10 minutes, stops it.
long-results RUN_FOLDER [--dry]: what follows from a long run's results: repairs, tool-fault lines, the report, new pictures.
--final  level 4: the main and long levels on one commit. "Done" needs it passed with no check planned or known.
--only CHECK[,CHECK]: only the checks those shell patterns name, and the checks they need (for tracing a red main).
speed-base make|anchor|show: the kept builds of the speed check (tools/gate/speedbase.py, docs/plan/04-GATE-AND-MAIN.md section 10.2).
land BRANCH [BRANCH ...] | land --queue: the landing queue (tools/gate/landing.py, docs/plan/04-GATE-AND-MAIN.md section 15): the
         branches wait in D:/NebulaWork/plan/landing-queue.tsv, the runner replays them on origin/main in the gate's own work
         folder, runs the checks of the groups the union of their changed files needs (tools/gate/scope.tsv) once, moves main, and
         halves the batch to find the branch at fault. Exit 0 landed, 1 fail, 2 not judged, 3 the same failure three times.
--scope GROUPS|auto|full: with --main (--move too), only the checks of these groups; what `land` runs on the batch's tip.
--also NAMES: with --scope, and with `land` (a step's proof that asks for more): more checks, by name (comma-separated): the
         nightly's parts in ALSO_PARTS (stress, sanitizer/tour, soak, soak/long, replays/software, replays/rates, speed/anchor: the pair against
         the anchor), or any check of the main level.
         They run after the checks the groups need have passed (a failed landing runs none), before measure/step/pc, one after
         another, as the nightly run runs them (every stop of a tour), so a landing with --also runs its groups' checks at that
         depth too. `stress` asked so counts a stress result as a fail. A name of the table whose part is not on main yet is refused
         with "--also NAME: this check is not built yet"; a name that is none of them is unknown; either refuses the landing before
         anything runs (exit 2).
--mods off|short|full: the mod suites and LumaLink's check; off in tools/gate/scope.tsv (they run in no landing and no nightly).
data list | adopt | path DISC | make DISC | clean: the game data by key (tools/gate/gamedata.py).
--serial: the parts that start the game one after another; by default they run side by side (section 7).

The machine (tools/gate/machine.py, docs/plan/04-GATE-AND-MAIN.md section 15): the gate is in one Windows job object that ends everything
it started when it ends. A run that starts the game first checks the machine (machine_ready): leftover game processes ended, drive D:
has the room the level needs (old run folders thinned first), an answering tablet holds this tree's test data (a tablet away is owed, not a stop), the graphics card and
the PC have room for at least one game copy. What cannot be met is env (a row machine/<point>); the run plays as many game copies at
once as fit (70% of the card's free memory and of the PC's, at most what the lanes ask for) and says "card shared" when other programs
hold more than half the card.

Every check and every test ends as one kind (tools/gate/kinds.py; docs/plan/04-GATE-AND-MAIN.md section 9):
  pass   it passed;
  fail   the game or the change is wrong: the only kind that fails a run;
  tool   the test tool broke: the runner or a script raised, a result file is missing or cannot be read, a check ran
         another number of cases than it declares, or none;
  env    the machine was not fit: the tablet away, the drive full, another graphics card than the approved pictures
         name, a lock not had in time;
  flake  it failed, then passed when run again alone, or the runner played it once more at once after a fault of the tool's
         that one more play can clear (luma_test.py RETRIED) and it passed (a line in <plan>/native/flakes.tsv); the third flake of one test
         and renderer in seven days sets the test aside (<plan>/native/set-aside.tsv): it still runs and is reported,
         its fail does not count;
  main   it failed again alone and fails the same way on main's kept build (<build>/base-bins/<commit>): main is at
         fault, not the change;
  new    a picture with no approved copy, or one the change declares (tools/gate/pictures.py): it never fails a run, the
         landing approves it;
  worse  the speed measure of a landing (measure/step/pc, measure/step/tablet) found a figure past its band twice in a row: it
         fails the run like fail, is never run again alone and never a flake.
After every lane has ended, each test that is fail is run once more alone, then on main's build (settle).
Each check's state: those kinds, and
  known    it failed only in tests tools/gate/known-failures.tsv lists for it (each with an open ledger row); a listed
           test that passes fails the check: its line must go;
  planned  it is not built yet and tools/gate/planned.tsv lists it for this level; a listed check that runs fails;
  owed     env of the tablet's part, when the tablet does not answer, its lock stays held, or it leaves during the run:
           the part runs on main's newest commit first thing when the tablet is back; the board says so. It does not
           change the verdict.

It writes one result (JSON and text, side by side), verdict.json, one line in <work>/history.tsv and the owner's page
(tools/gate/status.py, --status). Its last lines: one `KIND <kind> <check> <test> :: <message>` for each result that
is not a pass, then `VERDICT pass` (exit 0), `VERDICT fail` (a fail is left; exit 1) or `VERDICT not-judged` (no fail,
but a tool or env; exit 2: the check is run again once the tool is fixed or the machine is fit). Exit 2 also when it
could not start (bad options, a lock held, a rule of section 5 not met). Main moves only on VERDICT pass. A run whose checks
passed and whose move of main did not happen ends `VERDICT fail` (exit 1) only when a job of GitHub is red; for every other reason (main
moved, the push could not reach GitHub, a lock, GitHub not finishing) nothing was judged: `VERDICT not-judged`, exit 2.
"""
import argparse
import contextlib
import ctypes
import datetime
import fnmatch
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import coverage as coverage_tool  # noqa: E402
import gate_tour  # noqa: E402
import gamedata  # noqa: E402
import github_rule  # noqa: E402
import kinds  # noqa: E402
import landing  # noqa: E402
import lockholder  # noqa: E402
import long_parts  # noqa: E402
import machine  # noqa: E402
import noprompt  # noqa: E402
import pictures as picture_tool  # noqa: E402
import replays_rates  # noqa: E402
import replays_software  # noqa: E402
import scope  # noqa: E402
import speedbase  # noqa: E402
import strictness  # noqa: E402
import step_hash  # noqa: E402
import judged  # noqa: E402
import sanitizer  # noqa: E402
import stress  # noqa: E402
import validate  # noqa: E402

LEVELS = ("quick", "main", "long", "final")
# A landing's own level (tools/gate/scope.py, gate.py land): the checks of the groups a change needs, the 12 quick stops of
# a tour, no GitHub check of its own (the landing waits for GitHub as a move does, when its plan has the group `github`). The parser has no flag for it: --scope
# makes a --main run this level unless the change needs everything (then it is the main level).
LAND_LEVEL = "land"
SHORT_LEVELS = ("quick", LAND_LEVEL)
# The US game data by key (tools/gate/gamedata.py): the checks that run the game on it need it besides the build; the other
# discs' data is asked for the same way by the checks that run their tours.
US_DISC = "RMGE01"
DATA = ("build/windows", "data/RMGE01")
# The twelve stops --quick plays, and the tablet's short part: one per kind of stage (a hub, water, gravity, a boss,
# sand, ice, lava, a 2D stage, a race, the fleet, the heaviest stop of the tour, the factory). Names as
# tools/verify/tours/tour_enterable.txt lists them.
QUICK_STOPS = [
    "HoneyBeeKingdomGalaxy 1", "OceanRingGalaxy 1", "EggStarGalaxy 1", "KoopaBattleVs1Galaxy 1",
    "SandClockGalaxy 1", "IceVolcanoGalaxy 1", "HellProminenceGalaxy 1", "TeresaMario2DGalaxy 1",
    "SurfingLv1Galaxy 1", "CannonFleetGalaxy 1", "OceanPhantomCaveGalaxy 4", "FactoryGalaxy 1",
]
# The other regions' discs: their tours run on Direct3D 12 at the main level, since a region changes the game's data
# (text, layouts, fonts), not what a renderer does with it; the long run plays them on every renderer. --quick plays
# two of their stops (a hub and the heaviest).
REGION_DISCS = ("RMGP01", "RMGJ01")
QUICK_REGION_STOPS = ["HoneyBeeKingdomGalaxy 1", "OceanPhantomCaveGalaxy 4"]
RENDERERS = ("d3d12", "vulkan", "d3d11")
# The checks of a quick gate that a main gate's --without-quick still needs passed first: the build, the pak and the
# flags are right before an hour is spent.
QUICK_FIRST = ("build/windows", "unit/windows", "start/d3d12", "engine/d3d12")
# The checks of the tablet's part, in the order they run.
TABLET_CHECKS = ("tablet/prepare", "android/tablet", "tour/tablet", "tablet/remove", "tour/tablet-pc", "replays/tablet")
# GitHub's push workflow (.github/workflows/check.yml). A move whose plan has the group `github` needs every `must` job
# (tools/gate/github_jobs.tsv) green for the commit (section 8), waited for outside gate.lock for up to GITHUB_WAIT_MINUTES.
# A main gate without --move and the final check read the same jobs, for up to the same --github-wait.
WORKFLOW = "check.yml"
GITHUB_WAIT_MINUTES = 120
# What the readings of GitHub wait with and read the time from (tests put fakes in these).
GITHUB_SLEEP = time.sleep
GITHUB_CLOCK = time.time
WATCH_SECONDS = 60.0
CANDIDATE_PREFIX = "gate/"
# The two lists (section 4) and the ledger their lines name.
KNOWN = HERE / "known-failures.tsv"
PLANNED = HERE / "planned.tsv"
LEDGER = TREE / "docs" / "plan" / "ledger.tsv"
KNOWN_COLUMNS = ("check", "test", "ledger row", "first seen", "what")
PLANNED_COLUMNS = ("check", "level", "plan step or ledger row", "owner")
CLOSED = ("fixed", "not-a-fault", "decided", "out-of-scope")
# The runner, and where a run keeps what lies outside the repository, so that a test run never changes the tree: the
# flakes and the set-aside list (kinds.py), and main's kept builds (a copy of the bin folder each time the gate moves
# main, the two newest kept). Tests of this file put their own folders in these.
RUNNER = TREE / "tools" / "lumacore" / "luma_test.py"
NATIVE = pathlib.Path("D:/NebulaWork/plan/native")
FLAKES = NATIVE / "flakes.tsv"
SET_ASIDE = NATIVE / "set-aside.tsv"
MACHINE_JSON = NATIVE / "machine.json"
BASE_BINS = pathlib.Path("D:/NebulaWork/build/base-bins")
MAIN_REF = "origin/main"
# The picture store (the approved pictures' files, outside the repository): tests of this file put their own in it.
PICTURE_STORE = picture_tool.STORE


# The two lists ----------------------------------------------------------------------------------------------------


def read_tsv(path, columns):
    """The rows of a list (a header line of `columns`, '#' lines are comments): (rows as dicts, problems)."""
    rows, problems = [], []
    lines = pathlib.Path(path).read_text(encoding="utf-8").splitlines() if pathlib.Path(path).exists() else []
    body = [(n, l) for n, l in enumerate(lines, 1) if l.strip() and not l.startswith("#")]
    if not body or tuple(body[0][1].split("\t")) != columns:
        return [], ["%s: the first line is the header: %s" % (pathlib.Path(path).name, "\\t".join(columns))]
    for n, line in body[1:]:
        cells = line.split("\t")
        if len(cells) != len(columns) or not all(c.strip() for c in cells):
            problems.append("%s:%d: %d cells, every one filled" % (pathlib.Path(path).name, n, len(columns)))
            continue
        rows.append(dict(zip(columns, (c.strip() for c in cells))))
    return rows, problems


def ledger_states(path=LEDGER):
    """{row id: state} of the ledger."""
    out = {}
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines()[1:] if pathlib.Path(path).exists() else []:
        cells = line.split("\t")
        if len(cells) > 3:
            out[cells[0]] = cells[3]
    return out


LEDGER_ID = re.compile(r"^[A-Z]+-\d+$")


def read_lists(known_path=KNOWN, planned_path=PLANNED, ledger_path=LEDGER):
    """(known {check: {test: row}}, planned {check: row}, problems). Every line names an open ledger row (a planned
    line may name a plan step instead, e.g. M6); a check or test is listed once; a level is one of LEVELS."""
    states = ledger_states(ledger_path)
    known_rows, problems = read_tsv(known_path, KNOWN_COLUMNS)
    planned_rows, more = read_tsv(planned_path, PLANNED_COLUMNS)
    problems += more

    def open_row(what, row_id, may_be_step):
        if LEDGER_ID.match(row_id):
            if row_id not in states:
                problems.append("%s: ledger row %s is not in the ledger" % (what, row_id))
            elif states[row_id] in CLOSED:
                problems.append("%s: ledger row %s is %s: the line goes with the commit that closes it" % (
                    what, row_id, states[row_id]))
        elif not may_be_step:
            problems.append("%s: %r is not a ledger row" % (what, row_id))

    known = {}
    for r in known_rows:
        what = "known-failures.tsv %s %s" % (r["check"], r["test"])
        open_row(what, r["ledger row"], False)
        if r["test"] in known.setdefault(r["check"], {}):
            problems.append(what + ": listed twice")
        known[r["check"]][r["test"]] = r
    planned = {}
    for r in planned_rows:
        what = "planned.tsv %s" % r["check"]
        open_row(what, r["plan step or ledger row"], True)
        if r["level"] not in LEVELS:
            problems.append("%s: level %r is not one of %s" % (what, r["level"], ", ".join(LEVELS)))
        if r["check"] in planned:
            problems.append(what + ": listed twice")
        if r["check"] in known:
            problems.append(what + ": also in known-failures.tsv (a check is built or it is not)")
        planned[r["check"]] = r
    return known, planned, problems


def kind_of_verdict(verdict):
    """The kind of a runner verdict (luma_test.py kind_of, the one place it is decided), for a result that names none."""
    global _RUNNER_MODULE
    if _RUNNER_MODULE is None:
        import importlib.util
        spec = importlib.util.spec_from_file_location("luma_test_kinds", TREE / "tools" / "lumacore" / "luma_test.py")
        _RUNNER_MODULE = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_RUNNER_MODULE)
    return _RUNNER_MODULE.kind_of(verdict)


_RUNNER_MODULE = None


def judge(passed, detail, verdicts, known):
    """(state, detail) of a check's outcome: `verdicts` {test: verdict} when the check runs tests (None when it does
    not), `known` its known-failures lines {test: row}."""
    if verdicts is None:
        return ("pass" if passed else "fail"), detail
    cases = kinds.cases_of_verdicts(verdicts, kind_of_verdict)
    records, state = kinds.judge_cases(cases, known, (), passed)
    return state, with_notes(detail, records)


def with_notes(detail, records):
    """A check's detail with what its listed tests say: known ones, and a listed test that passes (its line must go)."""
    known = ["%s (%s)" % (r["test"], r["note"][7:-1]) for r in records if r["note"].startswith("known (")]
    listed = sorted(r["test"] for r in records if r["message"].startswith("passes but is listed"))
    if listed:
        return detail + "; passes but is listed in known-failures.tsv, remove the line: " + ", ".join(listed)
    return detail + ("; known: " + ", ".join(known) if known else "")


def level_has(level, planned_level):
    """Whether a check planned for `planned_level` belongs to a run of `level`."""
    return planned_level == level or (level == "final" and planned_level in ("main", "long", "final"))


# The run ----------------------------------------------------------------------------------------------------------


# How a state reads in a row: the kinds that need a person's eye in capitals (tools/plan/plan.py reads these words).
LABELS = {"fail": "FAIL", "worse": "WORSE", "tool": "TOOL", "env": "ENV", "flake": "FLAKE", "new": "NEW", "stress": "STRESS"}
NOT_PASSED = ("fail", "worse", "tool", "env")
STATES = ("pass", "known", "planned", "owed", "fail", "worse", "tool", "env", "flake", "main", "stress", "new")


def default_records(state, detail):
    """What a row without tests reports: nothing for a state that is a pass; the owed tablet is env that does not count;
    a fail, tool or env is its own one result."""
    if state == "owed":
        return [kinds.record("-", "env", detail, counts=False, note="owed")]
    if state in NOT_PASSED:
        return [kinds.record("-", state, detail)]
    return []


def table_mods():
    """`mods = off | short | full` of tools/gate/scope.tsv ('off' when the table cannot be read: the table's own check says so)."""
    try:
        return scope.read_table().mods
    except scope.ScopeError:
        return "off"


class Gate:
    """One run: its checks' results. A dry run (dry=True) runs no check: it learns the level's check names and what
    each needs, for --only."""

    def __init__(self, args, dry=False, known=None, planned=None, selected=None):
        self.args = args
        self.level = args.mode
        self.dry = dry
        self.results = []
        self.needs = {}
        self.known = known or {}
        self.planned = planned or {}
        self.selected = selected
        self.cases = {}
        self.picture_jobs = []
        self.picture_calls = []
        self.partial = set()          # (label, renderer) of the tours that play only the stops a declaration names: no picture of theirs is missing
        self.new_pictures = []
        self.runs_held = False
        self.tree = TREE
        self.mods = getattr(args, "mods", None) or table_mods()
        self.data_paks = {}   # disc -> its game.pak, once the data check found or made it
        self.main_ref = MAIN_REF
        self.open_rows = []
        self.flake_results = []       # the tests the run set aside: one repair is opened for each (stress.open_flake_repairs)
        self.played_again = []        # (check, Cases, records) with a test the runner played once more and that passed (kinds.PLAYED_AGAIN)
        self.aside = kinds.read_aside(SET_ASIDE)
        self.verdict = None
        self.github = getattr(args, "github_form", None)   # how a landing meets GitHub: "first" (waited), "after" (read after the move)
        self.printed_head = None      # the summary line last printed on the screen (late rows print it again)
        self.lines = []
        self.started = time.time()
        self.commit = git("rev-parse", "--short", "HEAD")
        self.dirty = git("status", "--porcelain") != ""
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.name = "%s-%s-%s" % (stamp, self.commit, args.mode)
        self.work = pathlib.Path(tempfile.mkdtemp()) if dry else pathlib.Path(args.work) / self.name
        self.work.mkdir(parents=True, exist_ok=True)
        self.bin = str(pathlib.Path(args.build_root) / "bins")
        self.android_bin = str(pathlib.Path(args.build_root) / "android" / "test-bin-arm64-v8a")
        self.lock = threading.Lock()
        self.local = threading.local()
        # The machine (machine_ready): what the run's summary says of it, how many game copies run at once (None: no limit set), and
        # the thread that reads what the game processes hold of the card.
        self.machine_notes = []
        self.layer_seen = {}          # the validate lane: {renderer: the layer error ids its first runs showed}
        self.measure_pending = []     # the landing's measures' pending files (measure.py --pending): kept when main moves
        self.measure_faulted = set()  # the checks that already left a line in tool-faults.tsv in this run
        self.speed = {}               # the speed checks' outcome for the move (judged, owed, rebase), by check name
        self.tablet_screen = None     # the tablet's screen size (width, height) as the tablet's lane read it at its start, or None
        self.tablet_screen_read = threading.Event()   # set once the tablet's lane has read (or failed to read) it
        self.android_over = threading.Event()         # set when the Android build is over (or will never run): the PC may start its last checks
        self.tablet_pc_screen = None  # the size tour/tablet-pc played at
        self.tablet_tail_ok = False   # the tablet's part reached its end (its PC half is to be played or compared)
        self.speed_ratios = {}        # speed/anchor's judged ratios {renderer: {figure: ratio}}, kept with the move
        self.card_shared = False
        self.slots = None
        self.sampler = None
        self.lanes_seen = 0
        self.part_log = []            # the long run: [{"part", "waited", "held"}] for each piece that took the machine (locked_part)
        self.carried = {}             # a check whose tests ran in pieces: the seconds the pieces took, added to its row
        self.pieces = {}              # the long run: {(label, renderer): PiecedRun} of the checks whose tests run in pieces
        self.android_built = False

    def game_slot(self, weight=1):
        """A game copy's place among the copies that may run at once (machine_ready): `with g.game_slot(): ...` around a program
        that starts the game on this computer. No limit before machine_ready has set one."""
        return contextlib.nullcontext() if self.slots is None else self.slots.holding(weight)

    @property
    def pak(self):
        """The US game.pak the run plays on: --pak when it names one, else the data found or made by key (data/RMGE01)."""
        return self.args.pak or self.data_paks.get(US_DISC, "")

    def paks_text(self):
        """LUMA_TEST_PAKS: the other discs' game.paks, DISC=path joined by the path separator (--paks, else those found by key)."""
        if self.args.paks:
            return self.args.paks
        return os.pathsep.join("%s=%s" % (d, p) for d, p in sorted(self.data_paks.items()) if d != US_DISC)

    def rows(self):
        """The rows of the lane this thread runs (lanes), else the run's."""
        sink = getattr(self.local, "sink", None)
        return self.results if sink is None else sink

    def lanes(self, lanes):
        """Runs each lane (a function that runs its checks in order) side by side, each in a thread of its own; the
        checks are programs, so they run at once. Their rows join the results in the lanes' order, so a result reads the
        same however the lanes interleaved. --serial runs them one after another (for the proof that both give the same
        pictures and step hashes, section 7). lanes_seen is the most game copies the lanes of one call ask for at once."""
        self.lanes_seen = max(self.lanes_seen, sum(getattr(lane, "copies", 1) for lane in lanes))
        if self.dry or self.args.serial or len(lanes) < 2:
            for lane in lanes:
                lane()
            return
        sinks = [[] for _ in lanes]

        def go(i):
            self.local.sink = sinks[i]
            try:
                lanes[i]()
            except Exception as e:  # noqa: BLE001 - a lane that breaks is the gate's own fault: tool, said plainly
                self.record("lane/%d" % i, "tool", 0.0, "%s: %s" % (type(e).__name__, e))
        threads = [threading.Thread(target=go, args=(i,), name="lane-%d" % i) for i in range(len(lanes))]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.results.extend(row for sink in sinks for row in sink)

    def lanes_with_tail(self, lanes, tablet_lanes, tail, wait_for_cpu_lane=True):
        """B14 part 3: `lanes` (the PC's) and `tablet_lanes` run side by side as in lanes(); when the PC's lanes have ended and the Android
        build is over, `tail()` runs in this thread (the PC's last checks, one at a time, with no other game copy of the gate on the PC) while the
        tablet's lane goes on; the call ends when everything has. Rows: the PC's lanes' first, then the tail's, then the tablet lanes'.
        `wait_for_cpu_lane` False: the lane of the tools' self-tests (part "self") is not waited for."""
        every = list(lanes) + list(tablet_lanes)
        self.lanes_seen = max(self.lanes_seen, sum(getattr(lane, "copies", 1) for lane in every))
        if self.dry or self.args.serial or len(every) < 2:
            for lane in every:
                lane()
            self.android_over.set()
            tail()
            return
        sinks = {id(l): [] for l in every}
        tail_sink = []

        def go(lane):
            self.local.sink = sinks[id(lane)]
            try:
                lane()
            except Exception as e:  # noqa: BLE001 - a lane that breaks is the gate's own fault: tool, said plainly
                self.record("lane/%s" % getattr(lane, "__name__", "?"), "tool", 0.0, "%s: %s" % (type(e).__name__, e))
            finally:
                if lane in tablet_lanes:
                    self.android_over.set()
        threads = {id(l): threading.Thread(target=go, args=(l,), name="lane-%d" % i) for i, l in enumerate(every)}
        for thread in threads.values():
            thread.start()
        waited = [l for l in lanes if wait_for_cpu_lane or getattr(l, "part", "") != "self"]
        for lane in waited:
            threads[id(lane)].join()
        self.results.extend(row for l in lanes if l in waited for row in sinks[id(l)])
        self.android_over.wait()
        self.local.sink = tail_sink
        try:
            tail()
        except Exception as e:  # noqa: BLE001 - the tail breaking is the gate's own fault: tool, said plainly (the tablet's lane is still joined)
            self.record("lane/tail", "tool", 0.0, "%s: %s" % (type(e).__name__, e))
        finally:
            self.local.sink = None
        for lane in lanes:
            if lane not in waited:
                threads[id(lane)].join()
        for lane in tablet_lanes:
            threads[id(lane)].join()
        self.results.extend(tail_sink)
        self.results.extend(row for l in lanes if l not in waited for row in sinks[id(l)])
        self.results.extend(row for l in tablet_lanes for row in sinks[id(l)])

    def wanted(self, name, needs=()):
        if self.dry:
            self.needs.setdefault(name, set()).update(needs)
        return self.selected is None or name in self.selected

    def record(self, name, state, seconds, detail, log=None, records=None, pending=False):
        row = {"check": name, "state": state, "passed": state not in NOT_PASSED, "seconds": seconds, "detail": detail,
               "log": str(log) if log is not None and pathlib.Path(log).exists() else None,
               "records": default_records(state, detail) if records is None else records}
        if pending:
            row["pending"] = True
        with self.lock:
            self.rows().append(row)
            print("%-34s %-7s %7.1f s  %s" % (name, "again" if pending else LABELS.get(state, state), seconds, detail),
                  flush=True)

    def check(self, name, fn, needs=()):
        """Runs one check. fn returns (passed, detail), or (passed, detail, tests) where tests is a kinds.Cases or
        {test: verdict}, or (passed, detail, tests, kind) where `kind` is what a failure of this check is (tool or env
        where it is not the change's: the default is fail; `worse` the speed measure's; `owed` a part that cannot run now and
        does not change the verdict). An exception is the tool's: kind tool, with its text. True
        unless it did not pass; None when --only leaves it out. A check whose tests failed is run again once every lane has
        ended (settle): its row is first printed `again`."""
        if not self.wanted(name, needs):
            return None
        if self.dry:
            return True
        t0 = time.time() - self.carried.pop(name, 0.0)
        log = self.work / ("%s.log" % name.replace("/", "-").replace(" ", "-"))
        cases = None
        try:
            out = fn(log)
            passed, detail = out[0], out[1]
            tests = out[2] if len(out) > 2 else None
            fail_kind = out[3] if len(out) > 3 and out[3] else "fail"
            if isinstance(tests, kinds.Cases):
                cases = tests
            elif tests is not None:
                cases = kinds.cases_of_verdicts(tests, kind_of_verdict)
            if cases is not None:
                records, state = kinds.judge_cases(cases, self.known.get(name, {}), self.aside, bool(passed))
                detail = with_notes(detail, records) if detail else kinds.describe(cases, records)
                if any(r["note"] == kinds.PLAYED_AGAIN for r in records):
                    self.played_again.append((name, cases, records))  # its flake lines are written in settle, once every lane has ended
            elif fail_kind == "owed" and not passed:
                state, records = "owed", default_records("owed", detail)
            else:
                state = "pass" if passed else fail_kind
                records = [] if passed else [kinds.record("-", fail_kind, detail)]
        except Exception as e:  # noqa: BLE001 - a check that raised is the tool's fault (a full drive: the machine's)
            state = "env" if kinds.drive_full(e) else "tool"
            detail = "%s%s: %s" % ("the drive is full: " if state == "env" else "", type(e).__name__, e)
            records = [kinds.record("-", state, detail)]
            cases = None
        if name in self.planned:
            state, detail = "fail", "it runs, so its line in planned.tsv must go; " + detail
            records = [kinds.record("-", "fail", detail)]
        pending = (cases is not None and cases.source is not None and any(kinds.needs_second_run(r) for r in records))
        if pending:
            self.cases[name] = cases
        self.record(name, state, round(time.time() - t0, 1), detail, log, records, pending=pending)
        return state not in NOT_PASSED

    def missing(self, name, why, needs=()):
        """A check that exists and cannot run here: a failure, said plainly."""
        if self.wanted(name, needs) and not self.dry:
            self.record(name, "fail", 0.0, "not run: " + why)

    def owe(self, name, why, needs=()):
        """A part of the tablet's that could not run because the tablet is away: owed, not failed (section 4.3)."""
        if self.wanted(name, needs) and not self.dry:
            self.record(name, "owed", 0.0, why)

    def owe_last(self, why):
        """The last check failed because the tablet left: owed."""
        row = self.rows()[-1]
        if row["state"] in ("fail", "env"):
            row["state"], row["passed"] = "owed", True
            row["detail"] = "%s; %s" % (why, row["detail"])
            row["records"] = default_records("owed", row["detail"])
            row.pop("pending", None)
            print("%-34s %-7s (%s)" % (row["check"], "owed", why), flush=True)

    def settle(self):
        """After every lane has ended: each test that is fail is run once more alone, a pass is flake; one that fails
        again is run alone on main's build. The rows' states are settled and printed again. A test the runner played once
        more after a fault of the tool's and that then passed (kinds.PLAYED_AGAIN) gets its line in the flakes file here, as
        a flake of a second run does."""
        pending = [r for r in self.results if r.get("pending")]
        if self.dry or not (pending or self.played_again):
            return
        settler = kinds.Settler(FLAKES, SET_ASIDE, self.commit, say=lambda text: print("gate.py: " + text, flush=True))
        for name, cases, records in self.played_again:
            for r in records:
                if r["note"] == kinds.PLAYED_AGAIN:
                    settler.flake(name, cases, r["test"], r["message"])
        for row in pending:
            cases = self.cases[row["check"]]
            if STOPPED.is_set():
                self.open_rows.append("the second runs of %s were not made: the run was stopped" % row["check"])
                records = row["records"]
            else:
                records = settler.settle(row["check"], cases, row["records"])
            row["records"] = records
            row["state"] = kinds.state_of(records)
            row["passed"] = row["state"] not in NOT_PASSED
            row["detail"] = kinds.describe(cases, records)
            row.pop("pending", None)
            print("%-34s %-7s %7.1f s  %s" % (row["check"], LABELS.get(row["state"], row["state"]), row["seconds"],
                                              row["detail"]), flush=True)
        self.open_rows += settler.open_rows
        self.flake_results += settler.flake_results
        if settler.stress_results:
            # The stress lane's results: the run folder is kept whole, and one repair is opened for each (never a fault of the run).
            stress.open_repairs(getattr(self.args, "live", NATIVE), self.work, self.commit, settler.stress_results,
                                say=lambda text: print(text, flush=True))
        live = getattr(self.args, "live", "")
        if settler.flake_results and live:
            # The third flake of a test in seven days sets it aside and opens one repair (source flake): find whether the test or the game is at fault.
            stress.open_flake_repairs(live, self.work, self.commit, settler.flake_results, say=lambda text: print(text, flush=True))
        self.aside = kinds.read_aside(SET_ASIDE)

    def plan_rows(self):
        """The planned checks of this level that --only keeps."""
        ran = {r["check"] for r in self.results}
        for name, row in sorted(self.planned.items()):
            if level_has(self.level, row["level"]) and self.wanted(name) and not self.dry and name not in ran:
                self.record(name, "planned", 0.0, "not built yet: %s (%s)" % (row["plan step or ledger row"], row["owner"]))

    def counts(self):
        out = {s: 0 for s in STATES}
        for r in self.results:
            out[r["state"]] = out.get(r["state"], 0) + 1
        return out

    def write(self):
        if not self.results:
            self.record("gate/none", "tool", 0.0, "no check ran")
        if not self.dry:
            machine_learn(self)
            self.new_pictures = picture_tool.gather(self.work, PICTURE_STORE)
        out, path, head, aside_line, new_line = self._save()
        passed = self.verdict == "pass"
        seconds = out["seconds"]
        append_history(pathlib.Path(self.args.work) / "history.tsv", out, getattr(self.args, "covers", ""))
        if passed and not self.args.keep_runs and not self.args.out:
            if self.new_pictures and getattr(self.args, "move", False):
                # The approval at the landing runs the picture checks again on these folders (main()): pruned after it.
                self.runs_held = True
            else:
                self.prune_runs()
        if self.args.record:
            # Kept with the tree (docs/plan/gate-runs): what the push workflow cannot run is proved by these.
            keep = TREE / "docs" / "plan" / "gate-runs" / self.name
            keep.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, keep.with_suffix(".json"))
            shutil.copyfile(path.with_suffix(".txt"), keep.with_suffix(".txt"))
            print("recorded:", keep.with_suffix(".txt"))
        for line in aside_line + new_line:
            print("gate.py: " + line)
        for text in self.open_rows:
            print("gate.py: ledger row to open: " + text)
        for text in self.machine_notes:
            print("gate.py: machine: " + text)
        print(head)
        self.printed_head = head
        print("result:", path)
        for line in self.lines:
            print(line)
        return passed

    def _save(self):
        """The verdict, the exit code, the KIND lines and the run's files (result.json, verdict.json, the text) from the rows as
        they are now. Returns (the result as written, its path, the head line, the set-aside line, the new-pictures line)."""
        self.verdict = kinds.verdict_of([r["records"] for r in self.results])
        self.code = kinds.EXIT_OF[self.verdict]
        passed = self.verdict == "pass"
        counts = self.counts()
        seconds = round(time.time() - self.started, 1)
        self.lines = kinds.kind_lines(self.results)
        aside = sorted("%s %s" % k for k in self.aside)
        out = {"gate": 2, "mode": self.level, "commit": self.commit, "dirty": self.dirty, "run": self.name,
               "only": self.args.only or None,
               "started": datetime.datetime.fromtimestamp(self.started).isoformat(timespec="seconds"),
               "seconds": seconds, "passed": passed, "verdict": self.verdict, "counts": counts, "checks": self.results}
        if self.machine_notes:
            out["machine"] = list(self.machine_notes)
        if self.github:
            out["github"] = self.github
        path = pathlib.Path(self.args.out) if self.args.out else self.work / "result.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
        self.write_verdict()
        head = "gate %s %s%s%s: %s (%d checks: %s; %.0f s)" % (
            self.level, self.commit, " (uncommitted changes)" if self.dirty else "",
            " only " + self.args.only if self.args.only else "", self.verdict.upper(), len(self.results),
            ", ".join("%d %s" % (n, s if s != "fail" else "failed") for s, n in counts.items() if n), seconds)
        aside_line = ["set aside (still run, do not count): " + ", ".join(aside)] if aside else []
        new_line = [self.new_pictures_line()] if self.new_pictures else []
        lines = aside_line + new_line + [head] + ["  machine: " + n for n in self.machine_notes] + [
            "  %-34s %-7s %s" % (r["check"], r["state"], r["detail"]) for r in self.results]
        path.with_suffix(".txt").write_text("\n".join(lines + self.lines) + "\n", encoding="utf-8")
        return out, path, head, aside_line, new_line

    def late(self, name, state, detail, log=None, records=None):
        """A check that ends after the run's files were written (GitHub's, after the local checks): its row joins the results;
        once the run has been written the verdict, the exit code and the files follow, and the row's KIND lines are printed
        (the run's own last lines name them again for the caller)."""
        self.record(name, state, 0.0, detail, log, records)
        if self.verdict is None:
            return
        head = self._save()[2]
        for line in kinds.kind_lines([self.results[-1]]):
            print(line, flush=True)
        # The files carry the new row's count and seconds: the last summary line on the screen says the same.
        if head != self.printed_head:
            self.printed_head = head
            print(head, flush=True)

    def prune_runs(self):
        """The passed tests' working files are not evidence once the run passed (N-1060): the drive keeps its room."""
        import prune_runs
        tests, freed = prune_runs.prune(self.work)
        self.runs_held = False
        if tests:
            print("gate.py: %d passed tests' working files removed (%.1f GB); --keep-runs keeps them" % (tests, freed / 1e9))

    def new_pictures_line(self):
        """What the summary says of the pictures that have no approved copy (or that the change declares)."""
        declared = sum(1 for e in self.new_pictures if e["kind"] == "declared")
        return "%d new picture%s%s: look at %s; the landing approves them" % (
            len(self.new_pictures), "" if len(self.new_pictures) == 1 else "s",
            " (%d declared in %s)" % (declared, picture_tool.CHANGES_NAME) if declared else "",
            self.work / "new-pictures" / "sheet" / "review.html")

    def write_verdict(self, verdict=None, why=""):
        """verdict.json in the run's folder: the verdict, and every result that is not a pass (the same facts as the KIND
        lines of the run's end). `verdict` and `why` say when the run's last word is not its checks' (main not moved)."""
        results = [dict(check=r["check"], **rec) for r in self.results for rec in r["records"]]
        body = {"verdict": verdict or self.verdict, "checks_verdict": self.verdict, "why": why, "run": self.name,
                "level": self.level, "commit": self.commit, "exit": kinds.EXIT_OF[verdict or self.verdict],
                "lines": self.lines, "results": results, "machine": list(self.machine_notes),
                "set_aside": sorted("%s %s" % k for k in self.aside), "open_rows": self.open_rows,
                "new_pictures": {"count": len(self.new_pictures), "list": str(self.work / "new-pictures.tsv"),
                                 "sheet": str(self.work / "new-pictures" / "sheet" / "review.html")}
                if self.new_pictures else None,
                "checks": [{"check": r["check"], "state": r["state"], "detail": r["detail"]} for r in self.results]}
        (self.work / "verdict.json").write_text(json.dumps(body, indent=2) + "\n", encoding="utf-8")


HISTORY_COLUMNS = ("started", "run", "level", "commit", "result", "seconds", "pass", "known", "planned", "owed",
                   "failed", "only", "checks", "covers", "moved")
# `covers` and `moved` came with package B15: what a landing's run covered (the branches) and, in a line of level `move`, the commit
# main moved to. A file written before them is read as it is (long_results.read_history).


def append_history(path, out, covers=""):
    """One line per run in gate-runs/history.tsv: when, which, the result and every check's state and seconds; `covers`: the branches
    the run answers for (a landing's)."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    new = not path.exists()
    c = out["counts"]
    cells = (out["started"], out["run"], out["mode"], out["commit"] + ("+" if out["dirty"] else ""),
             out["verdict"], "%.0f" % out["seconds"], c["pass"], c["known"], c["planned"],
             c["owed"], c["fail"] + c["worse"], out["only"] or "-",
             " ".join((["github-form=" + out["github"]] if out.get("github") else []) +
                      ["%s=%s/%.0f" % (r["check"], r["state"], r.get("seconds", 0.0)) for r in out["checks"]]),
             covers or "-", "-")
    with open(path, "a", encoding="utf-8") as f:
        if new:
            f.write("\t".join(HISTORY_COLUMNS) + "\n")
        f.write("\t".join(str(x) for x in cells) + "\n")


def append_move(path, run, moved, covers, now=None):
    """After a landing has moved main: one line of level `move` in history.tsv: when, the run, the commit main moved to and the branches
    the landing covered. It is the record that tracing a failure of the long run to a landing reads (tools/gate/long_results.py)."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    new = not path.exists()
    now = now or datetime.datetime.now()
    cells = (now.isoformat(timespec="seconds"), run, "move", moved, "pass", "0", "0", "0", "0", "0", "0", "-", "", covers or "-", moved)
    with open(path, "a", encoding="utf-8") as f:
        if new:
            f.write("\t".join(HISTORY_COLUMNS) + "\n")
        f.write("\t".join(str(x) for x in cells) + "\n")


# Git for Windows when it is there: another git on PATH (an msys2 one without autocrlf) reports every file changed.
GIT = next((p for p in ("C:/Program Files/Git/cmd/git.exe",) if os.path.exists(p)), "git")


def git(*args):
    r = subprocess.run([GIT, "-C", str(TREE), *args], capture_output=True, text=True)
    return r.stdout.strip()


# A run's checks stop when GitHub has a red job of the commit (GithubWatch): every command running is ended with its
# children, and one asked after is not started.
STOPPED = threading.Event()
STOPPED_WHY = [""]
CHILDREN = set()
CHILDREN_LOCK = threading.Lock()


def end_tree(process):
    """Ends a process and every process it started."""
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True)
    else:
        process.kill()


def stop_runs(why):
    """Ends every command the checks run now and starts no other (the run then fails)."""
    STOPPED_WHY[0] = why
    STOPPED.set()
    with CHILDREN_LOCK:
        running = list(CHILDREN)
    for process in running:
        end_tree(process)


# Per lane thread: below_normal starts a lane's programs at below-normal priority (the CPU-only checks beside the game
# lanes, so a game run never waits for a compiler).
LANE = threading.local()


def run(cmd, log, env=None, cwd=None, timeout=None):
    """Runs cmd with its output in log; returns the exit code (124: timed out; 125: stopped, stop_runs). In a lane that
    asks it (LANE.below_normal), the program and its children run at below-normal priority."""
    with open(log, "ab") as f:
        f.write(("$ %s\n" % " ".join(str(c) for c in cmd)).encode())
        f.flush()
        if STOPPED.is_set():
            f.write(("(not started: %s)\n" % STOPPED_WHY[0]).encode())
            return 125
        e = dict(os.environ)
        e.update(env or {})
        flags = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0) if getattr(LANE, "below_normal", False) else 0
        process = subprocess.Popen([str(c) for c in cmd], stdout=f, stderr=subprocess.STDOUT, env=e, cwd=cwd,
                                   creationflags=flags)
        with CHILDREN_LOCK:
            CHILDREN.add(process)
        if STOPPED.is_set():
            end_tree(process)   # stopped between the check above and the program's start: stop_runs did not see it
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            end_tree(process)
            process.wait()
            f.write(b"\n(timed out)\n")
            return 124
        finally:
            with CHILDREN_LOCK:
                CHILDREN.discard(process)
        if STOPPED.is_set():
            f.write(("\n(stopped: %s)\n" % STOPPED_WHY[0]).encode())
            return 125
        return code


# Checks ---------------------------------------------------------------------------------------------------------


def settings_table(g):
    """tools/settings_table.py --check: the settings table against the engine's readers, the apps and the game's menu."""
    def fn(log):
        code = run([sys.executable, "-X", "utf8", "-I", TREE / "tools" / "settings_table.py", "--check"], log, cwd=TREE)
        return code == 0, last_line(pathlib.Path(log).read_text(encoding="utf-8", errors="replace"))
    return fn


def repo_audit(g):
    """The tracked files (tools/audit_repo.ps1): no game content, build outputs, oversized files or credentials."""
    def fn(log):
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", TREE / "tools" / "audit_repo.ps1"],
                   log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        return code == 0, last_line(text)
    return fn


# The code checks (P-043): each needs only the tree and Python, and GitHub's Linux unit job runs the same three.
CODE_CHECKS = ("console_census.py", "cast_check.py", "byte_order_check.py")


def code_checks(tree=TREE, scripts=CODE_CHECKS, log=None):
    """The tree's code checks (tools/verify): the console census (closed groups read zero), cast_check (no number read
    by casting a pointer in the game) and byte_order_check (no meaning that depends on the byte order). (passed, what
    each said), each script's output in `log`."""
    said, ok = [], True
    for name in scripts:
        script = pathlib.Path(TREE) / "tools" / "verify" / name
        if log is not None:
            code = run([sys.executable, "-X", "utf8", "-I", script, tree], log, cwd=str(tree))
            text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace").split("$ ")[-1]
        else:
            r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(script), str(tree)], capture_output=True,
                               text=True, cwd=str(tree))
            code, text = r.returncode, r.stdout + r.stderr
        lines = [l.strip() for l in text.splitlines()[1 if log is not None else 0:] if l.strip()]
        ok = ok and code == 0
        said.append("%s %s" % (name[:-3], "pass" if code == 0 else "FAILED: " + (lines[0] if lines else "(no output)")[:120]))
    return ok, "; ".join(said)


def repo_code(g):
    def fn(log):
        return code_checks(TREE, log=log)
    return fn


def repo_strictness(g):
    """A change to a place that can make a check see less needs its line in docs/plan/strictness-log.tsv (tools/gate/strictness.py). Each
    branch the run covers is judged against main on its own; a `mend/*` branch may not change the game's source. A run on main's own
    newest commit has no change and passes."""
    def fn(log):
        names = [n.strip() for n in str(getattr(g.args, "covers", "") or "").split(",") if n.strip()]
        try:
            lines = strictness.check_branches(TREE, MAIN_REF, names)
        except strictness.StrictnessError as error:
            return False, "strictness.py could not judge: %s" % error, None, "tool"
        text = ("\n".join(lines) + "\n") if lines else "no change in a listed place without its line in the log\n"
        pathlib.Path(log).write_text(text, encoding="utf-8")
        return (not lines), (lines[0] if lines else "no change in a listed place without its line in the log")
    return fn


def parse_scripts(g):
    """Every tracked PowerShell and Python script parses (tools/gate/parse_scripts.py): a script that does not fails
    only when someone runs it."""
    def fn(log):
        code = run([sys.executable, "-X", "utf8", "-I", HERE / "parse_scripts.py", "--tree", TREE], log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        return code == 0, last_line(text)
    return fn


# What build_windows.ps1 (build_steps.ps1) prints when a step failed only in the compiler cache's own client and was run again, and
# when it failed so twice (exit 2).
CACHE_AGAIN = "job(s) failed in the compiler cache, not in the compiler; built again"
NOT_JUDGED_BUILD = "build not judged: the compiler cache failed twice in the same way, not the compiler"


def cache_again(g, text, check="build/windows"):
    """One line of tool-faults.tsv (kind TOOL) for each time the build was run again by itself because its compiler cache failed,
    so the planner sees how often it happens. Never fails the check."""
    lines = [l.strip() for l in text.splitlines() if CACHE_AGAIN in l]
    live = getattr(g.args, "live", None)
    if not lines or not live:
        return
    agent = getattr(g.args, "agent", None) or os.environ.get("NEBULA_AGENT", "1")
    for line in lines:
        try:
            fault_writer(live)(agent, "", "TOOL", check, "", line, str(g.work), could_not="nothing: the build was run again by itself")
        except Exception as e:  # noqa: BLE001 - the record is a report; the build's result stands without it
            print("gate.py: the tool-fault record could not be written (%s: %s)" % (type(e).__name__, e), flush=True)


STEP_RESULT = re.compile(r"^(\S[^:]*): exit (-?\d+), (\d+) FAILED, \d+ s\s*$")
COMPILER_MESSAGE = re.compile(r" error C|error LNK|: error:|fatal error")


def build_failure(text):
    """(the number of failed jobs, what failed) of build_windows.ps1's output: the count is the script's own, the sum of its steps'
    `N FAILED`; the text is `<step>: <first diagnostic line>`, the first indented line under the first failed step that holds a
    compiler or linker message, else that step's first `FAILED:` line, else the last line (last_line), cut to 200 characters. It is
    what the failure's signature is made of (three failures in a row of the same text stop the step), so it names the job."""
    lines = text.splitlines()
    total, first, count = 0, None, 0
    for i, line in enumerate(lines):
        m = STEP_RESULT.match(line)
        if m is None:
            continue
        failed = int(m.group(3))
        total += failed
        if first is None and (int(m.group(2)) != 0 or failed):
            first = (m.group(1), i)
        count += 1
    if first is None:
        return (total if count else None), last_line(text)
    step, at = first
    under = []
    for line in lines[at + 1:]:
        if not line.startswith(" ") or not line.strip():
            break
        under.append(line.strip())
    said = (next((l for l in under if COMPILER_MESSAGE.search(l) and not l.startswith("sccache: error")), None) or next((l for l in under if l.startswith("FAILED:")), None))
    return total, ("%s: %s" % (step, said) if said else last_line(text))[:200]


def build_windows(g, root=None, bin_dir=None, sanitize="", coverage=False):
    """The game modules (clang) and every Windows program with the unit tests and the test extension (MSVC), into the
    build root; then the bin folder the harness runs. `sanitize`: the game modules with those sanitizers, `coverage`:
    counting the lines and functions they run, each in a build root and bin folder of its own."""
    root, bin_dir = root or g.args.build_root, bin_dir or g.bin

    def fn(log):
        machine.ensure_sccache_server()     # the compiler cache's server, out of the run's job (shared with other builds)
        seed = ["-SeedFrom", g.args.seed_from] if g.args.seed_from else []
        # The build refuses no data any more: with --pak it names that pak, else it names none and the data check (data/RMGE01)
        # asks for the right data by key and writes the bin folder's pak.path.
        pak = ["-Pak", g.args.pak] if g.args.pak else []
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", HERE / "build_windows.ps1",
                    "-Tree", TREE, "-Root", root, "-Bin", bin_dir, *pak, "-Jobs", g.args.jobs, *seed]
                   + (["-Sanitize", sanitize] if sanitize else []) + (["-Coverage"] if coverage else []), log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        fails = [l for l in text.splitlines() if l.startswith("FAILED:")]
        cache_again(g, text, "build/windows-asan" if sanitize else "build/windows-coverage" if coverage else "build/windows")
        if code == 2 and NOT_JUDGED_BUILD in text:
            # The compiler cache's own client failed twice and no compiler said anything: not the change's fault, not counted.
            cached = next((l for l in text.splitlines() if CACHE_AGAIN in l), "")
            return False, "%s%s" % (NOT_JUDGED_BUILD, ("; " + cached) if cached else ""), None, "tool"
        if code != 0 or fails:
            count, why = build_failure(text)
            return False, "exit %d, %d FAILED: %s" % (code, count if count is not None else len(fails), why)
        if g.pak and not g.args.pak:
            write_pak_path(bin_dir, g.pak)    # another build of this run (the sanitizer's, the coverage one's): the data found already
        return True, "bins at %s" % bin_dir
    return fn


def write_pak_path(bin_dir, pak):
    """bin/pak.path: the game.pak the harness runs the bin folder's programs on (build_windows.ps1 writes it for --pak)."""
    (pathlib.Path(bin_dir) / "pak.path").write_text("%s\n" % pak, encoding="ascii")


def data_check(g, disc):
    """The game data of a disc by key (tools/gate/gamedata.py): <disc>-<store version>-<importer tree hash> found in
    D:/NebulaWork/build/game-data, else made there with this build's importer from the disc's image. For the US disc it also
    names the data in the bin folder's pak.path. A disc image not on this machine is env, an importer that is not there tool,
    an importer whose result is wrong fail."""
    def fn(log):
        importer = pathlib.Path(g.bin) / "nebula-import.exe"
        try:
            pak, how, key = gamedata.GameData().ensure(disc, TREE, importer, log)
        except gamedata.DataError as error:
            return False, str(error), None, error.kind
        g.data_paks[disc] = str(pak)
        if disc == US_DISC and pathlib.Path(g.bin).is_dir():
            write_pak_path(g.bin, pak)
        return True, "%s %s" % (key, how)
    return fn


def unit_tests(g):
    def fn(log):
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", HERE / "ctest.ps1",
                    "-Dir", pathlib.Path(g.args.build_root) / "msvc"], log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        summary = next((l.strip() for l in text.splitlines() if "tests passed" in l), last_line(text))
        return counted(code, summary, text)
    return fn


def counted(code, summary, text):
    """(passed, summary[, None, kind]) of a ctest run: one that ran no test is tool, never a pass. ctest's summary says
    'N tests failed out of M'."""
    m = re.search(r"out of (\d+)", text)
    if m is None or int(m.group(1)) == 0:
        return False, "no test ran: " + summary, None, "tool"
    return code == 0, summary


def game_tests(g, root=None):
    """The game's own unit tests (native/tests), built with the game modules. `root`: the build root they were built in (the
    gate's own by default; the sanitizer build's is its own, unit/game-asan)."""
    def fn(log):
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", HERE / "ctest.ps1",
                    "-Dir", pathlib.Path(root or g.args.build_root) / "game"], log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        summary = next((l.strip() for l in text.splitlines() if "tests passed" in l), last_line(text))
        return counted(code, summary, text)
    return fn


def rust_tests(g):
    def fn(log):
        env = {"CARGO_TARGET_DIR": str(pathlib.Path(g.args.build_root) / "cargo-test")}
        code = run(["cargo", "test", "--release", "--locked", "--offline", "-p", "nebula-import", "-p", "luma-validate"],
                   log, env=env, cwd=TREE)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        passed = sum(int(l.split()[3]) for l in text.splitlines() if l.startswith("test result:"))
        failed = sum(int(l.split()[5]) for l in text.splitlines() if l.startswith("test result:"))
        if passed + failed == 0:
            return False, "no test ran", None, "tool"
        return code == 0 and failed == 0, "%d passed, %d failed" % (passed, failed)
    return fn


PLATFORM_CHECK = TREE / "tools" / "lumacore" / "check_luma_platforms.py"


PIN_LOCK = threading.Lock()


def pinned(g):
    """The pinned LumaLink, (program, checkout): mod_tests.pinned_lumalink in <build root>/lumalink-pin (its nebula/ a
    link to this tree), built once for the lumalink check and every mods lane, whichever asks first (the lanes run side
    by side; --only may leave the lumalink check out). SystemExit names why it could not be built."""
    with PIN_LOCK:
        if getattr(g, "pin", None) is None:
            sys.path.insert(0, str(TREE / "tools" / "lumacore"))
            import mod_tests  # noqa: E402 - tools/lumacore, beside the mod tests it serves
            g.pin = mod_tests.pinned_lumalink(pathlib.Path(g.args.build_root) / "lumalink-pin")
        return g.pin


def lumalink_tests(g):
    """LumaLink's own tests at the commit tools/lumacore/lumalink.commit pins, in the checkout the mod tests build it in
    (mod_tests.pinned_lumalink: <build root>/lumalink-pin, its nebula/ a copy of the tree paths it builds from): every crate's tests, and
    those that need clang, this tree's SDK and this build (the generated C compiled for every platform, one mod built
    for all nine and each library read as its loader will, the same floating point on every platform, the programs
    checked and emitted against this build's names); the VS Code one needs VS Code. The mod tests use the same build
    unless --lumalink names another program."""
    def fn(log):
        try:
            _, checkout = pinned(g)
        except SystemExit as stop:
            pathlib.Path(log).write_text("%s\n" % stop, encoding="utf-8")
            return False, str(stop)
        env = {"LUMA_SDK": str(TREE / "native" / "include"), "LUMA_PLATFORM_CHECK": str(PLATFORM_CHECK),
               "LUMA_ROWS": str(pathlib.Path(g.bin) / "luma"), "LUMA_TYPES": str(pathlib.Path(g.bin) / "luma_types.json")}
        code = run(["cargo", "test", "--release", "--locked", "--no-fail-fast", "-j", g.args.jobs, "--workspace", "--",
                    "--include-ignored", "--skip", "vscode"], log, env=env, cwd=checkout)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        passed = sum(int(l.split()[3]) for l in text.splitlines() if l.startswith("test result:"))
        failed = sum(int(l.split()[5]) for l in text.splitlines() if l.startswith("test result:"))
        pin = (TREE / "tools" / "lumacore" / "lumalink.commit").read_text(encoding="utf-8").strip()
        if passed + failed == 0:
            return False, "no test ran (LumaLink %s)" % pin[:9], None, "tool"
        return code == 0 and failed == 0, "%d passed, %d failed (LumaLink %s)" % (passed, failed, pin[:9])
    return fn


def disc_images(folder):
    """{disc id: image} of a folder holding the discs as RMGE01.wbfs, RMGP01.rvz and so on."""
    out = {}
    for f in sorted(pathlib.Path(folder).glob("RMG*.*")) if folder else []:
        out.setdefault(f.stem.upper(), f)
    return out


def collision_proof(g, image):
    """Every collision model of a disc image cooked and proved as an import does (nebula-import collision-proof:
    each octree path reaches the same prisms as the disc's)."""
    def fn(log):
        code = run([pathlib.Path(g.bin) / "nebula-import.exe", "collision-proof", image], log, timeout=1800)
        return code == 0, last_line(pathlib.Path(log).read_text(encoding="utf-8", errors="replace"))
    return fn


EXES = {"d3d12": "Nebula.exe", "vulkan": "NebulaVk.exe", "d3d11": "NebulaD11.exe"}


def start_check(g, renderer, extra=()):
    """The program's start without game data (host_main.cpp run_start_check): window, renderer, 300 frames shown. `extra`: more
    options of the game (the validate lane's probe passes --gpu-validation 1)."""
    def fn(log):
        out = g.work / ("start-" + renderer)
        for sub in ("data", "settings"):
            (out / sub).mkdir(parents=True, exist_ok=True)
        hidden = {}
        if os.name == "nt":
            info = subprocess.STARTUPINFO()
            info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            info.wShowWindow = 0  # SW_HIDE: a test's start
            hidden = {"startupinfo": info}
        with open(log, "wb") as f, game_slot(g):
            try:
                code = subprocess.run([str(pathlib.Path(g.bin) / EXES[renderer]), "--start-check", "300",
                                       "--data", str(out / "data"), "--settings", str(out / "settings"),
                                       "--crash-dumps", str(out), *extra], stdout=f, stderr=subprocess.STDOUT, timeout=300,
                                      **hidden).returncode
            except subprocess.TimeoutExpired:
                code = 124
        said = [l for l in pathlib.Path(log).read_text(encoding="utf-8", errors="replace").splitlines()
                if l.startswith("start check")]
        return code == 0, "exit %d: %s" % (code, said[-1] if said else "(no start check line)")
    return fn


GUARDED = HERE / "tests" / "guarded.py"
GUARD_LOG = "guard-%s.log"      # beside the script's work folder (a script may remove its own folder, with the log in it)


def scripts(g, paths, extra=()):
    """Python self-tests without the game, each with its own work folder: passes when every one exits 0 and none reached the live
    state. Each script starts through tests/guarded.py with NEBULA_SELF_TEST=1 and a log file of its own (selftest_guard.py): a
    script whose log is not empty fails whatever its exit code, and the detail names the script and the first path."""
    def fn(log):
        bad, reached = [], []
        for path in paths:
            out = g.work / ("self-" + path.stem)
            out.mkdir(parents=True, exist_ok=True)
            guard_log = g.work / (GUARD_LOG % path.stem)
            guard_log.write_text("", encoding="utf-8")
            args = [a.replace("{out}", str(out)) for a in extra]
            code = run([sys.executable, "-X", "utf8", "-I", GUARDED, path, *args], log, cwd=TREE, timeout=900,
                       env={"NEBULA_SELF_TEST": "1", "NEBULA_SELF_TEST_LOG": str(guard_log)})
            touched = next((l.strip() for l in guard_log.read_text(encoding="utf-8", errors="replace").splitlines() if l.strip()), "")
            if touched:
                reached.append("%s reached the live state: %s" % (path.stem, touched))
                bad.append(path.stem)
            elif code != 0:
                bad.append(path.stem)
        detail = "%d of %d pass" % (len(paths) - len(bad), len(paths))
        if not paths:
            return False, "no script to run: nothing ran", None, "tool"
        return not bad, detail + ("; not: " + ", ".join(bad) if bad else "") + ("; " + "; ".join(reached) if reached else "")
    return fn


def game_slot(g, weight=1):
    """g.game_slot(weight): a game copy's place among those machine_ready allowed (no limit for a stand-in run object)."""
    return g.game_slot(weight) if hasattr(g, "game_slot") else contextlib.nullcontext()


# The harness's own tests (the runner and the host half, without the game).
HARNESS_SELF = sorted([*(TREE / "tools" / "lumacore").glob("harness_*_test.py"),
                       *(TREE / "tools" / "lumacore" / "tests").glob("harness_*_test.py"),
                       *(TREE / "tools" / "test_driver" / "tests").glob("*_test.py"),
                       *(TREE / "tools" / "gate" / "tests").glob("*_test.py"),
                       *(TREE / "tools" / "measure" / "tests").glob("*_test.py")])
# The game description's tools on their fixtures, with the game build's compile commands.
DESCRIPTION_SELF = sorted((TREE / "tools" / "lumacore").glob("*_fixture_test.py"))


# The game's watches by renderer (--diagnostics names): every game the gate starts on that renderer runs with them, and
# the runner fails a run that prints a watch's line. Direct3D 11's constants watch, ledger P-332 (luma_test.py
# CONSTANTS_WATCH).
WATCHES = {"d3d11": ("constants-watch",)}


def watch_env(renderer, env=None):
    """`env` (more for the games' environment) with `renderer`'s watches added to the runner's --diagnostics for every
    run (LUMA_TEST_DIAGNOSTICS, which luma_test.py reads, kept as given and each name once)."""
    out = dict(env or {})
    watches = WATCHES.get(renderer, ())
    if watches:
        given = [d for d in out.get("LUMA_TEST_DIAGNOSTICS", os.environ.get("LUMA_TEST_DIAGNOSTICS", "")).split(",") if d]
        out["LUMA_TEST_DIAGNOSTICS"] = ",".join(given + [w for w in watches if w not in given])
    return out


def suite_layout(suite):
    """(the folder name the runner files a suite's runs under, the tests it lists), the tests None for one test folder."""
    suite = pathlib.Path(suite)
    if suite.is_file() and suite.name == "suite.json":
        return suite.parent.name, list(json.loads(suite.read_text(encoding="utf-8")).get("tests", []))
    return suite.name, None


# What defines a test that is not in the repository (a tour is written into the run's folder from these).
GENERATED_TESTS = ("tools/gate/gate_tour.py", "tools/verify/tours", "tools/verify/step_hash.py")


class HarnessSource:
    """One harness suite on one renderer that can run its tests again (kinds.Settler's source): alone with the same
    build, and on main's kept build. The first run is harness()'s; this class makes the command for all three."""

    def __init__(self, g, suite, renderer, label, device=None, bin_dir=None, env=None, judge=None):
        self.g, self.suite, self.renderer, self.label = g, suite, renderer, label
        self.device, self.bin_dir, self.env = device, bin_dir, env
        self.suite_dir, self.listed = suite_layout(suite)
        self.tag = "%s-%s" % (label, renderer)
        self.second = None
        # A lane's own judgement of each test after its run (validate_judge, stress_judge): judge(source, work, results) -> results.
        # It adds to `final` the tests that are not run again alone, and sets `counted` (what the first run's judgement counted).
        self.judge = judge
        self.final = set()
        self.counted = ""
        # The judge made a test that the runner ended with a non-zero code a pass (the sanitizer's listed report): that code is no fault.
        self.ended_listed = False

    def first_work(self):
        return self.g.work / ("runs-%s" % self.tag)

    def command(self, work, bin_dir, names, lock):
        g = self.g
        where = ["--device", self.device, "--bin", g.android_bin] if self.device else ["--bin", bin_dir]
        cmd = [sys.executable, "-X", "utf8", "-I", RUNNER, "run", self.suite] + where
        cmd += ["--work", work, "--who", "gate", "--lock", lock, "--commit", g.commit, "--renderer", self.renderer]
        if names is not None and self.listed is not None:
            cmd += [a for n in names for a in ("--test", n)]
        return cmd

    def start(self, work, bin_dir, names, log, lock):
        """Runs the runner on the tests (None: all) into `work`; its exit code. The run's copies of the programs are not
        evidence: removed so the drive does not fill."""
        g = self.g
        pathlib.Path(log).parent.mkdir(parents=True, exist_ok=True)
        # A game on this computer takes one of the places machine_ready allowed (a tablet's runs take none: they do not use this card).
        with contextlib.nullcontext() if self.device else game_slot(g):
            code = run(self.command(work, bin_dir, names, lock), log,
                       env=watch_env(self.renderer, dict(self.env or {}, LUMA_TEST_PAKS=g.paks_text())),
                       cwd=g.args.build_root)
        for d in pathlib.Path(work).rglob("bin"):
            if d.is_dir():
                shutil.rmtree(d, ignore_errors=True)
        return code

    def read(self, work, names):
        results, declared, problem = kinds.read_cases(work, self.suite_dir, names, self.renderer, kind_of_verdict)
        if self.judge is not None and not problem:
            results = self.judge(self, work, results)
        return results, declared, problem

    def kept_first(self, name):
        """Where the first run of a test that failed and passed alone is kept (adopt)."""
        return str(self.g.work / "rerun" / "first" / self.tag / name)

    def alone(self, names):
        """The tests run again by themselves, nothing else running: the same build, a fresh folder rerun/ in the run's."""
        g = self.g
        self.second = g.work / "rerun" / self.tag
        shutil.rmtree(self.second, ignore_errors=True)
        self.start(self.second, self.bin_dir or g.bin, names, g.work / "rerun" / ("%s.log" % self.tag),
                   g.work / ("rerun-%s.lock" % self.tag))
        results, _, _ = self.read(self.second, names if self.listed is not None else None)
        return results

    def adopt(self, name):
        """A test that failed once and passed alone: its second run takes the first's place in the run's folder (the first
        goes to rerun/first/), so the pictures and replays that read the runs read the run that passed."""
        first = self.first_work() / "runs" / self.suite_dir / name
        second = self.second / "runs" / self.suite_dir / name
        if not second.is_dir():
            return
        keep = self.g.work / "rerun" / "first" / self.tag
        try:
            keep.mkdir(parents=True, exist_ok=True)
            if first.is_dir():
                shutil.move(str(first), str(keep / name))
            shutil.move(str(second), str(first))
        except OSError as error:
            print("gate.py: %s: the second run of %s could not take the first's place: %s" % (self.tag, name, error), flush=True)

    def definition(self, name):
        """The repository paths that define a test: its folder, or for a test written into the run's folder (a tour) what
        writes it."""
        folder = pathlib.Path(self.suite).parent / name if self.listed is not None else pathlib.Path(self.suite)
        try:
            return [folder.resolve().relative_to(pathlib.Path(self.g.tree).resolve()).as_posix()]
        except ValueError:
            return list(GENERATED_TESTS)

    def on_main(self, names):
        """The tests run alone on main's kept build, only those whose definition is main's: ({name: result}, {name: why
        it was not run})."""
        g = self.g
        commit = git_in(g.tree, "rev-parse", g.main_ref)
        base = kinds.base_of(BASE_BINS, commit)
        if base is None:
            return {}, {n: "no kept build of main %s in %s" % (commit[:9] or g.main_ref, BASE_BINS) for n in names}
        if g.pak and not (pathlib.Path(base) / "pak.path").is_file():
            # A kept build made by a run that did not look for game data (a documents or tools landing) has no pak.path: the runner
            # would stop with a harness error, and no test could ever be told to fail on main too (T11-B).
            write_pak_path(base, g.pak)
        skipped, run_names = {}, []
        for n in names:
            if kinds.definition_same(GIT, g.tree, g.main_ref, self.definition(n)):
                run_names.append(n)
            else:
                skipped[n] = "its definition differs from %s" % g.main_ref
        if not run_names:
            return {}, skipped
        work = g.work / "main" / self.tag
        shutil.rmtree(work, ignore_errors=True)
        self.start(work, str(base), run_names, g.work / "main" / ("%s.log" % self.tag), g.work / ("main-%s.lock" % self.tag))
        results, _, _ = self.read(work, run_names if self.listed is not None else None)
        return results, skipped


def git_in(tree, *args):
    r = subprocess.run([GIT, "-C", str(tree), *args], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


class PiecedRun:
    """One harness check whose tests are played in pieces (the long run, package B15 point 12): each piece is the runner on some of
    the suite's tests into the one run folder, under the lock for itself; `harness(..., pieced=run)` then reads and judges all of them
    as one check, so the board keeps one line for the part. The pieces' exit codes and logs are kept for that reading."""

    def __init__(self, g, suite, renderer, label, check, device=None, bin_dir=None, env=None, judge=None):
        self.g, self.bin_dir, self.device, self.check = g, bin_dir, device, check
        self.source = HarnessSource(g, suite, renderer, label, device, bin_dir, env, judge)
        self.names = list(self.source.listed or [])
        self.codes, self.logs, self.seconds = [], [], 0.0

    def run_piece(self, names, index):
        """Plays `names` (some of the suite's tests); nothing in a dry run. A piece that cannot start is the runner's own exit code."""
        if self.g.dry:
            return
        started = time.time()
        log = self.g.work / ("%s-piece%d.log" % (self.source.tag, index + 1))
        code = self.source.start(self.source.first_work(), self.bin_dir or self.g.bin, list(names), log,
                                 self.g.work / ("harness-%s.lock" % self.source.tag))
        self.codes.append(code)
        self.logs.append(log)
        self.seconds += time.time() - started

    def finish(self, log):
        """The pieces' logs in the check's own log, and the exit code the check reads: the first that was not 0 (none run: 1)."""
        with open(log, "w", encoding="utf-8") as f:
            for i, piece in enumerate(self.logs):
                f.write("=== piece %d of %d ===\n" % (i + 1, len(self.logs)))
                try:
                    f.write(pathlib.Path(piece).read_text(encoding="utf-8", errors="replace"))
                except OSError:
                    pass
        self.g.carried[self.check] = self.seconds
        return next((c for c in self.codes if c != 0), 0 if self.codes else 1)


def harness(g, suite, renderer, label, device=None, bin_dir=None, env=None, judge=None, stress_lane=False, stress_fails=False, pieced=None):
    """One harness suite on one renderer, on this computer or on an Android device (its serial; the test app
    prepared and the device's lock held by the gate). Each test's kind is read from its result.json (the runner
    writes it); a test that left none, a result that cannot be read, a number of results other than the suite lists,
    and none at all are tool. Passes when every test passed (a test whose disc has no pak is not-run: env). The
    results go with it, and a source that runs a failed test again alone and on main's build (not on a device: its
    test app is removed when its part ends). `env`: more for the games' environment (the coverage build's counts). `judge`: the
    lane's own judgement of each test (HarnessSource). `stress_lane`: the check is a stress lane's (kinds.Cases.stress);
    `stress_fails`: its stress result is a fail (a landing asked for the lane by --also)."""
    def fn(log):
        if pieced is not None:
            # The tests ran in pieces, each under the lock for itself (PiecedRun); this reads them, with the pieces' logs as the check's.
            source, code = pieced.source, pieced.finish(log)
        else:
            source = HarnessSource(g, suite, renderer, label, device, bin_dir, env, judge)
            code = source.start(source.first_work(), bin_dir or g.bin, None, log, g.work / ("harness-%s-%s.lock" % (label, renderer)))
        work = source.first_work()
        results, declared, problem = source.read(work, source.listed)
        said = last_line(pathlib.Path(log).read_text(encoding="utf-8", errors="replace"))
        missing = [n for n, (k, m) in results.items() if k == "tool" and m.startswith("no result.json")]
        if code == ENV_EXIT and len(missing) == len(results) and results:
            # The runner would not start: the machine was not fit (the device away, a lock held), not the tests.
            results = {n: ("env", said) for n in results}
            problem = None
        elif missing:
            results.update({n: ("tool", "%s (the runner said: %s)" % (results[n][1], said)) for n in missing})
        passes = all(k == "pass" for k, _ in results.values())
        if problem is None and passes and code != 0 and not source.ended_listed:
            problem = "the runner exited %d though every test passed" % code
        elif problem is None and not passes and code == 0 and not judged_only(source, work, renderer):
            problem = "the runner exited 0 though a test did not pass"
        cases = kinds.Cases(results, declared, problem, renderer, None if device else source, final=source.final,
                            extra=source.counted, stress=stress_lane, stress_fails=stress_fails,
                            again=kinds.played_again(work, source.suite_dir, renderer))
        return passes and problem is None, "", cases
    return fn


def judged_only(source, work, renderer):
    """True when the runner's own results all passed and the lane's judge (validate, stress) made the failures: the runner's exit 0 is
    then right and says nothing against the check (T11-B: a game that plays differently showed a `tool` line beside its fails)."""
    if source.judge is None:
        return False
    raw, _, _ = kinds.read_cases(work, source.suite_dir, source.listed, renderer, kind_of_verdict)
    return all(k == "pass" for k, _ in raw.values())


ENV_EXIT = 3  # luma_test.py ENV_EXIT: the runner could not start because the machine was not fit


def start_soak(g, minutes=None):
    """The soak (tools/gate/soak.py, file 00 section 4.2): two hours of random input on each renderer (`minutes`: another length),
    in processes of its own, started so it runs beside the Android part. Returns the process and its log (soak-run.log: the
    check's own log is soak.log, N-1065)."""
    log = g.work / "soak-run.log"
    out = log.open("wb")
    process = subprocess.Popen([sys.executable, "-X", "utf8", "-I", str(HERE / "soak.py"), "run", "--bin", str(g.bin),
                                "--pak", getattr(g, "pak", "") or g.args.pak, "--work", str(g.work / "soak"), "--minutes",
                                str(g.args.soak_minutes if minutes is None else minutes),
                                "--who", "gate"], stdout=out, stderr=subprocess.STDOUT, cwd=g.args.build_root)
    out.close()
    return process, log


MEASURE = TREE / "tools" / "measure" / "measure.py"


def measure_command(sha, bin_dir, android_bin, apk, work, args, pak=None, paks=None, runs=None, pending=None, quiet=None,
                    device_wait=None):
    """tools/measure/measure.py run: the play route on the PC's renderers, the PC held to four cores, and the tablet
    when the gate has one (N-1043, 07-BASE-FIRST stage 1 item 1). The tablet's run installs the test app `apk` with
    the gate's pak and removes it after, inside the tablet's lock (P-321). `runs`: the run names to measure (the default is
    every PC run and the tablet when the gate has one); `pending`: the file the trusted lines go to, not the history;
    `quiet`: minutes to wait for a quiet machine; `device_wait`: minutes to wait for the tablet's lock."""
    names = None if runs is None else runs.split(",")
    tablet = bool(args.tablet and android_bin and apk) and (names is None or "tablet" in names)
    if runs is None:
        runs = "d3d12,vulkan,d3d11,d3d12-4cores" + (",tablet" if tablet else "")
    cmd = [sys.executable, "-X", "utf8", "-I", str(MEASURE), "run", "--bin", str(bin_dir), "--work", str(work),
           "--commit", sha, "--runs", runs, "--history", str(pathlib.Path(args.work) / "measure-history.tsv"),
           "--paks", args.paks if paks is None else paks, "--who", args.who]
    if tablet:
        cmd += ["--device", args.tablet, "--android-bin", str(android_bin), "--apk", str(apk), "--pak", pak or args.pak,
                "--device-lock", args.tablet_lock]
        if device_wait is not None:
            cmd += ["--device-wait", "%g" % device_wait]
    if pending is not None:
        cmd += ["--pending", str(pending)]
    if quiet is not None:
        cmd += ["--quiet-minutes", str(quiet)]
    return cmd


def test_apk(args):
    """The gate's build of the test app (build/android, platform/android/build_apk.ps1 -Debuggable -Harness)."""
    return pathlib.Path(args.build_root) / "android" / "Nebula.apk"


# The speed measure of a landing (package B8, docs/plan/04-GATE-AND-MAIN.md): the play route once on Direct3D 12 (measure/step/pc,
# last, with the machine alone) and once on the tablet (measure/step/tablet, in the Android lane after its build). The two run
# names are the planner's to change.
MEASURE_PC_RUN = "d3d12"
MEASURE_TABLET_RUN = "tablet"
MEASURE_PC_CHECK = "measure/step/pc"
MEASURE_TABLET_CHECK = "measure/step/tablet"
MEASURE_LEVELS = ("main", LAND_LEVEL)
# The time the tablet's check may take: a route of three minutes measured at most twice; each
# run's start and end take two minutes; the install and removal five, and the wait for its lock is the landing's --tablet-wait.
MEASURE_ROUTE_MINUTES = 3
MEASURE_RUN_MINUTES = 2
MEASURE_TABLET_SET_MINUTES = 5
# measure.py's exit code for each state of a run in result.json: a state and a code that differ are a fault of the reading.
MEASURE_EXIT_OF = {"ok": 0, "better": 0, "no-baseline": 0, "left-out": 0, "worse": 1, "not-trusted": 3, "tool": 2}


def measure_limit(wait_minutes=0.0):
    """Seconds the tablet's check may run: the route measured twice, and the wait for the tablet's lock and its install."""
    twice = 2 * (MEASURE_ROUTE_MINUTES + MEASURE_RUN_MINUTES)
    return int(60 * (twice + wait_minutes + MEASURE_TABLET_SET_MINUTES))


def measure_figures_text(state, figures):
    """What a passed measure says: its judged figures with value and median, `no baseline yet` where a figure has none, and what the
    anchor says of a figure that is slower than at the start of the native work."""
    judged = [(n, f) for n, f in figures.items() if f.get("judged")]
    parts = []
    for name, f in judged:
        if f.get("value") is None:
            parts.append("%s not measured" % name)
        elif f.get("median") is None:
            parts.append("%s %s (no baseline yet)" % (name, kinds.say_number(f["value"])))
        else:
            parts.append("%s %s (median %s)" % (name, kinds.say_number(f["value"]), kinds.say_number(f["median"])))
    says = ["%s: %s" % (n, f["anchor"]["says"]) for n, f in judged if isinstance(f.get("anchor"), dict) and f["anchor"].get("says")]
    head = {"ok": "holds", "better": "better", "no-baseline": "no baseline yet"}[state]
    return "%s: %s" % (head, "; ".join(parts + says) if parts or says else "no judged figure")


def measure_reading(work, run_name, code):
    """What one run of measure.py says: (kind, text). From WORK/result.json, the run's entry; the exit code only as a cross-check.
    kind `pass` (ok, better, no baseline yet: the text names the judged figures), `worse` (the text is kinds.worse_message),
    `not-trusted`, `left-out` (the tablet was away), `stopped` (the gate ended the run), or `could-not` (the measure could not run:
    its time limit, no readable result.json, a fault it reports, an exit code and a state that disagree)."""
    if code == 124:
        return "could-not", "its time limit"
    if code == 125:
        return "stopped", "the run was stopped"
    try:
        result = json.loads((pathlib.Path(work) / "result.json").read_text(encoding="utf-8"))
        entry = result["runs"][run_name]
        state = entry["state"]
        figures = entry.get("figures") or {}
        reason = kinds.first_line(entry.get("reason") or "")
        said = result.get("exit")
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        return "could-not", "no readable result.json for %s (exit %d): %s: %s" % (run_name, code, type(error).__name__,
                                                                                kinds.first_line(error))
    if state not in MEASURE_EXIT_OF:
        return "could-not", "result.json names the state %r for %s" % (state, run_name)
    if code != MEASURE_EXIT_OF[state] or (isinstance(said, int) and said != code):
        return "could-not", "exit %d and state %s disagree" % (code, state)
    try:
        if state == "tool":
            return "could-not", reason or "the measure reported a fault"
        if state == "not-trusted":
            return "not-trusted", reason or "no reason was given"
        if state == "left-out":
            return "left-out", reason or "the tablet was left out"
        if state == "worse":
            worse = [(n, f["value"], f["median"], f["band"]) for n, f in figures.items()
                     if f.get("judged") and f.get("verdict") == "worse"]
            if not worse:
                return "could-not", "state worse and no judged figure is worse in result.json"
            return "worse", kinds.worse_message(worse)
        return "pass", measure_figures_text(state, figures)
    except (KeyError, TypeError, AttributeError) as error:
        return "could-not", "result.json for %s cannot be read: %s: %s" % (run_name, type(error).__name__, kinds.first_line(error))


def measure_fault(g, check, kind, reason):
    """One line in tool-faults.tsv for a measure that could not run or was not trusted (kind TOOL or ENV): the test named is the check,
    the text the reason; at most one for each check in a landing. A record that cannot be written is said, never a fault of the run."""
    if check in g.measure_faulted:
        return
    g.measure_faulted.add(check)
    live = getattr(g.args, "live", "")
    if not live:
        return
    try:
        fault_writer(live)("gate", "", kind, check, check, reason, str(g.work))
    except Exception as error:  # noqa: BLE001 - the record is a report; the landing stands without it
        print("gate.py: %s: the tool-fault line was not written: %s: %s" % (check, type(error).__name__, error), flush=True)


def measure_step(g):
    """The landing's speed measure of the tablet (measure/step/tablet), as measure.py run with its own pending file; the row from
    measure_reading (docs/plan/04-GATE-AND-MAIN.md section 5.2): pass, `worse` (main does not move), or owed with one line in
    tool-faults.tsv (not trusted, could not run, its time limit). Its form stands alone: its own device lock, install and removal.
    The pending file is kept by keep_measures when main has moved. The PC's check is the pair (pair_step)."""
    name = MEASURE_TABLET_CHECK
    run_name = MEASURE_TABLET_RUN

    def fn(log):
        folder = "measure-tablet"
        work, pending = g.work / folder, g.work / (folder + ".tsv")
        apk = test_apk(g.args)
        cmd = measure_command(g.commit, g.bin, g.android_bin, apk if apk.is_file() else None, work, g.args, g.pak,
                              g.paks_text(), runs=run_name, pending=pending, device_wait=g.args.tablet_wait)
        limit = measure_limit(g.args.tablet_wait)
        g.measure_pending.append(pending)
        code = run(cmd, log, timeout=limit)
        kind, text = measure_reading(work, run_name, code)
        if kind == "pass":
            return True, text
        gap = tablet_gap_lines(getattr(g.args, "live", ""))
        if kind == "worse" and gap:
            # The tablet was away for the landings in tablet-owed.tsv, so its history is older than the change that is judged: a figure worse
            # against it is not this change's to answer for. The landing is owed; after the move a repair holds the loss and the history
            # starts anew at this commit (rebase_tablet_after_gap).
            g.tablet_gap = {"text": text, "landings": [l.split("\t")[1] if len(l.split("\t")) > 1 else "?" for l in gap]}
            return False, tablet_gap_text(len(gap), text), None, "owed"
        if kind == "worse":
            return False, text, None, "worse"
        if kind == "left-out":
            return False, "the tablet was left out: " + text, None, "owed"
        if kind == "stopped":
            return False, "the measure could not run: " + text, None, "owed"
        if kind == "not-trusted":
            measure_fault(g, name, "ENV", "the measure was not trusted: " + text)
            return False, "the measure was not trusted: " + text, None, "owed"
        measure_fault(g, name, "TOOL", "the measure could not run: " + text)
        return False, "the measure could not run: " + text, None, "owed"
    return fn


def landing_failed(g):
    """Whether a check of the run did not pass: the landing has failed already, and what follows it only costs time."""
    return any(not r["passed"] for r in list(g.results) + list(getattr(g.local, "sink", None) or []))


def measure_tablet(g, device=None):
    """measure/step/tablet, in the Android lane right after a build/android that passed and before the tablet's part (so the tablet is
    rested). No tablet named, or one that does not answer adb: owed. No row when the Android build failed (its row says so)."""
    needs = ("build/android",) + DATA
    if g.level not in MEASURE_LEVELS or not g.wanted(MEASURE_TABLET_CHECK, needs):
        return
    if g.dry or not g.android_built:
        return
    if not g.args.tablet:
        g.owe(MEASURE_TABLET_CHECK, "no tablet is named (--tablet): the measure on the tablet is owed")
        return
    if device is None:
        machine.ensure_adb_server(android_module().Device("").adb_path)
        device = android_module().Device(g.args.tablet)
    if not device.answers():
        g.owe(MEASURE_TABLET_CHECK, "%s does not answer adb: the measure on the tablet is owed" % g.args.tablet)
        return
    g.check(MEASURE_TABLET_CHECK, measure_step(g), needs)


# The pair (package B13, docs/plan/04-GATE-AND-MAIN.md section 10.1): the PC's speed check of a landing and `speed/anchor` run the candidate's
# build against a kept build (tools/gate/speedbase.py) with `measure.py pair`, the gate reading result.json and the --pending table as data.
SPEED_ROOT = speedbase.ROOT
ANCHOR_CHECK = "speed/anchor"
PAIR_ROUNDS = 3
PAIR_ROUND_SECONDS = 150      # a round took 121 s on this PC (measured 2026-11-21); the limit has room for a slower one
PAIR_FIXED_SECONDS = 120
PAIR_PROBE_SECONDS = 300
# The renderers a pair judges: Direct3D 12 always; Direct3D 11 and Vulkan where the plan runs them (the engine or the tour group).
PAIR_RENDERERS = ("d3d12", "d3d11", "vulkan")
PAIR_REQUIRED = ("steps_over_18", "render_ms")     # what measure.py requires of a side (its REQUIRED): the probe shows them


def pair_renderers(g):
    """The renderers the landing's pair judges: d3d12, and each other one the landing's plan plays (a check `engine/R` or `tour/R`)."""
    return [r for r in PAIR_RENDERERS if r == "d3d12" or g.selected is None or any(
        n in g.selected for n in ("engine/%s" % r, "tour/%s" % r))]


def pair_limit(renderers):
    """Seconds a pair may run: three rounds of every renderer asked for and one repeat, each round with room for a slow one, and the
    start and end of the tool."""
    return int(PAIR_FIXED_SECONDS + (PAIR_ROUNDS * len(renderers) + 1) * PAIR_ROUND_SECONDS)


def pair_command(g, kept, check, work, pending, renderers):
    """measure.py pair of the candidate's build (this run's bins and game data) against the kept build `kept`. No gate lock option: the
    landing holds it. Both sides keep their own game data."""
    return [sys.executable, "-X", "utf8", "-I", str(MEASURE), "pair", "--base", str(kept.bins), "--cand", str(g.bin),
            "--runs", ",".join(renderers), "--work", str(work), "--pending", str(pending), "--base-pak", str(kept.pak),
            "--cand-pak", str(g.pak), "--base-commit", kept.commit, "--cand-commit", g.commit, "--name", check, "--who", g.args.who]


def candidate_runs(g, renderers, folder):
    """Whether the candidate's build reaches the route's stage on each of `renderers` by itself (measure.py pair-probe): True when every
    one does. Asked only when the pair says the kept build's side could not."""
    for r in renderers:
        log = g.work / ("%s-probe-%s.log" % (folder, r))
        cmd = [sys.executable, "-X", "utf8", "-I", str(MEASURE), "pair-probe", "--bin", str(g.bin), "--renderer", r, "--work",
               str(g.work / folder / ("probe-" + r)), "--base-pak", str(g.pak), "--commit", g.commit]
        code = run(cmd, log, timeout=PAIR_PROBE_SECONDS)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace") if pathlib.Path(log).is_file() else ""
        if code in (124, 125) or not speedbase.probe_ran(text, PAIR_REQUIRED):
            return False
    return True


def calibrate_hint(g, renderers):
    return ("python -X utf8 -I tools/measure/measure.py pair-calibrate --bin %s --runs %s --pairs 5 --work <a folder> --commit %s" % (
        g.bin, ",".join(renderers), g.commit))


def pair_step(g, check, kind, folder, renderers):
    """A pair check of the landing: the candidate against the kept build `kind` (`base` for measure/step/pc, `anchor` for speed/anchor).
    The row: pass (every figure ok, better or accepted), `worse` (a figure worse in all three rounds: the landing fails), or owed -
    no kept build yet, not calibrated on this PC, the pair could not run (one line in tool-faults.tsv), or the kept build can no longer
    run the route while the candidate's does: the kept build then moves to the candidate after the move, unjudged. g.speed[check] says
    what the move does with it (judged, owed, rebase)."""
    def fn(log):
        try:
            kept = speedbase.read(kind, SPEED_ROOT)
        except speedbase.KeptError as error:
            g.speed[check] = "owed"
            return False, "the %s cannot be read: %s" % (kind, error), None, "owed"
        if kept is None:
            g.speed[check] = "owed"
            made = "make" if kind == speedbase.BASE else "anchor"
            return False, ("there is no %s yet, so nothing is judged: make it with python -X utf8 -I tools/gate/gate.py speed-base %s "
                           "--bin <a main build's bins> --commit <its commit>" % (kind, made)), None, "owed"
        work, pending = g.work / folder, g.work / (folder + ".tsv")
        code = run(pair_command(g, kept, check, work, pending, renderers), log, timeout=pair_limit(renderers))
        reading = speedbase.read_pair(work, pending, code, renderers)
        if reading.kind == "pass":
            g.speed[check] = "judged"
            g.speed_ratios[check] = reading.ratios
            return True, "holds against the %s %s: %s" % (kind, kept.commit[:9], reading.text)
        g.speed[check] = "owed"
        if reading.kind == "worse":
            return False, reading.text, None, "worse"
        if reading.kind == "stopped":
            return False, "the measure could not run: " + reading.text, None, "owed"
        if reading.kind == "uncalibrated":
            return False, "%s; the check is owed until the bands are made: %s" % (reading.text, calibrate_hint(g, renderers)), None, "owed"
        if (reading.kind == "tool" and speedbase.base_side_only(reading) and not speedbase.store_difference(reading)
                and candidate_runs(g, list(reading.tool), folder)):
            g.speed[check] = "rebase"
            return False, "the %s %s cannot run this route any more (%s): the %s moves to %s unjudged" % (
                kind, kept.commit[:9], speedbase.base_side_ended(reading), kind, g.commit[:9]), None, "owed"
        measure_fault(g, check, "TOOL", "the measure could not run: " + reading.text)
        return False, "the measure could not run: " + reading.text, None, "owed"
    return fn


def anchor_part(g):
    """`--also speed/anchor`: the candidate against the anchor on every renderer, judged as the pair of the landing is (worse fails with
    kind `worse`). Where the anchor cannot run today's route the check is owed and the anchor becomes the candidate after the move."""
    if not g.wanted(ANCHOR_CHECK, DATA) or g.dry:
        return
    g.check(ANCHOR_CHECK, pair_step(g, ANCHOR_CHECK, speedbase.ANCHOR, "speed-anchor", list(PAIR_RENDERERS)), DATA)


def keep_speed(g, landed, args):
    """After main moved: the kept builds follow the landing. The base becomes the candidate when the landing's pair judged it (a pass, or
    a worse figure accepted by the owner's word) or when the base could no longer run the route (`rebased`, unjudged); a pair that did not
    judge (owed, could not run) leaves it, so that the next pair covers both changes. A landing whose plan had no measure_pc check
    leaves it. The anchor is replaced only where it could no longer run the route; a judged `speed/anchor` keeps its ratios. A failure
    changes nothing about the landing: one printed line and one line in tool-faults.tsv."""
    try:
        before = speedbase.read(speedbase.BASE, SPEED_ROOT)
        state = g.speed.get(MEASURE_PC_CHECK)
        if state in ("judged", "rebase"):
            speedbase.make(speedbase.BASE, g.bin, landed, g.pak or None, SPEED_ROOT)
            speedbase.remember_base("moved" if state == "judged" else "rebased", landed, before.commit if before else None,
                                    "the pair judged it" if state == "judged" else "the base could not run the route: unjudged", SPEED_ROOT)
            print("gate.py: speed base %s (was %s)" % ("moved to " + landed[:9] if state == "judged" else "rebased to " + landed[:9] + ", unjudged",
                  before.commit[:9] if before else "none"), flush=True)
        elif state == "owed":
            speedbase.remember_base("kept", before.commit if before else "-", before.commit if before else None,
                                    "the landing %s did not judge its speed" % landed[:9], SPEED_ROOT)
            print("gate.py: speed base stays %s: the landing's pair did not judge" % (before.commit[:9] if before else "empty"), flush=True)
        state = g.speed.get(ANCHOR_CHECK)
        anchor = speedbase.read(speedbase.ANCHOR, SPEED_ROOT)
        if state == "judged" and anchor is not None:
            speedbase.note_anchor_ratios(g.speed_ratios.get(ANCHOR_CHECK, {}), anchor.commit, landed, SPEED_ROOT)
            print("gate.py: speed anchor %s: the ratios of %s are kept" % (anchor.commit[:9], landed[:9]), flush=True)
        elif state == "rebase":
            speedbase.make_anchor(g.bin, landed, g.pak or None, SPEED_ROOT)
            print("gate.py: speed anchor rebased to %s, unjudged (the old one could not run the route)" % landed[:9], flush=True)
    except (speedbase.KeptError, OSError) as error:
        reason = "the speed base was not kept up (%s: %s)" % (type(error).__name__, error)
        print("gate.py: " + reason, flush=True)
        g.measure_faulted.discard("speed/keep")
        measure_fault(g, "speed/keep", "TOOL", reason)


# B14 part 3: the PC's last checks (tour/tablet-pc, then the speed pair, one at a time) start when the PC's own lanes are done and the Android
# build is over, while the tablet's lane goes on. PAIR_WAITS_FOR_SELF: whether the lane of the tools' self-tests (it starts no game) is among
# the lanes waited for. Decided by the proof of B14 part 3 (2026-11-22: five pairs main against main beside the self-tests, 8 at a time, every
# figure of every pair inside its band, one round each): False. Rust's tests (a build) stay in the lane that is waited for.
PAIR_WAITS_FOR_SELF = False


def lanes_without(lanes, taken):
    return [l for l in lanes if l not in taken]


def pc_tail(g, stops):
    """The PC's last checks, in this order and one at a time: tour/tablet-pc (when the tablet's screen size is known: else tablet_after plays it
    once the tablet's tour has reported it), then measure/step/pc, unless a check of the PC's lanes has not passed or a part was asked for by name
    (`--also`: the pair then stays last, as it always was)."""
    def tail():
        g.tablet_screen_read.wait()
        if g.tablet_screen is not None and not g.dry:
            tablet_pc_tour(g, stops, g.tablet_screen)
        if not getattr(g.args, "also", "") and getattr(g, "android_built", True) and not any(r.get("pending") for r in g.results):
            measure_pc(g)
    return tail


def measure_pc(g):
    """measure/step/pc, last, where the long level has measure/route: every lane has ended, the tests that failed ran again alone,
    the pictures and replays are checked, and nothing else of the landing runs. A landing that has failed already does not run it."""
    if g.level not in MEASURE_LEVELS or not g.wanted(MEASURE_PC_CHECK, DATA):
        return
    if g.dry or landing_failed(g) or any(r["check"] == MEASURE_PC_CHECK for r in g.results):
        return
    g.check(MEASURE_PC_CHECK, pair_step(g, MEASURE_PC_CHECK, speedbase.BASE, "measure-pc", pair_renderers(g)), DATA)


def tablet_gap_lines(live):
    """The lines of tablet-owed.tsv in the live folder: one for each landing that owed the tablet's checks since a landing last played
    them (the gap in the tablet's measure history). [] when there are none."""
    path = pathlib.Path(live) / TABLET_OWED if live else None
    try:
        return [l for l in path.read_text(encoding="utf-8").splitlines() if l.strip()] if path is not None and path.is_file() else []
    except OSError:
        return []


def tablet_gap_text(count, worse_text):
    return ("the tablet's history is %d landing%s old (the tablet was away): %s; owed, not blamed on this change" % (
        count, "" if count == 1 else "s", worse_text))


def rebase_tablet_after_gap(g, args, run=subprocess.run):
    """After the move of a landing whose tablet measure was worse against a history older than the change (tablet_gap): the history is
    based anew at this landing's commit (`measure.py accept`, the tool's own restart of a baseline: the lines before it stop counting, its
    worse marks become trusted), so that later landings are judged against the figures from here on, and one repair (source measure, kind
    worse, check measure/step/tablet) holds the loss, with the gap's landings and the figures. Never fails the landing."""
    gap = getattr(g, "tablet_gap", None)
    if not gap:
        return
    history = str(pathlib.Path(args.work) / "measure-history.tsv")
    try:
        done = run([sys.executable, "-X", "utf8", "-I", str(MEASURE), "accept", "--commit", g.commit, "--renderer", "tablet", "--history", history],
                   capture_output=True, text=True, timeout=300)
        code, said = done.returncode, kinds.first_line((done.stdout or "") + (done.stderr or ""))
    except (OSError, subprocess.TimeoutExpired) as error:
        code, said = 2, "%s: %s" % (type(error).__name__, error)
    print("gate.py: the tablet's history after a gap: %s (exit %d)" % (said, code), flush=True)
    live = getattr(args, "live", "")
    note = ("the tablet's speed measure was worse against a history older than the landings %s (the tablet was away) and was owed; the history "
            "starts anew at %s; the loss since the last judged figures is held here: %s" % (", ".join(gap["landings"]), g.commit[:9], gap["text"]))
    if code != 0:
        note += " (the history was NOT restarted: %s)" % said
    if not live:
        return
    cmd = [sys.executable, "-X", "utf8", "-I", str(stress.PLAN_TOOL), "--live", str(live), "repair", "add", "--source", "measure", "--kind", "worse",
           "--check", MEASURE_TABLET_CHECK, "--renderer", "tablet", "--commit", g.commit[:9], "--evidence", str(g.work), "--note", note]
    try:
        made = run(cmd, capture_output=True, text=True, timeout=300, cwd=str(TREE))
        print("gate.py: the tablet's loss: repair %s" % (((made.stdout or "").strip().splitlines() or ["not opened (exit %d)" % made.returncode])[-1]), flush=True)
    except (OSError, subprocess.TimeoutExpired) as error:
        print("gate.py: the repair for the tablet's loss could not be opened: %s: %s" % (type(error).__name__, error), flush=True)


def keep_measures(g, args):
    """After main moved: `measure.py keep` for each pending file of the run that exists (the history holds main's commits only). A keep
    that fails changes nothing about the landing: one line in tool-faults.tsv and one printed line. When main did not move the files stay
    in the run folder, never kept."""
    history = str(pathlib.Path(args.work) / "measure-history.tsv")
    for path in g.measure_pending:
        if not pathlib.Path(path).is_file():
            continue
        try:
            done = subprocess.run([sys.executable, "-X", "utf8", "-I", str(MEASURE), "keep", str(path), "--history", history],
                                  capture_output=True, text=True, timeout=300)
            code, said = done.returncode, kinds.first_line((done.stdout or "") + (done.stderr or ""))
        except (OSError, subprocess.TimeoutExpired) as error:
            code, said = 2, "%s: %s" % (type(error).__name__, error)
        if code == 0:
            print("gate.py: " + said, flush=True)
            continue
        reason = "the measure's figures were not kept in the history (exit %d): %s" % (code, said)
        print("gate.py: " + reason, flush=True)
        g.measure_faulted.discard("measure/keep")
        measure_fault(g, "measure/keep", "TOOL", reason)


def soak_result(process, soak_log):
    """The soak's verdict once it ends: one line per renderer (soak.py run)."""
    def fn(log):
        code = process.wait()
        text = soak_log.read_text(encoding="utf-8", errors="replace")
        if pathlib.Path(log).resolve() != pathlib.Path(soak_log).resolve():
            shutil.copyfile(soak_log, log)
        lines = [l.strip() for l in text.splitlines() if l.strip()]
        return code == 0, "; ".join(lines[-3:]) if lines else "no output (exit %d)" % code
    return fn


def replays(g, label, renderer, against=None):
    """The step-hash replay (tools/verify/step_hash.py check): a tour's runs, hashed at every step, against the
    recorded stops (tests/replays), or against another tour's runs (`against`: (label, renderer)) of the same tests.
    The quick gate plays only some recorded stops."""
    def fn(log):
        work = g.work / ("runs-%s-%s" % (label, renderer))
        other = None if against is None else g.work / ("runs-%s-%s" % against)
        ok, lines, kind = step_hash.check_kind(work, other, some=g.level in SHORT_LEVELS)
        pathlib.Path(log).write_text("\n".join(lines) + "\n", encoding="utf-8")
        # Not one run compared is no proof: tool. A run whose window is not the recordings' is tool too. A step hash that differs is a fail.
        return ok, lines[-1], None, "tool" if kind == "tool" or lines[-1].startswith("0 of 0 the same") else "fail"
    return fn


def device_screen(work):
    """The device's screen size, (width, height), from its runs' logs in a work folder (the Vulkan renderer's
    "[vulkan] swapchain WxH" line, the last one of the first run that has one), or None."""
    for log in sorted(pathlib.Path(work).rglob("run.log")):
        sizes = re.findall(r"\[vulkan\] swapchain (\d+)x(\d+)", log.read_text(encoding="utf-8", errors="replace"))
        if sizes:
            return int(sizes[-1][0]), int(sizes[-1][1])
    return None


def jvm_test_counts(folder):
    """The launcher's JVM tests from Gradle's result files: (tests, not passed), or None without any."""
    import xml.etree.ElementTree as ElementTree
    tests = bad = 0
    files = sorted(pathlib.Path(folder).glob("TEST-*.xml"))
    for f in files:
        suite = ElementTree.parse(f).getroot()
        tests += int(suite.get("tests", "0"))
        bad += int(suite.get("failures", "0")) + int(suite.get("errors", "0")) + int(suite.get("skipped", "0"))
    return (tests, bad) if files else None


def build_android(g):
    """The test app for a device (platform/android/build_apk.ps1 -Debuggable -Harness: dev.nebula.galaxy.test beside
    the player's game) and the runner's bin folder for arm64 devices, with the launcher's JVM tests (app/src/test) in
    the same Gradle run. Its shader tools are the MSVC build's."""
    def fn(log):
        work = pathlib.Path(g.args.build_root) / "android"
        results = TREE / "platform" / "android" / "app" / "build" / "test-results" / "testReleaseUnitTest"
        shutil.rmtree(results, ignore_errors=True)
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    TREE / "platform" / "android" / "build_apk.ps1", "-Deps", g.args.android_deps, "-Work", work,
                    "-HostTools", pathlib.Path(g.args.build_root) / "msvc", "-Abis", "arm64-v8a", "-Debuggable",
                    "-Harness", TREE / "tools" / "test_driver" / "android" / "java", "-ReleaseKey", "", "-UnitTests"],
                   log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        apk = work / "Nebula.apk"
        counts = jvm_test_counts(results)
        ok = (code == 0 and apk.is_file() and (work / "test-bin-arm64-v8a" / "libnebula_test_extension.so").is_file()
              and counts is not None and counts[0] > 0 and counts[1] == 0)
        if not ok:
            if code == 0 and (counts is None or counts[0] == 0):
                return False, "the launcher's JVM tests did not run"
            if counts is not None and counts[1] != 0:
                return False, "the launcher's JVM tests: %d of %d did not pass" % (counts[1], counts[0])
            return False, last_line(text)
        return True, "%s (%.1f MB); JVM tests %d of %d" % (apk, apk.stat().st_size / 1e6, counts[0], counts[0])
    return fn


def device_step(g, step):
    """luma_test_android.py prepare (install the test app, fill its game.pak from the copy kept on the tablet: B14 part 2) or remove, on the
    gate's tablet. The `machine:` lines it prints (what is kept, no room, no hash tool) join the run's machine notes; exit 3 of prepare
    (the pushed data is not the PC's after two pushes) is kind tool."""
    def fn(log):
        args = [sys.executable, "-X", "utf8", "-I", TREE / "tools" / "lumacore" / "luma_test_android.py", step,
                "--device", g.args.tablet]
        if step == "prepare":
            args += ["--apk", test_apk(g.args), "--pak", g.pak,
                     "--who", "gate"]
        code = run(args, log, timeout=3600)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        for line in text.splitlines():
            if line.startswith("machine: "):
                g.machine_notes.append("the tablet: " + line[len("machine: "):])
        if code == 3 and step == "prepare":
            return False, last_line(text), None, "tool"
        return code == 0, last_line(text)
    return fn


def process_alive(pid):
    """Whether a process of this id runs (Windows: OpenProcess and its exit code; elsewhere signal 0)."""
    if os.name != "nt":
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
        return True
    kernel = ctypes.windll.kernel32
    handle = kernel.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return ctypes.GetLastError() == 5  # access denied: it runs, as another user
    try:
        code = ctypes.c_ulong()
        return bool(kernel.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259  # STILL_ACTIVE
    finally:
        kernel.CloseHandle(handle)


def lock_holder(lock):
    """(text, pid) of a held lock folder: its holder.txt, and the process id it names (`pid N` anywhere in it, tools/gate/lockholder.py),
    None when none."""
    holder = pathlib.Path(lock) / "holder.txt"
    text = holder.read_text(encoding="utf-8", errors="replace").strip() if holder.exists() else "unknown"
    return text, lockholder.parse(text)[0]


def board_line(board, text):
    if board:
        with open(board, "a", encoding="utf-8") as f:
            f.write("%s %s\n" % (datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), text))


def owed_line(who, mode, name, commit, owed):
    """The board line for the rows a run owes (`owed`: their check names): what is owed, by the rows' own names. A row of the speed
    measure (measure/...) is the measure's; any other owed row is the tablet's part. It says when the rows run again."""
    measure = [r for r in owed if r.startswith(("measure/", "speed/"))]
    part = [r for r in owed if not r.startswith(("measure/", "speed/"))]
    what = " and ".join((["the tablet's part"] if part else []) + (["the speed measure"] if measure else []))
    when = " and ".join((["the tablet is back"] if part or any("tablet" in r for r in measure) else []) +
                        (["the machine is quiet"] if any("tablet" not in r for r in measure) else []))
    rows = ",".join(owed)
    return ("%s OWED: %s of %s gate %s on %s: %s. It runs on main's newest commit first when %s (gate.py --main --only \"%s\")." % (
        who, what, mode, name, commit[:9], ", ".join(owed), when, rows))


TABLET_OWED = "tablet-owed.tsv"       # in the live folder: time, commit, the tablet's checks a landing owed (the progress page says the tablet is away)


def tablet_check(name):
    return name in TABLET_NAMES or name == MEASURE_TABLET_CHECK


def record_tablet_owed(live, commit, results, now=None):
    """After a landing that moved main: the tablet's checks it owed are one line of tablet-owed.tsv (the page shows "Tablet away since ...:
    n checks owed"); a landing that played a tablet check clears the file (the tablet is back). Never fails the run."""
    if not live:
        return
    path = pathlib.Path(live) / TABLET_OWED
    try:
        owed = [r["check"] for r in results if r["state"] == "owed" and tablet_check(r["check"])]
        played = any(r["state"] in ("pass", "known", "fail", "worse") and tablet_check(r["check"]) for r in results)
        if played:
            path.unlink(missing_ok=True)
        elif owed:
            path.parent.mkdir(parents=True, exist_ok=True)
            stamp = (now or datetime.datetime.now()).isoformat(timespec="seconds")
            with open(path, "a", encoding="utf-8", newline="\n") as f:
                f.write("%s\t%s\t%s\n" % (stamp, commit[:9], ",".join(owed)))
    except OSError as error:
        print("gate.py: %s was not written: %s" % (TABLET_OWED, error), flush=True)


# Who goes first among the lock's waiters (07-BASE-FIRST, How the work flows: the long run does not take the lock
# while a landing waits): a landing's gate (quick or main) before the work that waits for no one (the long run's
# parts, the measure, queued loops), otherwise first come. gate.py keeps the order, not the agents.
LANDING_GATES = ("quick gate ", "main gate ", "land gate ")


def queue_rank(holder):
    """0 for a landing's gate (its holder line), 1 for the rest."""
    return 0 if holder.startswith(LANDING_GATES) else 1


def queue_folder(lock):
    """The waiting gates of a lock: <lock>.queue beside it, one file each."""
    return pathlib.Path(str(lock) + ".queue")


def queue_order(lock, alive):
    """The waiting gates in the order they take the lock, (rank, time, pid, file), from files named
    <rank>-<time in ms>-<pid>.txt. An entry whose process has ended is removed."""
    entries = []
    for f in queue_folder(lock).glob("*.txt"):
        parts = f.stem.split("-")
        if len(parts) != 3 or not all(x.isdigit() for x in parts):
            continue
        rank, stamp, pid = (int(x) for x in parts)
        if not alive(pid):
            f.unlink(missing_ok=True)
            continue
        entries.append((rank, stamp, pid, f))
    return sorted(entries)


def locks_for(mode, patterns, gate_lock, long_lock):
    """The locks a gate of this level takes: the gate lock at every level but the long run (--only runs too), the long
    lock for the long and final runs of all their checks."""
    locks = [gate_lock] if mode in ("quick", "main", "final", LAND_LEVEL) else []
    if mode in ("long", "final") and not patterns:
        locks.append(long_lock)
    return locks


def take_gate_lock(path, holder, wait_minutes, board, who, alive=process_alive, sleep=time.sleep, poll=30.0,
                   pid=None, clock=time.time, started=lockholder.process_started):
    """Takes the gate lock for `holder` (a line naming the run, with "pid N"): None when taken, else why not. Waits
    up to wait_minutes while another holds it; a holder whose process has ended (or runs from another start than its line's `since`:
    the number was used again) loses it, and one printed line and the board say so, naming the old pid.
    A gate that has to wait joins the lock's queue and takes the lock only when it is the queue's first (queue_rank,
    then first come). A lock taken by hand (a holder.txt naming no process) holds until its holder frees it."""
    lock = pathlib.Path(path)
    pid = os.getpid() if pid is None else pid
    deadline = clock() + 60.0 * wait_minutes
    said_waiting = False
    mine = None
    try:
        while True:
            # A newcomer takes a free lock only when nobody waits (one freed between two polls is not its to
            # take); a waiter takes it when it heads the queue.
            order = queue_order(lock, alive)
            first = not order if mine is None else order[0][3] == mine
            if first:
                try:
                    lock.mkdir()
                except FileExistsError:
                    text = lock_holder(lock)[0]
                    old = lockholder.gone(text, alive, started)
                    if old is not None:
                        print("gate.py: the gate lock %s was held by pid %d, which has ended: taken over (%s)" % (lock, old, text),
                              flush=True)
                        board_line(board, "%s NOTICE: the gate lock held by a process that has ended (%s) is taken over."
                                   % (who, text))
                        shutil.rmtree(lock, ignore_errors=True)
                        continue
                else:
                    (lock / "holder.txt").write_text("%s %s\n" % (holder, datetime.datetime.now().strftime(
                        "%Y-%m-%d %H:%M:%S")), encoding="utf-8")
                    board_line(board, "%s LOCK: gate.lock taken by gate.py: %s." % (who, holder))
                    return None
                text = lock_holder(lock)[0]
            else:
                text = "a gate before this one in the queue"
            if clock() >= deadline:
                return "the gate lock %s is held: %s" % (lock, text)
            if mine is None:
                queue_folder(lock).mkdir(exist_ok=True)
                mine = queue_folder(lock) / ("%d-%d-%d.txt" % (queue_rank(holder), int(clock() * 1000), pid))
                mine.write_text(holder + "\n", encoding="utf-8")
                continue
            if not said_waiting:
                print("gate.py: the gate lock is held (%s); waiting up to %g minutes" % (text, wait_minutes), flush=True)
                said_waiting = True
            sleep(poll)
    finally:
        if mine is not None:
            mine.unlink(missing_ok=True)


def free_gate_lock(path, holder, board, who, summary):
    """Frees the gate lock when this run holds it (its holder line is this run's), with the run's summary."""
    text, _ = lock_holder(path)
    if text.startswith(holder):
        shutil.rmtree(path, ignore_errors=True)
        board_line(board, "%s FREE: gate.lock freed by gate.py: %s; %s." % (who, holder, summary))


def quick_passed(work, commit, whole=False):
    """The newest quick gate of this commit in the work folder that passed whole (`whole`; section 5.2), or whose
    QUICK_FIRST checks all passed, else None. A run of some checks (--only) does not count."""
    for folder in sorted(pathlib.Path(work).glob("*-%s-quick" % commit), reverse=True):
        try:
            result = json.loads((folder / "result.json").read_text(encoding="utf-8"))
            rows = {r["check"]: r["passed"] for r in result["checks"]}
        except (OSError, ValueError, KeyError, TypeError):
            continue
        if result.get("only") or result.get("dirty"):
            continue
        if (result.get("passed") is True) if whole else all(rows.get(c) for c in QUICK_FIRST):
            return folder
    return None


def take_device_lock(path, who, alive=process_alive, started=lockholder.process_started):
    """The device's lock (tools/lumacore/luma_test_android.py's): None when taken, else who holds it. The holder line ends with the
    holder's `since T pid N`; a holder whose process has ended (or runs from another start) loses the lock, with one printed line, so that a
    run that died with the lock does not keep the tablet away for ever. A lock taken by hand (no pid in its line) holds until it is freed."""
    lock = pathlib.Path(path)
    for _ in range(3):
        try:
            lock.mkdir()
        except FileExistsError:
            holder = lock / "holder.txt"
            text = holder.read_text(encoding="utf-8", errors="replace").strip() if holder.exists() else "unknown"
            old = lockholder.gone(text, alive, started)
            if old is not None and old != os.getpid():
                print("gate.py: the device lock %s was held by pid %d, which has ended: taken over (%s)" % (lock, old, text), flush=True)
                shutil.rmtree(lock, ignore_errors=True)
                continue
            return text
        (lock / "holder.txt").write_text("%s %s %s\n" % (who, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), lockholder.named()),
                                         encoding="utf-8")
        return None
    return "the device lock could not be taken"


# What each line of pictures.py check starts with, as a kind (tools/gate/kinds.py).
PICTURE_KINDS = {"same": "pass", "new": "new", "declared": "new", "changed": "fail", "missing": "fail", "machine": "env"}


def picture_result(g, label, renderer, place, log, keep_new=True):
    """pictures.py check on a harness suite's runs against the approved pictures of that place, pc or tablet (tolerance none;
    the manifest, the store and PICTURES-CHANGE.txt are the tree under test's). (passed, detail, cases, kind): each picture
    is a case of its own kind: same is pass, a picture with no approved copy or one the change declares is new, a difference
    nothing declares or an approved one not taken (above the quick level) is fail, another graphics card or driver than the
    approved pictures name is env, and no summary line, a bad declaration file, or no picture at all is tool. `keep_new`: the
    new and declared pictures go to <run>/new-pictures/<check>/ (the landing approves them)."""
    work = g.work / ("runs-%s-%s" % (label, renderer))
    tag = "%s-%s-%s" % (label, renderer, place)
    cmd = [sys.executable, "-X", "utf8", "-I", HERE / "pictures.py", "check", "--runs", work, "--renderer", renderer,
           "--place", place, "--manifest", pathlib.Path(g.tree) / picture_tool.MANIFEST_NAME, "--store", PICTURE_STORE,
           "--changes", pathlib.Path(g.tree) / picture_tool.CHANGES_NAME]
    if g.level not in SHORT_LEVELS and (label, renderer) not in g.partial:
        cmd.append("--complete")
    if keep_new:
        cmd += ["--new-out", g.work / "new-pictures" / tag]
    code = run(cmd, log, cwd=TREE)
    lines = pathlib.Path(log).read_text(encoding="utf-8", errors="replace").splitlines()
    results = {}
    for line in lines:
        words = line.split(None, 2)
        if len(words) >= 2 and words[0] in PICTURE_KINDS:
            results[words[1]] = (PICTURE_KINDS[words[0]], words[2].strip() if len(words) > 2 else "")
    summary = next((l for l in reversed(lines) if l.startswith("pictures: ")), None)
    review = next((l for l in lines if l.startswith("review: ")), "")
    others = [w[0] + " " + w[1] for w in (l.split() for l in lines) if len(w) > 1 and w[0] in PICTURE_KINDS and w[0] != "same"]
    detail = ((summary or "no summary")[len("pictures: "):] + ("; not: " + ", ".join(others[:8]) + (" ..." if len(others) > 8 else "")
                                                                if others else ""))
    problem = None
    if summary is None:
        problem = "pictures.py reported no summary: %s" % (last_line("\n".join(lines)) or "nothing")
    elif summary.startswith("pictures: 0 of 0") and not any(k == "env" for k, _ in results.values()):
        problem = "the runs took no picture: nothing was compared"
    elif code == 2:
        problem = "pictures.py stopped: %s" % (last_line("\n".join(lines)) or "no reason given")
    cases = kinds.Cases(results, None, problem, renderer, None)
    return code == 0, detail + ("; " + review if review else ""), cases, "fail"


def pictures(g, label, renderer, place="pc"):
    """The pictures a harness suite's runs took against the approved ones of that place, pc or tablet
    (tools/gate/pictures.py). Above the quick level it also fails on an approved picture its runs did not take."""
    def fn(log):
        if (label, renderer, place) not in g.picture_calls:
            g.picture_calls.append((label, renderer, place))
        return picture_result(g, label, renderer, place, log)
    return fn


def picture_recheck(g):
    """The picture checks of the run again on the same run folders, as the tree stands now (with the approvals): [(check,
    state)]. No game runs."""
    states = []
    for label, renderer, place in list(g.picture_calls):
        log = g.work / ("pictures-again-%s-%s-%s.log" % (label, renderer, place))
        try:
            passed, _, cases, _ = picture_result(g, label, renderer, place, log, keep_new=False)
            _, state = kinds.judge_cases(cases, {}, (), passed)
        except Exception as error:  # noqa: BLE001 - a check that raised is the tool's: not a pass
            state = "tool (%s: %s)" % (type(error).__name__, error)
        states.append(("pictures/%s/%s%s" % (label, renderer, "" if place == "pc" else "/" + place), state))
    return states


def declared_check(g):
    """Every line of PICTURES-CHANGE.txt names a differing picture some picture check took (a line that names none is stale:
    tool). When the tablet's part was owed, its pictures were not taken, so a stale line cannot be told from one for them:
    env. A step's landing (LAND_LEVEL, mend M-006) plays the 12 quick stops only: a line no picture of theirs used waits for the
    section's full check, which judges it (a line a step's landing used is marked, pictures.STEP_MARK, and is not stale there)."""
    def fn(log):
        entries = picture_tool.gather(g.work, PICTURE_STORE)
        path = pathlib.Path(g.tree) / picture_tool.CHANGES_NAME
        try:
            unused = picture_tool.unused_declarations(path, entries)
        except picture_tool.DeclarationError as error:
            return False, str(error), None, "tool"
        if not unused:
            return True, "every declared change names a differing picture" if path.is_file() else "no declared change"
        if g.level == LAND_LEVEL:
            return True, "%d declared change%s name no picture of the 12 quick stops: the section's full check judges %s" % (
                len(unused), "" if len(unused) == 1 else "s", "it" if len(unused) == 1 else "them")
        owed = any(r["state"] == "owed" for r in g.results)
        text = "; ".join("%s line %d: %s" % (picture_tool.CHANGES_NAME, n, t) for n, t in unused[:4])
        if owed:
            return False, "%d declared change%s name no differing picture (the tablet's pictures were not taken): %s" % (
                len(unused), "" if len(unused) == 1 else "s", text), None, "env"
        return False, "%d declared change%s name no differing picture (stale): %s" % (
            len(unused), "" if len(unused) == 1 else "s", text), None, "tool"
    return fn


def approve_pictures(g, commit, say=print):
    """The approval at a landing (pictures.py approve_landing): (the commit main moves to, None), or (the tested commit, why
    not) when it could not be done. Nothing to approve: the tested commit."""
    if not g.new_pictures:
        return commit, None
    sha, why = picture_tool.approve_landing(g.tree, g.work, PICTURE_STORE, lambda: picture_recheck(g), GIT, say=say,
                                            carry=g.level == LAND_LEVEL)
    if why is not None:
        return commit, why
    return sha or commit, None


MOD_TESTS = TREE / "tools" / "lumacore" / "mod_tests.py"


def mods_on(g):
    """Whether this run has the mod suites and LumaLink's own check: the mods switch (tools/gate/scope.tsv, or --mods) is not
    off, and the mod tests are in the tree."""
    return getattr(g, "mods", "off") != "off" and MOD_TESTS.exists()


def mod_tests(g, renderer, bin_dir=None, label="mods", more_env=None):
    """The mod tests on one renderer, through the mods' own entry point (it builds the test mods with LumaLink, then
    runs them side by side, --mod-parallel at a time, each game in its own folder): every suite of tests/mods and of
    the pinned LumaLink's tests/game, or at the quick level the quick set (tests/mods/quick.json, one or more of every
    suite). `bin_dir`: another build's bin folder (the sanitizer build's), `label` its runs' name, `more_env` more for
    the games' environment. Passes when it exits 0; its rows name each test that did not pass."""
    def fn(log):
        work = g.work / ("runs-%s-%s" % (label, renderer))
        env = watch_env(renderer, dict(more_env or {}, LUMA_TEST_PAKS=g.paks_text()))
        if g.args.lumalink:
            env["LUMALINK"] = g.args.lumalink
        else:
            try:
                program, checkout = pinned(g)
            except SystemExit as stop:
                pathlib.Path(log).write_text("%s\n" % stop, encoding="utf-8")
                return False, str(stop)
            env["LUMALINK"], env["LUMALINK_TREE"] = str(program), str(checkout)
        cmd = [sys.executable, "-X", "utf8", "-I", MOD_TESTS, "--bin", bin_dir or g.bin, "--work", work, "--renderer",
               renderer, "--lock", g.work / ("harness-%s-%s.lock" % (label, renderer)), "--commit", g.commit, "--jobs", g.args.jobs,
               "--parallel", str(g.args.mod_parallel)]
        if g.level in SHORT_LEVELS or g.mods == "short":
            cmd += ["--set", "quick"]
        with game_slot(g, g.args.mod_parallel):
            code = run(cmd, log, env=env, cwd=g.args.build_root)
        for d in work.rglob("bin"):
            if d.is_dir():
                shutil.rmtree(d, ignore_errors=True)
        seen = verdicts(log, renderer)
        cases = kinds.cases_of_verdicts(seen, kind_of_verdict, renderer)
        if not seen:
            cases.problem = "ran no case"
        elif code != 0 and all(v == "pass" for v in seen.values()):
            cases.problem = "the mod tests exited %d though every test passed" % code
        return code == 0 and not cases.problem, summarize(log, renderer), cases
    return fn


def verdicts(log, renderer):
    """{test: verdict} from the harness's result rows (name, renderer, verdict, reason)."""
    text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace").splitlines()
    return {l.split()[0]: l.split()[2] for l in text if len(l.split()) >= 3 and l.split()[1] == renderer}


def summarize(log, renderer):
    """'N of M pass; not: ...' from the harness's result rows (name, renderer, verdict, reason)."""
    text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace").splitlines()
    rows = [l.split() for l in text if len(l.split()) >= 3 and l.split()[1] == renderer]
    bad = [r for r in rows if r[2] != "pass"]
    detail = "%d of %d pass" % (len(rows) - len(bad), len(rows))
    if bad:
        detail += "; not: " + ", ".join("%s %s" % (r[0], r[2]) for r in bad[:6]) + (" ..." if len(bad) > 6 else "")
    return detail


def last_line(text):
    """The line that says what went wrong: the first error PowerShell or a tool printed, else the last line."""
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    said = next((l for l in lines if "Exception" in l or ": error" in l or l.startswith("error")), None)
    return (said or (lines[-1] if lines else "(no output)"))[:160]


# GitHub ----------------------------------------------------------------------------------------------------------
# The rule itself (the jobs' classes, which run counts for a commit, the runs again, the limit) is tools/gate/github_rule.py
# with tools/gate/github_jobs.tsv; this is the reading of GitHub (gh), the watch while the checks run, and the rows the
# result becomes in a run.


def gh_json(*args):
    r = subprocess.run(["gh", *args], capture_output=True, text=True, cwd=TREE, timeout=120)
    if r.returncode != 0:
        raise RuntimeError("gh %s: %s" % (" ".join(args[:2]), (r.stderr or r.stdout).strip()[-200:]))
    return json.loads(r.stdout or "null")


def commit_runs(sha):
    """[{id, branch, jobs}] of every push run of the commit on GitHub, under any branch name, newest first."""
    runs = gh_json("run", "list", "--commit", sha, "--workflow", WORKFLOW, "--event", "push", "--json",
                   "databaseId,headBranch", "--limit", "20") or []
    return [{"id": r["databaseId"], "branch": r.get("headBranch") or "?",
             "jobs": gh_json("run", "view", str(r["databaseId"]), "--json", "jobs").get("jobs", [])} for r in runs]


def gh_rerun(run_id, job_id):
    """(ok, what happened): the job run again (`gh run rerun RUN --job JOB`); where GitHub refuses that for the run's state,
    the run's failed jobs (`--failed`). One request each; the caller asks again later when both are refused."""
    said = ""
    for extra in (["--job", str(job_id)], ["--failed"]):
        r = subprocess.run(["gh", "run", "rerun", str(run_id), *extra], capture_output=True, text=True, cwd=TREE, timeout=120)
        if r.returncode == 0:
            return True, "gh run rerun %s %s" % (run_id, " ".join(extra))
        said = (r.stderr or r.stdout).strip()[-150:]
    return False, "refused (%s)" % said


def gh_log(job_id):
    """The failed steps' log of a job (`gh run view --job JOB --log-failed`)."""
    r = subprocess.run(["gh", "run", "view", "--job", str(job_id), "--log-failed"], capture_output=True, text=True,
                       cwd=TREE, timeout=180, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        raise RuntimeError("gh run view --log-failed: %s" % (r.stderr or r.stdout).strip()[-150:])
    return r.stdout


def planned_jobs(planned):
    """The jobs planned.tsv's github/<job name> lines take out of the move's rule (each with its ledger row)."""
    return {name[len("github/"):] for name in planned if name.startswith("github/")}


def red_minute(job):
    """' at HH:MM' (this computer's time) of a job's end, from GitHub's completedAt; '' where it has none."""
    return github_rule.minute_of(job)


class Notes:
    """github.log of a run: the readings' notes with the time. Kept until the run's folder is known (`to`)."""

    def __init__(self):
        self.path, self.kept = None, []

    def __call__(self, text):
        line = "%s %s\n" % (datetime.datetime.now().strftime("%H:%M:%S"), text)
        if self.path is None:
            self.kept.append(line)
        else:
            with open(self.path, "a", encoding="utf-8") as f:
                f.write(line)

    def to(self, path):
        self.path = path
        with open(path, "a", encoding="utf-8") as f:
            f.write("".join(self.kept))
        self.kept = []


def make_rule(sha, classes, planned=(), notes=None):
    """The rule for a commit, reading GitHub through gh (the tests put fakes in commit_runs, gh_rerun and GITHUB_CLOCK)."""
    try:
        setup = github_rule.setup_classifier(github_rule.WORKFLOW_FILE.read_text(encoding="utf-8"))
    except OSError:
        setup = None      # no workflow file to read the steps from: a red job stays red
    return github_rule.Rule(sha, classes, commit_runs, gh_rerun, planned, clock=GITHUB_CLOCK, note=notes, setup=setup)


def github_again(rule, args, commit, say=print, faults=None):
    """--github-again (before the red-before-start check): every `must` job that is red for the commit is run again once on
    GitHub; each is written to the tool-fault record as kind ENV with the job's name and the first line of its saved log
    (<work>/github-again-<commit>/github-<job>.log), and to github.log. The run then goes on and judges by the new result.
    Returns the names of the jobs run again."""
    try:
        runs = rule.runs_of(rule.sha)
    except github_rule.ANSWER_ERRORS as e:
        say("gate.py: --github-again: GitHub did not answer: %s" % e)
        return []
    states = [s for s in github_rule.judge(runs, rule.classes, rule.planned, rule.setup).values()
              if s.klass == github_rule.MUST and s.state == "red"]
    agent = getattr(args, "agent", None) or os.environ.get("NEBULA_AGENT", "1")
    folder = pathlib.Path(args.work) / ("github-again-%s" % commit[:9])
    done = []
    for st in states:
        ok, text = rule.rerun_red(st)
        if not ok:
            say("gate.py: --github-again: %s could not be run again: %s" % (st.name, text))
            continue
        folder.mkdir(parents=True, exist_ok=True)
        finding = github_rule.Finding(st, st.said())
        github_rule.save_log(finding, folder, gh_log, say=lambda line: None, advise=False)
        first = github_rule.first_log_line(finding.log.read_text(encoding="utf-8")) if finding.log is not None else ""
        message = "%s run again once on GitHub (--github-again); first line of its log: %s" % (st.said(False), first or "(no log)")
        say("gate.py: --github-again: " + message)
        rule.note(message)
        if faults is not None:
            try:
                faults(agent, "", "ENV", "github/" + st.name, "", message, str(folder))
            except Exception as e:  # noqa: BLE001 - the record is a report; the re-run is made, and github.log has the line
                say("gate.py: the tool-fault record could not be written (%s: %s); the line is in github.log" % (type(e).__name__, e))
        done.append(st.name)
    return done


def red_on_github(sha, planned=(), runs_of=None, classes=None):
    """Why a main gate of `sha` would not start, already known on GitHub (section 8: read before the checks start): a red
    `must` job of any run of the commit, with its branch and the minute it went red. None when none is red, or when GitHub
    does not answer (the run then starts, and its own wait reads GitHub again)."""
    rule = github_rule.Rule(sha, classes or github_rule.read_table(), runs_of or commit_runs, None, planned)
    return rule.red_before()


class GithubWatch:
    """Reads GitHub once a minute while the gate's checks run (section 8); at the first red `must` job calls on_red with why (the
    job and the minute it went red) and stops reading. A job that ended broken twice is kept in `envs` (the run is then
    not judged, and does not wait). Its readings also run the cancelled jobs again."""

    def __init__(self, rule, branch="", on_red=None, every=None):
        self.rule, self.branch = rule, branch
        self.on_red = on_red or stop_runs
        self.every = WATCH_SECONDS if every is None else every
        self.red, self.reds, self.envs = None, [], []
        self._end = threading.Event()
        self._thread = threading.Thread(target=self._loop, name="github-watch", daemon=True)

    def start(self):
        self._thread.start()
        return self

    def stop(self):
        self._end.set()
        self._thread.join(timeout=10.0)

    def _loop(self):
        while not self._end.wait(self.every):
            try:
                reading = self.rule.read()
            except github_rule.ANSWER_ERRORS:
                continue  # asked again the next minute
            self.envs = reading.envs
            if reading.reds:
                self.reds = reading.reds
                self.red = "GitHub's jobs on %s: %s" % (self.branch, reading.detail())
                self.on_red(self.red)
                return


def github_rows(g, rule, kind, detail, reading, minutes, say=print):
    """The result of the rule as rows of the run (Gate.late): one `github/<job>` row for each job that stopped it (a red job
    with its log saved first: github-<job>.log in the run's folder, its first lines printed), `github` for a pass or for GitHub
    not answering. The jobs the table does not know and the advisory ones that ended red twice are told once, as rows that
    do not count (KIND tool / KIND env)."""
    if kind == "pass":
        g.late("github", "pass", detail)
    elif kind == "fail":
        for f in reading.reds:
            github_rule.save_log(f, g.work, gh_log, say)
            g.late("github/" + f.job, "fail", f.state.said(False) + ("; its log is %s" % f.log if f.log else ""), f.log)
    elif reading is not None and reading.envs:
        for f in reading.envs:
            g.late("github/" + f.job, "env", f.text)
    elif reading is not None and reading.pending:
        for name in reading.pending:
            g.late("github/" + name, "env", "not finished after %g minutes" % minutes)
    else:
        g.late("github", "env", detail)
    github_notes(g, rule)


def github_notes(g, rule):
    """The warnings and advisory lines of the rule, once: a job the table does not know (kind tool) and an advisory job that
    ended red twice (kind env). Neither counts: they change no verdict."""
    done = rule.told_rows
    for name, text in rule.warnings():
        if ("warning", name) not in done:
            done.add(("warning", name))
            g.late("github/" + name, "pass", text, None, [kinds.record("-", "tool", text, counts=False, note="warning")])
    for name, text in sorted(rule.advised.items()):
        if ("advice", name) not in done:
            done.add(("advice", name))
            g.late("github/" + name, "pass", text, None, [kinds.record("-", "env", text, counts=False)])


def github_part(g, wait_minutes=None, rule=None, sleep=None):
    """The commit's GitHub jobs as one of the gate's checks (a main gate without --move, the final check): every `must` job
    green for HEAD, waited for up to the same limit as a move's (--github-wait, 120 minutes unless given)."""
    if not g.wanted("github") or g.dry:
        return
    if wait_minutes is None:
        wait_minutes = g.args.github_wait
    try:
        classes = github_rule.read_table()
    except github_rule.TableError as e:
        g.record("github", "tool", 0.0, "tools/gate/github_jobs.tsv: " + str(e).splitlines()[0])
        return
    notes = Notes()
    notes.to(g.work / "github.log")
    rule = rule or make_rule(git("rev-parse", "HEAD"), classes, planned_jobs(g.planned), notes)
    kind, detail, reading = rule.wait(wait_minutes, sleep=sleep or GITHUB_SLEEP)
    github_rows(g, rule, kind, detail, reading, wait_minutes)


def settle_github_pending(live, say=print):
    """Reads the GitHub jobs of the landings that moved main before they ended (github_rule.settle_pending): one reading for each
    line of <live>/github-pending.tsv, no waiting. Called at the start of `gate.py land` and by the step tool's next, status and
    land. It never raises. Returns [(commit, what, text)]."""
    try:
        if not github_rule._live().read_pending(live):
            return []      # nothing is pending: no table to read, nothing to ask
        classes = github_rule.read_table()
        planned = planned_jobs(read_lists()[1])
    except Exception as e:  # noqa: BLE001 - the table or the lists are wrong: the lines stay, and the run says so once
        say("gate.py: GitHub's pending results were not read (%s: %s)" % (type(e).__name__, e))
        return []
    return github_rule.settle_pending(live, lambda sha: make_rule(sha, classes, planned), delete_candidate, gh_log, say)


# The candidates that wait for GitHub (section 5.8): one line each in <work>/waiting.tsv, "sha<TAB>branch<TAB>pid".


def waiting_path(work):
    return pathlib.Path(work) / "waiting.tsv"


def add_waiting(work, sha, branch, pid):
    with open(waiting_path(work), "a", encoding="utf-8") as f:
        f.write("%s\t%s\t%d\n" % (sha, branch, pid))


def drop_waiting(work, sha):
    path = waiting_path(work)
    if path.exists():
        keep = [l for l in path.read_text(encoding="utf-8").splitlines() if l.split("\t")[0] != sha]
        path.write_text("".join(l + "\n" for l in keep), encoding="utf-8")


def push_candidate(commit, candidate):
    """The commit pushed as the candidate branch (section 5.3): None, or why it could not be."""
    r = subprocess.run([GIT, "-C", str(TREE), "push", "origin", "%s:refs/heads/%s" % (commit, candidate)],
                       capture_output=True, text=True)
    return None if r.returncode == 0 else (r.stderr or r.stdout).strip()[-300:]


def delete_candidate(candidate):
    subprocess.run([GIT, "-C", str(TREE), "push", "origin", "--delete", candidate], capture_output=True, text=True)


def unheld_candidates(work, sha, tree=None, alive=None):
    """The waiting candidates (their gate still running) that `sha` does not hold: such a commit may not enter, since
    main must reach both by fast-forward."""
    path = waiting_path(work)
    if not path.exists():
        return []
    alive = alive or process_alive
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) != 3 or not alive(int(parts[2])):
            continue
        held = subprocess.run([GIT, "-C", str(tree or TREE), "merge-base", "--is-ancestor", parts[0], sha],
                              capture_output=True, text=True).returncode == 0
        if not held:
            out.append(parts[1])
    return out


# Main --------------------------------------------------------------------------------------------------------------


def git_ok(*args):
    return subprocess.run([GIT, "-C", str(TREE), *args], capture_output=True, text=True).returncode == 0


ALLOWED_ENV = "NEBULA_MAIN_ALLOWED"


RATCHET = pathlib.Path("D:/NebulaWork/agents/guards")


def push_checks(sha, tree=TREE, guards=RATCHET):
    """The pre-push hook's checks of main (the console census, the native ratchet), run before the gate's runs: a gate
    that passes is then never refused at the push. The ratchet runs on a copy of itself, since a pass rewrites its
    baseline. None when both pass, else why not."""
    census = pathlib.Path(tree) / "tools" / "verify" / "console_census.py"
    if census.exists():
        r = subprocess.run([sys.executable, "-I", str(census), str(tree)], capture_output=True, text=True)
        if r.returncode != 0:
            # What the census said: the groups that are not as they must be (a closed one above zero, an open one over its ceiling), then the
            # files with their counts.
            groups = [" ".join(l.split()) for l in r.stdout.splitlines() if re.search(r"\s[1-9]\d*\s+closed|OVER THE CEILING", l)]
            shut = [l.strip() for l in r.stdout.splitlines() if l.startswith("    ")]
            return "the console census refuses it (a closed group is not zero, or an open one is over its ceiling): %s; files: %s" % (
                "; ".join(groups[:5]), "; ".join(shut[:5]))
    script = pathlib.Path(guards) / "ratchet_prepush.py"
    if not script.exists():
        return None
    with tempfile.TemporaryDirectory(prefix="gate-ratchet-") as copy:
        for name in ("ratchet_prepush.py", "ratchet_baseline.json"):
            if (pathlib.Path(guards) / name).exists():
                shutil.copy2(pathlib.Path(guards) / name, copy)
        r = subprocess.run([sys.executable, "-I", str(pathlib.Path(copy) / "ratchet_prepush.py"), str(tree), sha],
                           capture_output=True, text=True)
    if r.returncode != 0:
        return "the native ratchet refuses it: %s" % (r.stdout + r.stderr).strip()[-400:]
    return None


# The push of main is tried again when it did not reach GitHub (a network failure): the seconds to wait before each next try, so six
# tries in all, about eight minutes. A refusal (the hook, GitHub's own answer) is never tried again.
PUSH_WAITS = (15, 30, 60, 120, 240)
# What git says when the network failed (lower case), and what it says when GitHub answered and refused.
PUSH_NETWORK_TEXTS = ("unable to access", "could not resolve host", "connection was reset", "connection reset", "timed out",
                      "failed to connect", "remote end hung up", "rpc failed", "early eof", "returned error: 5")
PUSH_REFUSED_TEXTS = ("returned error: 4", "[rejected]", "[remote rejected]")
# What git says when GitHub wants a sign-in nobody is there to give (noprompt.py keeps git from asking): nothing is landed, the page says so.
SIGN_IN_TEXTS = ("authentication failed", "could not read username", "could not read password", "terminal prompts disabled", "invalid username",
                 "bad credentials", "permission denied (publickey)", "returned error: 401", "returned error: 403")
SIGN_IN = "GitHub sign-in needed"


def needs_sign_in(text):
    low = (text or "").lower()
    return any(k in low for k in SIGN_IN_TEXTS)


def push_is_network(text):
    """Whether git's text of a failed push is the network's (the push may not have arrived, and is worth trying again) and not an
    answer of GitHub or of a hook (a refusal)."""
    low = text.lower()
    return any(k in low for k in PUSH_NETWORK_TEXTS) and not any(k in low for k in PUSH_REFUSED_TEXTS)


def move_main(sha, run_name, work, board, who, covers, tree=TREE, sleep=time.sleep):
    """Fast-forwards origin's main to sha (section 5.5): main must still be its ancestor. The push carries a one-time
    allowance the pre-push hook's guard (tools/gate/main_push_guard.py) reads (written for each try, gone after it). A push that
    failed is looked at: when origin's main holds sha the push arrived and only its answer was lost (a move); when main moved on,
    that is said; when git's text is the network's the push is tried again after PUSH_WAITS (six tries); any other text is a
    refusal and ends it. None when moved, else why not."""
    def tree_git(*args, env=None):
        return subprocess.run([GIT, "-C", str(tree), *args], capture_output=True, text=True, env=env)

    def main_moved(now):
        return "main moved during the gate (now %s): rebase on it and gate again" % now[:9]

    tree_git("fetch", "origin", "main")
    before = tree_git("rev-parse", "origin/main").stdout.strip()
    if tree_git("merge-base", "--is-ancestor", sha, before).returncode == 0:
        # A later candidate that held this one moved main first (section 5.8): the move is done.
        board_line(board, "%s MAIN-MOVED: main already holds %s (main is %s); gate %s." % (who, sha[:9], before[:9], run_name))
        return None
    if tree_git("merge-base", "--is-ancestor", before, sha).returncode != 0:
        return main_moved(before)
    allowed = pathlib.Path(work) / "main-allowed.txt"
    tries = len(PUSH_WAITS) + 1
    text, lost = "", False
    for attempt in range(tries):
        allowed.write_text("%s %s\n" % (sha, run_name), encoding="utf-8")
        try:
            r = tree_git("push", "origin", "%s:refs/heads/main" % sha, env=dict(os.environ, **{ALLOWED_ENV: str(allowed)}))
        finally:
            allowed.unlink(missing_ok=True)
        if r.returncode == 0:
            break
        text = (r.stderr or r.stdout).strip()
        # Did the push arrive, and only its answer get lost? Or did main move on? Look at origin before anything else.
        if tree_git("fetch", "origin", "main").returncode == 0:
            now = tree_git("rev-parse", "origin/main").stdout.strip()
            if now and tree_git("merge-base", "--is-ancestor", sha, now).returncode == 0:
                lost = True
                break
            if now and now != before:
                return main_moved(now)
        if needs_sign_in(text):
            return "%s: the push of main was refused (%s)" % (SIGN_IN, " ".join(text.split())[-200:])
        if not push_is_network(text):
            if "pre-push" in text or "census" in text.lower() or "ratchet" in text.lower():
                return "the pre-push hook refuses main (a repository rule): %s" % text[-300:]
            return "the push of main failed: %s" % text[-300:]
        if attempt == tries - 1:
            return "GitHub could not be reached for the push of main (%d tries in %d minutes): %s" % (
                tries, round(sum(PUSH_WAITS) / 60.0), text[-300:])
        wait = PUSH_WAITS[attempt]
        last = (text.splitlines() or [""])[-1].strip()
        print("gate.py: the push of main did not reach GitHub (%s); trying again in %d s (try %d of %d)" % (
            last, wait, attempt + 2, tries), file=sys.stderr, flush=True)
        sleep(wait)
    tree_git("fetch", "origin", "main")
    count = tree_git("rev-list", "--count", "%s..%s" % (before, sha)).stdout.strip()
    board_line(board, "%s MAIN-MOVED: main %s -> %s (%s commits)%s; gate %s.%s" % (
        who, before[:9], sha[:9], count, (": " + covers) if covers else "", run_name,
        " The answer of the push was lost; main holds it." if lost else ""))
    return None


LONG_EVERY_HOURS = 20.0


def long_due(work, hours=LONG_EVERY_HOURS, now=None):
    """Whether a long run is due (section 14): none in history.tsv and none started by a move (long-started.txt, the
    run's history line comes only at its end) within `hours`."""
    path = pathlib.Path(work) / "history.tsv"
    now = now or datetime.datetime.now()
    rows = [l.split("\t") for l in path.read_text(encoding="utf-8").splitlines()[1:]] if path.exists() else []
    started = [datetime.datetime.fromisoformat(r[0]) for r in rows if len(r) > 2 and r[2] == "long" and (len(r) < 12 or r[11] in ("-", ""))]
    marker = pathlib.Path(work) / "long-started.txt"
    if marker.exists():
        started.append(datetime.datetime.fromisoformat(marker.read_text(encoding="utf-8").split()[0]))
    return not started or (now - max(started)).total_seconds() >= hours * 3600


# The long run's own tree (Planner 11-18 04:39, N-1046): the gate's, not an agent's. It reads its tree for hours and
# must be on main's newest commit, so it cannot be the mover's (the agent edits that next). gate.py makes it with git
# worktree add on first use and brings it to each moved commit, detached, reset and cleaned; nobody edits or builds in
# it by hand. Its build folder is gate-Nebula-long beside the others (--build-root's default), pruned as they are.
LONG_TREE = pathlib.Path(os.environ.get("NEBULA_GATE_LONG_TREE", "D:/NebulaWork/MarioGalaxyRecomp/Nebula-long"))


def long_tree_at(sha, tree=LONG_TREE, source=TREE):
    """Brings the long run's tree to `sha` (made from `source`'s repository on first use): None, or why not."""
    tree = pathlib.Path(tree)

    def git_in(where, *a):
        r = subprocess.run([GIT, "-C", str(where), *a], capture_output=True, text=True)
        return None if r.returncode == 0 else "git %s: %s" % (a[0], (r.stderr or r.stdout).strip()[-300:])

    if not (tree / ".git").exists():
        return git_in(source, "worktree", "add", "--detach", str(tree), sha)
    for a in (("checkout", "-q", "--force", "--detach", sha), ("reset", "-q", "--hard", sha), ("clean", "-q", "-d", "-f", "-f")):
        why = git_in(tree, *a)
        if why is not None:
            return why
    return None


def long_lock_free(lock, alive=process_alive):
    """Whether the long lock is free: no lock folder, or one whose process has ended (the run then takes it over)."""
    if not pathlib.Path(lock).exists():
        return True
    return lockholder.gone(lock_holder(lock)[0], alive) is not None


def long_command(sha, args, tree=LONG_TREE):
    """The long run a move starts: gate.py --long of the long run's tree, with the mover's folders, locks and tablet."""
    cmd = [sys.executable, "-X", "utf8", "-I", str(pathlib.Path(tree) / "tools" / "gate" / "gate.py"), "--long",
           "--who", args.who, "--covers", "main %s, started by its move (section 14)" % sha[:9], "--work", args.work,
           "--board", args.board, "--status", args.status, "--gate-lock", args.gate_lock, "--long-lock", args.long_lock,
           "--tablet-lock", args.tablet_lock, "--discs", args.discs, "--jobs", args.jobs]
    cmd += ["--pak", args.pak] if args.pak else []
    cmd += ["--paks", args.paks] if args.paks else []
    return cmd + (["--tablet", args.tablet] if args.tablet else [])


LONG_BY_HAND_GAP_MINUTES = 10.0


def start_long(sha, args, tree=LONG_TREE, source=TREE, popen=subprocess.Popen, now=None, why=""):
    """After a move: when a long run is due and the long lock is free, brings the long run's tree to `sha` and starts
    the run there, detached and below normal priority (it lowers itself too, below_normal), its output in
    <work>/long-<sha>.log. Returns (command or None, what happened) for the board.
    `why` (gate.py long-start): a start by hand. The 20-hour rule is not asked, and the reason is written after the commit in
    long-started.txt; the long lock is asked as for a move (a holder that runs stops it, one that ended is taken over by the run),
    and so is a start of the last LONG_BY_HAND_GAP_MINUTES minutes, whose run may not have taken the lock yet."""
    if why:
        marker = pathlib.Path(args.work) / "long-started.txt"
        if marker.exists():
            last = datetime.datetime.fromisoformat(marker.read_text(encoding="utf-8").split()[0])
            if 0 <= ((now or datetime.datetime.now()) - last).total_seconds() < LONG_BY_HAND_GAP_MINUTES * 60:
                return None, "a long run was started less than %d minutes ago (long-started.txt)" % LONG_BY_HAND_GAP_MINUTES
    elif not long_due(args.work, now=now):
        return None, "no long run is due: the last one started less than %d hours ago" % LONG_EVERY_HOURS
    if not long_lock_free(args.long_lock):
        return None, "the long lock is held (%s)" % lock_holder(args.long_lock)[0]
    problem = long_tree_at(sha, tree, source)
    if problem is not None:
        return None, "its tree %s could not be brought to %s: %s" % (tree, sha[:9], problem)
    cmd = long_command(sha, args, tree)
    log = pathlib.Path(args.work) / ("long-%s.log" % sha[:9])
    log.parent.mkdir(parents=True, exist_ok=True)
    # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | BELOW_NORMAL_PRIORITY_CLASS, and out of the mover's job object
    # (machine.start_outliving): the run outlives the mover's gate.
    detach = {"creationflags": 0x8 | 0x200 | 0x4000} if os.name == "nt" else {"start_new_session": True}
    with open(log, "ab") as out:
        machine.start_outliving(popen, cmd, cwd=str(tree), stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                                close_fds=True, **detach)
    (pathlib.Path(args.work) / "long-started.txt").write_text(
        "%s %s%s\n" % ((now or datetime.datetime.now()).isoformat(timespec="seconds"), sha,
                       (" by hand: " + " ".join(why.split())) if why else ""), encoding="utf-8")
    return cmd, "the long run of main %s started in %s (log %s)" % (sha[:9], tree, log)


def long_start_parser():
    """The options of `gate.py long-start`: a run's options and the two of its own."""
    p = parser()
    p.prog = "gate.py long-start"
    p.add_argument("--why", default="", help="why the long run is started by hand (written in the marker that stops a second start for 20 hours)")
    p.add_argument("--sha", default="", help="the commit to run on (default origin/main of this repository, as fetched)")
    return p


def long_start_main(argv):
    """gate.py long-start --why TEXT [--sha COMMIT] [the options of a run, e.g. --who NAME --tablet SERIAL]: the long run of a commit
    (default: origin/main as the repository has it now: fetch first), started by hand with start_long's own call; for a day on which
    the 20-hour rule stands in the way (a long run that died after its start was written). Exit 0 started, 2 not started."""
    args = long_start_parser().parse_args(["--main"] + list(argv))
    if not args.why.strip():
        print("gate.py long-start: --why TEXT is needed (it is written in long-started.txt)", file=sys.stderr)
        return 2
    r = subprocess.run([GIT, "-C", str(TREE), "rev-parse", "--verify", (args.sha or MAIN_REF) + "^{commit}"], capture_output=True, text=True)
    if r.returncode != 0:
        print("gate.py long-start: no such commit: %s" % (args.sha or MAIN_REF), file=sys.stderr)
        return 2
    sha = r.stdout.strip()
    started, what = start_long(sha, args, why=args.why)
    print("gate.py: long run: %s" % what)
    if started is None:
        return 2
    print("LONG-STARTED %s" % (pathlib.Path(args.work) / ("long-%s.log" % sha[:9])), flush=True)
    board_line(args.board, "%s LONG: %s; started by hand: %s." % (args.who, what, " ".join(args.why.split())))
    return 0


# The tablet --------------------------------------------------------------------------------------------------------


def android_module():
    import importlib.util
    spec = importlib.util.spec_from_file_location("luma_test_android", TREE / "tools" / "lumacore" / "luma_test_android.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def take_device_lock_wait(path, who, wait_minutes, sleep=time.sleep):
    """The device's lock, waited for up to wait_minutes (a main gate and the long run share the tablet through it)."""
    deadline = time.time() + 60.0 * wait_minutes
    while True:
        held = take_device_lock(path, who)
        if held is None or time.time() >= deadline:
            return held
        sleep(30.0)


def tablet_needs():
    """What each of the tablet part's checks needs (the part and the long run's pieces name them alike)."""
    built = ("build/android",)
    return {"tablet/prepare": built + ("data/RMGE01", "tablet/remove"), "android/tablet": built + ("tablet/prepare",),
            "tour/tablet": built + ("tablet/prepare",), "tablet/remove": (), "tour/tablet-pc": DATA,
            "replays/tablet": ("tour/tablet", "tour/tablet-pc"), "pictures/tablet": ("tour/tablet",)}


def tablet_part(g, stops, device=None, pc_tail=True):
    """The tablet's part: the test app installed with the pak, tests/android, a tour of `stops` (None: all), the
    test app removed; then the PC plays the same stops at the tablet's screen size and both must take the same game
    steps at every step (the game runs the actors its picture shows: its clipping follows the picture's shape). When
    the tablet does not answer, its lock stays held, or it leaves during the run, the part is owed (section 4.3). `pc_tail` False
    (a landing, B14 part 3): the PC's half (tablet_after) is left to the caller, which plays it beside the tablet's lane."""
    needs = tablet_needs()
    # The tablet's pictures have rows of their own (place tablet, N-1040) for the main level's twelve stops; the long run's tour
    # takes more.
    checks = TABLET_CHECKS + (("pictures/tablet",) if g.level in ("main", LAND_LEVEL) else ())
    wanted = [n for n in checks if g.wanted(n, needs[n])]
    if g.dry or not wanted:
        return
    if not g.android_built:
        for n in wanted:
            g.missing(n, "the Android build failed")
        return
    if device is None:
        # Before the first adb call of the run: an adb server of its own, out of the run's job (machine.ensure_adb_server).
        machine.ensure_adb_server(android_module().Device("").adb_path)
        device = android_module().Device(g.args.tablet)

    def owe_rest(why):
        for n in wanted:
            if not any(r["check"] == n for r in g.rows()):
                g.owe(n, why)

    if not device.answers():
        owe_rest("%s does not answer adb" % g.args.tablet)
        return
    held = take_device_lock_wait(g.args.tablet_lock, "gate %s" % g.name, g.args.tablet_wait)
    if held is not None:
        owe_rest("the tablet's lock stayed held %g minutes: %s" % (g.args.tablet_wait, held))
        return
    gone = None
    try:
        prepared = g.check("tablet/prepare", device_step(g, "prepare"))
        if prepared is False and not device.answers():
            gone = "the tablet left adb"
            g.owe_last(gone)
        elif prepared:
            g.check("android/tablet", harness(g, TREE / "tests" / "android" / "suite.json", "vulkan", "android",
                                              g.args.tablet))
            if not device.answers():
                gone = "the tablet left adb"
                g.owe_last(gone)
            else:
                tour = gate_tour.write(g.work / "tour-tablet", stops)
                step_hash.with_hash(tour.parent)
                g.check("tour/tablet", harness(g, tour, "vulkan", "tour-tablet", g.args.tablet))
                if "pictures/tablet" in wanted:
                    g.check("pictures/tablet", pictures(g, "tour-tablet", "vulkan", "tablet"), ("tour/tablet",))
                if not device.answers():
                    gone = "the tablet left adb"
                    g.owe_last(gone)
    finally:
        if gone is None and device.answers():
            g.check("tablet/remove", device_step(g, "remove"))
        else:
            g.owe("tablet/remove", "the tablet left adb: the test app may still be on it")
        shutil.rmtree(g.args.tablet_lock, ignore_errors=True)
    if gone is not None:
        owe_rest(gone + " during its part")
        return
    g.tablet_tail_ok = True
    if pc_tail:
        tablet_after(g, stops)


def tablet_screen_of(device):
    """The tablet's screen size as the game plays it, landscape: (the longer side, the shorter), from `wm size` (its Override size where it
    has one, else its Physical size, which is the natural, portrait one: 1752x2800 for the 2800x1752 the Vulkan swap chain reports), or None."""
    code, out = device.shell("wm size")
    sizes = re.findall(r"(Override|Physical) size: (\d+)x(\d+)", out) if code == 0 else []
    if not sizes:
        return None
    chosen = next((x for x in sizes if x[0] == "Override"), sizes[0])
    a, b = int(chosen[1]), int(chosen[2])
    return max(a, b), min(a, b)


def tablet_pc_tour(g, stops, screen):
    """tour/tablet-pc: the PC plays the tablet's stops at the tablet's screen size (the game runs the actors its picture shows)."""
    pc_tour = gate_tour.write(g.work / "tour-tablet-pc", stops, window=screen)
    step_hash.with_hash(pc_tour.parent)
    g.tablet_pc_screen = screen
    g.check("tour/tablet-pc", harness(g, pc_tour, "d3d12", "tour-tablet-pc"))


def screen_difference(asked, got):
    """The one plain line of two screen sizes that are not equal (kind tool: never a difference of hashes)."""
    return ("the tablet's tour reports a screen of %dx%d and the PC's run of its stops used %dx%d" % (got[0], got[1], asked[0], asked[1]))


def tablet_after(g, stops):
    """The checks that need both sides of the tablet's tour, once the tablet's lane has ended: the PC's tour at the tablet's size where it was
    not played already (the tablet's size was not known at the lane's start), the size the tablet's tour reports against the one the PC's
    run used (unequal: kind tool, and replays/tablet is not compared), and replays/tablet. The tablet away: its rows were owed by the lane."""
    if g.dry:
        return
    screen = device_screen(g.work / "runs-tour-tablet-vulkan")
    played = g.tablet_pc_screen is not None or any(r["check"] == "tour/tablet-pc" for r in g.results)
    if screen is None:
        if not played:
            g.missing("tour/tablet-pc", "the tablet's runs name no screen size")
        g.missing("replays/tablet", "the tablet's runs name no screen size")
        return
    if not played:
        tablet_pc_tour(g, stops, screen)
    elif g.tablet_pc_screen is not None and g.tablet_pc_screen != screen:
        why = screen_difference(g.tablet_pc_screen, screen)
        g.record("tablet/screen", "tool", 0.0, why)
        g.record("replays/tablet", "tool", 0.0, "not compared: " + why)
        return
    g.check("replays/tablet", replays(g, "tour-tablet", "vulkan", ("tour-tablet-pc", "d3d12")))


# The machine (tools/gate/machine.py, package T5) ----------------------------------------------------------------------------

# The checks of a run that start the game (on this computer or the tablet), and those that never do: a run whose checks are all of
# the second kind (documents, tools, builds) asks nothing of the machine. A test pins that every check of every level is in one.
GAME_CHECKS = ("start/", "engine/", "tour", "mods", "soak", "coverage", "android/", "tablet/", "measure/", "speed/", "validate/", "stress/")
NO_GAME_CHECKS = ("repo/", "build/", "data/", "unit/", "rust", "lumalink", "import/", "harness/", "description/", "pictures/",
                  "replays/", "github", "part/", "lane/", "gate/")
# The locks a held gate, long run or measure keeps: a game process that one of them may own is never ended as a leftover.
MACHINE_LOCKS = ("gate.lock", "long.lock", "measure.lock", "move.lock")


def starts_game(name):
    """Whether a check starts the game."""
    return any(name.startswith(p) for p in GAME_CHECKS)


def run_starts_game(selected):
    """Whether a run starts the game: every check of its level (no selection) does, else one whose selection names such a check."""
    return True if selected is None else any(starts_game(n) for n in selected)


def run_has_tablet_part(args, level, selected):
    """Whether the run has a tablet part: a tablet is named and the level (or the selection) has one."""
    if not args.tablet:
        return False
    if selected is not None:
        return any(n in TABLET_NAMES for n in selected)
    return level != "quick"


def lanes_asked(g):
    """The game copies the lanes of this run ask to run at once, at most (run_checks builds the lanes; a test keeps the two
    equal): a lane of each renderer's engine and tour tests, a lane of each other disc, each mods lane's --mod-parallel games, the
    Android lane (its PC run beside the tablet's) and, at the final check, the sanitizer and coverage lanes. The long run plays
    its parts one after another: the biggest part. --serial runs them in turn."""
    level = g.level
    if g.args.serial:
        return 1
    renderers = 1 if level == "quick" else len(RENDERERS)
    regions = 0 if level == LAND_LEVEL else len(REGION_DISCS)
    tablet = 0 if level == "quick" else 1
    if level == "long":
        # The long run's parts play one after another: the biggest is the stress part (twelve copies, stress_part).
        return max(renderers + regions, tablet, stress.COPIES)
    mods = renderers * g.args.mod_parallel if mods_on(g) else 0
    # The validate lane: one copy for each renderer (asking for it costs nothing where the layer is not installed).
    validate_copies = 0 if level == "quick" else len(RENDERERS)
    asked = renderers + regions + mods + tablet + validate_copies + (2 if level == "final" else 0)
    stress_after = level == "final" or (level in MEASURE_LEVELS and "stress" in landing.also_names(getattr(g.args, "also", "")))
    return max(asked, stress.COPIES) if stress_after else asked     # the stress part follows the lanes (the final check's; a landing's --also)


def renderers_of(level):
    """The renderers a level's runs play on."""
    return ("d3d12",) if level == "quick" else RENDERERS


class RealTablet:
    """The gate's tablet, as machine_ready asks about it (tests put a fake in its place)."""

    def __init__(self, g, device=None):
        self.g = g
        self.serial = g.args.tablet
        self.device = device or android_module().Device(self.serial)

    def state(self):
        """What adb says of the tablet: device (it answers and is authorised), unauthorized, offline, or absent (not listed)."""
        r = subprocess.run([self.device.adb_path, "devices"], capture_output=True, env=self.device.env, timeout=60)
        for line in r.stdout.decode("utf-8", "replace").splitlines():
            parts = line.split()
            if len(parts) >= 2 and parts[0] == self.serial:
                return parts[1]
        return "absent"

    def data(self):
        """(whether the test app is on the tablet, the store version of its game.pak or None when it has none)."""
        if not self.device.installed():
            return False, None
        head = self.device.pak_header()
        if len(head) >= 12 and head[:8] == b"NBSTORE\0":
            return True, int.from_bytes(head[8:12], "little")
        return True, None

    def kept(self):
        """(key, size) of the game data kept on the tablet between runs (luma_test_android.py kept_report), or None."""
        return android_module().kept_report(self.device)

    def push(self, pak):
        """The test data pushed again (luma_test_android.py push-data): (ok, what it said)."""
        log = self.g.work / "machine-push-data.log"
        code = run([sys.executable, "-X", "utf8", "-I", TREE / "tools" / "lumacore" / "luma_test_android.py", "push-data",
                    "--device", self.serial, "--pak", pak, "--who", "gate", "--device-lock", self.g.args.tablet_lock], log,
                   timeout=3600)
        return code == 0, last_line(pathlib.Path(log).read_text(encoding="utf-8", errors="replace"))


class GateProbes(machine.Probes):
    """machine.Probes with the gate's own parts: the tablet, the tree's store version, the data a push would use."""

    def adb_server(self, g):
        machine.ensure_adb_server(android_module().Device("").adb_path)

    def tablet(self, g):
        return RealTablet(g)

    def tree_store_version(self):
        return gamedata.tree_store_version(TREE)

    def data_pak(self, g, want):
        """(a game.pak of store version `want` that a push could use or None, a problem or ""): --pak when it is given (a wrong
        version is the problem), else the data found by key (None while it is not made: the run makes it and pushes it itself)."""
        if g.args.pak:
            try:
                version = gamedata.read_header(g.args.pak)[0]
            except ValueError as error:
                return None, "the game data --pak names does not open: %s" % error
            if version != want:
                return None, "the game data --pak names (%s) is store version %d, this tree's is %d" % (g.args.pak, version, want)
            return g.args.pak, ""
        try:
            pak = gamedata.GameData().lookup(gamedata.key_of(US_DISC, want, gamedata.importer_tree_hash(GIT, TREE)))
            return (str(pak) if pak else None), ""
        except (OSError, ValueError) as error:
            return None, "the game data could not be looked up: %s" % error


def machine_ready(g, asked, tablet, soak=0, probes=None, learned=None):
    """Before a run that starts the game: can this machine carry it? In this order, each a row of kind env with one plain line when
    it cannot be met (the run then ends not judged; nothing else runs): leftover game processes ended; drive D: has the room the
    level needs, old run folders thinned first; the tablet, when it answers, holds this tree's test data (pushed again when it does
    not; a tablet that does not answer is not a stop, its part is owed); the graphics card and the PC's memory have room for at least one copy of the game (waited for up to four
    hours). Sets how many copies run at once (g.slots: what fits, at most `asked`, a place kept for the soak), and starts the
    thread that reads the game processes' use of the card. What it did is in g.machine_notes, for the run's summary. True when the
    run goes on. A tablet that does not answer or is not authorised is no point of these: its part is owed (tablet_part)."""
    import prune_runs
    probes = probes or GateProbes()
    notes = g.machine_notes

    def say(text):
        print(text, flush=True)

    def fail(name, text):
        g.record(name, "env", 0.0, text)
        return False

    # 1. Leftovers.
    procs = probes.processes()
    holders = machine.read_holders([pathlib.Path(g.args.gate_lock).with_name(n) for n in MACHINE_LOCKS], procs)
    leftovers, kept = machine.find_leftovers(procs, probes.clock(), holders)
    for proc, why in kept:
        notes.append("a game process left from an earlier run was not ended, %s: %s (process %d)" % (why, proc.path, proc.pid))
    if leftovers:
        for proc in leftovers:
            probes.end(proc.pid)
        alive = {p.pid for p in probes.processes()}
        stuck = [p for p in leftovers if p.pid in alive]
        if stuck:
            return fail("machine/leftovers", "a game process left from an earlier run could not be ended: %s (process %d)" % (
                stuck[0].path, stuck[0].pid))
        notes.append("ended %d game process%s left from earlier runs (their parents were gone, older than ten minutes): %s" % (
            len(leftovers), "" if len(leftovers) == 1 else "es", "; ".join("%s, process %d, started %s" % (
                p.path, p.pid, datetime.datetime.fromtimestamp(p.started).strftime("%m-%d %H:%M")) for p in leftovers)))

    # 2. Disk.
    need = machine.disk_need(g.level)
    free = probes.free_disk(str(g.args.work))
    if free < need:
        runs, freed = machine.thin_runs(g.args.work, held=prune_runs.waiting(g.args.work))
        after = probes.free_disk(str(g.args.work))
        notes.append("drive D: had %s free and a %s run needs %d GB: %d old run folders thinned (%s freed), now %s free" % (
            machine.gb(free), g.level, need // machine.GB, runs, machine.gb(freed), machine.gb(after)))
        free = after
        if free < need:
            return fail("machine/disk", "drive D: has %s free, a %s run needs %d GB, and thinning the old run folders did not free "
                        "enough (%d thinned, %s freed)" % (machine.gb(free), g.level, need // machine.GB, runs, machine.gb(freed)))

    # 3. The tablet.
    if tablet:
        probes.adb_server(g)
        device = probes.tablet(g)
        state = device.state()
        if state != "device":
            # Not an env stop: the tablet may be away for a day and the run is judged without it. Its part is owed by the existing
            # mechanism (tablet_part: the tablet does not answer adb).
            notes.append("the tablet %s %s: its part is owed and the rest of the run is judged" % (
                g.args.tablet, "is not authorised for this computer (unlock it and accept the USB debugging question)"
                if state == "unauthorized" else "does not answer adb (%s)" % (
                    "not listed: not plugged in, or its USB is off" if state == "absent" else state)))
            device = None
        installed, have, want = False, None, None
        if device is not None:
            want = probes.tree_store_version()
            installed, have = device.data()
        if installed and have != want:
            pak, problem = probes.data_pak(g, want)
            if problem:
                return fail("machine/tablet", problem)
            on_it = "none it can read" if have is None else "store version %d" % have
            if pak is None:
                notes.append("the test data on the tablet is %s, this tree's is %d; the data is not made yet, the run pushes it" % (
                    on_it, want))
            else:
                ok, said = device.push(pak)
                if not ok:
                    return fail("machine/tablet", "the test data on the tablet is %s, this tree's is store version %d, and pushing "
                                "it again failed: %s" % (on_it, want, said))
                notes.append("the test data on the tablet was %s, this tree's is %d: pushed again" % (on_it, want))
        elif device is not None and not installed:
            notes.append("the test app is not on the tablet: the run installs it and fills its data from the copy kept there")
        if device is not None and hasattr(device, "kept"):
            kept = device.kept()
            notes.append("game data kept on the tablet: %s, %s" % (kept[0], "%.1f GB" % (kept[1] / 1e9)) if kept else
                         "no game data is kept on the tablet: the run pushes it once and keeps it")

    # 4. The graphics card and the PC's memory.
    learned = learned or machine.Learned(MACHINE_JSON)
    shown = learned.read()
    one_copy = learned.need(renderers_of(g.level))
    room = machine.wait_for_room(probes, one_copy, asked + soak, say)
    if room.card_error:
        notes.append("the graphics card could not be read (%s): the card sets no limit" % room.card_error)
    if room.env:
        return fail("machine/graphics-card" if room.by_card < 1 and room.card is not None else "machine/memory", room.env)
    copies = max(1, room.copies - soak) if room.copies > 1 else room.copies
    g.slots = machine.Slots(copies)
    g.card_shared = room.shared
    card = room.card
    notes.append("%d game cop%s at once (the lanes ask for %d%s): %s; the PC has %s free; a copy takes %s of the card%s" % (
        copies, "y" if copies == 1 else "ies", asked, ", the soak keeps one" if soak else "",
        "no reading of the card" if card is None else "%s has %s free of %s, other programs hold %s" % (
            card.name, machine.gb(card.free), machine.gb(card.total), machine.gb(card.used)),
        "no reading" if room.memory is None else machine.gb(room.memory), machine.gb(one_copy),
        "" if any(shown.get(r) for r in renderers_of(g.level)) else " (assumed: no run has shown it yet)"))
    if room.shared:
        notes.append("card shared: other programs hold %s, more than half of the card's %s; every result is judged as usual" % (
            machine.gb(card.used), machine.gb(card.total)))
    g.sampler = probes.sampler(g.work, None if card is None else card.luids, say)
    g.sampler.start()
    return True


def machine_learn(g, learned=None):
    """At the end of a run: stops the thread that read the game processes' use of the card and adds what it saw (the largest for each
    renderer) to machine.json. Never fails the run."""
    sampler, g.sampler = g.sampler, None
    if sampler is None:
        return
    try:
        (learned or machine.Learned(MACHINE_JSON)).add(sampler.stop())
    except Exception as error:  # noqa: BLE001 - a record of use must never fail a run
        print("gate.py: tool: %s was not updated: %s: %s" % (MACHINE_JSON, type(error).__name__, error), flush=True)


# The levels ----------------------------------------------------------------------------------------------------------


def build_part(g):
    """The repository checks and the Windows build: False when the build failed (nothing after it can run)."""
    g.check("repo/settings", settings_table(g))
    g.check("repo/audit", repo_audit(g))
    g.check("repo/scripts", parse_scripts(g))
    g.check("repo/code", repo_code(g))
    g.check("repo/strictness", repo_strictness(g))
    if not g.args.no_build and g.check("build/windows", build_windows(g)) is False:
        return False
    # The game data by key, found or made with this build's importer (a disc's data under the tree's own importer is another
    # key, so a change of the importer is played on the data it makes). Nothing that runs the game can run without it.
    if not g.args.pak and g.check("data/RMGE01", data_check(g, US_DISC), ("build/windows",)) is False:
        return False
    if not g.args.paks:
        for disc in REGION_DISCS if g.level != LAND_LEVEL else ():
            g.check("data/%s" % disc, data_check(g, disc), ("build/windows",))
    return True


def unit_part(g):
    """The unit tests and the start checks: they prove the bins every lane after them runs."""
    g.check("unit/windows", unit_tests(g), ("build/windows",))
    g.check("unit/game", game_tests(g), ("build/windows",))
    for r in RENDERERS:
        g.check("start/%s" % r, start_check(g, r), ("build/windows",))


def cpu_lane(g, discs_proved, with_self=True):
    """The checks that start no game (Rust, LumaLink, the collision proof, the harness's and the descriptions' own
    tests): a lane beside the game lanes at below-normal priority (N-1070). One after another they held the quick gate
    for 500 s before any game ran. `with_self` False: the tools' self-tests are left to self_lane (a landing, B14 part 3: the
    speed pair may run beside them but not beside Rust's build)."""
    def lane():
        LANE.below_normal = True
        try:
            g.check("rust", rust_tests(g))
            if mods_on(g):
                g.check("lumalink", lumalink_tests(g), ("build/windows",))
            # The cooked collision against each disc's (P-169).
            discs = disc_images(g.args.discs)
            for disc in discs_proved:
                if disc in discs:
                    g.check("import/collision/%s" % disc, collision_proof(g, discs[disc]), ("build/windows",))
                else:
                    g.missing("import/collision/%s" % disc, "no %s image in %s (--discs)" % (disc, g.args.discs))
            if with_self:
                self_checks(g)
        finally:
            LANE.below_normal = False
    lane.copies = 0      # it starts no game
    return lane


def self_checks(g):
    g.check("harness/self", scripts(g, HARNESS_SELF, ("--work", "{out}")))
    commands = pathlib.Path(g.args.build_root) / "game" / "compile_commands.json"
    g.check("description/self", scripts(g, DESCRIPTION_SELF, ("--commands", str(commands), "--out", "{out}")),
            ("build/windows",))


def self_lane(g):
    """The tools' and the descriptions' own tests as a lane of their own at below-normal priority (it starts no game; no build runs in it)."""
    def lane():
        LANE.below_normal = True
        try:
            self_checks(g)
        finally:
            LANE.below_normal = False
    lane.copies = 0
    lane.part = "self"
    return lane


def engine_lane(g, r):
    """The engine's own tests (tests/engine) on one renderer: start, device loss, window shapes and sizes, stage
    loads, save files."""
    def lane():
        g.check("engine/%s" % r, harness(g, TREE / "tests" / "engine" / "suite.json", r, "engine"), DATA)
        g.picture_jobs.append(("pictures/engine/%s" % r, "engine", r, ("engine/%s" % r,)))
    return lane


def tour_lane(g, tour, label, r, needs=DATA, pieced=None, partial=False):
    """One tour on one renderer, with its pictures. `pieced`: the long run played its stops in pieces (PiecedRun); this reads them.
    `partial`: the tour holds only some stops (a sibling play): the pictures of the others are not missing."""
    def lane():
        if partial:
            g.partial.add((label, r))
        g.check("%s/%s" % (label, r), harness(g, tour, r, label, pieced=pieced), needs)
        g.picture_jobs.append(("pictures/%s/%s" % (label, r), label, r, ("%s/%s" % (label, r),)))
    return lane


def picture_part(g):
    """The pictures of the engine tests and the tours, checked after the second runs (settle): a test that failed once and
    passed alone has its second run's pictures. In the order of their names, whichever lane asked first."""
    jobs = sorted(g.picture_jobs, key=lambda job: job[0])
    for name, label, r, needs in jobs:
        g.check(name, pictures(g, label, r), needs)
    if g.level in ("main", "final", LAND_LEVEL):
        # A declared change that names no picture is stale; only a level that takes every picture can tell.
        g.check("pictures/declared", declared_check(g), tuple(j[0] for j in jobs))


def mods_lane(g, r):
    def lane():
        g.check("mods/%s" % r, mod_tests(g, r), DATA)
    lane.copies = g.args.mod_parallel     # each mods lane plays --mod-parallel games at once
    return lane


# The validate lane (tools/gate/validate.py): the runner's --gpu-validation 1, and its time limits four times the usual.
VALIDATE_ENV = {"LUMA_TEST_GPU_VALIDATION": "1", "LUMA_TEST_LIMIT_SCALE": str(validate.LIMIT_SCALE)}


def layer_probe(g, renderer, log):
    """Whether the renderer's debug layer is on when the game starts with --gpu-validation 1: True or False as its start says, None
    when the start said nothing (the lane then runs, and each test's own log says). The program's start check, no game data."""
    start_check(g, renderer, ("--gpu-validation", "1"))(log)
    return validate.layer_state(pathlib.Path(log).read_text(encoding="utf-8", errors="replace"), renderer)


def layer_fault(g, renderer, line):
    """The line a layer that is not installed leaves in tool-faults.tsv (the progress page shows it): a day at most for each renderer."""
    folder = getattr(g.args, "live", None)
    if not folder or renderer in validate.EXPECTED_ABSENT:
        return
    sys.path.insert(0, str(TREE / "tools" / "plan"))
    import live  # noqa: E402 - tools/plan, as fault_writer
    name = "validate/%s" % renderer
    if validate.fault_due(live.read_faults(folder), name, datetime.datetime.now()):
        fault_writer(folder)("gate", "", "ENV", name, "", line, str(g.work))


def read_logs(work, suite_dir, renderer, tests):
    """{test: the text of its run.log} of the runs a work folder holds."""
    out = {}
    for test in tests:
        path = pathlib.Path(work) / "runs" / suite_dir / test / renderer / "run.log"
        try:
            out[test] = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            out[test] = ""
    return out


def validate_judge(g, renderer, known):
    """HarnessSource.judge of the validate lane: the layer's messages and the time limit (validate.judge_results), then the step
    hashes against the recordings. A listed error is counted into the check's detail (source.counted), an unlisted one is a fail."""
    def judge(source, work, results):
        out, final, totals = validate.judge_results(renderer, results, read_logs(work, source.suite_dir, renderer, results), known)
        source.final |= final
        if pathlib.Path(work) == source.first_work():
            source.counted = validate.summary(renderer, totals, known)
            g.layer_seen[renderer] = set(totals.errors)
        return judged.hash_results(work, out)
    return judge


def validate_check(g, renderer, tour):
    """validate/<renderer>: the tour with the debug layer on, one copy at a time; the check is left out, with one plain line, on a PC
    where the layer is not installed (never fail, tool or env)."""
    def fn(log):
        known = validate.read_known()
        if layer_probe(g, renderer, log) is False:
            line = validate.missing_layer_line(renderer)
            layer_fault(g, renderer, line)
            return True, line
        return harness(g, tour, renderer, "validate", env=VALIDATE_ENV, judge=validate_judge(g, renderer, known))(log)
    return fn


def validate_lane(g, r, tour):
    def lane():
        g.check("validate/%s" % r, validate_check(g, r, tour), DATA)
    lane.copies = 1
    lane.part = "validate"
    return lane


def validate_tour(g, tours):
    """The tour the validate lane plays: the 12 stops (a landing's tour, or one written beside it at the main level); the long run's
    whole tour."""
    if g.level in SHORT_LEVELS or g.level == "long":
        return tours["tour"]
    tour = gate_tour.write(g.work / "tour-validate", QUICK_STOPS)
    step_hash.with_hash(tour.parent)
    return tour


def validate_gone(g, known):
    """After the whole tour: the listed ids that no renderer's run showed any more (the nightly report says so)."""
    for renderer, ident, row, words in validate.not_seen(known, g.layer_seen):
        text = "validate: %s %s (%s, %s) was not seen in this whole tour: its line in validate-known.tsv can go" % (
            renderer, ident, row, words)
        g.machine_notes.append(text)
        print("gate.py: " + text, flush=True)


def stress_judge(g, renderer):
    """HarnessSource.judge of the stress lane: the step hashes against the recordings, and the pictures against their approved copies
    (pictures.py check, the runs of this source's folder). A test judged a fail is run again alone, as any."""
    def judge(source, work, results):
        out = judged.hash_results(work, results)
        if any(kind == "pass" for kind, _ in out.values()):
            log = g.work / ("%s-pictures-%s.log" % (source.tag, "first" if pathlib.Path(work) == source.first_work() else "again"))
            run([sys.executable, "-X", "utf8", "-I", HERE / "pictures.py", "check", "--runs", work, "--renderer", renderer,
                 "--place", "pc", "--manifest", pathlib.Path(g.tree) / picture_tool.MANIFEST_NAME, "--store", PICTURE_STORE,
                 "--changes", pathlib.Path(g.tree) / picture_tool.CHANGES_NAME], log, cwd=TREE)
            out = judged.picture_results(log.read_text(encoding="utf-8", errors="replace").splitlines(), out)
        return out
    return judge


def stress_lane_of(g, plan, tour, fails):
    """One of the twelve lanes (stress.lanes): its checks one after another, each a harness suite with this renderer's watches on."""
    def lane():
        for name, kind, r, n in plan:
            suite = TREE / "tests" / "engine" / "suite.json" if kind == "engine" else tour
            label = "stress-engine" if kind == "engine" else "stress-tour-%d" % n
            g.check(name, harness(g, suite, r, label, judge=stress_judge(g, r), stress_lane=True, stress_fails=fails), DATA)
    lane.copies = 1
    return lane


def stress_part(g):
    """The stress lane: twelve copies of the game side by side whatever the graphics card's rule says (stress.copies_for: fewer only
    when the PC's memory is short), the engine suite and the 12-stop tour on each renderer and nine more tours. A failure with the
    copies running that passes alone is `stress` (kinds.Settler)."""
    tour = gate_tour.write(g.work / "tour-stress", QUICK_STOPS)
    step_hash.with_hash(tour.parent)
    fails = "stress" in landing.also_names(getattr(g.args, "also", ""))
    previous = g.slots
    if not g.dry:
        copies, _, note = stress.copies_for(machine.memory_free())
        g.machine_notes.append(note)
        print("gate.py: " + note, flush=True)
        g.slots = machine.Slots(copies)
    try:
        g.lanes([stress_lane_of(g, plan, tour, fails) for plan in stress.lanes()])
    finally:
        g.slots = previous


def stress_lane(g):
    def lane():
        stress_part(g)
    lane.part = "stress"
    lane.copies = stress.COPIES
    return lane


def write_tours(g, stops, region_stops, siblings=None):
    """The tour and the other discs' tours: {label: suite.json}. `siblings`: the stops ("Stage N") of PICTURES-CHANGE.txt that the other
    discs play on the renderers their main-level tour does not (sibling_stops): each disc's tour of those, as `sibling-<disc>`."""
    tours = {"tour": gate_tour.write(g.work / "tour", stops)}
    step_hash.with_hash(tours["tour"].parent)
    for disc in REGION_DISCS:
        tours["tour-%s" % disc] = gate_tour.write(g.work / ("tour-%s" % disc), region_stops, disc)
        if siblings:
            tours["sibling-%s" % disc] = gate_tour.write(g.work / ("sibling-%s" % disc), siblings, disc)
    return tours


def sibling_stops(g):
    """The stops the tree's PICTURES-CHANGE.txt names that the other discs must also play on Direct3D 11 and Vulkan, as "Stage N" (the
    long run compares those rows; a landing's main level plays the other discs on Direct3D 12 only): [] when no line names a stop, or
    when the file cannot be read (the picture checks say so)."""
    try:
        declarations = picture_tool.read_declarations(pathlib.Path(g.tree) / picture_tool.CHANGES_NAME)
    except picture_tool.DeclarationError:
        return []
    names = gate_tour.test_names()
    wanted = set(picture_tool.declared_stops(declarations, names))
    return ["%s %d" % (stage, scenario) for name, (_, stage, scenario) in zip(names, gate_tour.stops()) if name in wanted]


def replay_part(g, renderers):
    """The game's steps: Direct3D 12's against the recordings; another renderer's against Direct3D 12's."""
    for r in renderers:
        g.check("replays/%s" % r, replays(g, "tour", r, None if r == "d3d12" else ("tour", "d3d12")),
                ("tour/%s" % r,) + (() if r == "d3d12" else ("tour/d3d12",)))


def read_tablet_screen(g):
    """The tablet's screen size, asked of the tablet at the start of its lane (B14 part 3), so that the PC can play its half of the stops
    while the tablet plays its own. None when no tablet is named, it does not answer, or it names no size."""
    try:
        if g.dry or not g.args.tablet:
            return None
        machine.ensure_adb_server(android_module().Device("").adb_path)
        device = android_module().Device(g.args.tablet)
        return tablet_screen_of(device) if device.answers() else None
    except Exception as error:  # noqa: BLE001 - an unread size only means the PC's half waits for the tablet's tour
        print("gate.py: the tablet's screen size was not read: %s: %s" % (type(error).__name__, error), flush=True)
        return None


def android_lane(g, stops, pc_tail=True):
    """The Android build, then the tablet's part (its stops: the twelve quick ones at the main level, all after). `pc_tail` False: the PC's half
    of the part is played by the caller beside the lane; the lane reads the tablet's screen size first and says when its build is over."""
    def lane():
        try:
            if not pc_tail:
                g.tablet_screen = read_tablet_screen(g)
                g.tablet_screen_read.set()
            g.android_built = g.args.no_build or g.check("build/android", build_android(g)) is not False
            if not pc_tail:
                g.android_over.set()
            measure_tablet(g)
            tablet_part(g, stops, pc_tail=pc_tail)
        finally:
            g.tablet_screen_read.set()
    lane.part = "tablet"
    return lane


def sanitizer_lane(g):
    def lane():
        sanitizer_part(g)
    lane.part = "sanitizer"
    return lane


# The sanitizers of the game modules in the sanitizer build (build_windows.ps1 -Sanitize): the tour ran clean with both, 102 of 102 stops (B5).
SANITIZERS = "address,undefined"


def sanitizer_tour(g, tour, bin_dir, pieced=None):
    """tour-asan/d3d12: the tour on the sanitizer build, each stop's log read for the sanitizers' reports (tools/gate/sanitizer.py): one
    that sanitizer-known.tsv does not list is a fail with the report as its message, a listed one is counted into the detail."""
    def judge(known):
        def one(source, work, results):
            out, final, totals = sanitizer.judge_results(results, read_logs(work, source.suite_dir, "d3d12", results), known)
            source.final |= final
            source.ended_listed = source.ended_listed or any(out[t][0] == "pass" != results[t][0] for t in final)
            if pathlib.Path(work) == source.first_work():
                source.counted = sanitizer.summary(totals, known)
            return out
        return one

    def fn(log):
        try:
            known = sanitizer.read_known()
        except sanitizer.KnownError as error:
            pathlib.Path(log).write_text("%s\n" % error, encoding="utf-8")
            return False, str(error).splitlines()[0]
        if pieced is not None:
            pieced.source.judge = judge(known)
            return harness(g, tour, "d3d12", "tour-asan", bin_dir=bin_dir, env=sanitizer.ENV, pieced=pieced)(log)
        return harness(g, tour, "d3d12", "tour-asan", bin_dir=bin_dir, env=sanitizer.ENV, judge=judge(known))(log)
    return fn


def sanitizer_part(g):
    """The game modules built with the sanitizers (SANITIZERS: build_windows.ps1 -Sanitize, a build root of its own)
    run the game's unit tests (unit/game-asan: the same tests as unit/game, among them the arenas' own, which check that
    the sanitizer sees the game's memory), then play the tour and every mod test on Direct3D 12: a memory fault either
    reaches ends the run with the sanitizer's report. The mod tests reach the game's code from mods (calls, hooks, events)
    that the tour never does (N-1041)."""
    root = pathlib.Path(g.args.build_root) / "asan"
    asan_bin = str(root / "bins")
    mods = mods_on(g)
    if g.check("build/windows-asan", build_windows(g, root=root, bin_dir=asan_bin, sanitize=SANITIZERS)) is False:
        g.missing("unit/game-asan", "the sanitizer build failed", ("build/windows-asan",))
        g.missing("tour-asan/d3d12", "the sanitizer build failed", ("build/windows-asan",))
        if mods:
            g.missing("mods-asan/d3d12", "the sanitizer build failed", ("build/windows-asan",))
        return
    g.check("unit/game-asan", game_tests(g, root), ("build/windows-asan",))
    tour = gate_tour.write(g.work / "tour-asan", None)
    step_hash.with_hash(tour.parent)
    g.check("tour-asan/d3d12", sanitizer_tour(g, tour, asan_bin), ("build/windows-asan", "data/RMGE01"))
    if mods:
        g.check("mods-asan/d3d12", mod_tests(g, "d3d12", bin_dir=asan_bin, label="mods-asan"), ("build/windows-asan",))


LLVM_BIN = pathlib.Path(os.environ.get("NEBULA_LLVM_BIN", "C:/Program Files/LLVM/bin"))


def coverage_lane(g):
    def lane():
        coverage_part(g)
    lane.part = "coverage"
    return lane


def coverage_part(g):
    """Which code no test runs (tools/gate/coverage.py, file 04 3.3, N-1042): the game modules and the programs built
    counting the lines and functions they run (build_windows.ps1 -Coverage, a build root of its own) play the engine
    tests, the tour and every mod test on Direct3D 12, each process writing its counts; then the list of what none of
    them ran and the share of lines run, which may not fall below the last long run's (gate-runs/coverage-history.tsv,
    per set of binaries)."""
    root = pathlib.Path(g.args.build_root) / "coverage"
    bin_dir = str(root / "bins")
    if g.check("build/windows-coverage", build_windows(g, root=root, bin_dir=bin_dir, coverage=True)) is False:
        g.missing("coverage", "the coverage build failed", ("build/windows-coverage",))
        return
    tour = gate_tour.write(g.work / "tour-coverage", None)
    step_hash.with_hash(tour.parent)
    g.check("coverage", coverage_check(g, bin_dir, tour), ("build/windows-coverage", "data/RMGE01"))


def coverage_check(g, bin_dir, tour, parts=None):
    """The counting runs, then the report. The runs' own verdicts are the other checks' business (the same tests run
    there on the players' build); here they say only what ran. `parts`: {name: function(log)} of runs already played in pieces
    (the long run: their counts are in the folder already), read here in place of being played."""
    def fn(log):
        out = g.work / "coverage"
        counts = out / "counts"
        if parts is None:
            shutil.rmtree(counts, ignore_errors=True)
            counts.mkdir(parents=True)
        # %m: one file per module (Nebula.exe, smg_game, smg_game_plain), %p: per process.
        env = {"LLVM_PROFILE_FILE": str(counts / "counts-%m-%p.profraw")}
        runs = [("engine", harness(g, TREE / "tests" / "engine" / "suite.json", "d3d12", "engine-coverage",
                                   bin_dir=bin_dir, env=env)),
                ("tour", harness(g, tour, "d3d12", "tour-coverage", bin_dir=bin_dir, env=env))]
        if parts is not None:
            runs = [(n, parts[n]) for n, _ in runs]
        if mods_on(g):
            runs.append(("mods", mod_tests(g, "d3d12", bin_dir=bin_dir, label="mods-coverage", more_env=env)))
        said, played = [], []
        for name, part in runs:
            result = part(out / ("%s.log" % name))
            said.append("%s %s" % (name, result[1]))
            played.append((name, result))
        with open(log, "a", encoding="utf-8") as f:
            f.write("runs: %s\n" % "; ".join(said))
        # A run that did not play (no test, or a test that ended as the tool's or the machine's fault) counted nothing of its own:
        # the share it leaves is not the share of the tests, so none is made, no history line is written, and the check is owed.
        missing = coverage_tool.missing_runs(played)
        raws = sorted(counts.glob("*.profraw"))
        if missing or not raws:
            reason = "; ".join(missing) if missing else "no process wrote counts"
            with open(log, "a", encoding="utf-8") as f:
                f.write("coverage: could not measure: %s\n" % reason)
            shutil.rmtree(counts, ignore_errors=True)
            return False, "could not measure, no share and no history line: %s" % reason, None, "owed"
        llvm = LLVM_BIN
        merged = out / "counts.profdata"
        # Hundreds of files: their names in a list, past the command line's length.
        listed = out / "counts.txt"
        listed.write_text("".join("%s\n" % r for r in raws), encoding="utf-8")
        if run([llvm / "llvm-profdata.exe", "merge", "-o", merged, "-f", listed], log) != 0:
            return False, "llvm-profdata merge failed"
        # The program the runs on Direct3D 12 start, and the game modules it loads.
        modules = [pathlib.Path(bin_dir) / m for m in ("Nebula.exe", "smg_game.dll", "smg_game_plain.dll")
                   if (pathlib.Path(bin_dir) / m).exists()]
        binaries = [a for m in modules for a in ("--binary", m)]
        builds = pathlib.Path(bin_dir).parent
        code = run([sys.executable, "-X", "utf8", "-I", HERE / "coverage.py", "report", *binaries, "--profile", merged,
                    "--work", out, "--root", TREE, "--rebase", builds / "game", "--rebase", builds / "msvc",
                    "--history", pathlib.Path(g.args.work) / "coverage-history.tsv",
                    "--commit", g.commit, "--suites", ",".join(n for n, _ in runs), "--llvm", llvm], log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        line = next((l for l in reversed(text.splitlines()) if l.startswith("coverage: ")), "coverage: no report")
        # The counts are large and made again each run; the lists stay with the results.
        shutil.rmtree(counts, ignore_errors=True)
        return code == 0, "%s (%d processes)" % (line[len("coverage: "):], len(raws))
    return fn


def locked_part(g, name, fn):
    """Runs one part of the long run under the gate lock, taken through the queue and freed at the part's end, so a
    waiting gate runs between two parts (Planner 11-18 03:44, condition 4). Other levels hold the lock for the whole
    run (main()), so the part just runs. A part whose lock is not had within --wait is a failed check, part/<name>.
    Returns what fn returns, None when it did not run."""
    if g.dry or g.args.mode != "long":
        return fn()
    holder = "long run part %s of %s %s" % (name, TREE.name, lockholder.named())
    watch = landing_watch(g.args)
    started = PART_CLOCK()
    deadline = started + 60.0 * g.args.wait
    while True:
        # Landings first (B15 point 10): the part does not take the lock while a branch waits or runs in the landing queue. A branch
        # that arrives between the look and the lock is let go first too: the lock is given back and the look made again.
        why = long_parts.wait_clear(watch, max(0.0, (deadline - PART_CLOCK()) / 60.0), say=print, sleep=PART_SLEEP, clock=PART_CLOCK,
                                    poll=PART_POLL[0])
        if why:
            g.record("part/" + name, "env", 0.0, "not run: " + why)
            return None
        held = take_gate_lock(g.args.gate_lock, holder, max(0.0, (deadline - PART_CLOCK()) / 60.0), g.args.board, g.args.who,
                              sleep=PART_SLEEP, poll=PART_POLL[0])
        if held is not None:
            g.record("part/" + name, "env", 0.0, "not run: " + held)
            return None
        if not watch.busy():
            break
        free_gate_lock(g.args.gate_lock, holder, g.args.board, g.args.who, "part %s: a landing came first" % name)
    taken = PART_CLOCK()
    try:
        return fn()
    finally:
        if hasattr(g, "part_log"):
            g.part_log.append({"part": name, "waited": round(taken - started, 1), "held": round(PART_CLOCK() - taken, 1)})
        free_gate_lock(g.args.gate_lock, holder, g.args.board, g.args.who, "part %s done" % name)


# The long run's clock and sleep, which a self-test replaces.
PART_CLOCK = time.time
PART_SLEEP = time.sleep
PART_POLL = [long_parts.POLL_SECONDS]


def landing_watch(args):
    """The landing queue as the long run's parts look at it (long_parts.LandingWatch)."""
    return long_parts.LandingWatch(getattr(args, "queue_file", landing.QUEUE), getattr(args, "runner_lock", landing.RUNNER_LOCK),
                                   process_alive, lambda lock: lock_holder(lock)[1])


def beside_part(g, name, fn):
    """A part of the long run that plays BESIDE the PC's parts (the tablet's pieces, B15's addition to point 12): it takes no gate lock (the
    tablet's lock is `fn`'s own), so a landing with no tablet check never waits for it. It does not start while a branch waits or runs
    in the landing queue (a landing that needs the tablet goes first), nor while a speed measure of the PC is being measured
    (measure.lock) unless a proof has shown the measure keeps its figures beside the tablet's lane (long_parts.TABLET_BESIDE_MEASURE).
    Returns (True, what fn returned) or (False, why it did not start)."""
    if g.dry or g.args.mode != "long":
        return True, fn()
    watch = landing_watch(g.args)

    def measuring():
        if long_parts.TABLET_BESIDE_MEASURE:
            return ""
        return long_parts.measure_held(pathlib.Path(g.args.gate_lock).parent, process_alive, lambda lock: lock_holder(lock)[1])

    started = PART_CLOCK()
    while True:
        why = long_parts.wait_clear(watch, max(0.0, (started + 60.0 * g.args.wait - PART_CLOCK()) / 60.0), say=print, sleep=PART_SLEEP,
                                    clock=PART_CLOCK, poll=PART_POLL[0], extra=measuring)
        if why:
            return False, why
        break
    begun = PART_CLOCK()
    try:
        return True, fn()
    finally:
        g.part_log.append({"part": name, "waited": round(begun - started, 1), "held": round(PART_CLOCK() - begun, 1), "beside": True})


# The long run's parts in pieces (package B15, point 12): no piece holds the machine longer than long_parts.PIECE_SECONDS, and a landing
# goes first between two pieces. The pieces of one check play into one run folder; the check is read and judged once, after the last.


def long_tour_set(g, tours):
    """[(label, suite, renderer, needs)] of the tours the long run plays: the US disc's and each other disc's, on every renderer."""
    sets = [("tour", tours["tour"], DATA)] + [("tour-%s" % d, tours["tour-%s" % d], DATA + ("data/%s" % d,)) for d in REGION_DISCS]
    return [(label, suite, r, needs) for label, suite, needs in sets for r in RENDERERS]


def play_in_pieces(g, name, runs, per_stop, fixed=0.0, before=None):
    """Plays the tests of the PiecedRuns `runs` (side by side) in pieces, each under the gate lock for itself (locked_part). The pieces
    are named `name i/n`. `before(i, run)` runs first in a piece's lane (the validate lane's probe): False leaves that run out."""
    count = max((len(pr.names) for pr in runs), default=0)
    sizes = long_parts.pieces_of(count, per_stop, fixed)
    for i in range(len(sizes)):
        def play(i=i):
            lanes = []
            for pr in runs:
                def lane(pr=pr):
                    if before is not None and before(i, pr) is False:
                        return
                    pr.run_piece(long_parts.split(pr.names, sizes)[i], i)
                lanes.append(lane)
            g.lanes(lanes)
        locked_part(g, long_parts.piece_label(name, i, len(sizes)), play)
    return sizes


def pc_tour_pieces(g, tours):
    """The tours of every disc on every renderer, side by side, in pieces; then each check read and judged."""
    todo = long_tour_set(g, tours)
    runs = {(label, r): PiecedRun(g, suite, r, label, "%s/%s" % (label, r)) for label, suite, r, needs in todo}
    play_in_pieces(g, "tours", list(runs.values()), long_parts.TOURS_PER_STOP)
    g.lanes([tour_lane(g, suite, label, r, needs, pieced=runs[(label, r)]) for label, suite, r, needs in todo])


def validate_pieces(g, tours):
    """validate/<renderer> on the whole tour, in pieces; the debug layer is probed in the first piece (a renderer whose layer is not
    installed plays nothing and says so, as the lane always did)."""
    tour = validate_tour(g, tours)
    runs = {r: PiecedRun(g, tour, r, "validate", "validate/%s" % r, env=VALIDATE_ENV) for r in RENDERERS}
    absent = {}
    by_run = {id(pr): r for r, pr in runs.items()}

    def before(i, pr):
        r = by_run[id(pr)]
        if i == 0 and not g.dry:
            if layer_probe(g, r, g.work / ("validate-%s-probe.log" % r)) is False:
                absent[r] = validate.missing_layer_line(r)
                layer_fault(g, r, absent[r])
        return r not in absent

    play_in_pieces(g, "validate", list(runs.values()), long_parts.VALIDATE_PER_STOP, before=before)

    def reading(r):
        def lane():
            def fn(log):
                known = validate.read_known()
                if r in absent:
                    return True, absent[r]
                runs[r].source.judge = validate_judge(g, r, known)
                return harness(g, tour, r, "validate", env=VALIDATE_ENV, pieced=runs[r])(log)
            g.check("validate/%s" % r, fn, DATA)
        return lane
    g.lanes([reading(r) for r in RENDERERS])


def sanitizer_pieces(g):
    """The sanitizer build (a part of its own: it keeps the lock to its end), then its tour in pieces, then what the mod tests are."""
    root = pathlib.Path(g.args.build_root) / "asan"
    asan_bin = str(root / "bins")

    def build_and_test():
        # The game's unit tests of that build start no game: they run in the build's hold of the lock.
        ok = g.check("build/windows-asan", build_windows(g, root=root, bin_dir=asan_bin, sanitize=SANITIZERS))
        if ok is not False:
            g.check("unit/game-asan", game_tests(g, root), ("build/windows-asan",))
        return ok
    built = locked_part(g, "sanitizer build", build_and_test)
    if built is False or built is None:
        why = "the sanitizer build failed" if built is False else "its build did not run"
        g.missing("unit/game-asan", why, ("build/windows-asan",))
        g.missing("tour-asan/d3d12", why, ("build/windows-asan",))
        if mods_on(g):
            g.missing("mods-asan/d3d12", why, ("build/windows-asan",))
        return
    tour = gate_tour.write(g.work / "tour-asan", None)
    step_hash.with_hash(tour.parent)
    pr = PiecedRun(g, tour, "d3d12", "tour-asan", "tour-asan/d3d12", bin_dir=asan_bin, env=sanitizer.ENV)
    play_in_pieces(g, "sanitizer tour", [pr], long_parts.ASAN_PER_STOP)
    g.check("tour-asan/d3d12", sanitizer_tour(g, tour, asan_bin, pieced=pr), ("build/windows-asan", "data/RMGE01"))
    if mods_on(g):
        locked_part(g, "sanitizer mods", lambda: g.check("mods-asan/d3d12", mod_tests(g, "d3d12", bin_dir=asan_bin, label="mods-asan"),
                                                         ("build/windows-asan",)))


def coverage_pieces(g):
    """The coverage build (a part of its own), the engine suite (one piece), the tour in pieces, then the merge and the report (one piece)."""
    root = pathlib.Path(g.args.build_root) / "coverage"
    bin_dir = str(root / "bins")
    built = locked_part(g, "coverage build", lambda: g.check("build/windows-coverage", build_windows(g, root=root, bin_dir=bin_dir, coverage=True)))
    if built is False or built is None:
        g.missing("coverage", "the coverage build failed" if built is False else "its build did not run", ("build/windows-coverage",))
        return
    tour = gate_tour.write(g.work / "tour-coverage", None)
    step_hash.with_hash(tour.parent)
    out = g.work / "coverage"
    counts = out / "counts"
    if not g.dry:
        shutil.rmtree(counts, ignore_errors=True)
        counts.mkdir(parents=True)
    env = {"LLVM_PROFILE_FILE": str(counts / "counts-%m-%p.profraw")}
    engine = {}
    locked_part(g, "coverage engine", lambda: engine.setdefault("result", harness(
        g, TREE / "tests" / "engine" / "suite.json", "d3d12", "engine-coverage", bin_dir=bin_dir, env=env)(out / "engine.log")
        if not g.dry else None))
    pr = PiecedRun(g, tour, "d3d12", "tour-coverage", "coverage", bin_dir=bin_dir, env=env)
    play_in_pieces(g, "coverage tour", [pr], long_parts.COVERAGE_PER_STOP)
    parts = {"engine": lambda log: engine.get("result") or (False, "the engine suite did not run"),
             "tour": harness(g, tour, "d3d12", "tour-coverage", bin_dir=bin_dir, env=env, pieced=pr)}
    locked_part(g, "coverage report", lambda: g.check("coverage", coverage_check(g, bin_dir, tour, parts), ("build/windows-coverage", "data/RMGE01")))


class TabletTrack:
    """The long run's tablet pieces (package B15 point 12 and the planner's addition of 11-21 23:55): they take the tablet's lock, not
    the gate lock, and play in a thread beside the PC's parts. The PC's own run of the same stops stays a PC part: after each tablet
    piece, its PC piece takes the gate lock (`pc_pieces`). One row each for tablet/prepare, android/tablet, tour/tablet and tablet/remove."""

    def __init__(self, g, device=None):
        self.g, self.device = g, device
        self.thread = None
        self.sized, self.finished = threading.Event(), threading.Event()
        self.events, self.sizes, self.pr = {}, [], None
        self.prepared, self.removed = [], []
        self.complete, self.why = False, ""
        self.pc, self.pc_next, self.no_screen = None, 0, False
        self.needs = tablet_needs()
        self.wanted = []

    def event(self, i):
        return self.events.setdefault(i, threading.Event())

    def start(self):
        g = self.g
        self.wanted = [n for n in TABLET_CHECKS if g.wanted(n, self.needs[n])]
        if g.dry:
            g.check("build/android", build_android(g))
            return
        if not self.wanted and not g.wanted("build/android", ()):
            self.finished.set()
            return
        self.thread = threading.Thread(target=self.main, name="tablet-track")
        self.thread.start()

    def owe_rest(self, why):
        g = self.g
        for n in self.wanted:
            if not any(r["check"] == n for r in g.rows()):
                g.owe(n, why)

    def main(self):
        g = self.g
        try:
            built = locked_part(g, "android build", lambda: g.args.no_build or g.check("build/android", build_android(g)) is not False)
            g.android_built = bool(built)
            if not g.android_built:
                for n in self.wanted:
                    g.missing(n, "the Android build failed" if built is False else "its build did not run")
                return
            if not self.wanted:
                return
            machine.ensure_adb_server(android_module().Device("").adb_path)
            device = self.device or android_module().Device(g.args.tablet)
            if not device.answers():
                self.owe_rest("%s does not answer adb" % g.args.tablet)
                return
            tour = gate_tour.write(g.work / "tour-tablet", None)
            step_hash.with_hash(tour.parent)
            self.pr = PiecedRun(g, tour, "vulkan", "tour-tablet", "tour/tablet", device=g.args.tablet)
            self.sizes = long_parts.pieces_of(len(self.pr.names), long_parts.TABLET_PER_STOP, long_parts.TABLET_FIXED + long_parts.TABLET_SUITE)
            self.sized.set()
            gone = None
            for i, names in enumerate(long_parts.split(self.pr.names, self.sizes)):
                ran, out = beside_part(g, long_parts.piece_label("tablet", i, len(self.sizes)),
                                       lambda i=i, names=names: self.piece(device, i, names))
                if not ran:
                    gone = "a tablet piece did not start: " + out
                    break
                if out:
                    gone = out
                    break
                self.event(i).set()
            if gone is None and not self.problem():
                self.rows()
                self.complete = True
            else:
                self.why = gone or ""
                self.finish_rows(gone)
        except Exception as error:  # noqa: BLE001 - the track broke: a fault of the gate's own, said plainly
            g.record("lane/tablet", "tool", 0.0, "%s: %s" % (type(error).__name__, error))
        finally:
            self.sized.set()
            self.finished.set()

    def problem(self):
        """Why the pieces cannot be read as one: an install that failed while the tablet answered ('' when none)."""
        return next((d for ok, d in self.prepared if not ok), "")

    def piece(self, device, i, names):
        """One tablet piece under the tablet's lock: install, tests/android (the first piece), the stops, remove. None, or why the tablet is gone."""
        g = self.g
        held = take_device_lock_wait(g.args.tablet_lock, "gate %s" % g.name, g.args.tablet_wait)
        if held is not None:
            return "the tablet's lock stayed held %g minutes: %s" % (g.args.tablet_wait, held)
        gone = None
        try:
            ok, detail = device_step(g, "prepare")(g.work / ("tablet-prepare-piece%d.log" % (i + 1)))[:2]
            self.prepared.append((ok, detail))
            if not ok:
                if not device.answers():
                    gone = "the tablet left adb"
            else:
                if i == 0:
                    g.check("android/tablet", harness(g, TREE / "tests" / "android" / "suite.json", "vulkan", "android", g.args.tablet),
                            self.needs["android/tablet"])
                    if not device.answers():
                        gone = "the tablet left adb"
                if gone is None:
                    self.pr.run_piece(names, i)
                    if not device.answers():
                        gone = "the tablet left adb"
        finally:
            if gone is None and device.answers():
                self.removed.append(device_step(g, "remove")(g.work / ("tablet-remove-piece%d.log" % (i + 1)))[:2])
            else:
                self.removed.append(None)
            shutil.rmtree(g.args.tablet_lock, ignore_errors=True)
        return gone

    @staticmethod
    def merged(results, what):
        """A check fn over the pieces' (passed, detail) results: passes when every piece did; the first that did not names itself."""
        def fn(log):
            bad = next((d for ok, d in results if not ok), None)
            pathlib.Path(log).write_text("\n".join(d for _, d in results) + "\n", encoding="utf-8")
            return bad is None, ("%s in %d pieces: %s" % (what, len(results), results[-1][1])) if bad is None else bad
        return fn

    def rows(self):
        """The four rows of the tablet's part, from all the pieces."""
        g = self.g
        g.check("tablet/prepare", self.merged(self.prepared, "installed"), self.needs["tablet/prepare"])
        g.check("tour/tablet", harness(g, self.pr.source.suite, "vulkan", "tour-tablet", g.args.tablet, pieced=self.pr), self.needs["tour/tablet"])
        g.check("tablet/remove", self.merged([r for r in self.removed if r], "removed"), ())

    def finish_rows(self, gone):
        """The tablet left, its lock stayed held, an install failed, or a piece could not start: what ran is on the board, the rest is owed
        (a failed install is a fail)."""
        g = self.g
        bad = self.problem()
        if bad:
            g.check("tablet/prepare", self.merged(self.prepared, "installed"), self.needs["tablet/prepare"])
            for n in self.wanted:
                g.missing(n, "tablet/prepare failed: " + bad)
            return
        if self.removed and not any(self.removed):
            g.owe("tablet/remove", "the tablet left adb: the test app may still be on it")
        self.owe_rest((gone or "the tablet's part") + " during its part")

    def pc_pieces(self, block=True):
        """The PC plays the tablet's stops at the tablet's screen size, a piece after each tablet piece, each under the gate lock for itself.
        Called between the PC's parts with block=False it plays the pieces that are ready; the last call (block=True) waits for the rest,
        and then both runs must take the same steps."""
        g = self.g
        if g.dry or self.thread is None:
            return
        if not self.sized.is_set():
            if not block:
                return
            self.sized.wait()
        while self.pc_next < len(self.sizes):
            i = self.pc_next
            while block and not (self.event(i).is_set() or self.finished.is_set()):
                self.event(i).wait(5.0)
            if not self.event(i).is_set():
                break
            if self.pc is None:
                screen = device_screen(g.work / "runs-tour-tablet-vulkan")
                if screen is None:
                    self.pc_next = len(self.sizes)
                    self.no_screen = True
                    break
                pc_tour = gate_tour.write(g.work / "tour-tablet-pc", None, window=screen)
                step_hash.with_hash(pc_tour.parent)
                self.pc = (pc_tour, PiecedRun(g, pc_tour, "d3d12", "tour-tablet-pc", "tour/tablet-pc"))
            names = long_parts.split(self.pc[1].names, self.sizes)[i]
            locked_part(g, long_parts.piece_label("tablet-pc", i, len(self.sizes)), lambda i=i, names=names: self.pc[1].run_piece(names, i))
            self.pc_next += 1
        if not block:
            return
        self.thread.join()
        if not self.complete:
            return
        if self.pc is None or self.pc_next < len(self.sizes):
            why = "the tablet's runs name no screen size" if self.no_screen else "its pieces did not all run"
            g.missing("tour/tablet-pc", why)
            g.missing("replays/tablet", why)
            return
        g.check("tour/tablet-pc", harness(g, self.pc[0], "d3d12", "tour-tablet-pc", pieced=self.pc[1]), self.needs["tour/tablet-pc"])
        g.check("replays/tablet", replays(g, "tour-tablet", "vulkan", ("tour-tablet-pc", "d3d12")), self.needs["replays/tablet"])

    def join(self):
        if self.thread is not None:
            self.thread.join()


def long_run(g, tours):
    """The long run's checks that play the game, in the order of the parts: the tablet's pieces start beside; the PC's parts follow one
    after another (the tours of every disc and renderer, the validate lane, the sanitizer build and its tour, the coverage build and its
    runs, the stress lane), the PC's pieces of the tablet's stops between them as the tablet's pieces are done."""
    track = TabletTrack(g)
    track.start()
    try:
        for part in (lambda: pc_tour_pieces(g, tours), lambda: validate_pieces(g, tours), lambda: sanitizer_pieces(g),
                     lambda: coverage_pieces(g), lambda: locked_part(g, "stress", lambda: stress_part(g))):
            part()
            track.pc_pieces(block=False)
        track.pc_pieces(block=True)
    finally:
        track.join()


# The parts of the nightly run that `--also` asks for by name (package B8): the name, the function that runs the part in the landing
# tree, and the patterns of the check names it registers (what the landing's selection takes besides the plan's). `run` None: the part
# is not on main yet and the name is refused at the start of the landing.
SOAK_SHORT_MINUTES = 30


def soak_part(g, name, minutes):
    """The soak (tools/gate/soak.py: its one plan, one process for each renderer, side by side) for `minutes`, after the other checks;
    one check `name`."""
    if not g.wanted(name, DATA) or g.dry:
        return
    process, log = start_soak(g, minutes)
    g.check(name, soak_result(process, log), DATA)


def replays_software_part(g):
    """The recorded plays on Microsoft's software graphics device (tools/gate/replays_software.py, T9), after the other checks: one check
    `replays/software`. It takes no graphics-card slot, so no lane's place is held for it."""
    if not g.wanted("replays/software", DATA) or g.dry:
        return
    g.check("replays/software", replays_software_check(g), DATA)


def replays_software_check(g):
    """The script plays the recorded stops on both Direct3D renderers (and Vulkan where a software device is) as many copies at once as the
    PC holds, and ends with its KIND lines and a VERDICT; its result.json names every play, which become this check's tests."""
    def fn(log):
        out = g.work / "replays-software"
        shutil.rmtree(out, ignore_errors=True)
        code = run([sys.executable, "-X", "utf8", "-I", HERE / "replays_software.py", "run", "--bin", g.bin, "--work", out, "--who", "gate",
                    "--commit", g.commit], log, env={"LUMA_TEST_PAKS": g.paks_text()}, cwd=g.args.build_root)
        found = replays_software.cases_of(out)
        if found is None:
            return False, "no result.json: %s" % last_line(pathlib.Path(log).read_text(encoding="utf-8", errors="replace")), None, "tool"
        results, detail = found
        return code == 0, detail, kinds.Cases(results)
    return fn


def replays_rates_part(g):
    """Each recorded stop played at a world rate of 1 and of 2 (tools/gate/replays_rates.py), after the other checks: one check
    `replays/rates`. It plays on the graphics card, when every lane is done."""
    if not g.wanted("replays/rates", DATA) or g.dry:
        return
    g.check("replays/rates", replays_rates_check(g), DATA)


def replays_rates_check(g):
    """The script plays every recorded stop twice and ends with its KIND lines and a VERDICT; its result.json names every stop, which
    become this check's tests."""
    def fn(log):
        out = g.work / "replays-rates"
        shutil.rmtree(out, ignore_errors=True)
        code = run([sys.executable, "-X", "utf8", "-I", HERE / "replays_rates.py", "run", "--bin", g.bin, "--work", out, "--who", "gate",
                    "--commit", g.commit], log, env={"LUMA_TEST_PAKS": g.paks_text()}, cwd=g.args.build_root)
        found = replays_rates.cases_of(out)
        if found is None:
            return False, "no result.json: %s" % last_line(pathlib.Path(log).read_text(encoding="utf-8", errors="replace")), None, "tool"
        results, detail = found
        return code == 0, detail, kinds.Cases(results)
    return fn


class AlsoPart:
    def __init__(self, run, checks):
        self.run, self.checks = run, checks


ALSO_PARTS = {
    "stress": AlsoPart(lambda g: stress_part(g), ("stress/*",)),
    "sanitizer/tour": AlsoPart(lambda g: sanitizer_part(g), ("build/windows-asan", "unit/game-asan", "tour-asan/*", "mods-asan/*")),
    "soak": AlsoPart(lambda g: soak_part(g, "soak", SOAK_SHORT_MINUTES), ("soak",)),
    "soak/long": AlsoPart(lambda g: soak_part(g, "soak/long", g.args.soak_minutes), ("soak/long",)),
    "replays/software": AlsoPart(lambda g: replays_software_part(g), ("replays/software",)),
    "replays/rates": AlsoPart(lambda g: replays_rates_part(g), ("replays/rates",)),
    "speed/anchor": AlsoPart(lambda g: anchor_part(g), ("speed/anchor",)),
}
# Two lengths of one soak: they share a work folder and a log, so a landing asks for one.
ALSO_ONE_OF = ("soak", "soak/long")


def also_parts(g):
    """The parts `--also` asked for, one after another, after the needed checks have passed (a landing that failed runs none) and
    before measure/step/pc. Each runs under its usual rules of kind; the tests that failed are run again alone after each (a stress
    result asked for so is a fail: stress_part reads the name)."""
    for name in landing.also_names(getattr(g.args, "also", "")):
        part = ALSO_PARTS.get(name)
        if part is None or part.run is None:
            continue
        if not g.dry and landing_failed(g):
            return
        part.run(g)
        if any(r.get("pending") for r in g.results):
            g.settle()


def run_checks(args, known=None, planned=None, selected=None, dry=False):
    """The level's checks. The build and the unit checks first, one after another; then the parts that start the game
    side by side (section 7), each renderer's engine tests and tours in a lane of their own, each other disc's tour,
    the mod tests per renderer, the Android build with the tablet; then what compares their runs. A dry run returns the
    Gate (its names and needs) without running any."""
    g = Gate(args, dry=dry, known=known, planned=planned, selected=selected)
    level = args.mode
    if not dry and run_starts_game(selected):
        soak = 1 if level in ("long", "final") and (selected is None or "soak" in selected) else 0
        if not machine_ready(g, lanes_asked(g), run_has_tablet_part(args, level, selected), soak):
            g.write()
            return g
    if not locked_part(g, "build", lambda: build_part(g)):
        g.write()
        return g
    quick = level == "quick"
    short = level in SHORT_LEVELS
    every = level in ("main", "final")
    # A landing's checks are picked by name (--scope), so it registers every renderer and the selection leaves out what
    # its groups do not name.
    renderers = ("d3d12",) if quick else RENDERERS
    if level != "long":
        unit_part(g)
    if level != "long" and g.mods != "off" and not MOD_TESTS.exists():
        g.missing("mods", "the mod tests are not in the tree (tools/lumacore/mod_tests.py, mods M0)")
    soak = start_soak(g) if level in ("long", "final") and not g.dry and g.wanted("soak", DATA) else None
    # A stop PICTURES-CHANGE.txt names is also played by the other discs on the renderers their tour does not play (sibling_stops).
    siblings = sibling_stops(g) if level == "main" else []
    tours = write_tours(g, QUICK_STOPS if short else None, QUICK_REGION_STOPS if short else None, siblings)
    # Region discs: Direct3D 12 at the quick and main levels; every renderer in the long run and the final check.
    region_renderers = RENDERERS if level in ("long", "final") else ("d3d12",)
    if level == "long":
        # The long run's parts (B15): pieces of at most seven minutes, each under the gate lock for itself, a landing first between two
        # pieces; the tablet's pieces beside them under the tablet's lock. The soak, started above in processes of its own at
        # below-normal priority, is the only work that runs beside another gate.
        long_run(g, tours)
    else:
        # The CPU-only checks first in the list, so their rows read where they always did. In a landing the tools' self-tests are a lane of their
        # own: the speed pair does not wait for them (the proof of B14 part 3).
        discs_proved = ("RMGE01",) if short else ("RMGE01",) + REGION_DISCS
        split_self = level in MEASURE_LEVELS and not PAIR_WAITS_FOR_SELF
        lanes = [cpu_lane(g, discs_proved, with_self=not split_self)] + ([self_lane(g)] if split_self else [])
        for r in renderers:
            def renderer_lane(r=r):
                engine_lane(g, r)()
                tour_lane(g, tours["tour"], "tour", r)()
            lanes.append(renderer_lane)
        for disc in REGION_DISCS if level != LAND_LEVEL else ():
            def region_lane(disc=disc):
                for r in region_renderers:
                    tour_lane(g, tours["tour-%s" % disc], "tour-%s" % disc, r, DATA + ("data/%s" % disc,))()
                for r in RENDERERS if siblings else ():
                    if r not in region_renderers:
                        tour_lane(g, tours["sibling-%s" % disc], "tour-%s" % disc, r, DATA + ("data/%s" % disc,), partial=True)()
            lanes.append(region_lane)
        if mods_on(g):
            lanes += [mods_lane(g, r) for r in renderers]
        tablet_stops = None
        if not quick:
            # The main level's tablet part is the twelve quick stops; the long run's (and the final check's) all of them. In a landing the PC's
            # half of the part (tour/tablet-pc) and the speed pair are played by the PC beside the tablet's lane (B14 part 3).
            tablet_stops = QUICK_STOPS if level in ("main", LAND_LEVEL) else None
            lanes.append(android_lane(g, tablet_stops, pc_tail=level not in MEASURE_LEVELS))
        if level in ("long", "final"):
            lanes += [sanitizer_lane(g), coverage_lane(g)]
        if level != "quick":
            # The debug layers' messages (T12): one lane for each renderer, beside the others in a landing, a part of its own in the long run.
            vtour = validate_tour(g, tours)
            lanes += [validate_lane(g, r, vtour) for r in RENDERERS]
        if level in MEASURE_LEVELS and any(getattr(l, "part", "") == "tablet" for l in lanes):
            tablet_lanes = [l for l in lanes if getattr(l, "part", "") == "tablet"]
            g.lanes_with_tail(lanes_without(lanes, tablet_lanes), tablet_lanes, pc_tail(g, tablet_stops), wait_for_cpu_lane=PAIR_WAITS_FOR_SELF)
            if g.tablet_tail_ok:
                tablet_after(g, tablet_stops)
        else:
            g.lanes(lanes)
        if level == "final":
            stress_part(g)       # the final check is the main and long levels on one commit: the stress lane follows the others
    if level == "long" and not g.dry and g.layer_seen:
        try:
            validate_gone(g, validate.read_known())
        except validate.KnownError:
            pass        # the check validate/<renderer> already said the list is wrong
    # The soak plays beside the lanes in processes of its own: it is waited for first, so that what is run again alone
    # really runs alone.
    if level in ("long", "final") and g.wanted("soak", DATA) and not g.dry:
        if soak is None:
            g.missing("soak", "it did not start")
        else:
            g.check("soak", soak_result(*soak))
    # Once every lane has ended, nothing else runs: the tests that failed are run again alone (and on main's build), then
    # the checks that read the runs' files (pictures, replays) read the runs as they now stand.
    if any(r.get("pending") for r in g.results):
        locked_part(g, "again", g.settle)
    picture_part(g)
    replay_part(g, renderers)
    if level in MEASURE_LEVELS:
        also_parts(g)
        # Last, with the machine alone (the landing holds the gate lock, measure.py takes measure.lock): a landing that has failed
        # already does not measure.
        measure_pc(g)
    if every and not getattr(g.args, "move", False):
        # With --move the gate waits for GitHub after its own checks, outside gate.lock (main()).
        github_part(g)
    g.plan_rows()
    if not dry:
        g.write()
    return g


def select(names_needs, patterns):
    """The checks --only keeps: those a pattern names, and what each needs, transitively."""
    chosen = {n for n in names_needs if any(fnmatch.fnmatchcase(n, p) for p in patterns)}
    grow = list(chosen)
    while grow:
        for n in names_needs.get(grow.pop(), ()):
            if n in names_needs and n not in chosen:
                chosen.add(n)
                grow.append(n)
    return chosen


TABLET_NAMES = TABLET_CHECKS + ("pictures/tablet",)


def level_needs(args, level, known, planned):
    """{check: needs} of one level: the names a dry run of that level registers."""
    a = argparse.Namespace(**vars(args))
    a.mode = level
    dry = run_checks(a, known, planned, dry=True)
    shutil.rmtree(dry.work, ignore_errors=True)
    return dry.needs


def also_problem(args, also, known, planned):
    """Why `--also` cannot be run ("" when it can): a name is one of the nightly's parts in ALSO_PARTS, or a check of the main level,
    which is how a landing runs it. A part whose function is not on main is "not built yet"; the soak's two lengths are asked one at
    a time; any other name is unknown and the known names are printed."""
    main_needs = level_needs(args, "main", known, planned)
    parts = []
    unbuilt = [n for n in also if n in ALSO_PARTS and ALSO_PARTS[n].run is None]
    parts += ["--also %s: this check is not built yet" % n for n in unbuilt]
    if all(n in also for n in ALSO_ONE_OF):
        parts.append("--also: %s are one soak at two lengths; ask for one" % " and ".join(ALSO_ONE_OF))
    unknown = [n for n in also if n not in ALSO_PARTS and n not in main_needs]
    if unknown:
        parts.append("--also names no check the gate knows: %s. The checks a landing can run are: %s" % (
            ", ".join(unknown), ", ".join(sorted(set(main_needs) | set(ALSO_PARTS)))))
    return "; ".join(parts)


def landing_level(plan, also):
    """The level a landing of a plan that is not full has: its own (LAND_LEVEL: the 12 quick stops, on the three renderers and the tablet
    when the plan can move a picture) unless it is a section's full check that can move a picture (the groups `section` and `pictures`:
    every picture the main level compares) or asks for checks by name (the nightly's depth), which are of the main level."""
    return "main" if also or (plan.takes_pictures and plan.is_section) else LAND_LEVEL


def landing_selection(args, plan, also, known, planned):
    """The checks of a landing with a plan that is not full: those of the plan's groups, and those `also` names, each with what
    it needs, from a dry run of the level the landing has (args.mode: the main level when there is an --also, else the landing's
    own)."""
    dry = run_checks(args, known, planned, dry=True)
    shutil.rmtree(dry.work, ignore_errors=True)
    selected = scope.select(dry.needs, plan.groups)
    if also:
        patterns = [p for n in also for p in (ALSO_PARTS[n].checks if n in ALSO_PARTS else (n,))]
        selected |= select(dry.needs, patterns)
    return selected


def parser(land=False):
    """The options of a run. `land`: for `gate.py land`, whose options are a run's besides the branches (no level flag)."""
    p = argparse.ArgumentParser(prog="gate.py land" if land else None, description=__doc__.split("\n")[0])
    if land:
        p.add_argument("branches", nargs="*", help="the branches to land (put in the queue, in this order)")
        p.add_argument("--queue", action="store_true", help="run what waits in the queue and take no branch")
        p.add_argument("--agent", default=os.environ.get("NEBULA_AGENT", "1"), help="who asks, for the queue and the fault record")
        p.add_argument("--queue-file", default=str(landing.QUEUE))
        p.add_argument("--runner-lock", default=str(landing.RUNNER_LOCK), help="the lock folder of the landing runner (one at a time)")
        p.add_argument("--landing-tree", default=str(landing.LANDING_TREE), help="the work folder the gate owns for batches")
        p.add_argument("--failures", default=str(landing.FAILURES), help="the store of the same failure three times")
        p.add_argument("--live", default=os.environ.get("NEBULA_LIVE", "D:/NebulaWork/plan/native"),
                       help="the folder of tool-faults.tsv (the step tool's live folder)")
        p.add_argument("--wait-minutes", type=float, default=600.0, help="how long a caller waits for another runner")
    mode = p.add_mutually_exclusive_group(required=not land)
    for level in LEVELS:
        mode.add_argument("--" + level, dest="mode", action="store_const", const=level)
    p.add_argument("--move", action="store_true", help="--main: move main to this commit when it passes (section 5)")
    p.add_argument("--github-wait", type=float, default=GITHUB_WAIT_MINUTES,
                   help="--move: minutes to wait for GitHub's jobs. Longer only for the one candidate that brings a job "
                        "whose compiler cache is cold (Planner 11-18 03:49); --covers says why, and the board's LOCK "
                        "line shows the wait")
    p.add_argument("--github-again", action="store_true",
                   help="--move: before the start check, every `must` job that is red for the commit is run again once on "
                        "GitHub (each written to the tool-fault record as ENV with the first line of its log), and the run "
                        "judges by the new result. For a red whose log shows a download or an install failing, not the game")
    if land:
        p.add_argument("--section", action="store_true",
                       help="the section's full check (the step tool gives it at the last step of a package, or the fourth step since "
                            "the last check): the main level's whole set, whatever the changed files are")
        p.add_argument("--github", choices=("first",), default=None,
                       help="`first`: this landing waits for GitHub's `must` jobs before main moves (the step tool uses it for a "
                            "repair of a red job of GitHub, so the fix is proven there first)")
    if not land:
        p.add_argument("--live", default=os.environ.get("NEBULA_LIVE", "D:/NebulaWork/plan/native"),
                       help="the folder of tool-faults.tsv (the step tool's live folder)")
        p.add_argument("--agent", default=os.environ.get("NEBULA_AGENT", "1"), help="who asks, for the fault record")
        # The mark of the landing runner's own call (main moves only through gate.py land): the runner's process id, checked
        # against the holder of the runner's lock. Not for a person: a --move without it is refused.
        p.add_argument("--landing-runner", type=int, default=0, help=argparse.SUPPRESS)
        p.add_argument("--runner-lock", default=str(landing.RUNNER_LOCK), help=argparse.SUPPRESS)
        p.add_argument("--queue-file", default=str(landing.QUEUE),
                       help="the long run: the landing queue its parts look at before they take the gate lock")
    p.add_argument("--only", default="", help="only these checks (shell patterns, comma-separated) and what they need")
    p.add_argument("--scope", default="",
                   help="--main: only the checks of the groups a change needs (tools/gate/scope.py): GROUP[,GROUP...], 'auto' "
                        "for the groups of the files changed from the merge base with origin/main, or 'full'; what a "
                        "landing runs (gate.py land). With --move the run lands the commit the way a main gate does")
    p.add_argument("--also", default="",
                   help="--scope or `land`: more checks to run in the landing, by the names the gate gives them (comma-separated); "
                        "they run as the main level runs them. A name the gate does not know refuses the landing")
    p.add_argument("--mods", choices=scope.MODS_VALUES, default=None,
                   help="the mod suites and LumaLink's own check: off, short (the quick mod set) or full; the default is the "
                        "`mods =` line of tools/gate/scope.tsv (off: they run in no landing and no nightly run)")
    p.add_argument("--build-root", default=os.environ.get("NEBULA_GATE_BUILD", "D:/NebulaWork/build/gate-%s" % TREE.name),
                   help="where the builds go (kept between runs, so builds are incremental)")
    p.add_argument("--pak", default=os.environ.get("NEBULA_GATE_PAK", ""),
                   help="the US game.pak to run on; by default the data found by key, or made on demand, in "
                        "D:/NebulaWork/build/game-data (tools/gate/gamedata.py)")
    p.add_argument("--paks", default=os.environ.get("NEBULA_GATE_PAKS", ""),
                   help="the other discs' game.paks, DISC=path joined by the path separator: the tests with a \"disc\"; "
                        "by default found by key like the US data")
    p.add_argument("--work", default=os.environ.get("NEBULA_GATE_WORK", "D:/NebulaWork/build/gate-runs"))
    p.add_argument("--discs", default=os.environ.get("NEBULA_GATE_DISCS", "D:/NebulaWork/discs"),
                   help="a folder of disc images named by disc (RMGE01.wbfs, RMGP01.rvz, RMGJ01.rvz): the collision proof")
    p.add_argument("--out", help="the result file (default: result.json in the run's work folder)")
    p.add_argument("--jobs", default=str(max(4, (os.cpu_count() or 8) - 4)))
    p.add_argument("--no-build", action="store_true", help="use the bins already in the build root")
    p.add_argument("--record", action="store_true", help="also keep the result in docs/plan/gate-runs, for a commit")
    p.add_argument("--keep-runs", action="store_true",
                   help="keep the passed tests' working files (pictures, sound, game folders), for same_runs.py")
    p.add_argument("--seed-from", default=os.environ.get("NEBULA_GATE_SEED", ""),
                   help="a configured MSVC build folder whose compiled shader stages seed a new build (content-keyed)")
    p.add_argument("--lumalink", default=os.environ.get("LUMALINK", ""),
                   help="a lumalink program for the mod tests in place of the pinned one the gate builds")
    p.add_argument("--mod-parallel", type=int, default=2,
                   help="mod tests run at once in each mods lane, each game in its own folder (the lanes run side by "
                        "side too)")
    p.add_argument("--tablet", default=os.environ.get("NEBULA_GATE_TABLET", ""),
                   help="the main, long and final levels: the tablet's serial (its part is owed when it does not answer)")
    p.add_argument("--tablet-lock", default=os.environ.get("NEBULA_GATE_TABLET_LOCK", "D:/NebulaWork/plan/tablet.lock"))
    p.add_argument("--tablet-wait", type=float, default=20.0, help="minutes to wait for a held tablet lock before the "
                                                                   "tablet's part is owed")
    p.add_argument("--android-deps", default=os.environ.get("NEBULA_GATE_ANDROID_DEPS", "D:/NebulaWork/deps"),
                   help="build_apk.ps1 -Deps: SDL, Vulkan-Headers, volk, VMA and glslang's sources")
    p.add_argument("--gate-lock", default=os.environ.get("NEBULA_GATE_LOCK", "D:/NebulaWork/plan/gate.lock"),
                   help="the lock one main gate (or final check) at a time holds")
    p.add_argument("--long-lock", default=os.environ.get("NEBULA_GATE_LONG_LOCK", "D:/NebulaWork/plan/long.lock"),
                   help="the lock one long run (or final check) at a time holds")
    p.add_argument("--wait", type=float, default=240.0,
                   help="minutes to wait in the queue for a held gate or long lock (0: refuse at once)")
    p.add_argument("--board", default=os.environ.get("NEBULA_GATE_BOARD", "D:/NebulaWork/plan/BOARD.md"),
                   help="the board the lock, owed and MAIN-MOVED lines go to ('' for none)")
    p.add_argument("--status", default=os.environ.get("NEBULA_GATE_STATUS", "D:/NebulaWork/plan/STATUS.md"),
                   help="the owner's page tools/gate/status.py writes after the run ('' for none)")
    p.add_argument("--who", default=os.environ.get("NEBULA_GATE_WHO", "gate.py"), help="who runs it, for the board")
    p.add_argument("--covers", default="", help="what the run answers for (commits, packages), for the board")
    p.add_argument("--soak-minutes", type=int, default=int(os.environ.get("NEBULA_GATE_SOAK_MINUTES", "120")),
                   help="the long run: the soak's minutes on each renderer")
    p.add_argument("--serial", action="store_true",
                   help="run the parts one after another, not side by side (the proof of section 7)")
    p.add_argument("--without-quick", action="store_true",
                   help="--main without --move: run without a passed quick gate of the same commit")
    return p


def unbuffer_output():
    """A line is on the screen (or in the file the output goes to) when it is printed, not when the buffer fills: a landing's
    checks run for minutes, and `gate.py land > file` showed nothing until it ended."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(line_buffering=True)
        except (AttributeError, ValueError, OSError):
            pass


def main(argv=None):
    """One run of the gate, or `land`/`data`. A candidate branch (gate/<run>, section 5.3) this run pushed to origin is removed from
    origin when the run ends, whatever the end: passed and moved, checks failed, stopped on a red job of GitHub, not judged, refused after
    the push, or an exception. The one that stays is a candidate whose jobs are read after the move (github-pending.tsv): the settle
    removes it once it has read them. GitHub's run stays readable by the commit."""
    left = []
    try:
        return run_main(argv, left)
    finally:
        for candidate in left:
            delete_candidate(candidate)


def run_main(argv, left):
    """main() with the list `left` of the candidates this run pushed and has not handed on."""
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
    # What the gate starts (builds, test runs, a landing's measure) never waits for a speed measure: the gate has the machine's
    # lock itself, and a measure it started is its own (tools/gate/measure_wait.py).
    os.environ["NEBULA_NO_MEASURE_WAIT"] = "1"
    noprompt.install()     # no git prompt and no crash box for anything the gate starts (tools/gate/noprompt.py)
    argv = sys.argv[1:] if argv is None else list(argv)
    unbuffer_output()
    # The run's job object (machine.py): every program the gate starts, and theirs, ends with it, however it ends. Once a process.
    if machine.RUN_JOB[0] is None:
        machine.join_new_job(lambda text: print(text, file=sys.stderr, flush=True))
    if argv and argv[0] == "land":
        return land_main(argv[1:])
    if argv and argv[0] == "data":
        return gamedata.main(argv[1:])
    if argv and argv[0] == "speed-base":
        return speedbase.main(argv[1:])
    if argv and argv[0] == "long-start":
        return long_start_main(argv[1:])
    if argv and argv[0] == "long-results":
        import long_results
        return long_results.run_cli(sys.modules[__name__], argv[1:])
    args = parser().parse_args(argv)
    patterns = [s.strip() for s in args.only.split(",") if s.strip()]

    def refuse(text):
        print("gate.py: " + text, file=sys.stderr)
        print(kinds.verdict_line("not-judged"), flush=True)
        return 2

    # Main moves in one way only: the landing runner's own call carries its mark (the runner's process id, which must be the holder
    # of the runner's lock). Refused before anything runs: no lock is taken and no run folder is made.
    if args.move and not landing.called_by_runner(args.landing_runner, args.runner_lock):
        return refuse("main moves only through gate.py land <branch>")

    also = landing.also_names(args.also)
    if also and not args.scope:
        return refuse("--also goes with --scope: it adds checks to a landing's")
    plan = None
    if args.scope:
        if args.mode != "main" or patterns:
            return refuse("--scope goes with --main alone: the checks of the groups a change needs")
        try:
            if args.scope == "auto":
                paths, why = scope.changed_paths(GIT, TREE, MAIN_REF)
                if why:
                    return refuse(why)
                plan = scope.plan_for(paths, scope.read_table())
            else:
                groups, full = scope.groups_from_text(args.scope)
                plan = scope.Plan(groups, full, [], {})
        except scope.ScopeError as error:
            return refuse("tools/gate/scope.tsv or --scope is wrong:\n  " + "\n  ".join(error.problems))
        print("gate.py: scope: " + "; ".join(scope.describe(plan)), flush=True)
        if not plan.full and plan.takes_pictures and not plan.is_section:
            print("gate.py: the change can move a picture: a step's landing plays the 12 quick stops on the three renderers and the "
                  "tablet's 12 stops, with their pictures and replays; the rest of the main level's pictures, the debug layers and the "
                  "speed measures are the section's full check (gate.py land --section)", flush=True)
        if not plan.full and plan.takes_pictures and plan.is_section:
            print("gate.py: the section's full check: the landing is of the main level and takes every picture that level compares "
                  "(the engine tests and every stop of the tour on the three renderers, the two other discs on Direct3D 12, the tablet's "
                  "12 stops%s)" % (", and every stop a PICTURES-CHANGE.txt line names on the other discs' Direct3D 11 and Vulkan"
                                   if (pathlib.Path(TREE) / picture_tool.CHANGES_NAME).is_file() else ""), flush=True)
        if not plan.full:
            args.mode = landing_level(plan, also)
        if also and not plan.full:
            print("gate.py: and, asked for by name: %s. The landing keeps the main level so that they run as the nightly run runs "
                  "them (every stop of a tour)." % ", ".join(also), flush=True)
    if args.move and (args.mode not in ("main", LAND_LEVEL) or patterns or args.no_build):
        return refuse("--move goes with --main alone: the whole main gate, built here")
    if plan is not None and not plan.full:
        needs_tablet = "tablet12" in plan.groups or any(n in TABLET_NAMES for n in also)   # `android` alone is the build: no tablet
    else:
        needs_tablet = args.mode != "quick"
    if needs_tablet and not args.tablet:
        return refuse("--%s needs the tablet's serial (--tablet): its part is owed when the tablet is away" % args.mode)
    known, planned, problems = read_lists()
    if problems:
        return refuse("the lists are wrong:\n  " + "\n  ".join(problems))
    if also:
        why = also_problem(args, also, known, planned)
        if why:
            return refuse(why)
    selected = None
    if plan is not None and not plan.full:
        selected = landing_selection(args, plan, also, known, planned)
        if not selected:
            return refuse("the groups %s name no check of this tree" % ",".join(plan.groups))
        print("gate.py: the landing runs %d checks: %s" % (len(selected), ", ".join(sorted(selected))), flush=True)
    if patterns:
        dry = run_checks(args, known, planned, dry=True)
        shutil.rmtree(dry.work, ignore_errors=True)
        selected = select(dry.needs, patterns)
        if not selected:
            return refuse("--only %s names no check of --%s: %s" % (args.only, args.mode, ", ".join(sorted(dry.needs))))
    commit = git("rev-parse", "HEAD")
    if args.mode in ("long", "final") and not patterns:
        git("fetch", "origin", "main")
        if args.mode == "long" and commit != git("rev-parse", "origin/main"):
            return refuse("the long run is on main's newest commit (%s), this tree is at %s" % (
                git("rev-parse", "--short", "origin/main"), commit[:9]))
    # The GitHub part (section 8, package T8a): `first` (the move waits for the `must` jobs) for a plan with the group `github-first`,
    # for a full plan and for a run with no plan; `after` for a plan with `github` alone (main moves when this machine's checks
    # pass, the jobs are read after the move: settle_github_pending); None for a plan with neither: no candidate, no watch,
    # nothing from GitHub.
    form = scope.github_form(plan)
    github = form is not None
    args.github_form = form if args.move else None    # the run's record says which form it was (result.json, history.tsv)
    candidate = rule = notes = None
    if args.move:
        git("fetch", "origin", "main")
        if not git_ok("merge-base", "--is-ancestor", "origin/main", "HEAD"):
            return refuse("main (%s) is not an ancestor of this commit: rebase on it first" % git("rev-parse", "--short", "origin/main"))
        if git("status", "--porcelain"):
            return refuse("the tree has uncommitted changes")
        if github:
            try:
                classes = github_rule.read_table()
            except github_rule.TableError as error:
                return refuse("tools/gate/github_jobs.tsv is wrong:\n  " + "\n  ".join(error.problems))
            notes = Notes()
            rule = make_rule(commit, classes, planned_jobs(planned), notes)
            if args.github_again:
                github_again(rule, args, commit, faults=fault_writer(args.live))
            red = rule.red_before()
            if red:
                board_line(args.board, "%s NOTICE: no main gate of %s: %s." % (args.who, commit[:9], red))
                # The red jobs' logs are saved where the run would have been, and the way out is the message's last line.
                folder = pathlib.Path(args.work) / ("github-red-%s" % commit[:9])
                folder.mkdir(parents=True, exist_ok=True)
                for st in rule.last_reds:
                    github_rule.save_log(github_rule.Finding(st, st.said()), folder, gh_log,
                                         say=lambda line: print(line, file=sys.stderr), advise=False)
                return refuse("main could not move to %s: %s%s%s" % (
                    commit[:9], red, "\n", "\n".join(github_rule.advice(st.name) for st in rule.last_reds)))
        unheld = unheld_candidates(args.work, commit)
        if unheld:
            return refuse("candidates still wait for GitHub that this commit does not hold (%s): rebase on the newest "
                          "of them, so main can reach both" % ", ".join(unheld))
        # The candidate under a name of its own (section 5.3): GitHub runs every job on it while the gate runs, and no
        # later push to a work branch cancels that run.
        if rule is not None:
            candidate = CANDIDATE_PREFIX + "%s-%s" % (datetime.datetime.now().strftime("%Y%m%d-%H%M%S"), commit[:9])
            pushed = push_candidate(commit, candidate)
            if pushed is not None:
                return refuse("the candidate could not be pushed as %s: %s" % (candidate, pushed))
            left.append(candidate)
    quick = None
    if args.mode in ("main", "final") and not patterns and plan is None:
        quick = quick_passed(args.work, git("rev-parse", "--short", "HEAD"), whole=True)
        if quick is None and (args.move or not args.without_quick):
            return refuse("no whole quick gate of %s passed in %s: run --quick first" % (commit[:9], args.work))
    if args.mode == "long":
        below_normal()
    # Every level takes the gate lock for its own checks, --only runs too (Planner 11-18 03:44, one gate at a time: a
    # quick gate beside a main gate finishes no sooner, and beside a hand slot it disturbs a measurement). The long run
    # keeps the long lock alone until it takes the gate lock part by part.
    locks = locks_for(args.mode, patterns, args.gate_lock, args.long_lock)
    holder = "%s gate of %s@%s %s%s%s%s" % (args.mode, TREE.name, commit[:9], lockholder.named(),
                                               ("; covers " + args.covers) if args.covers else "",
                                               ("; quick gate " + quick.name) if quick else "",
                                               ("; GitHub wait %g minutes" % args.github_wait)
                                               if args.github_wait != GITHUB_WAIT_MINUTES else "")
    taken = []
    for lock in locks:
        held = take_gate_lock(lock, holder, args.wait, args.board, args.who)
        if held is not None:
            for t in taken:
                free_gate_lock(t, holder, args.board, args.who, "not run: " + held)
            return refuse(held)
        taken.append(lock)
    # The pre-push checks build a work tree of their own (the ratchet's inventory): machine work, so under the lock,
    # never beside another gate or a measurement (N-1069).
    why = push_checks(commit) if args.move or args.mode == "quick" else None
    if why:
        for t in taken:
            free_gate_lock(t, holder, args.board, args.who, "not run: " + why)
        # A repository rule (the census, the ratchet) refuses the commit: that is a refusal of the change, never "not judged" (T11-B: step
        # S0.1.3 was told "land once more" three times for the same census line). The KIND line is the check's own.
        print("gate.py: main could not move to %s: %s" % (commit[:9], why), file=sys.stderr)
        print("KIND fail %s - :: %s" % ("repo/code" if why.startswith("the console census") else "push/ratchet", " ".join(why.split())), flush=True)
        print(kinds.verdict_line("fail"), flush=True)
        return 1
    code, summary, g, moved_line = 2, "it ended before writing a result", None, None
    watch = GithubWatch(rule, candidate).start() if rule is not None else None
    try:
        try:
            g = run_checks(args, known, planned, selected)
        finally:
            if watch is not None:
                watch.stop()
        if watch is not None:
            notes.to(g.work / "github.log")
            if watch.reds:
                # A red `must` job stopped the checks: its log is saved in the run's folder and its first lines printed.
                github_rows(g, rule, "fail", watch.red, github_rule.Reading.of(reds=watch.reds), args.github_wait)
            elif watch.envs:
                github_rows(g, rule, "env", watch.envs[0].text, github_rule.Reading.of(envs=watch.envs), args.github_wait)
        code = g.code
        summary = {0: "all checks passed", 1: "checks failed (exit 1)"}.get(code, "not judged (exit %d): a tool or the machine, not the change" % code)
        if g.card_shared:
            summary += "; card shared"
        if watch is not None and watch.red:
            code, summary = 1, "stopped: " + watch.red
            board_line(args.board, "%s NOTICE: main gate %s on %s stopped: %s." % (args.who, g.name, commit[:9],
                                                                                    watch.red))
        if args.mode == "long" and not patterns and g is not None:
            long_results_step(g, args)
        owed = [r["check"] for r in g.results if r["state"] == "owed"]
        if owed and args.board:
            board_line(args.board, owed_line(args.who, args.mode, g.name, commit, owed))
        if args.move and code == 0:
            record_tablet_owed(getattr(args, "live", ""), commit, g.results)
        if args.move and code == 0 and not (watch is not None and watch.red):
            # The machine is free for the next gate from here (section 5.5); this process waits for GitHub alone, when the
            # plan has a GitHub part, and moves main once every `must` job is green for the commit.
            for lock in taken:
                free_gate_lock(lock, holder, args.board, args.who, ("its checks passed; it waits for GitHub's jobs on %s"
                                                                    % candidate) if rule is not None else
                               "its checks passed; the plan has no GitHub part")
            taken = []
            if rule is not None and form == "first":
                add_waiting(args.work, commit, candidate, os.getpid())
            landed = commit  # the commit main moves to: the tested one, or on top of it the pictures' approval
            why, detail, pending_after = None, "the plan has no GitHub part", False
            try:
                if rule is not None:
                    if form == "first":
                        kind, detail, reading = rule.wait(args.github_wait, sleep=GITHUB_SLEEP)
                    else:
                        # One reading, no waiting: every `must` job that has its end is judged now (green: move; red: main does
                        # not move); when some are still running main moves and the commit's jobs are read after the move.
                        kind, detail, reading = rule.look()
                        pending_after = kind == "pending"
                        if pending_after:
                            print("GitHub's jobs for %s are read after the move" % commit[:9], flush=True)
                    if not pending_after:
                        github_rows(g, rule, kind, detail, reading, args.github_wait)
                    code = g.code
                    if kind == "fail":
                        why = "GitHub's jobs on %s: %s" % (candidate, detail)
                    elif kind == "env":
                        why = detail
                if why is None:
                    move_lock = str(pathlib.Path(args.gate_lock).with_name("move.lock"))
                    held = take_gate_lock(move_lock, holder, 10.0, "", args.who, poll=5.0)
                    if held is not None:
                        why = held
                    else:
                        try:
                            # The pictures with no approved copy are approved as part of the landing: one commit on top of
                            # the tested one, the picture checks run again on the same runs, and only then main moves.
                            landed, approval_fault = approve_pictures(g, commit)
                            if approval_fault is not None:
                                code, tool_fault = 2, "pictures not approved: " + approval_fault
                                print("KIND tool pictures/approve - :: " + approval_fault)
                                why = tool_fault
                            else:
                                why = move_main(landed, g.name, g.work, args.board, args.who, args.covers)
                                if why is not None and landed != commit:
                                    git("reset", "--hard", "-q", commit)
                        finally:
                            shutil.rmtree(move_lock, ignore_errors=True)
            finally:
                if rule is not None and form == "first":
                    drop_waiting(args.work, commit)
            keep_candidate = False
            if pending_after and why is None:
                # Main moved with GitHub's jobs not finished: one line in the live folder; the candidate stays on GitHub until
                # settle_github_pending has read the jobs (it deletes the candidate then).
                try:
                    github_rule._live().add_pending(args.live, commit, args.covers, candidate, g.work)
                    keep_candidate = True
                except Exception as e:  # noqa: BLE001 - main has moved; the candidate is not left behind without a line to settle it
                    print("gate.py: the pending line for %s could not be written (%s: %s); GitHub's jobs are not read for it" % (
                        commit[:9], type(e).__name__, e), file=sys.stderr)
            if keep_candidate:
                g.late("github/after", "pass", "its jobs are read after the move (github-pending.tsv in the live folder)")
                left.remove(candidate)       # the settle of the pending line removes it
            if why is not None:
                # `fail` (exit 1) only where a job of GitHub is red: the change is at fault. Every other reason (main moved, the push, a
                # lock, GitHub not finishing) judged nothing: not-judged, exit 2.
                kind = not_moved_kind(why)
                code, summary = (1 if kind in ("github", "hook") else 2), "passed, main not moved: " + why + ("; card shared" if g.card_shared else "")
                print("gate.py: main not moved: " + why, file=sys.stderr)
                if kind == "hook":
                    print("KIND fail pre-push - :: " + " ".join(why.split()), flush=True)
                elif why.startswith(SIGN_IN):
                    print("KIND env github/sign-in - :: " + " ".join(why.split()), flush=True)
                moved_line = "NOT-MOVED %s %s" % (kind, why)
                board_line(args.board, "%s NOTICE: main gate %s on %s passed its checks; main not moved: %s." % (
                    args.who, g.name, commit[:9], why))
            else:
                moved_line = "MOVED %s" % landed
                try:
                    append_move(pathlib.Path(args.work) / "history.tsv", g.name, landed, args.covers)
                except OSError as e:
                    print("gate.py: the move was not written to history.tsv (%s: %s)" % (type(e).__name__, e), file=sys.stderr)
                keep_measures(g, args)
                rebase_tablet_after_gap(g, args)
                keep_speed(g, landed, args)
                summary = "passed, %s, main moved to %s" % (
                    "GitHub's jobs are read after the move" if pending_after else
                    ("GitHub green (%s)" % detail) if rule is not None else "no GitHub part in the plan", landed[:9]) + (
                                                                  "; card shared" if g.card_shared else "")
                # The build that moved main is kept (the two newest): a test that fails in a later change is run on it.
                kept = kinds.keep_base(git("rev-parse", "HEAD"), g.bin, BASE_BINS)
                if kept is not None:
                    print("gate.py: " + kept)
                    board_line(args.board, "%s NOTICE: %s." % (args.who, kept))
                # No speed measure starts at a move: it is a part of the nightly run.
                started, what = start_long(landed, args)
                print("gate.py: long run: %s" % what)
                if started is not None:
                    print("LONG-STARTED %s" % (pathlib.Path(args.work) / ("long-%s.log" % landed[:9])), flush=True)
                    board_line(args.board, "%s LONG: %s; none had started in %d hours (section 14, N-1046)." % (args.who, what, LONG_EVERY_HOURS))
                elif what.startswith("its tree"):
                    board_line(args.board, "%s NOTICE: main moved to %s; no long run started: %s." % (args.who, landed[:9], what))
    finally:
        if g is not None and g.runs_held:
            g.prune_runs()
        for lock in taken:
            free_gate_lock(lock, holder, args.board, args.who, summary)
        if args.status:
            try:
                import status
                status.write(args.status, work=args.work, board=args.board)
            except Exception as e:  # noqa: BLE001 - the page is a report; the gate's result stands without it
                print("gate.py: the status page was not written: %s: %s" % (type(e).__name__, e), file=sys.stderr)
    # The run's last word, and its last line: a pass only where main moved, or no move was asked and every check passed.
    verdict = {0: "pass", 1: "fail"}.get(code, "not-judged")
    if g is not None and rule is not None:
        github_notes(g, rule)
    if g is not None and (verdict != g.verdict or (moved_line or "").startswith("NOT-MOVED")):
        g.write_verdict(verdict, summary)
    if moved_line:
        print(moved_line, flush=True)
    print(kinds.verdict_line(verdict), flush=True)
    return code


def long_results_step(g, args):
    """The end of a long run that ran all its checks (not under --only): the pieces' times are written (parts.json) and what follows from
    the results is done (tools/gate/long_results.py). A fault of this step is said and goes to tool-faults.tsv; the run's verdict stands."""
    try:
        (g.work / "parts.json").write_text(json.dumps(g.part_log, indent=1) + "\n", encoding="utf-8")
        import long_results
        lr = long_results.make_args(live=args.live, work=args.work, queue_file=args.queue_file, tablet=args.tablet or None,
                                    build_root=args.build_root, board=args.board, gate_lock=args.gate_lock, long_lock=args.long_lock,
                                    tablet_lock=args.tablet_lock, runner_lock=args.runner_lock, wait=args.wait)
        long_results.results(long_results.Run(g.work), long_results.build_context(sys.modules[__name__], lr, False))
    except Exception as e:  # noqa: BLE001 - what follows from the results is not the results: the run's own word stands
        print("gate.py: long-results: %s: %s" % (type(e).__name__, e), file=sys.stderr, flush=True)
        try:
            fault_writer(args.live)("gate", "nightly", "TOOL", "long-results", "", "%s: %s" % (type(e).__name__, e), str(g.work))
        except Exception:  # noqa: BLE001
            pass


def not_moved_kind(why):
    """Why a pass did not move main, for gate.py land: main moved during the run (nothing was judged), a red job of GitHub
    (the batch fails), or another reason (a lock, the pictures' approval: not judged)."""
    if "main moved during the gate" in why:
        return "main-moved"
    if why.startswith("GitHub's jobs on "):
        return "github"
    if why.startswith("the pre-push hook refuses main"):
        return "hook"     # a repository rule at the push: a refusal of the change, like a red job
    if why.startswith("GitHub "):
        return "env"      # GitHub did not finish or could not run a job: nothing was judged, the branch waits
    return "other"


# gate.py land -------------------------------------------------------------------------------------------------------------


def parse_run_output(lines, code):
    """The landing.Result of one run of `gate.py --main --move --scope ...` from its output lines and exit code: its KIND
    lines, `MOVED <sha>` when main moved, `NOT-MOVED <kind> <why>` when the checks passed and main did not move, the run's
    folder from its `result:` line, and VERDICT. A run that wrote no VERDICT or exited with another code than 0, 1, 2 is not
    judged (the tool broke)."""
    kind_lines, moved, not_moved, folder, verdict, why, long_log = [], "", None, "", None, "", ""
    for raw in lines:
        line = raw.rstrip("\r\n")
        if line.startswith("KIND "):
            kind_lines.append(line)
        elif line.startswith("MOVED "):
            moved = line.split()[1]
        elif line.startswith("LONG-STARTED "):
            long_log = line[len("LONG-STARTED "):].strip()
        elif line.startswith("NOT-MOVED "):
            parts = line.split(" ", 2)
            not_moved = (parts[1], parts[2] if len(parts) > 2 else "")
        elif line.startswith("result: "):
            folder = str(pathlib.Path(line[len("result: "):].strip()).parent)
        elif line.startswith("VERDICT "):
            verdict = line.split()[1]
        elif line.startswith("gate.py: ") and not why:
            why = line[len("gate.py: "):]
    failures_ = landing.failures_of_lines(kind_lines)
    if verdict is None or code not in (0, 1, 2) or (code == 0 and verdict != "pass"):
        return landing.Result("not-judged", kind_lines, failures_ or [("gate.py", "tool", "exited %s with no usable verdict" % code)],
                              folder, why=why or "gate.py exited %s with no usable verdict" % code)
    if not_moved is not None:
        return landing.Result("pass", kind_lines, failures_, folder, "", not_moved[1], not_moved[0])
    if verdict == "pass" and not moved:
        return landing.Result("pass", kind_lines, failures_, folder, "", "the run passed and did not say that main moved", "other")
    return landing.Result(verdict, kind_lines, failures_, folder, moved, why if verdict == "not-judged" else "", long_log=long_log)


class GateChecker:
    """The checks of a batch (landing.Landing's checker): `gate.py --main --move --scope GROUPS` of the landing tree at the
    tip, run as a program of its own, so the tip's own tools run on the tip's own files. `popen` is replaced by tests."""

    def __init__(self, args, say=print, popen=subprocess.Popen):
        self.args, self.say, self.popen = args, say, popen

    def command(self, tree, names, groups_text, also=""):
        a = self.args
        scope_text = "full" if groups_text.startswith("full") else groups_text
        cmd = [sys.executable, "-X", "utf8", "-I", str(pathlib.Path(tree) / "tools" / "gate" / "gate.py"), "--main", "--move",
               "--scope", scope_text, "--who", a.who, "--covers", ", ".join(names), "--work", a.work, "--board", a.board,
               "--status", a.status, "--gate-lock", a.gate_lock, "--long-lock", a.long_lock, "--tablet-lock", a.tablet_lock,
               "--tablet-wait", str(a.tablet_wait), "--paks", a.paks, "--discs", a.discs, "--jobs", a.jobs,
               "--wait", str(a.wait), "--github-wait", str(a.github_wait),
               "--landing-runner", str(os.getpid()), "--runner-lock", str(getattr(a, "runner_lock", landing.RUNNER_LOCK))]
        if a.pak:
            cmd += ["--pak", a.pak]
        if a.tablet:
            cmd += ["--tablet", a.tablet]
        if a.mods:
            cmd += ["--mods", a.mods]
        if a.github_again:
            cmd += ["--github-again", "--live", a.live, "--agent", a.agent]
        if also:
            cmd += ["--also", also]
        return cmd

    def run(self, tree, tip, names, groups_text, also=""):
        cmd = self.command(tree, names, groups_text, also)
        lines = []
        env = dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONUNBUFFERED="1")     # each check's line arrives when it ends
        try:
            with self.popen(cmd, cwd=str(tree), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, text=True,
                            encoding="utf-8", errors="replace") as p:
                for line in p.stdout:
                    lines.append(line)
                    self.say(line.rstrip("\r\n"))
                code = p.wait()
        except OSError as error:
            return landing.Result("not-judged", [], [("gate.py", "tool", "could not be started: %s" % error)],
                                  why="gate.py could not be started: %s" % error)
        return parse_run_output(lines, code)


def fault_writer(live_folder):
    """landing.Landing's `faults`: one line of tool-faults.tsv at once (tools/plan/live.py), never through a landing."""
    def write(agent, step, kind, check, test, message, run_folder, could_not=""):
        sys.path.insert(0, str(TREE / "tools" / "plan"))
        import live  # noqa: E402 - tools/plan, beside the step tool that shows the table
        pathlib.Path(live_folder).mkdir(parents=True, exist_ok=True)
        live.append_fault(live_folder, agent, step, kind, check, message, test=test, run_folder=run_folder, could_not=could_not)
    return write


def land_main(argv):
    """gate.py land BRANCH [BRANCH ...] | gate.py land --queue: tools/gate/landing.py's queue and batches."""
    args = parser(land=True).parse_args(argv)
    args.mode = "main"
    args.move = True
    settle_github_pending(args.live)     # the jobs of landings that moved main before GitHub finished: read now, never waited for
    if bool(args.branches) == bool(args.queue):
        print("gate.py: land takes branches to put in the queue, or --queue to run what waits: one of the two", file=sys.stderr)
        print(kinds.verdict_line("not-judged"), flush=True)
        return landing.EXIT_NOT_JUDGED
    planted = landing.plant_branches(args.branches)
    if planted:
        print("gate.py: refused: %s: %s" % (", ".join(planted), landing.PLANT_REFUSAL), file=sys.stderr)
        print(kinds.verdict_line("not-judged"), flush=True)
        return landing.EXIT_NOT_JUDGED
    also = landing.also_names(args.also)
    if also and args.queue:
        print("gate.py: --also goes with branches: it is asked for by the caller that lands them", file=sys.stderr)
        print(kinds.verdict_line("not-judged"), flush=True)
        return landing.EXIT_NOT_JUDGED
    if also:
        # Before anything is queued or run: a name the gate does not know refuses the landing, and the names are printed.
        known, planned, problems = read_lists()
        why = ("the lists are wrong:\n  " + "\n  ".join(problems)) if problems else also_problem(args, also, known, planned)
        if why:
            print("gate.py: " + why, file=sys.stderr)
            print(kinds.verdict_line("not-judged"), flush=True)
            return landing.EXIT_NOT_JUDGED
    if args.section:
        also.append(landing.SECTION_CHECK)     # the queue keeps it with the branch: the batch is run as the section's full check
    if args.github == "first":
        also.append(landing.GITHUB_FIRST)      # the queue keeps it with the branch: the batch is run with `github-first`
    sys.path.insert(0, str(TREE / "tools" / "plan"))
    import failures  # noqa: E402 - tools/plan/failures.py: the step tool's rule, in one place
    queue = landing.Queue(args.queue_file)
    runner = landing.Landing(queue, GateChecker(args), landing.Repo(GIT), source=TREE, tree=args.landing_tree,
                             store=failures.Store(args.failures), faults=fault_writer(args.live),
                             runner=landing.FolderLock(args.runner_lock))
    if args.queue:
        return runner.run_only_queue()
    return runner.land(args.branches, args.agent, args.wait_minutes, also=",".join(also))


def below_normal():
    """The long run runs below normal priority (section 3.3), and so do the programs it starts."""
    if os.name == "nt":
        kernel = ctypes.windll.kernel32
        kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x4000)  # BELOW_NORMAL_PRIORITY_CLASS
    else:
        os.nice(5)


if __name__ == "__main__":
    sys.exit(main())
