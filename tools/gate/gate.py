"""gate.py: the one check of a Nebula tree, at the four levels of docs/plan/04-GATE-AND-MAIN.md. Every check that
starts the game for a test goes through here.

    python -X utf8 -I tools/gate/gate.py --quick | --main [--move] | --long | --final  [options]

--quick  level 1: the repository checks, the Windows build, the unit and Rust tests, a start on each renderer, the
         self-tests, the mod tests and the engine tests on Direct3D 12, 12 tour stops with pictures and step hashes,
         two stops of each other disc. Before asking for a main gate.
--main   level 2, what moves main: everything deterministic. Every renderer's engine tests and 102-stop tour with
         pictures and step hashes, the other discs, the mod tests on every renderer, the Android build, the tablet's
         short part (the 12 stops, their step hashes against the PC's at the tablet's screen size) and every job of
         GitHub's Check run of the same commit (Linux, Linux unit tests, both sanitizer jobs, Mac, Mac (Intel), Android,
         Rust-on-Linux and any job added later), planned.tsv's github/<job> lines aside.
         --move: main moves to this commit when it passes (section 5). It needs a whole quick gate of the commit
         passed, the branch pushed and main an ancestor of it; it takes the gate lock (or waits --wait minutes),
         and on a pass fast-forwards main and pushes it itself, writes MAIN-MOVED on the board and frees the lock.
         With --move the commit is pushed first as gate/<run> (section 5): GitHub runs every job on it while the gate
         runs, the gate frees gate.lock when its own checks pass, waits up to 120 minutes for every job of that run to
         be green (planned.tsv's github/<job> lines aside), then moves main and deletes gate/<run>.
--long   level 3, on main's newest commit, under the long lock, at below-normal priority: the tablet's 102 stops
         against the PC's, a sanitizer build's tour, the soak, every disc on every renderer.
--final  level 4: the main and long levels on one commit. "Done" needs it passed with no check planned or known.
--only CHECK[,CHECK]: only the checks those shell patterns name, and the checks they need (for tracing a red main).
--serial: the parts that start the game one after another; by default they run side by side (section 7).

Each check ends in one state:
  pass     it passed;
  fail     it failed, or it exists and could not run;
  known    it failed only in tests tools/gate/known-failures.tsv lists for it (each with an open ledger row); a listed
           test that passes fails the check: its line must go;
  planned  it is not built yet and tools/gate/planned.tsv lists it for this level; a listed check that runs fails;
  owed     the tablet's part, when the tablet does not answer, its lock stays held, or it leaves during the run: the
           part runs on main's newest commit first thing when the tablet is back; the board says so.
The run passes when no check failed.

It writes one result (JSON and text, side by side), one line in <work>/history.tsv and the owner's page
(tools/gate/status.py, --status), and exits 1 when a check failed, 0 when none did, 2 when it could not start (bad
options, a lock held, a rule of section 5 not met).
"""
import argparse
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
import step_hash  # noqa: E402

LEVELS = ("quick", "main", "long", "final")
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
# GitHub's push workflow (.github/workflows/check.yml). A move needs every job of the candidate's run green (section 8),
# waited for outside gate.lock for up to GITHUB_WAIT_MINUTES. A main gate without --move and the final check read
# the commit's newest run as one of their checks, for up to GITHUB_CHECK_MINUTES.
WORKFLOW = "check.yml"
GITHUB_WAIT_MINUTES = 120
GITHUB_CHECK_MINUTES = 15
CANDIDATE_PREFIX = "gate/"
# The two lists (section 4) and the ledger their lines name.
KNOWN = HERE / "known-failures.tsv"
PLANNED = HERE / "planned.tsv"
LEDGER = TREE / "docs" / "plan" / "ledger.tsv"
KNOWN_COLUMNS = ("check", "test", "ledger row", "first seen", "what")
PLANNED_COLUMNS = ("check", "level", "plan step or ledger row", "owner")
CLOSED = ("fixed", "not-a-fault", "decided", "out-of-scope")
MAIN_GATE_SECONDS = 30 * 60


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


def judge(passed, detail, verdicts, known):
    """(state, detail) of a check's outcome: `verdicts` {test: verdict} when the check runs tests (None when it does
    not, or when its runs are not all reported), `known` its known-failures lines {test: row}."""
    if verdicts is None:
        return ("pass" if passed else "fail"), detail
    listed_passing = sorted(t for t in known if verdicts.get(t) == "pass")
    if listed_passing:
        return "fail", detail + "; passes but is listed in known-failures.tsv, remove the line: " + ", ".join(listed_passing)
    if passed:
        return "pass", detail
    unexpected = [t for t, v in verdicts.items() if v != "pass" and t not in known]
    listed_failing = sorted(t for t in known if t in verdicts and verdicts[t] != "pass")
    if not unexpected and listed_failing:
        return "known", detail + "; known: " + ", ".join("%s (%s)" % (t, known[t]["ledger row"]) for t in listed_failing)
    return "fail", detail


def level_has(level, planned_level):
    """Whether a check planned for `planned_level` belongs to a run of `level`."""
    return planned_level == level or (level == "final" and planned_level in ("main", "long", "final"))


# The run ----------------------------------------------------------------------------------------------------------


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

    def rows(self):
        """The rows of the lane this thread runs (lanes), else the run's."""
        sink = getattr(self.local, "sink", None)
        return self.results if sink is None else sink

    def lanes(self, lanes):
        """Runs each lane (a function that runs its checks in order) side by side, each in a thread of its own; the
        checks are programs, so they run at once. Their rows join the results in the lanes' order, so a result reads the
        same however the lanes interleaved. --serial runs them one after another (for the proof that both give the same
        pictures and step hashes, section 7)."""
        if self.dry or self.args.serial or len(lanes) < 2:
            for lane in lanes:
                lane()
            return
        sinks = [[] for _ in lanes]

        def go(i):
            self.local.sink = sinks[i]
            try:
                lanes[i]()
            except Exception as e:  # noqa: BLE001 - a lane that breaks is a failed check, said plainly
                self.record("lane/%d" % i, "fail", 0.0, "%s: %s" % (type(e).__name__, e))
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

    def record(self, name, state, seconds, detail, log=None):
        row = {"check": name, "state": state, "passed": state != "fail", "seconds": seconds, "detail": detail,
               "log": str(log) if log is not None and pathlib.Path(log).exists() else None}
        with self.lock:
            self.rows().append(row)
            print("%-34s %-7s %7.1f s  %s" % (name, state if state != "fail" else "FAIL", seconds, detail), flush=True)

    def check(self, name, fn, needs=()):
        """Runs one check. fn returns (passed, detail) or (passed, detail, {test: verdict}); an exception is a failure
        with its text. True unless it failed; None when --only leaves it out."""
        if not self.wanted(name, needs):
            return None
        if self.dry:
            return True
        t0 = time.time()
        log = self.work / ("%s.log" % name.replace("/", "-").replace(" ", "-"))
        verdicts = None
        try:
            out = fn(log)
            passed, detail = out[0], out[1]
            verdicts = out[2] if len(out) > 2 else None
        except Exception as e:  # noqa: BLE001 - every failure of a check is the check's result
            passed, detail = False, "%s: %s" % (type(e).__name__, e)
        state, detail = judge(bool(passed), detail, verdicts, self.known.get(name, {}))
        if name in self.planned:
            state, detail = "fail", "it runs, so its line in planned.tsv must go; " + detail
        self.record(name, state, round(time.time() - t0, 1), detail, log)
        return state != "fail"

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
        if row["state"] == "fail":
            row["state"], row["passed"] = "owed", True
            row["detail"] = "%s; %s" % (why, row["detail"])
            print("%-34s %-7s (%s)" % (row["check"], "owed", why), flush=True)

    def plan_rows(self):
        """The planned checks of this level that --only keeps."""
        ran = {r["check"] for r in self.results}
        for name, row in sorted(self.planned.items()):
            if level_has(self.level, row["level"]) and self.wanted(name) and not self.dry and name not in ran:
                self.record(name, "planned", 0.0, "not built yet: %s (%s)" % (row["plan step or ledger row"], row["owner"]))

    def counts(self):
        out = {s: 0 for s in ("pass", "known", "planned", "owed", "fail")}
        for r in self.results:
            out[r["state"]] += 1
        return out

    def write(self):
        failed = [r for r in self.results if r["state"] == "fail"]
        counts = self.counts()
        seconds = round(time.time() - self.started, 1)
        out = {"gate": 2, "mode": self.level, "commit": self.commit, "dirty": self.dirty, "run": self.name,
               "only": self.args.only or None,
               "started": datetime.datetime.fromtimestamp(self.started).isoformat(timespec="seconds"),
               "seconds": seconds, "passed": not failed and bool(self.results), "counts": counts,
               "checks": self.results}
        path = pathlib.Path(self.args.out) if self.args.out else self.work / "result.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
        head = "gate %s %s%s%s: %s (%d checks: %s; %.0f s)" % (
            self.level, self.commit, " (uncommitted changes)" if self.dirty else "",
            " only " + self.args.only if self.args.only else "", "PASS" if out["passed"] else "FAIL", len(self.results),
            ", ".join("%d %s" % (n, s if s != "fail" else "failed") for s, n in counts.items() if n), seconds)
        lines = [head] + ["  %-34s %-7s %s" % (r["check"], r["state"], r["detail"]) for r in self.results]
        path.with_suffix(".txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        append_history(pathlib.Path(self.args.work) / "history.tsv", out)
        if out["passed"] and not self.args.keep_runs and not self.args.out:
            # The passed tests' working files are not evidence once the run passed (N-1060): the drive keeps its room.
            import prune_runs
            tests, freed = prune_runs.prune(self.work)
            if tests:
                print("gate.py: %d passed tests' working files removed (%.1f GB); --keep-runs keeps them" % (
                    tests, freed / 1e9))
        if self.level == "main" and not self.args.only and seconds > MAIN_GATE_SECONDS:
            print("gate.py: this main gate took %.0f minutes, over 30: a ledger row for Agent 1 (section 7)" % (seconds / 60))
        if self.args.record:
            # Kept with the tree (docs/plan/gate-runs): what the push workflow cannot run is proved by these.
            keep = TREE / "docs" / "plan" / "gate-runs" / self.name
            keep.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, keep.with_suffix(".json"))
            shutil.copyfile(path.with_suffix(".txt"), keep.with_suffix(".txt"))
            print("recorded:", keep.with_suffix(".txt"))
        print(head)
        print("result:", path)
        return out["passed"]


HISTORY_COLUMNS = ("started", "run", "level", "commit", "result", "seconds", "pass", "known", "planned", "owed",
                   "failed", "only", "checks")


def append_history(path, out):
    """One line per run in gate-runs/history.tsv: when, which, the result and every check's state and seconds."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    new = not path.exists()
    c = out["counts"]
    cells = (out["started"], out["run"], out["mode"], out["commit"] + ("+" if out["dirty"] else ""),
             "pass" if out["passed"] else "fail", "%.0f" % out["seconds"], c["pass"], c["known"], c["planned"],
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
        seed = ["-SeedFrom", g.args.seed_from] if g.args.seed_from else []
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", HERE / "build_windows.ps1",
                    "-Tree", TREE, "-Root", root, "-Bin", bin_dir, "-Pak", g.args.pak, "-Jobs", g.args.jobs, *seed]
                   + (["-Sanitize", sanitize] if sanitize else []) + (["-Coverage"] if coverage else []), log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        fails = [l for l in text.splitlines() if l.startswith("FAILED:")]
        if code != 0 or fails:
            return False, "exit %d, %d FAILED: %s" % (code, len(fails), fails[0][8:120] if fails else last_line(text))
        return True, "bins at %s" % bin_dir
    return fn


def unit_tests(g):
    def fn(log):
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", HERE / "ctest.ps1",
                    "-Dir", pathlib.Path(g.args.build_root) / "msvc"], log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        summary = next((l.strip() for l in text.splitlines() if "tests passed" in l), last_line(text))
        return code == 0, summary
    return fn


def game_tests(g):
    """The game's own unit tests (native/tests), built with the game modules."""
    def fn(log):
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", HERE / "ctest.ps1",
                    "-Dir", pathlib.Path(g.args.build_root) / "game"], log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        summary = next((l.strip() for l in text.splitlines() if "tests passed" in l), last_line(text))
        return code == 0, summary
    return fn


def rust_tests(g):
    def fn(log):
        env = {"CARGO_TARGET_DIR": str(pathlib.Path(g.args.build_root) / "cargo-test")}
        code = run(["cargo", "test", "--release", "--locked", "--offline", "-p", "nebula-import", "-p", "luma-validate"],
                   log, env=env, cwd=TREE)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        passed = sum(int(l.split()[3]) for l in text.splitlines() if l.startswith("test result:"))
        failed = sum(int(l.split()[5]) for l in text.splitlines() if l.startswith("test result:"))
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
        with open(log, "wb") as f:
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
        return not bad and bool(paths), detail + ("; not: " + ", ".join(bad) if bad else "")
    return fn


# The harness's own tests (the runner and the host half, without the game).
HARNESS_SELF = sorted([*(TREE / "tools" / "lumacore").glob("harness_*_test.py"),
                       *(TREE / "tools" / "lumacore" / "tests").glob("harness_*_test.py"),
                       *(TREE / "tools" / "test_driver" / "tests").glob("*_test.py"),
                       *(TREE / "tools" / "gate" / "tests").glob("*_test.py")])
# The game description's tools on their fixtures, with the game build's compile commands.
DESCRIPTION_SELF = sorted((TREE / "tools" / "lumacore").glob("*_fixture_test.py"))


def harness(g, suite, renderer, label, device=None, bin_dir=None, env=None):
    """One harness suite on one renderer, on this computer or on an Android device (its serial; the test app
    prepared and the device's lock held by the gate). Passes when every test in it passed (a test whose disc has no
    pak is not-run, which is not a pass). Its verdicts go with it when every test of the suite reported one, so the
    known-failures list can be applied. `env`: more for the games' environment (the coverage build's counts)."""
    def fn(log):
        work = g.work / ("runs-%s-%s" % (label, renderer))
        on_device = ["--device", device, "--bin", g.android_bin] if device else ["--bin", bin_dir or g.bin]
        code = run([sys.executable, "-X", "utf8", "-I", TREE / "tools/lumacore/luma_test.py", "run", suite]
                   + on_device + ["--work", work, "--who", "gate", "--lock", g.work / ("harness-%s-%s.lock" % (label, renderer)),
                                  "--commit", g.commit, "--renderer", renderer], log, env=dict(env or {}, LUMA_TEST_PAKS=g.args.paks),
                   cwd=g.args.build_root)
        # The run's copies of the programs are not evidence: removed so the drive does not fill.
        for d in work.rglob("bin"):
            if d.is_dir():
                shutil.rmtree(d, ignore_errors=True)
        detail = summarize(log, renderer)
        seen = verdicts(log, renderer)
        tests = suite_tests(suite)
        if tests is not None and len(seen) != tests:
            detail += "; %d of %d tests reported" % (len(seen), tests)
            seen = None
        return code == 0 and not detail.startswith("0 of 0") and "; not:" not in detail, detail, seen
    return fn


def suite_tests(suite):
    """How many tests a suite.json names, or None for one test's folder."""
    suite = pathlib.Path(suite)
    if suite.is_file() and suite.name == "suite.json":
        return len(json.loads(suite.read_text(encoding="utf-8")).get("tests", []))
    return None


def start_soak(g):
    """The soak (tools/gate/soak.py, file 00 section 4.2): two hours of random input on each renderer, in processes of
    its own, started so it runs beside the Android part. Returns the process and its log (soak-run.log: the check's own
    log is soak.log, N-1065)."""
    log = g.work / "soak-run.log"
    out = log.open("wb")
    process = subprocess.Popen([sys.executable, "-X", "utf8", "-I", str(HERE / "soak.py"), "run", "--bin", str(g.bin),
                                "--pak", g.args.pak, "--work", str(g.work / "soak"), "--minutes", str(g.args.soak_minutes),
                                "--who", "gate"], stdout=out, stderr=subprocess.STDOUT, cwd=g.args.build_root)
    out.close()
    return process, log


def measure_short(g):
    """The short measurement set at the long run's end (tools/gate/measure_short.py, section 10, N-1043): one renderer
    a run in turn, the 120 Hz pacing run and the benchmark under measure.lock, against the last ten runs' spread in
    gate-runs/measure-history.tsv."""
    def fn(log):
        code = run([sys.executable, "-X", "utf8", "-I", HERE / "measure_short.py", "run", "--bin", g.bin, "--pak",
                    g.args.pak, "--paks", g.args.paks, "--work", g.work / "measure", "--commit", g.commit, "--history",
                    pathlib.Path(g.args.work) / "measure-history.tsv"], log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        said = next((l for l in reversed(text.splitlines()) if l.startswith("measure: ")), "measure: no summary")
        return code == 0, said[len("measure: "):]
    return fn


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
        ok, lines = step_hash.check(work, other, some=g.level == "quick")
        pathlib.Path(log).write_text("\n".join(lines) + "\n", encoding="utf-8")
        return ok, lines[-1]
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
            args += ["--apk", pathlib.Path(g.args.build_root) / "android" / "Nebula.apk", "--pak", g.args.pak,
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


# Who goes first among waiting gates (Planner, 11-18 03:44): Agents 3 and 4 before Agents 1 and 2 (DECISION 01:15),
# otherwise first come. gate.py keeps the order, not the agents.
FIRST_IN_QUEUE = ("Agent 3", "Agent 4")


def queue_rank(who):
    """0 for an agent whose waiting gate goes first, 1 for the rest."""
    return 0 if any(who.startswith(name) for name in FIRST_IN_QUEUE) else 1


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
    locks = [gate_lock] if mode in ("quick", "main", "final") else []
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
                mine = queue_folder(lock) / ("%d-%d-%d.txt" % (queue_rank(who), int(clock() * 1000), pid))
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


def pictures(g, label, renderer):
    """The pictures a harness suite's runs took against the approved ones (tools/gate/pictures.py, tolerance none).
    Above the quick level it also fails on an approved picture its runs did not take."""
    def fn(log):
        work = g.work / ("runs-%s-%s" % (label, renderer))
        code = run([sys.executable, "-X", "utf8", "-I", TREE / "tools" / "gate" / "pictures.py", "check", "--runs", work,
                    "--renderer", renderer] + (["--complete"] if g.level != "quick" else []), log, cwd=TREE)
        lines = pathlib.Path(log).read_text(encoding="utf-8", errors="replace").splitlines()
        summary = next((l for l in reversed(lines) if l.startswith("pictures: ")), "no summary")
        others = [l.split()[0] + " " + l.split()[1] for l in lines if l.split() and l.split()[0] in ("changed", "new", "missing", "machine")]
        review = next((l for l in lines if l.startswith("review: ")), "")
        detail = summary[len("pictures: "):] + ("; not: " + ", ".join(others[:8]) + (" ..." if len(others) > 8 else "") if others else "")
        return code == 0, detail + ("; " + review if review else "")
    return fn


MOD_TESTS = TREE / "tools" / "lumacore" / "mod_tests.py"


def mod_tests(g, renderer, bin_dir=None, label="mods", more_env=None):
    """The mod tests on one renderer, through the mods' own entry point (it builds the test mods with LumaLink, then
    runs them side by side, --mod-parallel at a time, each game in its own folder): every suite of tests/mods and of
    the pinned LumaLink's tests/game, or at the quick level the quick set (tests/mods/quick.json, one or more of every
    suite). `bin_dir`: another build's bin folder (the sanitizer build's), `label` its runs' name, `more_env` more for
    the games' environment. Passes when it exits 0; its rows name each test that did not pass."""
    def fn(log):
        work = g.work / ("runs-%s-%s" % (label, renderer))
        env = dict(more_env or {}, LUMA_TEST_PAKS=g.args.paks)
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
        if g.level == "quick":
            cmd += ["--set", "quick"]
        code = run(cmd, log, env=env, cwd=g.args.build_root)
        for d in work.rglob("bin"):
            if d.is_dir():
                shutil.rmtree(d, ignore_errors=True)
        return code == 0, summarize(log, renderer)
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


def gh_json(*args):
    r = subprocess.run(["gh", *args], capture_output=True, text=True, cwd=TREE, timeout=120)
    if r.returncode != 0:
        raise RuntimeError("gh %s: %s" % (" ".join(args[:2]), (r.stderr or r.stdout).strip()[-200:]))
    return json.loads(r.stdout or "null")


def run_state(runs):
    """(status, jobs) of the newest of the push workflow's runs given ([{databaseId, status}], newest first)."""
    if not runs:
        return None, []
    run = runs[0]
    return run.get("status"), gh_json("run", "view", str(run["databaseId"]), "--json", "jobs").get("jobs", [])


def candidate_state(branch, sha):
    """(status, jobs) of the newest push run of the candidate branch at its commit."""
    return run_state(gh_json("run", "list", "--branch", branch, "--commit", sha, "--workflow", WORKFLOW, "--event",
                             "push", "--json", "databaseId,status", "--limit", "5") or [])


def commit_state(branch, sha):
    """(status, jobs) of the newest push run on the commit, under any branch name (branch unused)."""
    return run_state(gh_json("run", "list", "--commit", sha, "--workflow", WORKFLOW, "--event", "push", "--json",
                             "databaseId,status", "--limit", "5") or [])


def planned_jobs(planned):
    """The jobs planned.tsv's github/<job name> lines take out of the move's rule (each with its ledger row)."""
    return {name[len("github/"):] for name in planned if name.startswith("github/")}


def red_minute(job):
    """' at HH:MM' (this computer's time) of a job's end, from GitHub's completedAt; '' where it has none."""
    when = job.get("completedAt") or ""
    try:
        moment = datetime.datetime.fromisoformat(when.replace("Z", "+00:00"))
    except ValueError:
        return ""
    if moment.year < 2000:  # GitHub's zero time: not ended
        return ""
    return " at " + moment.astimezone().strftime("%H:%M")


def judge_run(status, jobs, planned_jobs=()):
    """(done, green, detail) of a run (section 8): every job is required except the planned ones. A job that ended
    other than green (failure, cancelled, skipped) ends it at once; otherwise it is done when the run has ended."""
    red, running, green = [], 0, 0
    for j in jobs:
        name = j.get("name") or "?"
        if name in planned_jobs:
            continue
        if j.get("status") != "completed":
            running += 1
        elif j.get("conclusion") == "success":
            green += 1
        else:
            red.append("%s %s%s" % (name, j.get("conclusion") or "?", red_minute(j)))
    if red:
        return True, False, "red: " + "; ".join(red)
    if status is None:
        return False, False, "no run yet"
    if status != "completed" or running:
        return False, False, "%d green, %d not ended" % (green, running)
    if green == 0:
        return True, False, "the run has no job"
    return True, True, "%d of %d jobs green" % (green, green)


def github_wait(branch, sha, planned=(), wait_minutes=GITHUB_WAIT_MINUTES, state_of=candidate_state, sleep=time.sleep,
                clock=time.time, log=None):
    """(green, detail): asks GitHub once a minute for the run of `branch` at `sha` until it is judged, or for at most
    wait_minutes. A question that fails (gh, the network) is asked again the next minute: it is not a red job."""
    deadline = clock() + 60.0 * wait_minutes
    detail = "not asked"
    while True:
        try:
            done, green, detail = judge_run(*state_of(branch, sha), planned_jobs=planned)
        except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as e:
            done, green, detail = False, False, "GitHub did not answer: %s" % e
        if log:
            with open(log, "a", encoding="utf-8") as f:
                f.write("%s %s\n" % (datetime.datetime.now().strftime("%H:%M:%S"), detail))
        if done:
            return green, detail
        if clock() >= deadline:
            return False, detail + " (no result after %g minutes)" % wait_minutes
        sleep(60.0)


# The answers that count as red before a main gate starts (a cancelled run is another push's, not a fault).
RED_CONCLUSIONS = ("failure", "timed_out", "startup_failure")


def commit_runs(sha):
    """[(branch, jobs)] of every push run of the commit on GitHub, under any branch name."""
    runs = gh_json("run", "list", "--commit", sha, "--workflow", WORKFLOW, "--event", "push", "--json",
                   "databaseId,headBranch", "--limit", "20") or []
    return [(r.get("headBranch") or "?", gh_json("run", "view", str(r["databaseId"]), "--json", "jobs").get("jobs", []))
            for r in runs]


def red_on_github(sha, planned=(), runs_of=commit_runs):
    """Why a main gate of `sha` would not move main, already known on GitHub (section 8: read before the checks
    start): a job of any run of the commit that failed, with its branch and the minute it went red. None when none
    did, or when GitHub does not answer (the run then starts, and its own wait reads GitHub again)."""
    try:
        runs = runs_of(sha)
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError):
        return None
    red = ["%s %s on %s%s" % (j.get("name") or "?", j.get("conclusion"), branch, red_minute(j))
           for branch, jobs in runs for j in jobs
           if j.get("status") == "completed" and j.get("conclusion") in RED_CONCLUSIONS
           and (j.get("name") or "?") not in planned]
    return ("red on GitHub: " + "; ".join(red[:4]) + (" ..." if len(red) > 4 else "")) if red else None


class GithubWatch:
    """Reads the candidate's run once a minute while the gate's checks run (section 8); at its first red job calls
    on_red with why (the job and the minute it went red) and stops reading."""

    def __init__(self, branch, sha, planned=(), on_red=None, state_of=candidate_state, every=60.0):
        self.branch, self.sha, self.planned = branch, sha, planned
        self.on_red = on_red or stop_runs
        self.state_of, self.every = state_of, every
        self.red = None
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
                done, green, detail = judge_run(*self.state_of(self.branch, self.sha), planned_jobs=self.planned)
            except (RuntimeError, OSError, ValueError, subprocess.SubprocessError):
                continue  # asked again the next minute
            if done and not green:
                self.red = "GitHub's jobs on %s: %s" % (self.branch, detail)
                self.on_red(self.red)
                return


def github_check(g, wait_minutes=GITHUB_CHECK_MINUTES, state_of=commit_state, sleep=time.sleep, clock=time.time):
    """The commit's newest run, every job (a main gate without --move, the final check): a check of the gate."""
    def fn(log):
        return github_wait("", git("rev-parse", "HEAD"), planned_jobs(g.planned), wait_minutes, state_of, sleep, clock, log)
    return fn


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
           "--tablet-lock", args.tablet_lock, "--pak", args.pak, "--paks", args.paks, "--discs", args.discs,
           "--jobs", args.jobs]
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
    # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | BELOW_NORMAL_PRIORITY_CLASS: the run outlives the mover's gate.
    detach = {"creationflags": 0x8 | 0x200 | 0x4000} if os.name == "nt" else {"start_new_session": True}
    with open(log, "ab") as out:
        popen(cmd, cwd=str(tree), stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT, close_fds=True, **detach)
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
    needs = {"tablet/prepare": built + ("tablet/remove",), "android/tablet": built + ("tablet/prepare",),
             "tour/tablet": built + ("tablet/prepare",), "tablet/remove": (), "tour/tablet-pc": ("build/windows",),
             "replays/tablet": ("tour/tablet", "tour/tablet-pc")}
    wanted = [n for n in TABLET_CHECKS if g.wanted(n, needs[n])]
    if g.dry or not wanted:
        return
    if not g.android_built:
        for n in wanted:
            g.missing(n, "the Android build failed")
        return
    device = device or android_module().Device(g.args.tablet)

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


# The levels ----------------------------------------------------------------------------------------------------------


def build_part(g):
    """The repository checks and the Windows build: False when the build failed (nothing after it can run)."""
    g.check("repo/settings", settings_table(g))
    g.check("repo/audit", repo_audit(g))
    g.check("repo/scripts", parse_scripts(g))
    g.check("repo/code", repo_code(g))
    if g.args.no_build:
        return True
    return g.check("build/windows", build_windows(g)) is not False


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
    return lane


def engine_lane(g, r):
    """The engine's own tests (tests/engine) on one renderer: start, device loss, window shapes and sizes, stage
    loads, save files."""
    def lane():
        g.check("engine/%s" % r, harness(g, TREE / "tests" / "engine" / "suite.json", r, "engine"), ("build/windows",))
        g.check("pictures/engine/%s" % r, pictures(g, "engine", r), ("engine/%s" % r,))
    return lane


def tour_lane(g, tour, label, r):
    """One tour on one renderer, with its pictures."""
    def lane():
        g.check("%s/%s" % (label, r), harness(g, tour, r, label), ("build/windows",))
        g.check("pictures/%s/%s" % (label, r), pictures(g, label, r), ("%s/%s" % (label, r),))
    return lane


def mods_lane(g, r):
    def lane():
        g.check("mods/%s" % r, mod_tests(g, r), ("build/windows",))
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
    mods = MOD_TESTS.exists()
    if g.check("build/windows-asan", build_windows(g, root=root, bin_dir=asan_bin, sanitize="address")) is False:
        g.missing("tour-asan/d3d12", "the sanitizer build failed", ("build/windows-asan",))
        if mods:
            g.missing("mods-asan/d3d12", "the sanitizer build failed", ("build/windows-asan",))
        return
    tour = gate_tour.write(g.work / "tour-asan", None)
    step_hash.with_hash(tour.parent)
    g.check("tour-asan/d3d12", harness(g, tour, "d3d12", "tour-asan", bin_dir=asan_bin), ("build/windows-asan",))
    if mods:
        g.check("mods-asan/d3d12", mod_tests(g, "d3d12", bin_dir=asan_bin, label="mods-asan"), ("build/windows-asan",))


LLVM_BIN = pathlib.Path(os.environ.get("NEBULA_LLVM_BIN", "C:/Program Files/LLVM/bin"))


def coverage_lane(g):
    def lane():
        coverage_part(g)
    lane.part = "coverage"
    return lane


def coverage_part(g):
    """Which game code no test runs (tools/gate/coverage.py, file 04 3.3, N-1042): the game modules built counting the
    lines and functions they run (build_windows.ps1 -Coverage, a build root of its own) play the engine tests, the
    tour and every mod test on Direct3D 12, each game writing its counts; then the list of what none of them ran and
    the share of lines run, which may not fall below the last long run's (gate-runs/coverage-history.tsv)."""
    root = pathlib.Path(g.args.build_root) / "coverage"
    bin_dir = str(root / "bins")
    if g.check("build/windows-coverage", build_windows(g, root=root, bin_dir=bin_dir, coverage=True)) is False:
        g.missing("coverage", "the coverage build failed", ("build/windows-coverage",))
        return
    tour = gate_tour.write(g.work / "tour-coverage", None)
    step_hash.with_hash(tour.parent)
    g.check("coverage", coverage_check(g, bin_dir, tour), ("build/windows-coverage",))


def coverage_check(g, bin_dir, tour):
    """The counting runs, then the report. The runs' own verdicts are the other checks' business (the same tests run
    there on the players' build); here they say only what ran."""
    def fn(log):
        out = g.work / "coverage"
        counts = out / "counts"
        shutil.rmtree(counts, ignore_errors=True)
        counts.mkdir(parents=True)
        # %m: one file per module (smg_game, smg_game_plain), %p: per process.
        env = {"LLVM_PROFILE_FILE": str(counts / "game-%m-%p.profraw")}
        runs = [("engine", harness(g, TREE / "tests" / "engine" / "suite.json", "d3d12", "engine-coverage",
                                   bin_dir=bin_dir, env=env)),
                ("tour", harness(g, tour, "d3d12", "tour-coverage", bin_dir=bin_dir, env=env))]
        if MOD_TESTS.exists():
            runs.append(("mods", mod_tests(g, "d3d12", bin_dir=bin_dir, label="mods-coverage", more_env=env)))
        said = []
        for name, part in runs:
            said.append("%s %s" % (name, part(out / ("%s.log" % name))[1]))
        with open(log, "a", encoding="utf-8") as f:
            f.write("runs: %s\n" % "; ".join(said))
        raws = sorted(counts.glob("*.profraw"))
        if not raws:
            return False, "no game wrote counts; runs: " + "; ".join(said)
        llvm = LLVM_BIN
        merged = out / "game.profdata"
        if run([llvm / "llvm-profdata.exe", "merge", "-o", merged] + raws, log) != 0:
            return False, "llvm-profdata merge failed"
        modules = [pathlib.Path(bin_dir) / m for m in ("smg_game.dll", "smg_game_plain.dll")
                   if (pathlib.Path(bin_dir) / m).exists()]
        binaries = [a for m in modules for a in ("--binary", m)]
        code = run([sys.executable, "-X", "utf8", "-I", HERE / "coverage.py", "report", *binaries, "--profile", merged,
                    "--work", out, "--root", TREE, "--history", pathlib.Path(g.args.work) / "coverage-history.tsv",
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
        g.record("part/" + name, "fail", 0.0, "not run: " + held)
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
    if not locked_part(g, "build", lambda: build_part(g)):
        g.code = 0 if g.write() else 1
        return g
    quick = level == "quick"
    every = level in ("main", "final")
    renderers = ("d3d12",) if quick else RENDERERS
    if level != "long":
        unit_part(g)
    if level != "long" and not MOD_TESTS.exists():
        g.missing("mods", "the mod tests are not in the tree (tools/lumacore/mod_tests.py, mods M0)")
    soak = start_soak(g) if level in ("long", "final") and not g.dry and g.wanted("soak", ("build/windows",)) else None
    tours = write_tours(g, QUICK_STOPS if quick else None, QUICK_REGION_STOPS if quick else None)
    # Region discs: Direct3D 12 at the quick and main levels; every renderer in the long run and the final check.
    region_renderers = RENDERERS if level in ("long", "final") else ("d3d12",)
    # The CPU-only checks first in the list, so their rows read where they always did.
    lanes = [cpu_lane(g, ("RMGE01",) if quick else ("RMGE01",) + REGION_DISCS)] if level != "long" else []
    for r in renderers:
        def renderer_lane(r=r):
            if level != "long":
                engine_lane(g, r)()
            tour_lane(g, tours["tour"], "tour", r)()
        lanes.append(renderer_lane)
    for disc in REGION_DISCS:
        def region_lane(disc=disc):
            for r in region_renderers:
                tour_lane(g, tours["tour-%s" % disc], "tour-%s" % disc, r)()
        lanes.append(region_lane)
    if level != "long" and MOD_TESTS.exists():
        lanes += [mods_lane(g, r) for r in renderers]
    if not quick:
        # The main level's tablet part is the twelve quick stops; the long run's (and the final check's) all of them.
        lanes.append(android_lane(g, QUICK_STOPS if level == "main" else None))
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
    replay_part(g, renderers)
    if level in ("long", "final") and g.wanted("soak", ("build/windows",)) and not g.dry:
        if soak is None:
            g.missing("soak", "it did not start")
        else:
            g.check("soak", soak_result(*soak))
    if level in ("long", "final"):
        # Last, with the machine alone: the gate lock keeps gates out, measure_short.py takes measure.lock.
        locked_part(g, "measure", lambda: g.check("measure/short", measure_short(g), ("build/windows",)))
    if every and not getattr(g.args, "move", False):
        # With --move the gate waits for GitHub after its own checks, outside gate.lock (main()).
        g.check("github", github_check(g))
    g.plan_rows()
    if not dry:
        g.code = 0 if g.write() else 1
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


def parser():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = p.add_mutually_exclusive_group(required=True)
    for level in LEVELS:
        mode.add_argument("--" + level, dest="mode", action="store_const", const=level)
    p.add_argument("--move", action="store_true", help="--main: move main to this commit when it passes (section 5)")
    p.add_argument("--github-wait", type=float, default=GITHUB_WAIT_MINUTES,
                   help="--move: minutes to wait for GitHub's jobs. Longer only for the one candidate that brings a job "
                        "whose compiler cache is cold (Planner 11-18 03:49); --covers says why, and the board's LOCK "
                        "line shows the wait")
    p.add_argument("--only", default="", help="only these checks (shell patterns, comma-separated) and what they need")
    p.add_argument("--build-root", default=os.environ.get("NEBULA_GATE_BUILD", "D:/NebulaWork/build/gate-%s" % TREE.name),
                   help="where the builds go (kept between runs, so builds are incremental)")
    p.add_argument("--pak", default=os.environ.get("NEBULA_GATE_PAK", "D:/NebulaWork/build/us-pak-37/game.pak"))
    p.add_argument("--paks", default=os.environ.get("NEBULA_GATE_PAKS", os.pathsep.join((
                   "RMGP01=D:/NebulaWork/build/a1-pak-pal-37/game.pak", "RMGJ01=D:/NebulaWork/build/a1-pak-jp-37/game.pak"))),
                   help="the other discs' game.paks, DISC=path joined by the path separator: the tests with a \"disc\"")
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
    args = parser().parse_args(argv)
    patterns = [s.strip() for s in args.only.split(",") if s.strip()]

    def refuse(text):
        print("gate.py: " + text, file=sys.stderr)
        return 2

    if args.move and (args.mode != "main" or patterns or args.no_build):
        return refuse("--move goes with --main alone: the whole main gate, built here")
    if args.mode != "quick" and not args.tablet:
        return refuse("--%s needs the tablet's serial (--tablet): its part is owed when the tablet is away" % args.mode)
    known, planned, problems = read_lists()
    if problems:
        return refuse("the lists are wrong:\n  " + "\n  ".join(problems))
    selected = None
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
    candidate = None
    if args.move:
        git("fetch", "origin", "main")
        if not git_ok("merge-base", "--is-ancestor", "origin/main", "HEAD"):
            return refuse("main (%s) is not an ancestor of this commit: rebase on it first" % git("rev-parse", "--short", "origin/main"))
        if git("status", "--porcelain"):
            return refuse("the tree has uncommitted changes")
        red = red_on_github(commit, planned_jobs(planned))
        if red:
            board_line(args.board, "%s NOTICE: no main gate of %s: %s." % (args.who, commit[:9], red))
            return refuse("main could not move to %s: %s" % (commit[:9], red))
        unheld = unheld_candidates(args.work, commit)
        if unheld:
            return refuse("candidates still wait for GitHub that this commit does not hold (%s): rebase on the newest "
                          "of them, so main can reach both" % ", ".join(unheld))
        # The candidate under a name of its own (section 5.3): GitHub runs every job on it while the gate runs, and no
        # later push to a work branch cancels that run.
        candidate = CANDIDATE_PREFIX + "%s-%s" % (datetime.datetime.now().strftime("%Y%m%d-%H%M%S"), commit[:9])
        r = subprocess.run([GIT, "-C", str(TREE), "push", "origin", "%s:refs/heads/%s" % (commit, candidate)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            return refuse("the candidate could not be pushed as %s: %s" % (candidate, (r.stderr or r.stdout).strip()[-300:]))
    quick = None
    if args.mode in ("main", "final") and not patterns:
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
    code, summary = 2, "it ended before writing a result"
    watch = GithubWatch(candidate, commit, planned_jobs(planned)).start() if args.move else None
    try:
        try:
            g = run_checks(args, known, planned, selected)
        finally:
            if watch is not None:
                watch.stop()
        code = g.code
        summary = "all checks passed" if code == 0 else "checks failed (exit %d)" % code
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
            # The machine is free for the next gate from here (section 5.5); this process waits for GitHub alone.
            for lock in taken:
                free_gate_lock(lock, holder, args.board, args.who, "its checks passed; it waits for GitHub's jobs on %s"
                               % candidate)
            taken = []
            add_waiting(args.work, commit, candidate, os.getpid())
            try:
                green, detail = github_wait(candidate, commit, planned_jobs(planned), wait_minutes=args.github_wait,
                                            log=str(g.work / "github.log"))
                why = None if green else "GitHub's jobs on %s: %s" % (candidate, detail)
                if why is None:
                    move_lock = str(pathlib.Path(args.gate_lock).with_name("move.lock"))
                    held = take_gate_lock(move_lock, holder, 10.0, "", args.who, poll=5.0)
                    if held is not None:
                        why = held
                    else:
                        try:
                            why = move_main(commit, g.name, g.work, args.board, args.who, args.covers)
                        finally:
                            shutil.rmtree(move_lock, ignore_errors=True)
            finally:
                drop_waiting(args.work, commit)
            subprocess.run([GIT, "-C", str(TREE), "push", "origin", "--delete", candidate], capture_output=True, text=True)
            if why is not None:
                code, summary = 1, "passed, main not moved: " + why
                print("gate.py: main not moved: " + why, file=sys.stderr)
                board_line(args.board, "%s NOTICE: main gate %s on %s passed its checks; main not moved: %s." % (
                    args.who, g.name, commit[:9], why))
            else:
                summary = "passed, GitHub green (%s), main moved to %s" % (detail, commit[:9])
                started, what = start_long(commit, args)
                print("gate.py: long run: %s" % what)
                if started is not None:
                    board_line(args.board, "%s LONG: %s; none had started in 6 hours (section 14, N-1046)." % (args.who, what))
                elif what.startswith("its tree"):
                    board_line(args.board, "%s NOTICE: main moved to %s; no long run started: %s." % (args.who, commit[:9], what))
    finally:
        for lock in taken:
            free_gate_lock(lock, holder, args.board, args.who, summary)
        if args.status:
            try:
                import status
                status.write(args.status, work=args.work, board=args.board)
            except Exception as e:  # noqa: BLE001 - the page is a report; the gate's result stands without it
                print("gate.py: the status page was not written: %s: %s" % (type(e).__name__, e), file=sys.stderr)
    return code


def below_normal():
    """The long run runs below normal priority (section 3.3), and so do the programs it starts."""
    if os.name == "nt":
        kernel = ctypes.windll.kernel32
        kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x4000)  # BELOW_NORMAL_PRIORITY_CLASS
    else:
        os.nice(5)


if __name__ == "__main__":
    sys.exit(main())
