"""gate.py: the one check of a Nebula tree. Every check that starts the game for a test goes through here.

    python -X utf8 -I tools/gate/gate.py --quick [options]
    python -X utf8 -I tools/gate/gate.py --full  [options]

--quick: the Windows builds, the unit tests, the Rust tests, the mod tests on Direct3D 12 and twelve tour stops.
--full:  every check of docs/plan/00-READ-FIRST.md section 4.2 this computer can run; a check that does not exist yet
         or cannot run here is a failure, named as such, never a pass.

It writes one result (JSON and text, side by side) and exits 1 when any check failed, 0 when all passed, 2 when it
could not start (bad options, a lock held). Checks run one after another; each records its time and its log.
"""
import argparse
import datetime
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
sys.path.insert(0, str(HERE))
import gate_tour  # noqa: E402

# The twelve stops --quick plays: one per kind of stage (a hub, water, gravity, a boss, sand, ice, lava, a 2D stage,
# a race, the fleet, the heaviest stop of the tour, the factory). Names as tools/verify/tours/tour_enterable.txt
# lists them.
QUICK_STOPS = [
    "HoneyBeeKingdomGalaxy 1", "OceanRingGalaxy 1", "EggStarGalaxy 1", "KoopaBattleVs1Galaxy 1",
    "SandClockGalaxy 1", "IceVolcanoGalaxy 1", "HellProminenceGalaxy 1", "TeresaMario2DGalaxy 1",
    "SurfingLv1Galaxy 1", "CannonFleetGalaxy 1", "OceanPhantomCaveGalaxy 4", "FactoryGalaxy 1",
]
RENDERERS = ("d3d12", "vulkan", "d3d11")


class Gate:
    def __init__(self, args):
        self.args = args
        self.results = []
        self.started = time.time()
        self.commit = git("rev-parse", "--short", "HEAD")
        self.dirty = git("status", "--porcelain") != ""
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.name = "%s-%s-%s" % (stamp, self.commit, args.mode)
        self.work = pathlib.Path(args.work) / self.name
        self.work.mkdir(parents=True, exist_ok=True)

    def check(self, name, fn):
        """Runs one check. fn returns (passed, detail); an exception is a failure with its text."""
        t0 = time.time()
        log = self.work / ("%s.log" % name.replace("/", "-").replace(" ", "-"))
        try:
            passed, detail = fn(log)
        except Exception as e:  # noqa: BLE001 - every failure of a check is the check's result
            passed, detail = False, "%s: %s" % (type(e).__name__, e)
        row = {"check": name, "passed": bool(passed), "seconds": round(time.time() - t0, 1), "detail": detail,
               "log": str(log) if log.exists() else None}
        self.results.append(row)
        print("%-34s %-4s %7.1f s  %s" % (name, "pass" if passed else "FAIL", row["seconds"], detail), flush=True)
        return passed

    def missing(self, name, why):
        """A check the gate does not have yet, or that cannot run on this computer: a failure, said plainly."""
        self.check(name, lambda log: (False, "not run: " + why))

    def write(self):
        failed = [r for r in self.results if not r["passed"]]
        out = {"gate": 1, "mode": self.args.mode, "commit": self.commit, "dirty": self.dirty,
               "started": datetime.datetime.fromtimestamp(self.started).isoformat(timespec="seconds"),
               "seconds": round(time.time() - self.started, 1), "passed": not failed and bool(self.results),
               "checks": self.results}
        path = pathlib.Path(self.args.out) if self.args.out else self.work / "result.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
        lines = ["gate %s %s%s: %s (%d checks, %d failed, %.0f s)" % (
            self.args.mode, self.commit, " (uncommitted changes)" if self.dirty else "",
            "PASS" if out["passed"] else "FAIL", len(self.results), len(failed), out["seconds"])]
        lines += ["  %-34s %s  %s" % (r["check"], "pass" if r["passed"] else "FAIL", r["detail"]) for r in self.results]
        path.with_suffix(".txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        if self.args.record:
            # Kept with the tree (docs/plan/gate-runs): what the push workflow cannot run is proved by these.
            keep = TREE / "docs" / "plan" / "gate-runs" / self.name
            keep.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, keep.with_suffix(".json"))
            shutil.copyfile(path.with_suffix(".txt"), keep.with_suffix(".txt"))
            print("recorded:", keep.with_suffix(".txt"))
        print("\n".join(lines[:1]))
        print("result:", path)
        return out["passed"]


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


def build_windows(g):
    """The game modules (clang) and every Windows program with the unit tests and the test extension (MSVC), into the
    build root; then the bin folder the harness runs."""
    def fn(log):
        seed = ["-SeedFrom", g.args.seed_from] if g.args.seed_from else []
        code = run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", HERE / "build_windows.ps1",
                    "-Tree", TREE, "-Root", g.args.build_root, "-Bin", g.bin, "-Pak", g.args.pak, "-Jobs", g.args.jobs,
                    *seed], log)
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        fails = [l for l in text.splitlines() if l.startswith("FAILED:")]
        if code != 0 or fails:
            return False, "exit %d, %d FAILED: %s" % (code, len(fails), fails[0][8:120] if fails else last_line(text))
        return True, "bins at %s" % g.bin
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
                       *(TREE / "tools" / "test_driver" / "tests").glob("*_test.py")])
# The game description's tools on their fixtures, with the game build's compile commands.
DESCRIPTION_SELF = sorted((TREE / "tools" / "lumacore").glob("*_fixture_test.py"))


def harness(g, suite, renderer, label):
    """One harness suite on one renderer. Passes when every test in it passed."""
    def fn(log):
        work = g.work / ("runs-%s-%s" % (label, renderer))
        code = run([sys.executable, "-X", "utf8", "-I", TREE / "tools/lumacore/luma_test.py", "run", suite,
                    "--bin", g.bin, "--work", work, "--who", "gate", "--lock", g.work / "harness.lock",
                    "--commit", g.commit, "--renderer", renderer], log, cwd=g.args.build_root)
        # The run's copies of the programs are not evidence: removed so the drive does not fill.
        for d in work.rglob("bin"):
            if d.is_dir():
                shutil.rmtree(d, ignore_errors=True)
        detail = summarize(log, renderer)
        return code == 0 and not detail.startswith("0 of 0") and "; not:" not in detail, detail
    return fn


MOD_TESTS = TREE / "tools" / "lumacore" / "mod_tests.py"


def mod_tests(g, renderer):
    """Every mod test in tests/mods on one renderer, through the mods' own entry point (it builds the test mods with
    LumaLink, then runs them). Passes when it exits 0; its rows name each test that did not pass."""
    def fn(log):
        work = g.work / ("runs-mods-%s" % renderer)
        env = {"LUMALINK": g.args.lumalink} if g.args.lumalink else {}
        env["LUMA_TEST_PAKS"] = g.args.paks
        code = run([sys.executable, "-X", "utf8", "-I", MOD_TESTS, "--bin", g.bin, "--work", work,
                    "--renderer", renderer, "--lock", g.work / "harness.lock", "--commit", g.commit], log, env=env,
                   cwd=g.args.build_root)
        for d in work.rglob("bin"):
            if d.is_dir():
                shutil.rmtree(d, ignore_errors=True)
        return code == 0, summarize(log, renderer)
    return fn


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


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--quick", dest="mode", action="store_const", const="quick")
    mode.add_argument("--full", dest="mode", action="store_const", const="full")
    p.add_argument("--build-root", default=os.environ.get("NEBULA_GATE_BUILD", "D:/NebulaWork/build/gate-%s" % TREE.name),
                   help="where the builds go (kept between runs, so builds are incremental)")
    p.add_argument("--pak", default=os.environ.get("NEBULA_GATE_PAK", "D:/NebulaWork/build/all1-pak-34/game.pak"))
    p.add_argument("--paks", default=os.environ.get("NEBULA_GATE_PAKS", os.pathsep.join((
                   "RMGP01=D:/NebulaWork/build/a2-pak-pal/game.pak", "RMGJ01=D:/NebulaWork/build/a2-pak-jp/game.pak"))),
                   help="the other discs' game.paks, DISC=path joined by the path separator: the tests with a \"disc\"")
    p.add_argument("--work", default=os.environ.get("NEBULA_GATE_WORK", "D:/NebulaWork/build/gate-runs"))
    p.add_argument("--out", help="the result file (default: result.json in the run's work folder)")
    p.add_argument("--jobs", default=str(max(4, (os.cpu_count() or 8) - 4)))
    p.add_argument("--no-build", action="store_true", help="use the bins already in the build root")
    p.add_argument("--record", action="store_true", help="also keep the result in docs/plan/gate-runs, for a commit")
    p.add_argument("--seed-from", default=os.environ.get("NEBULA_GATE_SEED", ""),
                   help="a configured MSVC build folder whose compiled shader stages seed a new build (content-keyed)")
    p.add_argument("--lumalink", default=os.environ.get("LUMALINK", ""), help="the lumalink program the mod tests use")
    args = p.parse_args()

    g = Gate(args)
    g.bin = str(pathlib.Path(args.build_root) / "bins")
    renderers = ("d3d12",) if args.mode == "quick" else RENDERERS
    g.check("repo/audit", repo_audit(g))
    if not args.no_build:
        if not g.check("build/windows", build_windows(g)):
            return 0 if g.write() else 1
    g.check("unit/windows", unit_tests(g))
    g.check("unit/game", game_tests(g))
    for r in RENDERERS:
        g.check("start/%s" % r, start_check(g, r))
    g.check("rust", rust_tests(g))
    g.check("harness/self", scripts(g, HARNESS_SELF, ("--work", "{out}")))
    commands = pathlib.Path(args.build_root) / "game" / "compile_commands.json"
    g.check("description/self", scripts(g, DESCRIPTION_SELF, ("--commands", str(commands), "--out", "{out}")))

    if not MOD_TESTS.exists():
        g.missing("mods", "the mod tests are not in the tree yet (tools/lumacore/mod_tests.py, mods M0)")
    else:
        for r in renderers:
            g.check("mods/%s" % r, mod_tests(g, r))

    stops = QUICK_STOPS if args.mode == "quick" else None
    tour = gate_tour.write(g.work / "tour", stops)
    for r in renderers:
        g.check("tour/%s" % r, harness(g, tour, r, "tour"))

    if args.mode == "full":
        for name, why in (
            ("build/android", "the Android build is not in the gate yet (E0.3)"),
            ("build/linux", "runs in the push workflow only (E0.3); no Linux on this computer"),
            ("build/macos", "runs in the push workflow only (E0.3)"),
            ("unit/linux", "runs in the push workflow only (E0.3, E0.4)"),
            ("unit/android", "runs in the push workflow only: JUnit is not on this computer (board ask, E0.4)"),
            ("mods/android", "the Android runner is not in the gate yet (E0.13)"),
            ("tour/tablet", "the Android runner is not in the gate yet (E0.13)"),
            ("pictures", "approved pictures not in the gate yet (E0.7)"),
            ("replays", "the step-hash replay not in the gate yet (E0.10)"),
            ("hostile-files", "the bad-file corpus is not in the gate yet (mods M6)"),
            ("two-mods", "the two-mod tests are not in the gate yet (mods M2)"),
            ("sanitizers", "sanitizer builds not in the gate yet (E0.14)"),
            ("soak", "the soak run is not in the gate yet (E0.14)"),
        ):
            g.missing(name, why)
    return 0 if g.write() else 1


if __name__ == "__main__":
    sys.exit(main())
