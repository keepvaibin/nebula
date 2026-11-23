"""units.py: the unit tests that `plan.py check` runs (AH, after the trial).

GitHub's Check runs the unit tests on Windows (the root project's ctest and the game's), on Linux (the same list, tests/unit, and again
under the address, undefined and thread sanitizers), and the Rust tests. A step whose own tests passed on this PC failed at GitHub six minutes
after `check` was silent (S0.2.6, galaxy_launch_args_tests), because `check` ran none of them. Now it runs every one that can run on this PC:

  the build, by the agent's own build command (only what changed is built; it waits by itself while a speed measure runs);
  ctest over the build's msvc folder (the Windows host's unit tests) and its game folder (the game's own unit tests);
  cargo test of nebula-import and luma-validate, as the gate runs it.

and names, as not proven here, what GitHub runs and this PC cannot: the unit tests that only the shared list has, and each of GitHub's
other jobs a landing waits for (Mac, Linux, the Linux unit tests, the sanitizers).

Every function that starts a process does it through `RUN`, which a self-test replaces. Python standard library only.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys

BUILD_SCRIPT = "tools/gate/build_windows.ps1"
CTEST_SCRIPT = "tools/gate/ctest.ps1"
SHARED_LIST = "cmake/unit_tests.cmake"
GITHUB_JOBS = "tools/gate/github_jobs.tsv"
RUST_TESTS = ("cargo", "test", "--release", "--locked", "--offline", "-p", "nebula-import", "-p", "luma-validate")
POWERSHELL = ("powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File")
# The jobs of GitHub's Check that a landing waits for (class must) and that this PC runs in the form it can: the Windows Rust tests.
RUN_HERE = ("Rust tests (windows-2022)",)
BUILD_WAITED = 3          # build_windows.ps1: a speed measure still ran after the minutes it waits; nothing was built
NOT_JUDGED = 2            # build_windows.ps1: only the compiler cache failed, twice

TEST_LINE = re.compile(r"Test\s+#\d+:\s+(\S+)\s+\.+\s*(.*?)\s+[\d.]+ sec")
SUMMARY = re.compile(r"(\d+)% tests passed, (\d+) tests? failed out of (\d+)")
RUST_RESULT = re.compile(r"^test result: \w+\. (\d+) passed; (\d+) failed")
GALAXY_TEST = re.compile(r"^\s*galaxy_test\(\s*([A-Za-z0-9_]+)", re.M)


def run(argv, cwd=None, env=None):
    """(exit code, output) of a process; the output is stdout and stderr together."""
    r = subprocess.run(list(argv), capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd, env=env)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


RUN = run                 # a self-test puts its own in here


class Outcome:
    """One part of the run: `ok`, `lines` (what to print: a pass line, or the lines of what failed), `ran` (the test names it ran)."""

    def __init__(self, ok, lines, ran=(), skipped=False):
        self.ok, self.lines, self.ran, self.skipped = ok, list(lines), set(ran), skipped


def tail_errors(text, limit=12):
    """The lines of a build's or test's output that say what failed: FAILED, error and fatal lines, else the last lines."""
    lines = [l.rstrip() for l in text.splitlines() if l.strip()]
    hits = [l for l in lines if re.search(r"FAILED|\berror\b|\bfatal\b|\bError\b", l)]
    return hits[:limit] if hits else lines[-limit:]


def build(folder, root):
    """The agent's build command (plan.py AGENT_COMMANDS "build"), whole: the game for Windows, the three programs and both game modules."""
    folder = pathlib.Path(folder).as_posix()
    root = pathlib.Path(root).as_posix()
    argv = list(POWERSHELL) + ["%s/%s" % (folder, BUILD_SCRIPT), "-Tree", folder, "-Root", root, "-Bin", root + "/bins"]
    code, text = RUN(argv)
    if code == 0:
        return Outcome(True, ["the build passed"])
    if code == BUILD_WAITED:
        return Outcome(False, ["the build did not start: a speed measure still ran after the minutes it waits; give `check` again later"], skipped=True)
    return Outcome(False, ["the build fails (exit %d):" % code] + ["  " + l for l in tail_errors(text)])


def ctest(folder, directory, what):
    """ctest over one build folder with the gate's script (tools/gate/ctest.ps1), every test: the names it ran, and the failed ones."""
    folder = pathlib.Path(folder).as_posix()
    argv = list(POWERSHELL) + ["%s/%s" % (folder, CTEST_SCRIPT), "-Dir", pathlib.Path(directory).as_posix()]
    code, text = RUN(argv)
    ran = {m.group(1) for m in TEST_LINE.finditer(text)}
    m = SUMMARY.search(text)
    if m is None or int(m.group(3)) == 0:
        return Outcome(False, ["%s: no test ran (%s)" % (what, (tail_errors(text, 3) or ["no output"])[-1].strip())], ran)
    failed, total = int(m.group(2)), int(m.group(3))
    if code == 0 and failed == 0:
        return Outcome(True, ["%s: %d of %d pass" % (what, total, total)], ran)
    lines = ["%s: %d of %d fail:" % (what, failed or 1, total)]
    block = None
    shown = 0
    for line in text.splitlines():
        if SUMMARY.search(line):
            block = None                         # ctest's own summary is not the failed test's output
        t = TEST_LINE.search(line)
        if t:
            block = t.group(1) if re.search(r"Fail|Timeout|Exception|Not Run", t.group(2)) else None
            if block:
                lines.append("  %s: %s" % (block, t.group(2).strip()))
                shown = 0
            continue
        if block and line.strip() and shown < 10:
            lines.append("    " + line.rstrip())
            shown += 1
    return Outcome(False, lines, ran)


def rust(folder, root):
    """cargo test of the two Rust packages GitHub's Rust job tests, the way the gate runs it (its own target folder under the build root)."""
    if shutil.which("cargo") is None:
        return Outcome(True, [], skipped=True)
    env = dict(os.environ, CARGO_TARGET_DIR=pathlib.Path(root, "cargo-test").as_posix())
    code, text = RUN(list(RUST_TESTS), cwd=str(folder), env=env)
    passed = failed = 0
    for line in text.splitlines():
        m = RUST_RESULT.match(line)
        if m:
            passed += int(m.group(1))
            failed += int(m.group(2))
    if passed + failed == 0:
        return Outcome(False, ["the Rust tests: no test ran (exit %d)" % code] + ["  " + l for l in tail_errors(text, 6)])
    if code == 0 and failed == 0:
        return Outcome(True, ["the Rust tests: %d pass" % passed])
    return Outcome(False, ["the Rust tests: %d fail, %d pass:" % (failed, passed)] + ["  " + l for l in tail_errors(text)])


def shared_names(folder):
    """The unit tests the shared list registers (cmake/unit_tests.cmake: what Windows, Linux and the Mac all build)."""
    path = pathlib.Path(folder) / SHARED_LIST
    return sorted(set(GALAXY_TEST.findall(path.read_text(encoding="utf-8")))) if path.is_file() else []


def github_jobs(folder):
    """[name] of GitHub's jobs a landing waits for (class must in tools/gate/github_jobs.tsv)."""
    path = pathlib.Path(folder) / GITHUB_JOBS
    out = []
    if path.is_file():
        for line in path.read_text(encoding="utf-8").splitlines():
            cells = line.split("\t")
            if len(cells) == 2 and cells[1].strip() == "must" and not line.startswith("#"):
                out.append(cells[0].strip())
    return out


def not_proven_here(folder, ran):
    """[line] naming what GitHub's Check runs and this PC could not: the unit tests of the shared list that did not run here (a name
    each), and each of GitHub's jobs a landing waits for that this PC does not run (Mac, Linux, ...)."""
    out = []
    missing = [n for n in shared_names(folder) if n not in ran]
    if missing:
        out.append("not proven here, GitHub's Linux and Mac unit tests build them and none ran on this PC: " + ", ".join(missing))
    jobs = [j for j in github_jobs(folder) if j not in RUN_HERE]
    if jobs:
        out.append("not proven here, GitHub's jobs this PC does not run: " + "; ".join(jobs))
    return out


def check(folder, root, build_first=True):
    """What `plan.py check` adds: (groups, lines). `groups`: one list of lines for each thing that fails (the build, a ctest run, the Rust
    tests); `lines`: what passed, and what is not proven here. A folder with no build script has nothing to run: one line."""
    folder = pathlib.Path(folder)
    if not (folder / BUILD_SCRIPT).is_file():
        return [], ["unit tests: not run (%s has no %s)" % (folder.as_posix(), BUILD_SCRIPT)]
    groups, lines, ran = [], [], set()

    def take(outcome):
        if outcome.ok:
            lines.extend(outcome.lines)
        else:
            groups.append(outcome.lines)
        ran.update(outcome.ran)

    if build_first:
        built = build(folder, root)
        take(built)
        if not built.ok:
            lines.append("the unit tests were not run: there is no build of this tree to run them on")
            return groups, lines + not_proven_here(folder, ran)
    else:
        lines.append("the build was not run (--no-build): the tests below are those of the build folder as it stands")
    take(ctest(folder, pathlib.Path(root) / "msvc", "the Windows unit tests"))
    take(ctest(folder, pathlib.Path(root) / "game", "the game's unit tests"))
    r = rust(folder, root)
    if r.skipped:
        lines.append("not proven here, GitHub's Rust job: there is no cargo on this PC")
    else:
        take(r)
    return groups, lines + not_proven_here(folder, ran)
