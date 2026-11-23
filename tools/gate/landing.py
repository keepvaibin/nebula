"""landing.py: the landing queue and its batches (package T4, docs/plan/04-GATE-AND-MAIN.md), without the game.

    gate.py land BRANCH [BRANCH ...]     put the branches in the queue and wait until each is landed, failed or does not apply
    gate.py land --queue                 run what waits in the queue, take nothing new

The queue is a file, D:/NebulaWork/plan/landing-queue.tsv: time, branch, agent, state, commit, run folder, note, also; it is
changed only under a lock folder beside it. `also` is what the caller asked for with `gate.py land --also NAMES`: checks, by
the names the gate gives them, that run in this landing besides what the changed files need (a repair's check); a batch runs
the union of the `also` of its branches. A file of seven columns (before `also`) is read as it is. A state is `waiting`, `running`, `landed`, `failed` or `conflict`. A caller
puts its branches in the queue and, when no queue runner is alive (the runner lock folder), becomes the runner; otherwise it
waits until its branches reach an end state. The runner:

  1. takes every waiting branch in queue order;
  2. in the work folder the gate owns (D:/NebulaWork/MarioGalaxyRecomp/Nebula-landing) starts a temporary branch at
     origin/main and replays each branch's own commits on top, in order (history stays a straight line). A branch that does
     not apply cleanly is taken out as `conflict`, with the names of the files (it is not judged; the others go on);
  3. picks the checks from the union of the batch's changed files (tools/gate/scope.py) and runs them once on the tip
     (`gate.py --main --move --scope ...` of the landing tree: the run's own move of main, with the pictures' approval and
     GitHub's rule, is the landing);
  4. VERDICT pass: every branch is `landed` with its new commit. VERDICT fail with one branch: it is `failed`. With several:
     the batch is halved until it is known which fail (below); those are `failed` with their own KIND lines, the rest are
     landed. VERDICT not-judged (a tool or the machine, not the change): nothing lands, every branch goes back to `waiting`,
     the run ends and the tool or machine lines are printed.

Halving. The failing set S is known to hold a fault; U is what has not been told apart from it. Half of S is run alone (the
first half, rounded down). Passing, it lands at once and the other half is the failing set (when that is one branch, it is
named without a run: the others passed and landed, and the run that held it failed). Failing, that half is the failing set
and the other half joins U. When S is one branch it is named failed; when S is empty U is run once together and landed, or
becomes S. A batch of three with the fault in the first or the last branch takes three runs; in the middle, four (one run for
each branch that must be proved).

For each caller the last lines are the KIND lines of the runs that decided its branches and one VERDICT line; the exit code is
0 landed, 1 fail, 2 not judged (a branch that does not apply or a tool or machine fault), 3 the same failure three times
(tools/plan/failures.py: the signature of a failed branch is its first failing check and that check's first error line; a tool
or machine fault never counts toward it).
"""
import ctypes
import datetime
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "plan"))
import failures  # noqa: E402
import kinds  # noqa: E402
import lockholder  # noqa: E402
import scope  # noqa: E402

PLAN_DIR = pathlib.Path("D:/NebulaWork/plan")
QUEUE = PLAN_DIR / "landing-queue.tsv"
RUNNER_LOCK = PLAN_DIR / "landing-runner.lock"
FAILURES = PLAN_DIR / "landing-failures.tsv"
LANDING_TREE = pathlib.Path("D:/NebulaWork/MarioGalaxyRecomp/Nebula-landing")
TEMP_BRANCH = "land/tmp"
MAIN_REF = "origin/main"
GIT = next((p for p in ("C:/Program Files/Git/cmd/git.exe",) if os.path.exists(p)), "git")

COLUMNS = ("time", "branch", "agent", "state", "commit", "run folder", "note", "also")
WAITING, RUNNING, LANDED, FAILED, CONFLICT = "waiting", "running", "landed", "failed", "conflict"
STATES = (WAITING, RUNNING, LANDED, FAILED, CONFLICT)
ENDED = (LANDED, FAILED, CONFLICT)
NOT_JUDGED = "not judged"
EXIT_LANDED, EXIT_FAIL, EXIT_NOT_JUDGED, EXIT_SAME_THREE = 0, 1, 2, 3


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


def stamp(clock=time.time):
    return datetime.datetime.fromtimestamp(clock()).isoformat(timespec="seconds")


def cell(text):
    return str(text).replace("\t", " ").replace("\r", " ").replace("\n", " ")


PLANT_PREFIX = "plant/"      # a planted fault (docs/plan/native/TOOL-MEND-PLANTS.md): judged by hand, never landed
PLANT_REFUSAL = "a branch named plant/... is a planted fault and never lands (docs/plan/native/TOOL-MEND-PLANTS.md)"


def plant_branches(names):
    """The names among `names` that are planted-fault branches."""
    return [n for n in names if str(n).startswith(PLANT_PREFIX)]


def also_names(*texts):
    """The check names of comma-separated texts, each once, in the order given."""
    out = []
    for text in texts:
        for name in str(text or "").split(","):
            name = name.strip()
            if name and name not in out:
                out.append(name)
    return out


def also_text(*texts):
    return ",".join(also_names(*texts))


# `gate.py land --github first` travels in the queue's `also` cell as this word (the queue file keeps its columns): the batch it
# is in is run with the group `github-first`, so the move waits for GitHub's `must` jobs. It names no check.
GITHUB_FIRST = "@github-first"


def called_by_runner(pid, lock_path=RUNNER_LOCK, alive=process_alive):
    """Whether `pid` is the holder of the landing runner's lock and runs: the mark `gate.py --main --move` must carry (the runner's
    own call; main moves only through gate.py land). A pid that is not the holder, or no pid, is not the runner."""
    if not pid:
        return False
    holder = FolderLock(lock_path, alive).holder()[1]
    return holder == pid and bool(alive(pid))


# The lock folders ------------------------------------------------------------------------------------------------------


class FolderLock:
    """A lock folder with a holder.txt naming a process (`pid N` anywhere in the line, tools/gate/lockholder.py): a holder whose process
    ended loses it, with one printed line naming its pid."""

    def __init__(self, path, alive=process_alive, pid=None, clock=time.time, sleep=time.sleep, started=lockholder.process_started):
        self.path = pathlib.Path(path)
        self.alive, self.pid, self.clock, self.sleep = alive, os.getpid() if pid is None else pid, clock, sleep
        self.started = started

    def holder(self):
        text = ""
        try:
            text = (self.path / "holder.txt").read_text(encoding="utf-8").strip()
        except OSError:
            pass
        return text, lockholder.parse(text)[0]

    def try_take(self, what=""):
        """True when taken: the folder made (a held lock whose holder has ended is taken over)."""
        for _ in range(3):
            try:
                self.path.parent.mkdir(parents=True, exist_ok=True)
                self.path.mkdir()
            except FileExistsError:
                text, pid = self.holder()
                old = lockholder.gone(text, self.alive, self.started)
                if old is not None and old != self.pid:
                    print("landing: the lock %s was held by pid %d, which has ended: taken over (%s)" % (self.path, old, text), flush=True)
                    shutil.rmtree(self.path, ignore_errors=True)
                    continue
                return False
            who = lockholder.named(self.pid) if self.pid == os.getpid() else "pid %d" % self.pid
            (self.path / "holder.txt").write_text("%s %s %s\n" % (what or "landing", who, stamp(self.clock)), encoding="utf-8")
            return True
        return False

    def take(self, what="", timeout=60.0):
        """Waits up to `timeout` seconds for the lock; False when it was not had."""
        end = self.clock() + timeout
        while not self.try_take(what):
            if self.clock() >= end:
                return False
            self.sleep(0.05)
        return True

    def free(self):
        _, pid = self.holder()
        if pid in (None, self.pid):
            shutil.rmtree(self.path, ignore_errors=True)


# The queue --------------------------------------------------------------------------------------------------------------


class Queue:
    """landing-queue.tsv: one row per branch, changed only under the lock folder beside it (<file>.lock)."""

    def __init__(self, path=QUEUE, alive=process_alive, clock=time.time, sleep=time.sleep):
        self.path = pathlib.Path(path)
        self.lock = FolderLock(str(self.path) + ".lock", alive, clock=clock, sleep=sleep)
        self.clock = clock

    def read(self):
        text = None
        for _ in range(20):
            try:
                text = self.path.read_text(encoding="utf-8")
                break
            except FileNotFoundError:
                return []
            except PermissionError:      # Windows: the file is being replaced
                time.sleep(0.05)
        if text is None:
            raise RuntimeError("the queue %s cannot be read" % self.path)
        rows = []
        for line in text.splitlines()[1:]:
            cells = line.split("\t")
            if len(cells) == len(COLUMNS) - 1:       # a file written before the column `also`
                cells.append("")
            if len(cells) == len(COLUMNS):
                rows.append(dict(zip(COLUMNS, cells)))
        return rows

    def _write(self, rows):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        text = "\t".join(COLUMNS) + "\n" + "".join("\t".join(cell(r.get(c, "")) for c in COLUMNS) + "\n" for r in rows)
        tmp = self.path.with_name(self.path.name + ".tmp%d" % os.getpid())
        tmp.write_text(text, encoding="utf-8", newline="\n")
        for attempt in range(40):
            try:
                os.replace(tmp, self.path)
                return
            except PermissionError:      # Windows: a waiter has the file open for a moment
                if attempt == 39:
                    raise
                time.sleep(0.05)

    def change(self, fn):
        """Runs fn(rows) under the lock; the rows it returns are written. Returns what fn returned second, if it did."""
        if not self.lock.take("landing queue", timeout=60.0):
            raise RuntimeError("the queue's lock %s is held: %s" % (self.lock.path, self.lock.holder()[0]))
        try:
            rows = self.read()
            result = fn(rows)
            if isinstance(result, tuple):
                rows, extra = result
            else:
                rows, extra = result, None
            self._write(rows)
            return extra
        finally:
            self.lock.free()

    def row(self, branch):
        return next((r for r in self.read() if r["branch"] == branch), None)

    def add(self, branches, agent, commit_of, also=""):
        """Puts the branches in the queue as waiting, with the checks the caller asked for besides the changed files' (`also`,
        comma-separated names). A branch already waiting or running stays as it is (its caller attaches to it; a waiting one
        takes the caller's `also` in as well); one that ended, or went back to waiting not judged, starts again."""
        def fn(rows):
            for b in branches:
                new = dict(zip(COLUMNS, (stamp(self.clock), b, agent, WAITING, commit_of(b) or "", "", "", also_text(also))))
                old = next((r for r in rows if r["branch"] == b), None)
                if old is None:
                    rows.append(new)
                elif old["state"] == RUNNING or (old["state"] == WAITING and not old["note"].startswith(NOT_JUDGED)):
                    if old["state"] == WAITING:
                        old["also"] = also_text(old.get("also", ""), also)
                    continue
                else:
                    rows[rows.index(old)] = new
            return rows
        self.change(fn)

    def claim(self, skip=(), retry=False):
        """Every waiting branch not in `skip`, in queue order, now `running`: the rows as they were. A branch that went back to
        waiting after a run that judged nothing is taken only when `retry` (gate.py land --queue, or its own caller again): a
        tool fault of one branch does not hold up the landing of another's."""
        def fn(rows):
            taken = [dict(r) for r in rows if r["state"] == WAITING and r["branch"] not in skip
                     and (retry or not r["note"].startswith(NOT_JUDGED))]
            names = {r["branch"] for r in taken}
            for r in rows:
                if r["branch"] in names:
                    r["state"] = RUNNING
            return rows, taken
        return self.change(fn)

    def finish(self, branch, state, commit=None, run_folder="", note=""):
        def fn(rows):
            for r in rows:
                if r["branch"] == branch:
                    r["state"] = state
                    if commit:
                        r["commit"] = commit
                    r["run folder"], r["note"] = str(run_folder or ""), note
            return rows
        self.change(fn)

    def requeue(self, branches, note, run_folder=""):
        """Back to waiting, with the note (it starts `not judged`) and the run's folder: nothing was decided."""
        def fn(rows):
            for r in rows:
                if r["branch"] in branches and r["state"] == RUNNING:
                    r["state"], r["note"], r["run folder"] = WAITING, NOT_JUDGED + ": " + note, str(run_folder or "")
            return rows
        self.change(fn)

    def recover(self):
        """A `running` row with no runner alive (the runner was stopped) waits again."""
        def fn(rows):
            for r in rows:
                if r["state"] == RUNNING:
                    r["state"], r["note"] = WAITING, ""
            return rows
        self.change(fn)


def ended(row):
    """Whether a caller's branch is done with: landed, failed or conflict, or waiting after a run that judged nothing."""
    return row is not None and (row["state"] in ENDED or (row["state"] == WAITING and row["note"].startswith(NOT_JUDGED)))


# Git ----------------------------------------------------------------------------------------------------------------------


class Repo:
    def __init__(self, git=GIT):
        self.git = git

    def run(self, tree, *args):
        return subprocess.run([self.git, "-C", str(tree), *args], capture_output=True, text=True)

    def out(self, tree, *args):
        r = self.run(tree, *args)
        return r.stdout.strip() if r.returncode == 0 else ""

    def ok(self, tree, *args):
        return self.run(tree, *args).returncode == 0


def resolve_branch(repo, tree, branch):
    """The commit a branch names: the repository's own branch (the work folders share it), else origin's."""
    for ref in ("refs/heads/" + branch, "refs/remotes/origin/" + branch):
        sha = repo.out(tree, "rev-parse", "--verify", "-q", ref + "^{commit}")
        if sha:
            return sha
    return ""


def ensure_tree(repo, source, tree, ref=MAIN_REF):
    """The gate's own work folder: made with `git worktree add` from `source` on first use. None, or why not."""
    tree = pathlib.Path(tree)
    if (tree / ".git").exists():
        return None
    r = repo.run(source, "worktree", "add", "--detach", str(tree), ref)
    return None if r.returncode == 0 else "git worktree add %s: %s" % (tree, (r.stderr or r.stdout).strip()[-300:])


class Prepared:
    """The temporary branch of a batch: `tip`, the branches replayed in order (`order`, each one's last commit in `tips`),
    those taken out (`conflicts` {branch: why}), those already on main (`empty`), and the changed files."""

    def __init__(self):
        self.tip, self.base = "", ""
        self.order, self.tips, self.conflicts, self.empty = [], {}, {}, []


def prepare(repo, tree, branches, main_ref=MAIN_REF, temp=TEMP_BRANCH, resolve=None, fetch=True):
    """Starts the temporary branch at main and replays each branch's own commits on top, in order. A branch that does not
    apply is undone and left out (the files are named); a branch with no commit of its own is already on main."""
    tree = pathlib.Path(tree)
    out = Prepared()
    resolve = resolve or (lambda b: resolve_branch(repo, tree, b))
    repo.run(tree, "cherry-pick", "--abort")
    repo.run(tree, "reset", "--hard", "-q")
    repo.run(tree, "clean", "-fdq")
    if fetch:
        repo.run(tree, "fetch", "-q", "origin", "main")
    r = repo.run(tree, "checkout", "-q", "-f", "-B", temp, main_ref)
    if r.returncode != 0:
        raise RuntimeError("git checkout %s at %s: %s" % (temp, main_ref, (r.stderr or r.stdout).strip()[-300:]))
    out.base = repo.out(tree, "rev-parse", "HEAD")
    for b in branches:
        sha = resolve(b)
        if not sha:
            out.conflicts[b] = "the branch %s is not in the repository" % b
            continue
        base = repo.out(tree, "merge-base", main_ref, sha)
        commits = repo.out(tree, "rev-list", "--reverse", "--no-merges", "%s..%s" % (base, sha)).split() if base else []
        if not base:
            out.conflicts[b] = "the branch has no merge base with %s" % main_ref
            continue
        if not commits:
            out.empty.append(b)
            continue
        before = repo.out(tree, "rev-parse", "HEAD")
        bad = ""
        for c in commits:
            r = repo.run(tree, "cherry-pick", "--allow-empty", "--keep-redundant-commits", c)
            if r.returncode != 0:
                files = repo.out(tree, "diff", "--name-only", "--diff-filter=U").split()
                bad = "does not apply on %s: %s" % (main_ref, ", ".join(files) if files else (r.stderr or r.stdout).strip()[-200:])
                repo.run(tree, "cherry-pick", "--abort")
                repo.run(tree, "reset", "--hard", "-q", before)
                break
        if bad:
            out.conflicts[b] = bad
            continue
        out.order.append(b)
        out.tips[b] = repo.out(tree, "rev-parse", "HEAD")
    out.tip = repo.out(tree, "rev-parse", "HEAD")
    return out


# What a run says ------------------------------------------------------------------------------------------------------------


class Result:
    """One run of the checks on a tip. `verdict`: pass, fail or not-judged. `lines`: its KIND lines. `failures`:
    [(check, kind, first error line)] of what did not pass. `moved`: the commit main moved to ('' when it did not).
    `why_kind`: when a pass did not move main, `main-moved` (main moved during the run: nothing was judged) or `github` (a
    red job of GitHub: the batch fails)."""

    def __init__(self, verdict, lines=(), failures_=(), run_folder="", moved="", why="", why_kind="", long_log=""):
        self.verdict, self.lines, self.failures = verdict, list(lines), list(failures_)
        self.run_folder, self.moved, self.why, self.why_kind = str(run_folder or ""), moved, why, why_kind
        self.long_log = long_log      # the log of the long run this move started ('' when none started)


ADVISORY_END = "advisory, blocks nothing"


def not_failure(message):
    """Whether a KIND line's message says its result does not count: a note (known, set aside, the owed tablet), or the line of a
    GitHub job that is advisory and ended red twice (tools/gate/github_rule.py)."""
    if message.endswith(ADVISORY_END):
        return True
    return message.endswith("]") and any(message.rpartition(" [")[2].startswith(n) for n in ("known (", "set aside", "owed"))


def failures_of_lines(lines):
    """[(check, kind, message)] of the KIND lines that count as a failure of their kind: kind fail, worse, tool or env, not the
    ones a note marks as not counting (known, set aside, owed)."""
    out = []
    for line in lines:
        parsed = kinds.parse_kind_line(line)
        if parsed is None:
            continue
        kind, check, test, message = parsed
        if kind not in ("fail", "worse", "tool", "env"):
            continue
        if not_failure(message):
            continue
        out.append((check, kind, message.partition(" [")[0] if message.endswith("]") else message))
    return out


class Outcome:
    def __init__(self, kind, included=(), result=None):
        self.kind, self.included, self.result = kind, list(included), result


# The landing --------------------------------------------------------------------------------------------------------------------


class Landing:
    """The runner and the callers. `checker.run(tree, tip, names, groups_text)` runs the checks on the tip and, on a pass,
    moves main (gate.py's GateChecker; the self-tests' fake): a Result. `faults(agent, step, kind, check, test, message,
    run_folder)` records a tool or machine fault at once."""

    def __init__(self, queue, checker, repo=None, source=None, tree=LANDING_TREE, store=None, runner=None, faults=None,
                 say=print, sleep=time.sleep, clock=time.time, alive=process_alive, poll=5.0, main_ref=MAIN_REF, table=None):
        self.queue, self.checker = queue, checker
        self.repo = repo or Repo()
        self.source = source
        self.tree = pathlib.Path(tree)
        self.store = store or failures.Store(FAILURES)
        self.runner = runner or FolderLock(RUNNER_LOCK, alive)
        self.faults, self.say, self.sleep, self.clock, self.poll = faults, say, sleep, clock, poll
        self.main_ref, self.table = main_ref, table
        self.runs = 0
        self.last = {}        # branch -> its Result of this process (the run that decided it)

    # --- the caller ---------------------------------------------------------------------------------------------------

    def commit_of(self, branch):
        return resolve_branch(self.repo, self.tree if (self.tree / ".git").exists() else self.source, branch)[:9]

    def land(self, branches, agent="1", wait_minutes=600.0, also=""):
        """Queues the branches and sees them to an end: the runner when none is alive, else a waiter. Returns the exit code
        and prints the caller's lines. `also`: checks (comma-separated names) this landing runs besides the changed files'."""
        planted = plant_branches(branches)
        if planted:
            self.say("gate.py: refused: %s: %s" % (", ".join(planted), PLANT_REFUSAL))
            self.say(kinds.verdict_line("not-judged"))
            return EXIT_NOT_JUDGED
        self.queue.add(branches, agent, self.commit_of, also)
        deadline = self.clock() + 60.0 * wait_minutes
        while True:
            rows = {b: self.queue.row(b) for b in branches}
            if all(ended(r) for r in rows.values()):
                break
            if self.runner.try_take("landing runner"):
                try:
                    self.queue.recover()
                    self.run_queue()
                finally:
                    self.runner.free()
                continue
            if self.clock() >= deadline:
                self.say("gate.py: the queue's runner (%s) has not reached %s in %g minutes" % (
                    self.runner.holder()[0], ", ".join(b for b, r in rows.items() if not ended(r)), wait_minutes))
                return self.report(branches, give_up=True)
            if not getattr(self, "_said", False):
                self.say("gate.py: a queue runner is alive (%s); waiting for %s" % (self.runner.holder()[0], ", ".join(branches)))
                self._said = True
            self.sleep(self.poll)
        return self.report(branches)

    def run_only_queue(self):
        """`land --queue`: run what waits and take nothing new. The exit code is the worst of the branches it ran."""
        if not self.runner.try_take("landing runner"):
            self.say("gate.py: a queue runner is alive (%s): it runs what waits" % self.runner.holder()[0])
            self.say(kinds.verdict_line("pass"))
            return EXIT_LANDED
        self.ran = []
        try:
            self.queue.recover()
            self.run_queue(retry=True)
        finally:
            self.runner.free()
        if not self.ran:
            self.say("gate.py: nothing waits in the queue")
            self.say(kinds.verdict_line("pass"))
            return EXIT_LANDED
        return self.report(self.ran)

    # --- the report ---------------------------------------------------------------------------------------------------

    def exit_of(self, rows):
        code = EXIT_LANDED
        for r in rows:
            if r is None:
                code = max(code, EXIT_NOT_JUDGED, key=_rank)
            elif r["state"] == FAILED:
                code = max(code, EXIT_SAME_THREE if self.store.count(r["branch"]) >= failures.STOP_AFTER else EXIT_FAIL, key=_rank)
            elif r["state"] != LANDED:
                code = max(code, EXIT_NOT_JUDGED, key=_rank)
        return code

    def run_lines(self, row):
        """The KIND lines of the run that decided a branch (its verdict.json)."""
        folder = row["run folder"] if row else ""
        if folder:
            try:
                return list(json.loads((pathlib.Path(folder) / "verdict.json").read_text(encoding="utf-8")).get("lines", []))
            except (OSError, ValueError):
                pass
        return []

    def report(self, branches, give_up=False):
        """Prints the caller's lines (a status line for each branch, then the KIND lines of the runs that decided them, then
        VERDICT) and returns the exit code."""
        rows = [self.queue.row(b) for b in branches]
        code = self.exit_of(rows)
        status, kind_lines, seen, tool_env = [], [], set(), False
        for b, r in zip(branches, rows):
            if r is None:
                status.append("LAND %s unknown (it is not in the queue)" % b)
                continue
            detail = {LANDED: "main now has it as %s" % ((r["commit"] or "?")[:9]),
                      FAILED: r["note"], CONFLICT: r["note"],
                      WAITING: r["note"] or "still waiting", RUNNING: "still running"}[r["state"]]
            status.append("LAND %s %s %s" % (b, r["state"], detail))
            for line in self.run_lines(r):
                if line not in seen:
                    seen.add(line)
                    kind_lines.append(line)
            if r["state"] == WAITING and r["note"].startswith(NOT_JUDGED) and any(
                    k in ("tool", "env") for _, k, _ in failures_of_lines(self.run_lines(r))):
                tool_env = True
        for line in status:
            self.say(line)
        for b in branches:
            result = self.last.get(b)
            if result is not None and result.long_log:
                self.say("gate.py: a long run started after this move: its log is %s. It plays its parts one piece at a time (none longer "
                         "than 7 minutes) and gives way to every landing: a piece does not start while a branch waits or runs in the "
                         "queue." % result.long_log)
                break
        if code == EXIT_SAME_THREE:
            self.say(failures.STOP_TEXT)
        elif code == EXIT_NOT_JUDGED and tool_env:
            kinds_seen = {k for _, k, _ in failures_of_lines(kind_lines)}
            self.say(failures.TOOL_TEXT if "tool" in kinds_seen else failures.ENV_TEXT)
            self.say("The fault is in tool-faults.tsv. Say what it stopped you from doing: plan.py toolfault --last --could-not \"<one sentence>\"")
        elif code == EXIT_NOT_JUDGED and not give_up:
            self.say("Not judged: nothing here says the change is wrong. Fix what the line above says, then give the same landing command again.")
        for line in kind_lines:
            self.say(line)
        self.say(kinds.verdict_line({EXIT_LANDED: "pass", EXIT_FAIL: "fail", EXIT_SAME_THREE: "fail"}.get(code, "not-judged")))
        return code

    # --- the runner ---------------------------------------------------------------------------------------------------

    def run_queue(self, retry=False):
        skip = set()
        while True:
            rows = self.queue.claim(skip, retry)
            if not rows:
                return
            names = [r["branch"] for r in rows]
            if hasattr(self, "ran"):
                self.ran += [n for n in names if n not in self.ran]
            agents = {r["branch"]: r["agent"] for r in rows}
            self.agents = getattr(self, "agents", {})
            self.agents.update(agents)
            self.alsos = getattr(self, "alsos", {})
            self.alsos.update({r["branch"]: r.get("also", "") for r in rows})
            try:
                stop = self.batch(names)
            except Exception as error:  # noqa: BLE001 - a runner that breaks has judged nothing: the branches wait again
                self.queue.requeue(set(names), "the runner broke: %s: %s" % (type(error).__name__, error))
                stop = True
            if stop:
                skip.update(names)

    def attempt(self, names):
        """One run on a temporary branch of `names`: conflicts and branches already on main are finished here. An Outcome:
        kind `pass` (main moved, the included are landed), `fail`, `nj` (nothing judged: the included wait again) or
        `none` (nothing left to run)."""
        tree = self.tree
        why = ensure_tree(self.repo, self.source, tree, self.main_ref) if self.source is not None else None
        if why:
            self.queue.requeue(set(names), why)
            return Outcome("nj", names, Result("not-judged", [], [("landing/tree", "tool", why)]))
        prep = prepare(self.repo, tree, names, self.main_ref)
        for b, why in prep.conflicts.items():
            self.queue.finish(b, CONFLICT, note=why)
            self.say("gate.py: %s: %s" % (b, why))
        for b in prep.empty:
            self.queue.finish(b, LANDED, note="no commit of its own: main holds it already")
            self.store.clear(b)
            asked = [n for n in also_names(getattr(self, "alsos", {}).get(b, "")) if n != GITHUB_FIRST]
            if asked:
                # Nothing was run for this branch (main holds it already), so what it asked for by --also was not run either.
                self.say("gate.py: %s: main holds it already, so no check ran: the checks asked for with --also were NOT run: %s. Put a "
                         "commit of its own on the branch (a documents-only change is enough) to run them." % (b, ", ".join(asked)))
        if not prep.order:
            return Outcome("none")
        paths, why = scope.changed_paths(self.repo.git, tree, self.main_ref, prep.tip)
        if why:
            plan = scope.Plan([], True, [], {})
        else:
            plan = scope.plan_for(paths, self.table or self.tip_table(tree))
        self.runs += 1
        also_all = also_names(*[getattr(self, "alsos", {}).get(b, "") for b in prep.order])
        first = GITHUB_FIRST in also_all
        also = ",".join(n for n in also_all if n != GITHUB_FIRST)
        if first:
            plan = scope.with_group(plan, scope.GITHUB_FIRST)
        self.say("gate.py: run %d on %s: %s%s (%s)" % (self.runs, ", ".join(prep.order), plan.text(),
                                                      " and, asked for by name: %s" % also if also else "", prep.tip[:9]) +
                 ("; GitHub first (asked for)" if first else ""))
        result = self.checker.run(tree, prep.tip, list(prep.order), plan.text(), **({"also": also} if also else {}))
        for b in prep.order:
            self.last[b] = result
        if result.verdict == "not-judged" or (result.verdict == "pass" and not result.moved and result.why_kind not in ("github", "hook")):
            note = result.why or "; ".join("%s %s: %s" % (k, c, m) for c, k, m in result.failures[:2]) or "a tool or the machine"
            self.queue.requeue(set(prep.order), note, result.run_folder)
            self.record_faults(prep.order, result)
            return Outcome("nj", prep.order, result)
        if result.verdict == "pass" and result.moved:
            for b in prep.order:
                self.queue.finish(b, LANDED, prep.tips[b], result.run_folder, "main moved to %s" % result.moved[:9])
                self.store.clear(b)
            return Outcome("pass", prep.order, result)
        if result.verdict == "pass":      # a red job of GitHub, or a repository rule at the push: the batch's checks passed, its landing did not
            result.failures = [("pre-push" if result.why_kind == "hook" else "github", "fail", result.why)]
        return Outcome("fail", prep.order, result)

    def tip_table(self, tree):
        """The scope table of the tree at the tip (a batch that changes the table is run by its own rows), else the tool's."""
        path = pathlib.Path(tree) / "tools" / "gate" / "scope.tsv"
        try:
            return scope.read_table(path if path.is_file() else scope.TABLE)
        except scope.ScopeError:
            return scope.read_table()

    def record_faults(self, names, result):
        if self.faults is None:
            return
        agent = getattr(self, "agents", {}).get(names[0], "1")
        step = names[0][len("native/"):] if names and names[0].startswith("native/") else ""
        for line in result.lines:
            parsed = kinds.parse_kind_line(line)
            if parsed and parsed[0] in ("tool", "env") and not not_failure(parsed[3]):
                kind, check, test, message = parsed
                self.faults(agent, step, kind.upper(), check, test if test != "-" else "", message.partition(" [")[0], result.run_folder)

    def fail(self, branch, result):
        """A branch named as at fault: its row, and its signature in the store of the same failure three times."""
        # A `worse` result (the speed measure) counts as a fail; its signature is the check and the worse figures' names, never
        # their values (kinds.worse_names), so the same figures worse three times are the same failure three times.
        found = [(c, m) for c, k, m in (result.failures or failures_of_lines(result.lines)) if k in ("fail", "FAIL", "worse", "WORSE")]
        counted = [(c, "FAIL", kinds.worse_names(m) if m.startswith("worse: ") else m) for c, m in found]
        first = counted[0] if counted else ("landing", "FAIL", result.why or "the checks failed")
        note = "%s: %s" % ((found[0][0], found[0][1]) if found else (first[0], first[2]))
        self.queue.finish(branch, FAILED, run_folder=result.run_folder, note=note)
        outcome = self.store.record(branch, counted or [first])
        self.say("gate.py: %s failed: %s%s" % (branch, note, (" (the same failure %d times in a row)" % outcome.count) if outcome.counted else ""))
        self.last[branch] = result

    def batch(self, names):
        """Lands what can land of `names`, names what fails. True when the run judged nothing (a tool or machine fault): the
        runner then stops, and the branches that were not decided wait again."""
        try:
            first = self.attempt(names)
            if first.kind in ("pass", "none"):
                return False
            if first.kind == "nj":
                return True
            if len(first.included) == 1:
                self.fail(first.included[0], first.result)
                return False
            S, U, res = list(first.included), [], first.result
            while S or U:
                if not S:
                    o = self.attempt(U)
                    if o.kind == "nj":
                        return True
                    if o.kind in ("pass", "none"):
                        U = []
                    else:
                        S, U, res = list(o.included), [], o.result
                    continue
                if len(S) == 1:
                    self.fail(S[0], res)
                    S = []
                    continue
                half = len(S) // 2
                head, rest = S[:half], S[half:]
                o = self.attempt(head)
                if o.kind == "nj":
                    return True
                if o.kind in ("pass", "none"):
                    S = rest
                else:
                    S, U, res = list(o.included), rest + U, o.result
            return False
        finally:
            # A branch no run decided waits again (a tool fault ended the batch).
            for b in names:
                row = self.queue.row(b)
                if row and row["state"] == RUNNING:
                    self.queue.requeue({b}, "its batch ended before it was run")


def _rank(code):
    """The exit codes in the order of how much they say: 3 (stop), 1 (fail), 2 (not judged), 0 (landed)."""
    return {EXIT_SAME_THREE: 3, EXIT_FAIL: 2, EXIT_NOT_JUDGED: 1, EXIT_LANDED: 0}[code]
