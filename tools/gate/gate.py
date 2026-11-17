"""gate.py: the one check of a Nebula tree. Every check that starts the game for a test goes through here.

    python -X utf8 -I tools/gate/gate.py --quick [options]
    python -X utf8 -I tools/gate/gate.py --full  [options]

--quick: the Windows builds, the unit tests, the Rust tests, the mod tests, the engine tests (tests/engine) and twelve tour stops on Direct3D 12.
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
sys.path.insert(0, str(TREE / "tools" / "verify"))
import gate_tour  # noqa: E402
import step_hash  # noqa: E402

# The twelve stops --quick plays: one per kind of stage (a hub, water, gravity, a boss, sand, ice, lava, a 2D stage,
# a race, the fleet, the heaviest stop of the tour, the factory). Names as tools/verify/tours/tour_enterable.txt
# lists them.
QUICK_STOPS = [
    "HoneyBeeKingdomGalaxy 1", "OceanRingGalaxy 1", "EggStarGalaxy 1", "KoopaBattleVs1Galaxy 1",
    "SandClockGalaxy 1", "IceVolcanoGalaxy 1", "HellProminenceGalaxy 1", "TeresaMario2DGalaxy 1",
    "SurfingLv1Galaxy 1", "CannonFleetGalaxy 1", "OceanPhantomCaveGalaxy 4", "FactoryGalaxy 1",
]
# The other regions' discs: their tours run on Direct3D 12 only, since a region changes the game's data (text,
# layouts, fonts), not what a renderer does with it. --quick plays two of their stops (a hub and the heaviest);
# --full all of them.
REGION_DISCS = ("RMGP01", "RMGJ01")
QUICK_REGION_STOPS = ["HoneyBeeKingdomGalaxy 1", "OceanPhantomCaveGalaxy 4"]
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


def parse_scripts(g):
    """Every tracked PowerShell and Python script parses (tools/gate/parse_scripts.py): a script that does not fails
    only when someone runs it."""
    def fn(log):
        code = run([sys.executable, "-X", "utf8", "-I", HERE / "parse_scripts.py", "--tree", TREE], log)
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
                       *(TREE / "tools" / "test_driver" / "tests").glob("*_test.py"),
                       *(TREE / "tools" / "gate" / "tests").glob("*_test.py")])
# The game description's tools on their fixtures, with the game build's compile commands.
DESCRIPTION_SELF = sorted((TREE / "tools" / "lumacore").glob("*_fixture_test.py"))


def harness(g, suite, renderer, label, device=None):
    """One harness suite on one renderer, on this computer or on an Android device (its serial; the test app
    prepared and the device's lock held by the gate). Passes when every test in it passed (a test whose disc has no
    pak is not-run, which is not a pass)."""
    def fn(log):
        work = g.work / ("runs-%s-%s" % (label, renderer))
        on_device = ["--device", device, "--bin", g.android_bin] if device else ["--bin", g.bin]
        code = run([sys.executable, "-X", "utf8", "-I", TREE / "tools/lumacore/luma_test.py", "run", suite]
                   + on_device + ["--work", work, "--who", "gate", "--lock", g.work / "harness.lock",
                                  "--commit", g.commit, "--renderer", renderer], log, env={"LUMA_TEST_PAKS": g.args.paks},
                   cwd=g.args.build_root)
        # The run's copies of the programs are not evidence: removed so the drive does not fill.
        for d in work.rglob("bin"):
            if d.is_dir():
                shutil.rmtree(d, ignore_errors=True)
        detail = summarize(log, renderer)
        return code == 0 and not detail.startswith("0 of 0") and "; not:" not in detail, detail
    return fn


def replays(g, label, renderer, against=None):
    """The step-hash replay (tools/verify/step_hash.py check): a tour's runs, hashed at every step, against the
    recorded stops (tests/replays), or against another tour's runs (`against`: (label, renderer)) of the same tests.
    The quick gate plays only some recorded stops."""
    def fn(log):
        work = g.work / ("runs-%s-%s" % (label, renderer))
        other = None if against is None else g.work / ("runs-%s-%s" % against)
        ok, lines = step_hash.check(work, other, some=g.args.mode == "quick")
        pathlib.Path(log).write_text("\n".join(lines) + "\n", encoding="utf-8")
        return ok, lines[-1]
    return fn


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
    --full also fails on an approved picture its runs did not take."""
    def fn(log):
        work = g.work / ("runs-%s-%s" % (label, renderer))
        code = run([sys.executable, "-X", "utf8", "-I", TREE / "tools" / "gate" / "pictures.py", "check", "--runs", work,
                    "--renderer", renderer] + (["--complete"] if g.args.mode == "full" else []), log, cwd=TREE)
        lines = pathlib.Path(log).read_text(encoding="utf-8", errors="replace").splitlines()
        summary = next((l for l in reversed(lines) if l.startswith("pictures: ")), "no summary")
        others = [l.split()[0] + " " + l.split()[1] for l in lines if l.split() and l.split()[0] in ("changed", "new", "missing", "machine")]
        review = next((l for l in lines if l.startswith("review: ")), "")
        detail = summary[len("pictures: "):] + ("; not: " + ", ".join(others[:8]) + (" ..." if len(others) > 8 else "") if others else "")
        return code == 0, detail + ("; " + review if review else "")
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
    p.add_argument("--pak", default=os.environ.get("NEBULA_GATE_PAK", "D:/NebulaWork/build/all1-pak-35/game.pak"))
    p.add_argument("--paks", default=os.environ.get("NEBULA_GATE_PAKS", os.pathsep.join((
                   "RMGP01=D:/NebulaWork/build/a1-pak-pal-35/game.pak", "RMGJ01=D:/NebulaWork/build/a1-pak-jp-35/game.pak"))),
                   help="the other discs' game.paks, DISC=path joined by the path separator: the tests with a \"disc\"")
    p.add_argument("--work", default=os.environ.get("NEBULA_GATE_WORK", "D:/NebulaWork/build/gate-runs"))
    p.add_argument("--out", help="the result file (default: result.json in the run's work folder)")
    p.add_argument("--jobs", default=str(max(4, (os.cpu_count() or 8) - 4)))
    p.add_argument("--no-build", action="store_true", help="use the bins already in the build root")
    p.add_argument("--record", action="store_true", help="also keep the result in docs/plan/gate-runs, for a commit")
    p.add_argument("--seed-from", default=os.environ.get("NEBULA_GATE_SEED", ""),
                   help="a configured MSVC build folder whose compiled shader stages seed a new build (content-keyed)")
    p.add_argument("--lumalink", default=os.environ.get("LUMALINK", ""), help="the lumalink program the mod tests use")
    p.add_argument("--tablet", default=os.environ.get("NEBULA_GATE_TABLET", ""),
                   help="--full: an Android device's serial: the test app goes on it, tests/android and the tour run "
                        "there, and the test app is removed again")
    p.add_argument("--tablet-lock", default=os.environ.get("NEBULA_GATE_TABLET_LOCK", "D:/NebulaWork/plan/tablet.lock"))
    p.add_argument("--android-deps", default=os.environ.get("NEBULA_GATE_ANDROID_DEPS", "D:/NebulaWork/deps"),
                   help="build_apk.ps1 -Deps: SDL, Vulkan-Headers, volk, VMA and glslang's sources")
    args = p.parse_args()

    g = Gate(args)
    g.bin = str(pathlib.Path(args.build_root) / "bins")
    renderers = ("d3d12",) if args.mode == "quick" else RENDERERS
    g.check("repo/audit", repo_audit(g))
    g.check("repo/scripts", parse_scripts(g))
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

    # The engine's own tests (tests/engine): start, device loss, window shapes and sizes, stage loads, save files.
    for r in renderers:
        g.check("engine/%s" % r, harness(g, TREE / "tests" / "engine" / "suite.json", r, "engine"))
        g.check("pictures/engine/%s" % r, pictures(g, "engine", r))

    stops = QUICK_STOPS if args.mode == "quick" else None
    tour = gate_tour.write(g.work / "tour", stops)
    step_hash.with_hash(tour.parent)
    for r in renderers:
        g.check("tour/%s" % r, harness(g, tour, r, "tour"))
        g.check("pictures/tour/%s" % r, pictures(g, "tour", r))
        # The game's steps: Direct3D 12's against the recordings; another renderer's against Direct3D 12's.
        g.check("replays/%s" % r, replays(g, "tour", r, None if r == "d3d12" else ("tour", "d3d12")))
    region_stops = QUICK_REGION_STOPS if args.mode == "quick" else None
    for disc in REGION_DISCS:
        region = gate_tour.write(g.work / ("tour-%s" % disc), region_stops, disc)
        g.check("tour-%s/d3d12" % disc, harness(g, region, "d3d12", "tour-%s" % disc))
        g.check("pictures/tour-%s/d3d12" % disc, pictures(g, "tour-%s" % disc, "d3d12"))

    if args.mode == "full":
        # The Android test app, and on a named device its tests and the tour (Vulkan, the device's renderer).
        g.android_bin = str(pathlib.Path(args.build_root) / "android" / "test-bin-arm64-v8a")
        built = args.no_build or g.check("build/android", build_android(g))
        if not args.tablet:
            for name in ("android/tablet", "tour/tablet"):
                g.missing(name, "no device named (--tablet SERIAL)")
        elif not built:
            for name in ("android/tablet", "tour/tablet"):
                g.missing(name, "the Android build failed")
        else:
            held = take_device_lock(args.tablet_lock, "gate %s" % g.name)
            if held is not None:
                for name in ("android/tablet", "tour/tablet"):
                    g.missing(name, "the device's lock %s is held: %s" % (args.tablet_lock, held))
            else:
                try:
                    if g.check("tablet/prepare", device_step(g, "prepare")):
                        g.check("android/tablet", harness(g, TREE / "tests" / "android" / "suite.json", "vulkan",
                                                          "android", args.tablet))
                        tablet_tour = gate_tour.write(g.work / "tour-tablet", None)
                        step_hash.with_hash(tablet_tour.parent)
                        g.check("tour/tablet", harness(g, tablet_tour, "vulkan", "tour-tablet", args.tablet))
                        g.check("replays/tablet", replays(g, "tour-tablet", "vulkan", ("tour", "d3d12")))
                finally:
                    g.check("tablet/remove", device_step(g, "remove"))
                    shutil.rmtree(args.tablet_lock, ignore_errors=True)
        g.missing("pictures/tablet", "approved pictures are kept per renderer for one adapter; a device's own set is "
                                     "not in pictures.py yet")
        for name, why in (
            ("build/linux", "runs in the push workflow only (E0.3); no Linux on this computer"),
            ("build/macos", "runs in the push workflow only (E0.3)"),
            ("unit/linux", "runs in the push workflow only (E0.3, E0.4)"),
            ("mods/android", "the mod tests on a device are not in the gate yet (mods M0, tools/lumacore/mod_tests.py)"),
            ("hostile-files", "the bad-file corpus is not in the gate yet (mods M6)"),
            ("two-mods", "the two-mod tests are not in the gate yet (mods M2)"),
            ("sanitizers", "sanitizer builds not in the gate yet (E0.14)"),
            ("soak", "the soak run is not in the gate yet (E0.14)"),
        ):
            g.missing(name, why)
    return 0 if g.write() else 1


if __name__ == "__main__":
    sys.exit(main())
