"""gate.py: the one check of a Nebula tree, at the four levels of docs/plan/04-GATE-AND-MAIN.md. Every check that
starts the game for a test goes through here.

    python -X utf8 -I tools/gate/gate.py --quick | --main [--move] | --long | --final  [options]

--quick  level 1: the repository checks, the Windows build, the unit and Rust tests, a start on each renderer, the
         self-tests, the mod tests and the engine tests on Direct3D 12, 12 tour stops with pictures and step hashes,
         two stops of each other disc. Before asking for a main gate.
--main   level 2, what moves main: everything deterministic. Every renderer's engine tests and 102-stop tour with
         pictures and step hashes, the other discs, the mod tests on every renderer, the Android build, the tablet's
         short part (the 12 stops, their step hashes against the PC's at the tablet's screen size) and GitHub's
         Linux, Linux unit tests, Mac and Rust-on-Linux jobs of the same commit.
         --move: main moves to this commit when it passes (section 5). It needs a whole quick gate of the commit
         passed, the branch pushed and main an ancestor of it; it takes the gate lock (or waits --wait minutes),
         and on a pass fast-forwards main and pushes it itself, writes MAIN-MOVED on the board and frees the lock.
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
# GitHub's push workflow (.github/workflows/check.yml): the jobs a main gate needs green on its commit (section 8),
# and how long it waits for them after its own checks.
WORKFLOW = "check.yml"
GITHUB_JOBS = ("Linux", "Linux unit tests", "Mac", "Rust tests (ubuntu-24.04)")
GITHUB_WAIT_MINUTES = 15
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


def run(cmd, log, env=None, cwd=None, timeout=None):
    """Runs cmd with its output in log; returns the exit code."""
    with open(log, "ab") as f:
        f.write(("$ %s\n" % " ".join(str(c) for c in cmd)).encode())
        f.flush()
        e = dict(os.environ)
        e.update(env or {})
        try:
            return subprocess.run([str(c) for c in cmd], stdout=f, stderr=subprocess.STDOUT, env=e, cwd=cwd,
                                  timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            f.write(b"\n(timed out)\n")
            return 124


# Checks ---------------------------------------------------------------------------------------------------------


def repo_audit(g):
    """The tracked files (tools/audit_repo.ps1): no game content, build outputs, oversized files or credentials."""
    def fn(log):
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", TREE / "tools" / "audit_repo.ps1"],
                   log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        return code == 0, last_line(text)
    return fn


def parse_scripts(g):
    """Every tracked PowerShell and Python script parses (tools/gate/parse_scripts.py): a script that does not fails
    only when someone runs it."""
    def fn(log):
        code = run([sys.executable, "-X", "utf8", "-I", HERE / "parse_scripts.py", "--tree", TREE], log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        return code == 0, last_line(text)
    return fn


def build_windows(g, root=None, bin_dir=None, sanitize=""):
    """The game modules (clang) and every Windows program with the unit tests and the test extension (MSVC), into the
    build root; then the bin folder the harness runs. `sanitize`: the game modules with those sanitizers, in a build
    root and bin folder of their own."""
    root, bin_dir = root or g.args.build_root, bin_dir or g.bin

    def fn(log):
        seed = ["-SeedFrom", g.args.seed_from] if g.args.seed_from else []
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", HERE / "build_windows.ps1",
                    "-Tree", TREE, "-Root", root, "-Bin", bin_dir, "-Pak", g.args.pak, "-Jobs", g.args.jobs, *seed]
                   + (["-Sanitize", sanitize] if sanitize else []), log)
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
    (mod_tests.pinned_lumalink: <build root>/lumalink-pin, its nebula/ a link to this tree): every crate's tests, and
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


def harness(g, suite, renderer, label, device=None, bin_dir=None):
    """One harness suite on one renderer, on this computer or on an Android device (its serial; the test app
    prepared and the device's lock held by the gate). Passes when every test in it passed (a test whose disc has no
    pak is not-run, which is not a pass). Its verdicts go with it when every test of the suite reported one, so the
    known-failures list can be applied."""
    def fn(log):
        work = g.work / ("runs-%s-%s" % (label, renderer))
        on_device = ["--device", device, "--bin", g.android_bin] if device else ["--bin", bin_dir or g.bin]
        code = run([sys.executable, "-X", "utf8", "-I", TREE / "tools/lumacore/luma_test.py", "run", suite]
                   + on_device + ["--work", work, "--who", "gate", "--lock", g.work / ("harness-%s-%s.lock" % (label, renderer)),
                                  "--commit", g.commit, "--renderer", renderer], log, env={"LUMA_TEST_PAKS": g.args.paks},
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
    its own, started so it runs beside the Android part. Returns the process and its log."""
    log = g.work / "soak.log"
    out = log.open("wb")
    process = subprocess.Popen([sys.executable, "-X", "utf8", "-I", str(HERE / "soak.py"), "run", "--bin", str(g.bin),
                                "--pak", g.args.pak, "--work", str(g.work / "soak"), "--minutes", str(g.args.soak_minutes),
                                "--who", "gate"], stdout=out, stderr=subprocess.STDOUT, cwd=g.args.build_root)
    out.close()
    return process, log


def soak_result(process, soak_log):
    """The soak's verdict once it ends: one line per renderer (soak.py run)."""
    def fn(log):
        code = process.wait()
        text = soak_log.read_text(encoding="utf-8", errors="replace")
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


def take_gate_lock(path, holder, wait_minutes, board, who, alive=process_alive, sleep=time.sleep, poll=30.0):
    """Takes the gate lock for `holder` (a line naming the run, with "pid N"): None when taken, else why not. Waits
    up to wait_minutes while another holds it; a holder whose process has ended loses it, and the board says so."""
    lock = pathlib.Path(path)
    deadline = time.time() + 60.0 * wait_minutes
    said_waiting = False
    while True:
        try:
            lock.mkdir()
        except FileExistsError:
            text, pid = lock_holder(lock)
            if pid is not None and not alive(pid):
                board_line(board, "%s NOTICE: the gate lock held by a process that has ended (%s) is taken over." % (who, text))
                shutil.rmtree(lock, ignore_errors=True)
                continue
            if time.time() >= deadline:
                return "the gate lock %s is held: %s" % (lock, text)
            if not said_waiting:
                print("gate.py: the gate lock is held (%s); waiting up to %g minutes" % (text, wait_minutes), flush=True)
                said_waiting = True
            sleep(poll)
            continue
        (lock / "holder.txt").write_text("%s %s\n" % (holder, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")),
                                         encoding="utf-8")
        board_line(board, "%s LOCK: gate.lock taken by gate.py: %s." % (who, holder))
        return None


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


def mod_tests(g, renderer):
    """The mod tests on one renderer, through the mods' own entry point (it builds the test mods with LumaLink, then
    runs them side by side, --mod-parallel at a time, each game in its own folder): every suite of tests/mods and of
    the pinned LumaLink's tests/game, or at the quick level the quick set (tests/mods/quick.json, one or more of every
    suite). Passes when it exits 0; its rows name each test that did not pass."""
    def fn(log):
        work = g.work / ("runs-mods-%s" % renderer)
        env = {"LUMA_TEST_PAKS": g.args.paks}
        if g.args.lumalink:
            env["LUMALINK"] = g.args.lumalink
        else:
            try:
                program, checkout = pinned(g)
            except SystemExit as stop:
                pathlib.Path(log).write_text("%s\n" % stop, encoding="utf-8")
                return False, str(stop)
            env["LUMALINK"], env["LUMALINK_TREE"] = str(program), str(checkout)
        cmd = [sys.executable, "-X", "utf8", "-I", MOD_TESTS, "--bin", g.bin, "--work", work, "--renderer", renderer,
               "--lock", g.work / ("harness-mods-%s.lock" % renderer), "--commit", g.commit, "--jobs", g.args.jobs,
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


def github_jobs(sha):
    """Every job of the push workflow's runs on a commit (pushed under any branch name): [{name, status,
    conclusion}]."""
    jobs = []
    for run_row in gh_json("run", "list", "--commit", sha, "--workflow", WORKFLOW, "--event", "push",
                           "--json", "databaseId", "--limit", "20") or []:
        jobs += gh_json("run", "view", str(run_row["databaseId"]), "--json", "jobs").get("jobs", [])
    return jobs


def judge_jobs(jobs, required=GITHUB_JOBS):
    """(done, green, detail) of the required jobs: green in any run of the commit counts; a job that ended in no run
    green is red, and a red one ends the wait; one still running or not started keeps it."""
    words, done, green = [], True, True
    for name in required:
        mine = [j for j in jobs if j.get("name") == name]
        if any(j.get("conclusion") == "success" for j in mine):
            words.append(name + " green")
        elif any(j.get("status") != "completed" for j in mine):
            words.append(name + " running")
            done, green = done and False, False
        elif mine:
            words.append("%s %s" % (name, "/".join(sorted({j.get("conclusion") or "?" for j in mine}))))
            green = False
        else:
            words.append(name + " not started")
            done, green = False, False
    red = any(not w.endswith((" green", " running", " not started")) for w in words)
    return (done or red), (green and not red), "; ".join(words)


def github_check(g, wait_minutes=GITHUB_WAIT_MINUTES, jobs_of=github_jobs, sleep=time.sleep, clock=time.time):
    """GitHub's Linux, Linux unit tests, Mac and Rust-on-Linux jobs of this commit (section 8): green, waited for up
    to wait_minutes."""
    def fn(log):
        sha = git("rev-parse", "HEAD")
        deadline = clock() + 60.0 * wait_minutes
        while True:
            done, green, detail = judge_jobs(jobs_of(sha))
            with open(log, "a", encoding="utf-8") as f:
                f.write("%s %s\n" % (datetime.datetime.now().strftime("%H:%M:%S"), detail))
            if done or clock() >= deadline:
                return green, detail + ("" if done else " (after %g minutes)" % wait_minutes)
            sleep(60.0)
    return fn


# Main --------------------------------------------------------------------------------------------------------------


def git_ok(*args):
    return subprocess.run([GIT, "-C", str(TREE), *args], capture_output=True, text=True).returncode == 0


def pushed(sha):
    """Whether a branch on origin holds the commit (so GitHub's workflow runs on it)."""
    out = subprocess.run([GIT, "-C", str(TREE), "branch", "-r", "--contains", sha], capture_output=True, text=True).stdout
    return any(l.strip().startswith("origin/") and not l.strip().startswith("origin/HEAD") for l in out.splitlines())


ALLOWED_ENV = "NEBULA_MAIN_ALLOWED"


def move_main(sha, run_name, work, board, who, covers, tree=TREE):
    """Fast-forwards origin's main to sha (section 5.5): main must still be its ancestor. The push carries a one-time
    allowance the pre-push hook's guard (tools/gate/main_push_guard.py) reads. None when moved, else why not."""
    def tree_git(*args, env=None):
        return subprocess.run([GIT, "-C", str(tree), *args], capture_output=True, text=True, env=env)

    tree_git("fetch", "origin", "main")
    before = tree_git("rev-parse", "origin/main").stdout.strip()
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
    """Whether a long run is due (section 14): none in history.tsv started within `hours`."""
    path = pathlib.Path(work) / "history.tsv"
    now = now or datetime.datetime.now()
    rows = [l.split("\t") for l in path.read_text(encoding="utf-8").splitlines()[1:]] if path.exists() else []
    started = [datetime.datetime.fromisoformat(r[0]) for r in rows if len(r) > 2 and r[2] == "long"]
    return not started or (now - max(started)).total_seconds() >= hours * 3600


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
    g.check("repo/audit", repo_audit(g))
    g.check("repo/scripts", parse_scripts(g))
    if g.args.no_build:
        return True
    return g.check("build/windows", build_windows(g)) is not False


def unit_part(g, discs_proved):
    g.check("unit/windows", unit_tests(g), ("build/windows",))
    g.check("unit/game", game_tests(g), ("build/windows",))
    for r in RENDERERS:
        g.check("start/%s" % r, start_check(g, r), ("build/windows",))
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
    return lane


def sanitizer_lane(g):
    return lambda: sanitizer_part(g)


def sanitizer_part(g):
    """The game modules built with AddressSanitizer (build_windows.ps1 -Sanitize address, a build root of its own)
    play the tour on Direct3D 12: a memory fault the tour reaches ends the run with the sanitizer's report."""
    root = pathlib.Path(g.args.build_root) / "asan"
    asan_bin = str(root / "bins")
    if g.check("build/windows-asan", build_windows(g, root=root, bin_dir=asan_bin, sanitize="address")) is False:
        g.missing("tour-asan/d3d12", "the sanitizer build failed", ("build/windows-asan",))
        return
    tour = gate_tour.write(g.work / "tour-asan", None)
    step_hash.with_hash(tour.parent)
    g.check("tour-asan/d3d12", harness(g, tour, "d3d12", "tour-asan", bin_dir=asan_bin), ("build/windows-asan",))


def run_checks(args, known=None, planned=None, selected=None, dry=False):
    """The level's checks. The build and the unit checks first, one after another; then the parts that start the game
    side by side (section 7), each renderer's engine tests and tours in a lane of their own, each other disc's tour,
    the mod tests per renderer, the Android build with the tablet; then what compares their runs. A dry run returns the
    Gate (its names and needs) without running any."""
    g = Gate(args, dry=dry, known=known, planned=planned, selected=selected)
    level = args.mode
    if not build_part(g):
        g.code = 0 if g.write() else 1
        return g
    quick = level == "quick"
    every = level in ("main", "final")
    renderers = ("d3d12",) if quick else RENDERERS
    if level != "long":
        unit_part(g, ("RMGE01",) if quick else ("RMGE01",) + REGION_DISCS)
    if level != "long" and not MOD_TESTS.exists():
        g.missing("mods", "the mod tests are not in the tree (tools/lumacore/mod_tests.py, mods M0)")
    soak = start_soak(g) if level in ("long", "final") and not g.dry and g.wanted("soak", ("build/windows",)) else None
    tours = write_tours(g, QUICK_STOPS if quick else None, QUICK_REGION_STOPS if quick else None)
    # Region discs: Direct3D 12 at the quick and main levels; every renderer in the long run and the final check.
    region_renderers = RENDERERS if level in ("long", "final") else ("d3d12",)
    lanes = []
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
        lanes.append(sanitizer_lane(g))
    g.lanes(lanes)
    replay_part(g, renderers)
    if level in ("long", "final") and g.wanted("soak", ("build/windows",)) and not g.dry:
        if soak is None:
            g.missing("soak", "it did not start")
        else:
            g.check("soak", soak_result(*soak))
    if every:
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
    p.add_argument("--only", default="", help="only these checks (shell patterns, comma-separated) and what they need")
    p.add_argument("--build-root", default=os.environ.get("NEBULA_GATE_BUILD", "D:/NebulaWork/build/gate-%s" % TREE.name),
                   help="where the builds go (kept between runs, so builds are incremental)")
    p.add_argument("--pak", default=os.environ.get("NEBULA_GATE_PAK", "D:/NebulaWork/build/us-pak-35/game.pak"))
    p.add_argument("--paks", default=os.environ.get("NEBULA_GATE_PAKS", os.pathsep.join((
                   "RMGP01=D:/NebulaWork/build/a1-pak-pal-35/game.pak", "RMGJ01=D:/NebulaWork/build/a1-pak-jp-35/game.pak"))),
                   help="the other discs' game.paks, DISC=path joined by the path separator: the tests with a \"disc\"")
    p.add_argument("--work", default=os.environ.get("NEBULA_GATE_WORK", "D:/NebulaWork/build/gate-runs"))
    p.add_argument("--discs", default=os.environ.get("NEBULA_GATE_DISCS", "D:/NebulaWork/discs"),
                   help="a folder of disc images named by disc (RMGE01.wbfs, RMGP01.rvz, RMGJ01.rvz): the collision proof")
    p.add_argument("--out", help="the result file (default: result.json in the run's work folder)")
    p.add_argument("--jobs", default=str(max(4, (os.cpu_count() or 8) - 4)))
    p.add_argument("--no-build", action="store_true", help="use the bins already in the build root")
    p.add_argument("--record", action="store_true", help="also keep the result in docs/plan/gate-runs, for a commit")
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
    p.add_argument("--wait", type=float, default=0.0, help="minutes to wait for a held gate or long lock (0: refuse)")
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
    if args.move:
        git("fetch", "origin", "main")
        if not git_ok("merge-base", "--is-ancestor", "origin/main", "HEAD"):
            return refuse("main (%s) is not an ancestor of this commit: rebase on it first" % git("rev-parse", "--short", "origin/main"))
        if git("status", "--porcelain"):
            return refuse("the tree has uncommitted changes")
        if not pushed(commit):
            return refuse("this commit is on no branch of origin: push the branch first, so GitHub runs on it")
    quick = None
    if args.mode in ("main", "final") and not patterns:
        quick = quick_passed(args.work, git("rev-parse", "--short", "HEAD"), whole=True)
        if quick is None and (args.move or not args.without_quick):
            return refuse("no whole quick gate of %s passed in %s: run --quick first" % (commit[:9], args.work))
    if args.mode == "long":
        below_normal()
    locks = []
    if args.mode in ("main", "final") and not patterns:
        locks.append(args.gate_lock)
    if args.mode in ("long", "final") and not patterns:
        locks.append(args.long_lock)
    holder = "%s gate of %s@%s pid %d%s%s" % (args.mode, TREE.name, commit[:9], os.getpid(),
                                             ("; covers " + args.covers) if args.covers else "",
                                             ("; quick gate " + quick.name) if quick else "")
    taken = []
    for lock in locks:
        held = take_gate_lock(lock, holder, args.wait, args.board, args.who)
        if held is not None:
            for t in taken:
                free_gate_lock(t, holder, args.board, args.who, "not run: " + held)
            return refuse(held)
        taken.append(lock)
    code, summary = 2, "it ended before writing a result"
    try:
        g = run_checks(args, known, planned, selected)
        code = g.code
        summary = "all checks passed" if code == 0 else "checks failed (exit %d)" % code
        owed = [r["check"] for r in g.results if r["state"] == "owed"]
        if owed and args.board:
            board_line(args.board, "%s OWED: the tablet's part of %s gate %s on %s: %s. It runs on main's newest "
                       "commit first when the tablet is back (gate.py --main --only \"%s\")." % (
                           args.who, args.mode, g.name, commit[:9], ", ".join(owed), ",".join(owed)))
        if args.move and code == 0:
            why = move_main(commit, g.name, g.work, args.board, args.who, args.covers)
            if why is not None:
                code, summary = 1, "passed, main not moved: " + why
                print("gate.py: main not moved: " + why, file=sys.stderr)
            else:
                summary = "passed, main moved to %s" % commit[:9]
                if long_due(args.work):
                    print("gate.py: no long run started in 6 hours: start one on main's newest commit (section 14)")
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
