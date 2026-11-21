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
         --move: main moves to this commit when it passes (section 5). It needs a whole quick gate of the commit
         passed, the branch pushed and main an ancestor of it; it takes the gate lock (or waits --wait minutes),
         and on a pass fast-forwards main and pushes it itself, writes MAIN-MOVED on the board and frees the lock.
         With --move, when the plan has the group `github` (tools/gate/scope.tsv; a run with no plan has it), the commit is
         pushed first as gate/<run> (section 5): GitHub runs every job on it while the gate runs (a red `must` job stops the
         run), the gate frees gate.lock when its own checks pass, waits up to 120 minutes for every `must` job to be green
         for the commit (any push run of it; the newest result wins), then moves main and deletes gate/<run>. Main never
         moves before they are green; a cancelled job, or one red at a set-up (`uses:`) step, is run again once, and the limit gives env
         (not judged). --github-again: every red `must` job is run again once before the start check, each a tool-fault line (ENV). A plan
         with no `github` pushes no candidate, reads nothing from GitHub and moves main when its own checks pass
         (tools/gate/github_rule.py).
         A picture with no approved copy (kind new), or one the branch declares in PICTURES-CHANGE.txt, never fails a run:
         when the checks have passed and GitHub is green, the gate approves them before it moves main, as one commit on
         top of the tested one that touches only tests/pictures/approved.tsv (and removes PICTURES-CHANGE.txt), runs the
         picture checks again on the same runs (all must pass, else nothing moves: tool), and moves main to that commit.
         After the move the play route measure (tools/measure) starts on a copy of the build, then the long run.
--long   level 3, on main's newest commit, under the long lock, at below-normal priority: the tablet's 102 stops
         against the PC's, a sanitizer build's tour, the soak, every disc on every renderer.
--final  level 4: the main and long levels on one commit. "Done" needs it passed with no check planned or known.
--only CHECK[,CHECK]: only the checks those shell patterns name, and the checks they need (for tracing a red main).
land BRANCH [BRANCH ...] | land --queue: the landing queue (tools/gate/landing.py, docs/plan/04-GATE-AND-MAIN.md section 15): the
         branches wait in D:/NebulaWork/plan/landing-queue.tsv, the runner replays them on origin/main in the gate's own work
         folder, runs the checks of the groups the union of their changed files needs (tools/gate/scope.tsv) once, moves main, and
         halves the batch to find the branch at fault. Exit 0 landed, 1 fail, 2 not judged, 3 the same failure three times.
--scope GROUPS|auto|full: with --main (--move too), only the checks of these groups; what `land` runs on the batch's tip.
--also NAMES: with --scope, and with `land` (a repair's check): more checks, by the names the gate gives them (comma-separated),
         run in the landing besides what the groups need. They run as the main level runs them, every stop of a tour, as the
         nightly run does (`--main --only NAME` is how one nightly check is run by hand), so a landing with --also runs its groups'
         checks at that depth too. A name the gate does not know, or one only the long run has, refuses the landing before
         anything runs (exit 2) and lists the names.
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
  flake  it failed, then passed when run again alone (a line in <plan>/native/flakes.tsv); the third flake of one test
         and renderer in seven days sets the test aside (<plan>/native/set-aside.tsv): it still runs and is reported,
         its fail does not count;
  main   it failed again alone and fails the same way on main's kept build (<build>/base-bins/<commit>): main is at
         fault, not the change;
  new    a picture with no approved copy, or one the change declares (tools/gate/pictures.py): it never fails a run, the
         landing approves it.
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
could not start (bad options, a lock held, a rule of section 5 not met). Main moves only on VERDICT pass.
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
import gate_tour  # noqa: E402
import gamedata  # noqa: E402
import github_rule  # noqa: E402
import kinds  # noqa: E402
import landing  # noqa: E402
import machine  # noqa: E402
import pictures as picture_tool  # noqa: E402
import scope  # noqa: E402
import step_hash  # noqa: E402

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
MAIN_GATE_SECONDS = 30 * 60
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
LABELS = {"fail": "FAIL", "tool": "TOOL", "env": "ENV", "flake": "FLAKE", "new": "NEW"}
NOT_PASSED = ("fail", "tool", "env")
STATES = ("pass", "known", "planned", "owed", "fail", "tool", "env", "flake", "main", "new")


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
        self.new_pictures = []
        self.runs_held = False
        self.tree = TREE
        self.mods = getattr(args, "mods", None) or table_mods()
        self.data_paks = {}   # disc -> its game.pak, once the data check found or made it
        self.main_ref = MAIN_REF
        self.open_rows = []
        self.aside = kinds.read_aside(SET_ASIDE)
        self.verdict = None
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
        self.card_shared = False
        self.slots = None
        self.sampler = None
        self.lanes_seen = 0

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
        where it is not the change's: the default is fail). An exception is the tool's: kind tool, with its text. True
        unless it did not pass; None when --only leaves it out. A check whose tests failed is run again once every lane has
        ended (settle): its row is first printed `again`."""
        if not self.wanted(name, needs):
            return None
        if self.dry:
            return True
        t0 = time.time()
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
        pending = (cases is not None and cases.source is not None
                   and any(r["kind"] == "fail" and r["counts"] and r["test"] != "-" and not r["final"] for r in records))
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
        again is run alone on main's build. The rows' states are settled and printed again."""
        pending = [r for r in self.results if r.get("pending")]
        if self.dry or not pending:
            return
        settler = kinds.Settler(FLAKES, SET_ASIDE, self.commit, say=lambda text: print("gate.py: " + text, flush=True))
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
        append_history(pathlib.Path(self.args.work) / "history.tsv", out)
        if passed and not self.args.keep_runs and not self.args.out:
            if self.new_pictures and getattr(self.args, "move", False):
                # The approval at the landing runs the picture checks again on these folders (main()): pruned after it.
                self.runs_held = True
            else:
                self.prune_runs()
        if self.level == "main" and not self.args.only and seconds > MAIN_GATE_SECONDS:
            print("gate.py: this main gate took %.0f minutes, over 30: a ledger row for Agent 1 (section 7)" % (seconds / 60))
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
        self._save()
        for line in kinds.kind_lines([self.results[-1]]):
            print(line, flush=True)

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
                   "failed", "only", "checks")


def append_history(path, out):
    """One line per run in gate-runs/history.tsv: when, which, the result and every check's state and seconds."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    new = not path.exists()
    c = out["counts"]
    cells = (out["started"], out["run"], out["mode"], out["commit"] + ("+" if out["dirty"] else ""),
             out["verdict"], "%.0f" % out["seconds"], c["pass"], c["known"], c["planned"],
             c["owed"], c["fail"], out["only"] or "-",
             " ".join("%s=%s/%.0f" % (r["check"], r["state"], r.get("seconds", 0.0)) for r in out["checks"]))
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


def parse_scripts(g):
    """Every tracked PowerShell and Python script parses (tools/gate/parse_scripts.py): a script that does not fails
    only when someone runs it."""
    def fn(log):
        code = run([sys.executable, "-X", "utf8", "-I", HERE / "parse_scripts.py", "--tree", TREE], log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        return code == 0, last_line(text)
    return fn


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
        if code != 0 or fails:
            return False, "exit %d, %d FAILED: %s" % (code, len(fails), fails[0][8:120] if fails else last_line(text))
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


def game_tests(g):
    """The game's own unit tests (native/tests), built with the game modules."""
    def fn(log):
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", HERE / "ctest.ps1",
                    "-Dir", pathlib.Path(g.args.build_root) / "game"], log)
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


def start_check(g, renderer):
    """The program's start without game data (host_main.cpp run_start_check): window, renderer, 300 frames shown."""
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
                                       "--crash-dumps", str(out)], stdout=f, stderr=subprocess.STDOUT, timeout=300,
                                      **hidden).returncode
            except subprocess.TimeoutExpired:
                code = 124
        said = [l for l in pathlib.Path(log).read_text(encoding="utf-8", errors="replace").splitlines()
                if l.startswith("start check")]
        return code == 0, "exit %d: %s" % (code, said[-1] if said else "(no start check line)")
    return fn


def scripts(g, paths, extra=()):
    """Python self-tests without the game, each with its own work folder: passes when every one exits 0."""
    def fn(log):
        bad = []
        for path in paths:
            out = g.work / ("self-" + path.stem)
            out.mkdir(parents=True, exist_ok=True)
            args = [a.replace("{out}", str(out)) for a in extra]
            if run([sys.executable, "-X", "utf8", "-I", path, *args], log, cwd=TREE, timeout=900) != 0:
                bad.append(path.stem)
        detail = "%d of %d pass" % (len(paths) - len(bad), len(paths))
        if not paths:
            return False, "no script to run: nothing ran", None, "tool"
        return not bad, detail + ("; not: " + ", ".join(bad) if bad else "")
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

    def __init__(self, g, suite, renderer, label, device=None, bin_dir=None, env=None):
        self.g, self.suite, self.renderer, self.label = g, suite, renderer, label
        self.device, self.bin_dir, self.env = device, bin_dir, env
        self.suite_dir, self.listed = suite_layout(suite)
        self.tag = "%s-%s" % (label, renderer)
        self.second = None

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
        return kinds.read_cases(work, self.suite_dir, names, self.renderer, kind_of_verdict)

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


def harness(g, suite, renderer, label, device=None, bin_dir=None, env=None):
    """One harness suite on one renderer, on this computer or on an Android device (its serial; the test app
    prepared and the device's lock held by the gate). Each test's kind is read from its result.json (the runner
    writes it); a test that left none, a result that cannot be read, a number of results other than the suite lists,
    and none at all are tool. Passes when every test passed (a test whose disc has no pak is not-run: env). The
    results go with it, and a source that runs a failed test again alone and on main's build (not on a device: its
    test app is removed when its part ends). `env`: more for the games' environment (the coverage build's counts)."""
    def fn(log):
        source = HarnessSource(g, suite, renderer, label, device, bin_dir, env)
        work = source.first_work()
        code = source.start(work, bin_dir or g.bin, None, log, g.work / ("harness-%s-%s.lock" % (label, renderer)))
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
        if problem is None and passes and code != 0:
            problem = "the runner exited %d though every test passed" % code
        elif problem is None and not passes and code == 0:
            problem = "the runner exited 0 though a test did not pass"
        cases = kinds.Cases(results, declared, problem, renderer, None if device else source)
        return passes and problem is None, "", cases
    return fn


ENV_EXIT = 3  # luma_test.py ENV_EXIT: the runner could not start because the machine was not fit


def start_soak(g):
    """The soak (tools/gate/soak.py, file 00 section 4.2): two hours of random input on each renderer, in processes of
    its own, started so it runs beside the Android part. Returns the process and its log (soak-run.log: the check's own
    log is soak.log, N-1065)."""
    log = g.work / "soak-run.log"
    out = log.open("wb")
    process = subprocess.Popen([sys.executable, "-X", "utf8", "-I", str(HERE / "soak.py"), "run", "--bin", str(g.bin),
                                "--pak", getattr(g, "pak", "") or g.args.pak, "--work", str(g.work / "soak"), "--minutes", str(g.args.soak_minutes),
                                "--who", "gate"], stdout=out, stderr=subprocess.STDOUT, cwd=g.args.build_root)
    out.close()
    return process, log


MEASURE = TREE / "tools" / "measure" / "measure.py"
# Where a move's measure runs from: a copy of the moved commit's build, so a later gate's build of the same folder
# never changes what is measured.
MEASURE_BINS = pathlib.Path("D:/NebulaWork/build/measure-bins")


def measure_command(sha, bin_dir, android_bin, apk, work, args, pak=None, paks=None):
    """tools/measure/measure.py run: the play route on the PC's renderers, the PC held to four cores, and the tablet
    when the gate has one (N-1043, 07-BASE-FIRST stage 1 item 1). The tablet's run installs the test app `apk` with
    the gate's pak and removes it after, inside the tablet's lock (P-321)."""
    tablet = bool(args.tablet and android_bin and apk)
    runs = "d3d12,vulkan,d3d11,d3d12-4cores" + (",tablet" if tablet else "")
    cmd = [sys.executable, "-X", "utf8", "-I", str(MEASURE), "run", "--bin", str(bin_dir), "--work", str(work),
           "--commit", sha, "--runs", runs, "--history", str(pathlib.Path(args.work) / "measure-history.tsv"),
           "--paks", args.paks if paks is None else paks, "--who", args.who]
    if tablet:
        cmd += ["--device", args.tablet, "--android-bin", str(android_bin), "--apk", str(apk), "--pak", pak or args.pak,
                "--device-lock", args.tablet_lock]
    return cmd


def test_apk(args):
    """The gate's build of the test app (build/android, platform/android/build_apk.ps1 -Debuggable -Harness)."""
    return pathlib.Path(args.build_root) / "android" / "Nebula.apk"


def measure_route(g):
    """The play route measure in the long run (the part holds the gate lock): one line per run in
    gate-runs/measure-history.tsv, compared with the last ten of its device and renderer."""
    def fn(log):
        apk = test_apk(g.args)
        code = run(measure_command(g.commit, g.bin, g.android_bin, apk if apk.is_file() else None, g.work / "measure",
                                   g.args, g.pak, g.paks_text()), log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        said = next((l for l in reversed(text.splitlines()) if l.startswith("measure: ")), "measure: no summary")
        return code == 0, said[len("measure: "):]
    return fn


def copies_in_use(bins, measure_lock, alive=None):
    """The names of the measure's build copies a measure uses (N-1090): a copy whose in-use.txt names a process that is alive
    (the measure started on it, `pid N`), and the commit measure.lock's holder line names (`measure.py <commit> <work>`:
    the measure that has the lock now). A move prunes none of these."""
    alive = alive or process_alive
    used = set()
    for d in (p for p in pathlib.Path(bins).iterdir() if p.is_dir()):
        try:
            words = (d / "in-use.txt").read_text(encoding="utf-8").split()
        except OSError:
            continue
        if len(words) >= 2 and words[0] == "pid" and words[1].isdigit() and alive(int(words[1])):
            used.add(d.name)
    try:
        words = (pathlib.Path(measure_lock) / "holder.txt").read_text(encoding="utf-8").split()
    except OSError:
        words = []
    if len(words) >= 2 and words[0] == "measure.py":
        used.add(words[1])
    return used


def start_measure(sha, g, args, popen=subprocess.Popen, bins=MEASURE_BINS, alive=None):
    """After a move: the moved commit's build copied (MEASURE_BINS/<sha>, the two before it kept, and any a measure uses:
    copies_in_use), then the measure started there, detached; it takes the gate lock through the queue, so the gates and long
    run parts behind it wait and nothing runs beside it. The copy names the process of its measure (in-use.txt). Returns what
    happened, for the board."""
    bins.mkdir(parents=True, exist_ok=True)
    used = copies_in_use(bins, pathlib.Path(args.gate_lock).with_name("measure.lock"), alive)
    for old in sorted((p for p in bins.iterdir() if p.is_dir()), key=lambda p: p.stat().st_mtime)[:-2]:
        if old.name not in used:
            shutil.rmtree(old, ignore_errors=True)
    copy = bins / sha[:9]
    try:
        shutil.rmtree(copy, ignore_errors=True)
        shutil.copytree(g.bin, copy / "bin")
        android = apk = None
        if args.tablet and pathlib.Path(g.android_bin).is_dir() and test_apk(args).is_file():
            # The test app with its bins: the tablet's run installs it (P-321), as the gate's tablet part did.
            android = copy / "android"
            shutil.copytree(g.android_bin, android)
            apk = copy / "Nebula.apk"
            shutil.copy2(test_apk(args), apk)
    except OSError as e:
        return "no measure: the build could not be copied: %s" % e
    cmd = measure_command(sha, copy / "bin", android, apk, pathlib.Path(args.work) / ("%s-measure" % g.name), args, getattr(g, "pak", "") or None,
                          g.paks_text() if hasattr(g, "paks_text") else None)
    cmd += ["--gate-lock", args.gate_lock, "--board", args.board]
    log = pathlib.Path(args.work) / ("measure-%s.log" % sha[:9])
    # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, and out of the run's job object (machine.start_outliving): the measure outlives the run.
    detach = {"creationflags": 0x8 | 0x200} if os.name == "nt" else {"start_new_session": True}
    with open(log, "ab") as out:
        process = machine.start_outliving(popen, cmd, cwd=str(TREE), stdin=subprocess.DEVNULL, stdout=out,
                                          stderr=subprocess.STDOUT, close_fds=True, **detach)
    pid = getattr(process, "pid", None)
    if isinstance(pid, int):
        (copy / "in-use.txt").write_text("pid %d\n" % pid, encoding="utf-8")
    return "the play route measure of main %s started (log %s)" % (sha[:9], log)


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
        ok, lines = step_hash.check(work, other, some=g.level in SHORT_LEVELS)
        pathlib.Path(log).write_text("\n".join(lines) + "\n", encoding="utf-8")
        # Not one run compared is no proof: tool. A step hash that differs is a fail.
        return ok, lines[-1], None, "tool" if lines[-1].startswith("0 of 0 the same") else "fail"
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
    """luma_test_android.py prepare (install the test app, copy the pak in) or remove, on the gate's tablet."""
    def fn(log):
        args = [sys.executable, "-X", "utf8", "-I", TREE / "tools" / "lumacore" / "luma_test_android.py", step,
                "--device", g.args.tablet]
        if step == "prepare":
            args += ["--apk", test_apk(g.args), "--pak", g.pak,
                     "--who", "gate"]
        code = run(args, log, timeout=3600)
        return code == 0, last_line(pathlib.Path(log).read_text(encoding="utf-8", errors="replace"))
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
    """(text, pid) of a held lock folder: its holder.txt, and the process id it names (pid N), None when none."""
    holder = pathlib.Path(lock) / "holder.txt"
    text = holder.read_text(encoding="utf-8", errors="replace").strip() if holder.exists() else "unknown"
    words = text.split()
    pid = next((int(words[i + 1]) for i, w in enumerate(words[:-1]) if w == "pid" and words[i + 1].isdigit()), None)
    return text, pid


def board_line(board, text):
    if board:
        with open(board, "a", encoding="utf-8") as f:
            f.write("%s %s\n" % (datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), text))


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
                   pid=None, clock=time.time):
    """Takes the gate lock for `holder` (a line naming the run, with "pid N"): None when taken, else why not. Waits
    up to wait_minutes while another holds it; a holder whose process has ended loses it, and the board says so.
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
                    text, holder_pid = lock_holder(lock)
                    if holder_pid is not None and not alive(holder_pid):
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


def take_device_lock(path, who):
    """The device's lock (tools/lumacore/luma_test_android.py's): None when taken, else who holds it."""
    lock = pathlib.Path(path)
    try:
        lock.mkdir()
    except FileExistsError:
        holder = lock / "holder.txt"
        return holder.read_text(encoding="utf-8", errors="replace").strip() if holder.exists() else "unknown"
    (lock / "holder.txt").write_text("%s %s\n" % (who, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")),
                                     encoding="utf-8")
    return None


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
    if g.level not in SHORT_LEVELS:
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
    env."""
    def fn(log):
        entries = picture_tool.gather(g.work, PICTURE_STORE)
        path = pathlib.Path(g.tree) / picture_tool.CHANGES_NAME
        try:
            unused = picture_tool.unused_declarations(path, entries)
        except picture_tool.DeclarationError as error:
            return False, str(error), None, "tool"
        if not unused:
            return True, "every declared change names a differing picture" if path.is_file() else "no declared change"
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
    sha, why = picture_tool.approve_landing(g.tree, g.work, PICTURE_STORE, lambda: picture_recheck(g), GIT, say=say)
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
        first = ""
        if finding.log is not None:
            first = next((l.strip() for l in finding.log.read_text(encoding="utf-8").splitlines() if l.strip()), "")
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
            shut = [l for l in r.stdout.splitlines() if l.startswith("    ")]
            return "the console census refuses it (a closed group is not zero): %s" % "; ".join(shut[:5]).strip()
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


def move_main(sha, run_name, work, board, who, covers, tree=TREE):
    """Fast-forwards origin's main to sha (section 5.5): main must still be its ancestor. The push carries a one-time
    allowance the pre-push hook's guard (tools/gate/main_push_guard.py) reads. None when moved, else why not."""
    def tree_git(*args, env=None):
        return subprocess.run([GIT, "-C", str(tree), *args], capture_output=True, text=True, env=env)

    tree_git("fetch", "origin", "main")
    before = tree_git("rev-parse", "origin/main").stdout.strip()
    if tree_git("merge-base", "--is-ancestor", sha, before).returncode == 0:
        # A later candidate that held this one moved main first (section 5.8): the move is done.
        board_line(board, "%s MAIN-MOVED: main already holds %s (main is %s); gate %s." % (who, sha[:9], before[:9], run_name))
        return None
    if tree_git("merge-base", "--is-ancestor", before, sha).returncode != 0:
        return "main moved during the gate (now %s): rebase on it and gate again" % before[:9]
    allowed = pathlib.Path(work) / "main-allowed.txt"
    allowed.write_text("%s %s\n" % (sha, run_name), encoding="utf-8")
    try:
        r = tree_git("push", "origin", "%s:refs/heads/main" % sha, env=dict(os.environ, **{ALLOWED_ENV: str(allowed)}))
    finally:
        allowed.unlink(missing_ok=True)
    if r.returncode != 0:
        return "the push of main failed: %s" % (r.stderr or r.stdout).strip()[-300:]
    tree_git("fetch", "origin", "main")
    count = tree_git("rev-list", "--count", "%s..%s" % (before, sha)).stdout.strip()
    board_line(board, "%s MAIN-MOVED: main %s -> %s (%s commits)%s; gate %s." % (
        who, before[:9], sha[:9], count, (": " + covers) if covers else "", run_name))
    return None


def long_due(work, hours=6.0, now=None):
    """Whether a long run is due (section 14): none in history.tsv and none started by a move (long-started.txt, the
    run's history line comes only at its end) within `hours`."""
    path = pathlib.Path(work) / "history.tsv"
    now = now or datetime.datetime.now()
    rows = [l.split("\t") for l in path.read_text(encoding="utf-8").splitlines()[1:]] if path.exists() else []
    started = [datetime.datetime.fromisoformat(r[0]) for r in rows if len(r) > 2 and r[2] == "long"]
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
    _, pid = lock_holder(lock)
    return pid is not None and not alive(pid)


def long_command(sha, args, tree=LONG_TREE):
    """The long run a move starts: gate.py --long of the long run's tree, with the mover's folders, locks and tablet."""
    cmd = [sys.executable, "-X", "utf8", "-I", str(pathlib.Path(tree) / "tools" / "gate" / "gate.py"), "--long",
           "--who", args.who, "--covers", "main %s, started by its move (section 14)" % sha[:9], "--work", args.work,
           "--board", args.board, "--status", args.status, "--gate-lock", args.gate_lock, "--long-lock", args.long_lock,
           "--tablet-lock", args.tablet_lock, "--discs", args.discs, "--jobs", args.jobs]
    cmd += ["--pak", args.pak] if args.pak else []
    cmd += ["--paks", args.paks] if args.paks else []
    return cmd + (["--tablet", args.tablet] if args.tablet else [])


def start_long(sha, args, tree=LONG_TREE, source=TREE, popen=subprocess.Popen, now=None):
    """After a move: when a long run is due and the long lock is free, brings the long run's tree to `sha` and starts
    the run there, detached and below normal priority (it lowers itself too, below_normal), its output in
    <work>/long-<sha>.log. Returns (command or None, what happened) for the board."""
    if not long_due(args.work, now=now):
        return None, "a long run started within 6 hours"
    if not long_lock_free(args.long_lock):
        return None, "the long lock is held (%s)" % lock_holder(args.long_lock)[0]
    why = long_tree_at(sha, tree, source)
    if why is not None:
        return None, "its tree %s could not be brought to %s: %s" % (tree, sha[:9], why)
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
        "%s %s\n" % ((now or datetime.datetime.now()).isoformat(timespec="seconds"), sha), encoding="utf-8")
    return cmd, "the long run of main %s started in %s (log %s)" % (sha[:9], tree, log)


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


def tablet_part(g, stops, device=None):
    """The tablet's part: the test app installed with the pak, tests/android, a tour of `stops` (None: all), the
    test app removed; then the PC plays the same stops at the tablet's screen size and both must take the same game
    steps at every step (the game runs the actors its picture shows: its clipping follows the picture's shape). When
    the tablet does not answer, its lock stays held, or it leaves during the run, the part is owed (section 4.3)."""
    built = ("build/android",)
    needs = {"tablet/prepare": built + ("data/RMGE01", "tablet/remove"), "android/tablet": built + ("tablet/prepare",),
             "tour/tablet": built + ("tablet/prepare",), "tablet/remove": (), "tour/tablet-pc": DATA,
             "replays/tablet": ("tour/tablet", "tour/tablet-pc"), "pictures/tablet": ("tour/tablet",)}
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
    screen = device_screen(g.work / "runs-tour-tablet-vulkan")
    if screen is None:
        g.missing("tour/tablet-pc", "the tablet's runs name no screen size")
        g.missing("replays/tablet", "the tablet's runs name no screen size")
        return
    pc_tour = gate_tour.write(g.work / "tour-tablet-pc", stops, window=screen)
    step_hash.with_hash(pc_tour.parent)
    g.check("tour/tablet-pc", harness(g, pc_tour, "d3d12", "tour-tablet-pc"))
    g.check("replays/tablet", replays(g, "tour-tablet", "vulkan", ("tour-tablet-pc", "d3d12")))


# The machine (tools/gate/machine.py, package T5) ----------------------------------------------------------------------------

# The checks of a run that start the game (on this computer or the tablet), and those that never do: a run whose checks are all of
# the second kind (documents, tools, builds) asks nothing of the machine. A test pins that every check of every level is in one.
GAME_CHECKS = ("start/", "engine/", "tour", "mods", "soak", "coverage", "android/", "tablet/", "measure/")
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
        return max(renderers + regions, tablet, 1)
    mods = renderers * g.args.mod_parallel if mods_on(g) else 0
    return renderers + regions + mods + tablet + (2 if level == "final" else 0)


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
            notes.append("the test app is not on the tablet: the run installs it and pushes the data")

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


def cpu_lane(g, discs_proved):
    """The checks that start no game (Rust, LumaLink, the collision proof, the harness's and the descriptions' own
    tests): a lane beside the game lanes at below-normal priority (N-1070). One after another they held the quick gate
    for 500 s before any game ran."""
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
            g.check("harness/self", scripts(g, HARNESS_SELF, ("--work", "{out}")))
            commands = pathlib.Path(g.args.build_root) / "game" / "compile_commands.json"
            g.check("description/self", scripts(g, DESCRIPTION_SELF, ("--commands", str(commands), "--out", "{out}")),
                    ("build/windows",))
        finally:
            LANE.below_normal = False
    lane.copies = 0      # it starts no game
    return lane


def engine_lane(g, r):
    """The engine's own tests (tests/engine) on one renderer: start, device loss, window shapes and sizes, stage
    loads, save files."""
    def lane():
        g.check("engine/%s" % r, harness(g, TREE / "tests" / "engine" / "suite.json", r, "engine"), DATA)
        g.picture_jobs.append(("pictures/engine/%s" % r, "engine", r, ("engine/%s" % r,)))
    return lane


def tour_lane(g, tour, label, r, needs=DATA):
    """One tour on one renderer, with its pictures."""
    def lane():
        g.check("%s/%s" % (label, r), harness(g, tour, r, label), needs)
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


def write_tours(g, stops, region_stops):
    """The tour and the other discs' tours: {label: suite.json}."""
    tours = {"tour": gate_tour.write(g.work / "tour", stops)}
    step_hash.with_hash(tours["tour"].parent)
    for disc in REGION_DISCS:
        tours["tour-%s" % disc] = gate_tour.write(g.work / ("tour-%s" % disc), region_stops, disc)
    return tours


def replay_part(g, renderers):
    """The game's steps: Direct3D 12's against the recordings; another renderer's against Direct3D 12's."""
    for r in renderers:
        g.check("replays/%s" % r, replays(g, "tour", r, None if r == "d3d12" else ("tour", "d3d12")),
                ("tour/%s" % r,) + (() if r == "d3d12" else ("tour/d3d12",)))


def android_lane(g, stops):
    """The Android build, then the tablet's part (its stops: the twelve quick ones at the main level, all after)."""
    def lane():
        g.android_built = g.args.no_build or g.check("build/android", build_android(g)) is not False
        tablet_part(g, stops)
    lane.part = "tablet"
    return lane


def sanitizer_lane(g):
    def lane():
        sanitizer_part(g)
    lane.part = "sanitizer"
    return lane


def sanitizer_part(g):
    """The game modules built with AddressSanitizer (build_windows.ps1 -Sanitize address, a build root of its own)
    play the tour and every mod test on Direct3D 12: a memory fault either reaches ends the run with the sanitizer's
    report. The mod tests reach the game's code from mods (calls, hooks, events) that the tour never does (N-1041)."""
    root = pathlib.Path(g.args.build_root) / "asan"
    asan_bin = str(root / "bins")
    mods = mods_on(g)
    if g.check("build/windows-asan", build_windows(g, root=root, bin_dir=asan_bin, sanitize="address")) is False:
        g.missing("tour-asan/d3d12", "the sanitizer build failed", ("build/windows-asan",))
        if mods:
            g.missing("mods-asan/d3d12", "the sanitizer build failed", ("build/windows-asan",))
        return
    tour = gate_tour.write(g.work / "tour-asan", None)
    step_hash.with_hash(tour.parent)
    g.check("tour-asan/d3d12", harness(g, tour, "d3d12", "tour-asan", bin_dir=asan_bin), ("build/windows-asan", "data/RMGE01"))
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


def coverage_check(g, bin_dir, tour):
    """The counting runs, then the report. The runs' own verdicts are the other checks' business (the same tests run
    there on the players' build); here they say only what ran."""
    def fn(log):
        out = g.work / "coverage"
        counts = out / "counts"
        shutil.rmtree(counts, ignore_errors=True)
        counts.mkdir(parents=True)
        # %m: one file per module (Nebula.exe, smg_game, smg_game_plain), %p: per process.
        env = {"LLVM_PROFILE_FILE": str(counts / "counts-%m-%p.profraw")}
        runs = [("engine", harness(g, TREE / "tests" / "engine" / "suite.json", "d3d12", "engine-coverage",
                                   bin_dir=bin_dir, env=env)),
                ("tour", harness(g, tour, "d3d12", "tour-coverage", bin_dir=bin_dir, env=env))]
        if mods_on(g):
            runs.append(("mods", mod_tests(g, "d3d12", bin_dir=bin_dir, label="mods-coverage", more_env=env)))
        said = []
        for name, part in runs:
            said.append("%s %s" % (name, part(out / ("%s.log" % name))[1]))
        with open(log, "a", encoding="utf-8") as f:
            f.write("runs: %s\n" % "; ".join(said))
        raws = sorted(counts.glob("*.profraw"))
        if not raws:
            return False, "no process wrote counts; runs: " + "; ".join(said)
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
                    "--commit", g.commit, "--llvm", llvm], log)
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
    holder = "long run part %s of %s pid %d" % (name, TREE.name, os.getpid())
    held = take_gate_lock(g.args.gate_lock, holder, g.args.wait, g.args.board, g.args.who)
    if held is not None:
        g.record("part/" + name, "env", 0.0, "not run: " + held)
        return None
    try:
        return fn()
    finally:
        free_gate_lock(g.args.gate_lock, holder, g.args.board, g.args.who, "part %s done" % name)


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
    tours = write_tours(g, QUICK_STOPS if short else None, QUICK_REGION_STOPS if short else None)
    # Region discs: Direct3D 12 at the quick and main levels; every renderer in the long run and the final check.
    region_renderers = RENDERERS if level in ("long", "final") else ("d3d12",)
    # The CPU-only checks first in the list, so their rows read where they always did.
    lanes = [cpu_lane(g, ("RMGE01",) if short else ("RMGE01",) + REGION_DISCS)] if level != "long" else []
    for r in renderers:
        def renderer_lane(r=r):
            if level != "long":
                engine_lane(g, r)()
            tour_lane(g, tours["tour"], "tour", r)()
        lanes.append(renderer_lane)
    for disc in REGION_DISCS if level != LAND_LEVEL else ():
        def region_lane(disc=disc):
            for r in region_renderers:
                tour_lane(g, tours["tour-%s" % disc], "tour-%s" % disc, r, DATA + ("data/%s" % disc,))()
        lanes.append(region_lane)
    if level != "long" and mods_on(g):
        lanes += [mods_lane(g, r) for r in renderers]
    if not quick:
        # The main level's tablet part is the twelve quick stops; the long run's (and the final check's) all of them.
        lanes.append(android_lane(g, QUICK_STOPS if level in ("main", LAND_LEVEL) else None))
    if level in ("long", "final"):
        lanes += [sanitizer_lane(g), coverage_lane(g)]
    if level == "long":
        # Part by part under the gate lock: the tours of every renderer and disc side by side, then the tablet, then
        # the sanitizer build's runs, then the coverage build's. The soak, started above in processes of its own at
        # below-normal priority, is the only work that runs beside another gate.
        apart = {n: [l for l in lanes if getattr(l, "part", "") == n] for n in ("tablet", "sanitizer", "coverage")}
        tour_lanes = [l for l in lanes if not getattr(l, "part", "")]
        for name, part in (("tours", tour_lanes),) + tuple(apart.items()):
            if part:
                locked_part(g, name, lambda part=part: g.lanes(part))
    else:
        g.lanes(lanes)
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
    if level in ("long", "final"):
        # Last, with the machine alone: the gate lock keeps gates out, measure.py takes measure.lock.
        locked_part(g, "measure", lambda: g.check("measure/route", measure_route(g), DATA))
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
    """Why `--also` cannot be run ("" when it can): a name must be a check of the main level, which is how a landing runs it. A
    name that only the long run has is said so; any other is unknown, and the known names are printed."""
    main_needs = level_needs(args, "main", known, planned)
    unknown = [n for n in also if n not in main_needs]
    if not unknown:
        return ""
    long_only = [n for n in unknown if n in level_needs(args, "long", known, planned)]
    other = [n for n in unknown if n not in long_only]
    parts = []
    if other:
        parts.append("--also names no check the gate knows: %s. The checks a landing can run are: %s" % (
            ", ".join(other), ", ".join(sorted(main_needs))))
    if long_only:
        parts.append("--also: %s only the long run (gate.py --long) has; a landing cannot run %s" % (
            ", ".join(long_only), "it" if len(long_only) == 1 else "them"))
    return " ".join(parts)


def landing_selection(args, plan, also, known, planned):
    """The checks of a landing with a plan that is not full: those of the plan's groups, and those `also` names, each with what
    it needs, from a dry run of the level the landing has (args.mode: the main level when there is an --also, else the landing's
    own)."""
    dry = run_checks(args, known, planned, dry=True)
    shutil.rmtree(dry.work, ignore_errors=True)
    selected = scope.select(dry.needs, plan.groups)
    if also:
        selected |= select(dry.needs, also)
    return selected


def parser(land=False):
    """The options of a run. `land`: for `gate.py land`, whose options are a run's besides the branches (no level flag)."""
    p = argparse.ArgumentParser(prog="gate.py land" if land else None, description=__doc__.split("\n")[0])
    if land:
        p.add_argument("branches", nargs="*", help="the branches to land (put in the queue, in this order)")
        p.add_argument("--queue", action="store_true", help="run what waits in the queue and take no branch")
        p.add_argument("--agent", default=os.environ.get("NEBULA_AGENT", "1"), help="who asks, for the queue and the fault record")
        p.add_argument("--queue-file", default=str(landing.QUEUE))
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
    if not land:
        p.add_argument("--live", default=os.environ.get("NEBULA_LIVE", "D:/NebulaWork/plan/native"),
                       help="the folder of tool-faults.tsv (the step tool's live folder)")
        p.add_argument("--agent", default=os.environ.get("NEBULA_AGENT", "1"), help="who asks, for the fault record")
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


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    # The run's job object (machine.py): every program the gate starts, and theirs, ends with it, however it ends. Once a process.
    if machine.RUN_JOB[0] is None:
        machine.join_new_job(lambda text: print(text, file=sys.stderr, flush=True))
    if argv and argv[0] == "land":
        return land_main(argv[1:])
    if argv and argv[0] == "data":
        return gamedata.main(argv[1:])
    args = parser().parse_args(argv)
    patterns = [s.strip() for s in args.only.split(",") if s.strip()]

    def refuse(text):
        print("gate.py: " + text, file=sys.stderr)
        print(kinds.verdict_line("not-judged"), flush=True)
        return 2

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
        if not plan.full and not also:
            args.mode = LAND_LEVEL
        elif also and not plan.full:
            print("gate.py: also: %s. The landing keeps the main level so that they run as the nightly run runs them (every stop of "
                  "a tour)." % ", ".join(also), flush=True)
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
    # The GitHub part (section 8, package T8a): on for a plan that has the group `github`, and for a run with no plan; a plan
    # without it pushes no candidate, starts no watch and waits for nothing from GitHub.
    github = plan is None or plan.full or "github" in plan.groups
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
    holder = "%s gate of %s@%s pid %d%s%s%s" % (args.mode, TREE.name, commit[:9], os.getpid(),
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
        return refuse("main could not move to %s: %s" % (commit[:9], why))
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
        owed = [r["check"] for r in g.results if r["state"] == "owed"]
        if owed and args.board:
            board_line(args.board, "%s OWED: the tablet's part of %s gate %s on %s: %s. It runs on main's newest "
                       "commit first when the tablet is back (gate.py --main --only \"%s\")." % (
                           args.who, args.mode, g.name, commit[:9], ", ".join(owed), ",".join(owed)))
        if args.move and code == 0 and not (watch is not None and watch.red):
            # The machine is free for the next gate from here (section 5.5); this process waits for GitHub alone, when the
            # plan has a GitHub part, and moves main once every `must` job is green for the commit.
            for lock in taken:
                free_gate_lock(lock, holder, args.board, args.who, ("its checks passed; it waits for GitHub's jobs on %s"
                                                                    % candidate) if rule is not None else
                               "its checks passed; the plan has no GitHub part")
            taken = []
            if rule is not None:
                add_waiting(args.work, commit, candidate, os.getpid())
            landed = commit  # the commit main moves to: the tested one, or on top of it the pictures' approval
            why, detail = None, "the plan has no GitHub part"
            try:
                if rule is not None:
                    kind, detail, reading = rule.wait(args.github_wait, sleep=GITHUB_SLEEP)
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
                if rule is not None:
                    drop_waiting(args.work, commit)
            if candidate is not None:
                delete_candidate(candidate)
            if why is not None:
                code, summary = (2 if code == 2 else 1), "passed, main not moved: " + why + ("; card shared" if g.card_shared else "")
                print("gate.py: main not moved: " + why, file=sys.stderr)
                moved_line = "NOT-MOVED %s %s" % (not_moved_kind(why), why)
                board_line(args.board, "%s NOTICE: main gate %s on %s passed its checks; main not moved: %s." % (
                    args.who, g.name, commit[:9], why))
            else:
                moved_line = "MOVED %s" % landed
                summary = "passed, %s, main moved to %s" % (("GitHub green (%s)" % detail) if rule is not None else
                                                              "no GitHub part in the plan", landed[:9]) + (
                                                                  "; card shared" if g.card_shared else "")
                # The build that moved main is kept (the two newest): a test that fails in a later change is run on it.
                kept = kinds.keep_base(git("rev-parse", "HEAD"), g.bin, BASE_BINS)
                if kept is not None:
                    print("gate.py: " + kept)
                    board_line(args.board, "%s NOTICE: %s." % (args.who, kept))
                # First the measure (it queues for the gate lock now), then the long run, whose parts queue behind it.
                measured = start_measure(landed, g, args)
                print("gate.py: measure: %s" % measured)
                board_line(args.board, "%s MEASURE: %s." % (args.who, measured))
                started, what = start_long(landed, args)
                print("gate.py: long run: %s" % what)
                if started is not None:
                    board_line(args.board, "%s LONG: %s; none had started in 6 hours (section 14, N-1046)." % (args.who, what))
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


def not_moved_kind(why):
    """Why a pass did not move main, for gate.py land: main moved during the run (nothing was judged), a red job of GitHub
    (the batch fails), or another reason (a lock, the pictures' approval: not judged)."""
    if "main moved during the gate" in why:
        return "main-moved"
    if why.startswith("GitHub's jobs on "):
        return "github"
    if why.startswith("GitHub "):
        return "env"      # GitHub did not finish or could not run a job: nothing was judged, the branch waits
    return "other"


# gate.py land -------------------------------------------------------------------------------------------------------------


def parse_run_output(lines, code):
    """The landing.Result of one run of `gate.py --main --move --scope ...` from its output lines and exit code: its KIND
    lines, `MOVED <sha>` when main moved, `NOT-MOVED <kind> <why>` when the checks passed and main did not move, the run's
    folder from its `result:` line, and VERDICT. A run that wrote no VERDICT or exited with another code than 0, 1, 2 is not
    judged (the tool broke)."""
    kind_lines, moved, not_moved, folder, verdict, why = [], "", None, "", None, ""
    for raw in lines:
        line = raw.rstrip("\r\n")
        if line.startswith("KIND "):
            kind_lines.append(line)
        elif line.startswith("MOVED "):
            moved = line.split()[1]
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
    return landing.Result(verdict, kind_lines, failures_, folder, moved, why if verdict == "not-judged" else "")


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
               "--wait", str(a.wait), "--github-wait", str(a.github_wait)]
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
        env = dict(os.environ, PYTHONIOENCODING="utf-8")
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
    def write(agent, step, kind, check, test, message, run_folder):
        sys.path.insert(0, str(TREE / "tools" / "plan"))
        import live  # noqa: E402 - tools/plan, beside the step tool that shows the table
        pathlib.Path(live_folder).mkdir(parents=True, exist_ok=True)
        live.append_fault(live_folder, agent, step, kind, check, message, test=test, run_folder=run_folder)
    return write


def land_main(argv):
    """gate.py land BRANCH [BRANCH ...] | gate.py land --queue: tools/gate/landing.py's queue and batches."""
    args = parser(land=True).parse_args(argv)
    args.mode = "main"
    args.move = True
    if bool(args.branches) == bool(args.queue):
        print("gate.py: land takes branches to put in the queue, or --queue to run what waits: one of the two", file=sys.stderr)
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
    sys.path.insert(0, str(TREE / "tools" / "plan"))
    import failures  # noqa: E402 - tools/plan/failures.py: the step tool's rule, in one place
    queue = landing.Queue(args.queue_file)
    runner = landing.Landing(queue, GateChecker(args), landing.Repo(GIT), source=TREE, tree=args.landing_tree,
                             store=failures.Store(args.failures), faults=fault_writer(args.live))
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
