"""mod_tests.py: the mod system's tests in the gate (tests/mods). Python 3, standard library.

    mod_tests.py --renderer d3d12|vulkan|d3d11 --bin <folder> --work <folder> [--only suite[/test]] [--lock <file>]
                 [--jobs n] [--pak <file>]

The lumalink program and LumaLink's tree are the LumaLink commit in tools/lumacore/lumalink.commit: without LUMALINK,
that commit is fetched into a checkout of its own (--lumalink-cache, default ./lumalink-pin, kept between runs so the
build is incremental; the repository is LUMALINK_REPO, default github.com/keepvaibin/lumalink) and built there, and its
tests/game (the tests of LumaKit and LumaScript) run here too, with its own lumakit/ as the kit under test.
LUMALINK names another lumalink program instead (a LumaLink tree under work), with LUMALINK_TREE its checkout; without
LUMALINK_TREE, the checkout the program was built in (<checkout>/target/...); for a program built in none, only this
tree's tests run, and a row says so (not a pass).

The tests are copied into <work>/tree (never built in the source tree), every mod there with a source/ folder is built
with `lumalink build` for the platforms its luma.json's "code" names (else this system's), and every suite.json is run
by luma_test.py on the one renderer asked. A test whose test.json lists renderers runs only on those.

One row per test: suite/name, renderer, verdict, reason. A test with an expected.json (the harness's own tests) passes
when the runner answered as expected.json says (harness_self.py), so a test that must fail passes by failing. A test
whose expected.json has "twice_same_facts" is run twice and passes when both runs left the same facts (but the keys
its "except" lists: measured times).
Exit 0 only when every row is a pass.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import time

# Its neighbours by path: the gate runs Python isolated (-I), which leaves the script's own folder off the path.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import harness_self  # noqa: E402

TREE = pathlib.Path(__file__).resolve().parents[2]
TESTS = TREE / "tests" / "mods"
RUNNER = TREE / "tools" / "lumacore" / "luma_test.py"
# Files outside tests/mods that the tests' sources include, by their path in the tree.
INCLUDED = ["tools/test_driver/extension/luma_test_services.h"]
HOST_PLATFORM = {"Windows": "windows-x64", "Linux": "linux-x64"}.get(platform.system(), "windows-x64")
if platform.machine().lower() in ("arm64", "aarch64"):
    HOST_PLATFORM = HOST_PLATFORM.replace("x64", "arm64")


def copies(ll_tree):
    """(source folder, its place under <work>/tree): what is copied and built."""
    out = [(TESTS, "tests/mods")] + [(TREE / name, name) for name in INCLUDED]
    if ll_tree is not None:
        # LumaKit beside the tests' mods, where `lumalink build` looks first for a mod a mod needs (../<id>), and
        # where the tests name it (../../mods/lumakit); without it, the build would take this machine's installed kit.
        out += [(ll_tree / "tests" / "game", "lumalink/tests/game"), (ll_tree / "lumakit", "lumalink/tests/game/mods/lumakit")]
    return out


def stamp_of(lumalink: pathlib.Path, ll_tree, names: pathlib.Path):
    """What the built copy depends on: every file copied, the SDK, the game's self-description and the lumalink program."""
    h = hashlib.sha256()
    # This script too: how it builds is part of what was built.
    for source, place in copies(ll_tree) + [(TREE / "native" / "include", "sdk"), (names, "names"),
                                            (pathlib.Path(__file__).resolve(), "mod_tests.py")]:
        files = [source] if source.is_file() else sorted(p for p in source.rglob("*") if p.is_file())
        for p in files:
            h.update((place + "/" + p.relative_to(source).as_posix()).encode())
            h.update(p.read_bytes())
    h.update(lumalink.read_bytes())
    return h.hexdigest()


def mod_folders(root: pathlib.Path):
    """Every mod folder (luma.json beside source/) under the copy; tests/mods/src holds shared sources, no mods."""
    shared = root / "tests" / "mods" / "src"
    return sorted(p.parent for p in root.rglob("source") if p.is_dir() and (p.parent / "luma.json").is_file()
                  and shared not in p.parents)


def manifest_of(mod: pathlib.Path):
    try:
        m = json.loads((mod / "luma.json").read_text(encoding="utf-8"))
    except ValueError:
        return {}
    return m if isinstance(m, dict) else {}


def manifest_id(mod: pathlib.Path):
    return manifest_of(mod).get("id", mod.name)


def needed_ids(mod: pathlib.Path):
    return [n.get("id") for n in manifest_of(mod).get("needs", []) if isinstance(n, dict)]


def platforms_of(mod: pathlib.Path):
    try:
        manifest = json.loads((mod / "luma.json").read_text(encoding="utf-8"))
    except ValueError:
        return [HOST_PLATFORM]
    code = manifest.get("code") if isinstance(manifest, dict) else None
    platforms = code.get("platforms") if isinstance(code, dict) else None
    return list(platforms) if isinstance(platforms, list) and platforms else [HOST_PLATFORM]


def build_one(lumalink, mod, names, sdk, log_folder):
    cmd = [str(lumalink), "build", str(mod), "--names", str(names), "--sdk", str(sdk)]
    for p in platforms_of(mod):
        cmd += ["--platform", p]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        log = log_folder / (mod.relative_to(log_folder.parent / "tree").as_posix().replace("/", "_") + ".log")
        log.write_text(" ".join(cmd) + "\n" + r.stdout + r.stderr, encoding="utf-8")
        last = [line for line in (r.stdout + r.stderr).splitlines() if line.strip()]
        return mod, (last[-1] if last else "exit %d" % r.returncode)
    return mod, None


def prepare(args, lumalink, work, ll_tree):
    """The tests' copy in <work>/tree, every mod in it built; {mod folder: why its build failed}."""
    tree = work / "tree"
    names = pathlib.Path(args.bin) / "luma"
    if not (names / "luma_types.json").exists() and (pathlib.Path(args.bin) / "luma_types.json").exists():
        names = pathlib.Path(args.bin)
    stamp = stamp_of(lumalink, ll_tree, names)
    stamp_file = work / "tree.stamp"
    failed_file = work / "tree.failed.json"
    if stamp_file.exists() and stamp_file.read_text() == stamp and failed_file.exists():
        return tree, {pathlib.Path(k): v for k, v in json.loads(failed_file.read_text(encoding="utf-8")).items()}
    if tree.exists():
        shutil.rmtree(tree)
    for source, place in copies(ll_tree):
        (tree / place).parent.mkdir(parents=True, exist_ok=True)
        if source.is_file():
            shutil.copy2(source, tree / place)
        else:
            shutil.copytree(source, tree / place)
    logs = work / "build-logs"
    if logs.exists():
        shutil.rmtree(logs)
    logs.mkdir(parents=True)
    sdk = TREE / "native" / "include"
    mods = mod_folders(tree)
    failed = {}
    # In waves: a mod is built once every mod beside it that it needs is (its interface, code/<id>.lumi, is read
    # by the build). A mod needing one that failed fails without a build.
    beside = {(m.parent, manifest_id(m)): m for m in mods}
    needs = {m: [beside[(m.parent, n)] for n in needed_ids(m) if (m.parent, n) in beside] for m in mods}
    left = list(mods)
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        while left:
            for m in left:
                gone = [n for n in needs[m] if n in failed]
                if gone:
                    failed[m] = "needs %s, whose build failed" % gone[0].name
            left = [m for m in left if m not in failed]
            wave = [m for m in left if not any(n in left for n in needs[m])]
            if not wave:
                # Needs in a circle (a loader test of exactly that): built as they are, each without the others.
                wave = left
            for mod, why in pool.map(lambda m: build_one(lumalink, m, names, sdk, logs), wave):
                if why is not None:
                    failed[mod] = why
            left = [m for m in left if m not in wave]
    print("mod_tests: %d mods built, %d failed (logs in %s)" % (len(mods) - len(failed), len(failed), logs), flush=True)
    failed_file.write_text(json.dumps({str(k): v for k, v in failed.items()}, indent=1), encoding="utf-8")
    stamp_file.write_text(stamp)
    return tree, failed


def test_mods(folder: pathlib.Path, test: dict):
    out = []
    for m in test.get("mods", []):
        path = m.get("path") if isinstance(m, dict) else m
        if isinstance(path, str):
            out.append((folder / path).resolve())
    return out


def run_suite(args, suite_json, names, work, run_root, origin="", tag=""):
    """Runs the named tests of one suite (the copy's suite.json, rewritten to list them); the runner's exit code and log."""
    suite = suite_json.parent
    listed = suite / "suite.json"   # the runner takes a suite by that name; this is the copy's, rewritten each run
    listed.write_text(json.dumps({"harness": 1, "tests": names}, indent=1), encoding="utf-8")
    log = work / "logs" / ("%s%s-%s%s.log" % (origin, suite.name, args.renderer, tag))
    log.parent.mkdir(parents=True, exist_ok=True)
    cmd = [sys.executable, str(RUNNER), "run", str(listed), "--bin", args.bin, "--work", str(run_root),
           "--renderer", args.renderer, "--who", "mod_tests", "--commit", args.commit, "--lock", str(args.lock)]
    if args.pak:
        cmd += ["--pak", args.pak]
    with open(log, "w", encoding="utf-8") as out:
        r = subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT)
    return r.returncode, log


def read_result(run_root, suite, name, renderer):
    for leaf in (renderer, "-"):
        path = run_root / "runs" / suite / name / leaf / "result.json"
        if path.exists():
            return json.loads(path.read_text(encoding="utf-8")), path.parent
    return None, None


def facts_lines(folder, ignored=()):
    """The run's facts, line for line, without the keys `ignored` names (measured times, which no two runs share)."""
    path = folder / "facts.jsonl"
    if not path.exists():
        return None
    return [line for line in path.read_text(encoding="utf-8").splitlines() if json.loads(line).get("key") not in ignored]


def run_origin(args, prefix, copy, source, run_root, work, build_failed):
    """The rows of every suite under `copy` (this tree's tests/mods, or LumaLink's tests/game): each suite's tests as
    its suite.json in `source` lists them, run on args.renderer, judged."""
    rows = []
    for suite_json in sorted(copy.glob("*/suite.json")):
        suite = suite_json.parent
        label = prefix + suite.name
        if args.only and not any(o == label or o.startswith(label + "/") for o in args.only):
            continue
        listed = json.loads((source / suite.name / "suite.json").read_text(encoding="utf-8"))["tests"]
        to_run, once, twice = [], {}, []
        for name in listed:
            if args.only and label not in args.only and "%s/%s" % (label, name) not in args.only:
                continue
            folder = suite / name
            test = json.loads((folder / "test.json").read_text(encoding="utf-8"))
            if test.get("renderers") and args.renderer not in test["renderers"]:
                continue
            broken = [m for m in test_mods(folder, test) if m in build_failed]
            if broken:
                rows.append((label, name, "build-failed", "%s: %s" % (broken[0].name, build_failed[broken[0]])))
                continue
            expected = json.loads((folder / "expected.json").read_text(encoding="utf-8")) if (folder / "expected.json").exists() else None
            to_run.append(name)
            once[name] = expected
            if expected and expected.get("twice_same_facts"):
                twice.append(name)
        if not to_run:
            continue
        while args.wait_while and pathlib.Path(args.wait_while).exists():
            time.sleep(30)
        for name in to_run:
            shutil.rmtree(run_root / "runs" / suite.name / name, ignore_errors=True)
        code, log = run_suite(args, suite_json, to_run, work, run_root, prefix.replace("/", "-"))
        second = run_root / "second"
        if twice:
            for name in twice:
                shutil.rmtree(second / "runs" / suite.name / name, ignore_errors=True)
            run_suite(args, suite_json, twice, work, second, prefix.replace("/", "-"), tag="-second")
        for name in to_run:
            result, folder = read_result(run_root, suite.name, name, args.renderer)
            if result is None:
                rows.append((label, name, "no-result", "the runner left no result.json (log: %s)" % log))
                continue
            expected = once[name]
            if expected is None:
                rows.append((label, name, result["verdict"], result.get("reason", "")))
                continue
            problems = harness_self.judge(expected, result, folder, suite / name)
            if name in twice:
                twin = expected["twice_same_facts"]
                ignored = set(twin.get("except", [])) if isinstance(twin, dict) else set()
                again, folder2 = read_result(second, suite.name, name, args.renderer)
                if again is None or facts_lines(folder2) is None:
                    problems.append("the second run left no facts")
                elif facts_lines(folder, ignored) != facts_lines(folder2, ignored):
                    first, other = facts_lines(folder, ignored), facts_lines(folder2, ignored)
                    at = next((i for i, (a, b) in enumerate(zip(first, other)) if a != b), min(len(first), len(other)))
                    problems.append("the two runs' facts differ at line %d: %r / %r" % (at + 1, first[at:at + 1], other[at:at + 1]))
            if problems:
                rows.append((label, name, "fail", "the runner answered %s (%s); %s" %
                             (result["verdict"], result.get("reason", ""), "; ".join(problems))))
            else:
                rows.append((label, name, "pass", "the runner answered %s, as expected.json says" % result["verdict"]))
    return rows


LUMALINK_PIN = TREE / "tools" / "lumacore" / "lumalink.commit"


def built_in(program):
    """The LumaLink checkout a program was built in (<checkout>/target/<profile>/lumalink), whose tests/game it is
    tested with when LUMALINK_TREE is not given; None for a program built elsewhere."""
    for up in program.parents[:4]:
        if (up / "Cargo.toml").is_file() and (up / "tests" / "game").is_dir() and program.is_relative_to(up / "target"):
            return up
    return None


def pinned_lumalink(cache):
    """The lumalink program built from the pinned LumaLink commit in `cache` (fetched there when it is not), and the
    checkout: (program, checkout). The checkout is reset to the pin each time; its target/ stays for the next build."""
    pin = LUMALINK_PIN.read_text(encoding="utf-8").strip()
    if len(pin) != 40 or any(c not in "0123456789abcdef" for c in pin):
        raise SystemExit("%s holds no full commit: %r" % (LUMALINK_PIN, pin))
    repo = os.environ.get("LUMALINK_REPO", "https://github.com/keepvaibin/lumalink.git")

    def git(*a, check=True):
        r = subprocess.run(["git", "-C", str(cache), *a], capture_output=True, text=True)
        if check and r.returncode != 0:
            raise SystemExit("git %s in %s: %s" % (" ".join(a), cache, (r.stderr or r.stdout).strip()[-400:]))
        return r

    if not (cache / ".git").is_dir():
        cache.mkdir(parents=True, exist_ok=True)
        git("init", "-q")
        git("remote", "add", "origin", repo)
    if git("cat-file", "-e", pin + "^{commit}", check=False).returncode != 0:
        print("mod_tests: fetching LumaLink %s from %s" % (pin[:9], repo), flush=True)
        git("fetch", "-q", "--depth", "1", "origin", pin)
    # nebula/: LumaLink builds Nebula's validator and cooker from it (its README, "Building"); here it is a link to
    # this tree, so the pinned LumaLink checks mods as the tree under test does. The link goes before the clean, so
    # the clean never walks into this tree, and comes back after it.
    link = cache / "nebula"
    if link.is_symlink() or (hasattr(link, "is_junction") and link.is_junction()):
        os.rmdir(link) if os.name == "nt" else link.unlink()
    elif link.exists():
        raise SystemExit("%s is a folder, not a link to a Nebula tree: move it away" % link)
    git("checkout", "-q", "--force", "--detach", pin)
    git("clean", "-q", "-d", "-f", "-x", "-e", "target/")
    if os.name == "nt":
        r = subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(TREE)], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("linking %s to %s failed: %s" % (link, TREE, (r.stderr or r.stdout).strip()))
    else:
        link.symlink_to(TREE, target_is_directory=True)
    exe = cache / "target" / "release" / ("lumalink.exe" if os.name == "nt" else "lumalink")
    print("mod_tests: building LumaLink %s (cargo build --release -p lumalink)" % pin[:9], flush=True)
    r = subprocess.run(["cargo", "build", "--release", "--locked", "-p", "lumalink"], cwd=str(cache),
                       capture_output=True, text=True)
    if r.returncode != 0 or not exe.is_file():
        raise SystemExit("building the pinned LumaLink failed: %s" % (r.stderr or r.stdout).strip()[-800:])
    return exe, cache


def main():
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description="The mod system's tests, one renderer at a time.")
    parser.add_argument("--renderer", required=True, choices=("d3d12", "vulkan", "d3d11"))
    parser.add_argument("--bin", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--only", action="append", default=[], help="a suite, or suite/test (repeat for several)")
    parser.add_argument("--lock", help="the runner's run lock (default: <work>/run.lock)")
    parser.add_argument("--pak", help="the game.pak (default: the bin folder's pak.path)")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("--build-only", action="store_true", help="copy and build the test mods, run nothing")
    parser.add_argument("--wait-while", default=os.environ.get("LUMA_TEST_WAIT_WHILE"),
                        help="a path (or LUMA_TEST_WAIT_WHILE): no suite starts while it exists (another's measuring lock)")
    parser.add_argument("--lumalink-cache", default=os.environ.get("LUMALINK_CACHE", "lumalink-pin"),
                        help="the pinned LumaLink's checkout and build when LUMALINK is not given (or LUMALINK_CACHE)")
    parser.add_argument("--commit", help="the commit the results are recorded under (default: the tree's HEAD)")
    args = parser.parse_args()
    lumalink = os.environ.get("LUMALINK")
    if not lumalink:
        lumalink, ll_checkout = pinned_lumalink(pathlib.Path(args.lumalink_cache).resolve())
        os.environ["LUMALINK_TREE"] = str(ll_checkout)
    if not pathlib.Path(lumalink).is_file():
        raise SystemExit("LUMALINK names no file: %s" % lumalink)
    lumalink = pathlib.Path(lumalink).resolve()
    work = pathlib.Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    args.bin = str(pathlib.Path(args.bin).resolve())
    args.lock = args.lock or str(work / "run.lock")
    if not args.commit:
        r = subprocess.run(["git", "-C", str(TREE), "rev-parse", "--short=9", "HEAD"], capture_output=True, text=True)
        args.commit = r.stdout.strip() or "unknown"
    if not args.pak and (pathlib.Path(args.bin) / "pak.path").exists():
        args.pak = (pathlib.Path(args.bin) / "pak.path").read_text(encoding="utf-8").strip()
    if args.pak:
        os.environ["LUMA_PAK"] = args.pak
    os.environ["LUMALINK"] = str(lumalink)

    ll_tree = os.environ.get("LUMALINK_TREE")
    ll_tree = pathlib.Path(ll_tree).resolve() if ll_tree else built_in(lumalink)
    if ll_tree is not None and not (ll_tree / "tests" / "game").is_dir():
        raise SystemExit("LUMALINK_TREE has no tests/game: %s" % ll_tree)
    tree, build_failed = prepare(args, lumalink, work, ll_tree)
    if args.build_only:
        for mod, why in sorted(build_failed.items()):
            print("build-failed %s: %s" % (mod.relative_to(tree).as_posix(), why))
        return 0 if not build_failed else 1
    # (label prefix, the copy's suites, the source's suites, the runs' folder)
    origins = [("", tree / "tests" / "mods", TESTS, work / args.renderer)]
    rows = []
    if ll_tree is not None:
        origins.append(("lumalink/", tree / "lumalink" / "tests" / "game", ll_tree / "tests" / "game",
                        work / args.renderer / "lumalink"))
    elif not args.only or any(o.startswith("lumalink/") for o in args.only):
        rows.append(("lumalink", "-", "not-run", "no LUMALINK_TREE, and LUMALINK was built in no LumaLink checkout: LumaLink's tests/game did not run"))
    for prefix, copy, source, run_root in origins:
        rows += run_origin(args, prefix, copy, source, run_root, work, build_failed)
    for suite, name, verdict, reason in rows:
        print("%-40s %-7s %-14s %s" % ("%s/%s" % (suite, name), args.renderer, verdict, reason))
    passed = sum(1 for r in rows if r[2] == "pass")
    print("mod_tests: %s: %d of %d pass" % (args.renderer, passed, len(rows)))
    return 0 if rows and passed == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
