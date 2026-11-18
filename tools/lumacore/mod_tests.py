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
# Beside each suite.json of the copy, its list as copied: run_suite rewrites suite.json to the tests one run takes (the
# runner reads a suite by that name), so a later run of the same copy reads what the suite holds from here.
COPIED = "suite.copied.json"
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
    # What is built depends on the selection too: a run of some tests builds only their mods.
    stamp = stamp_of(lumalink, ll_tree, names) + ("" if not args.only else " only " + " ".join(sorted(args.only)))
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
    for listed in tree.rglob("suite.json"):
        shutil.copy2(listed, listed.with_name(COPIED))
    logs = work / "build-logs"
    if logs.exists():
        shutil.rmtree(logs)
    logs.mkdir(parents=True)
    sdk = TREE / "native" / "include"
    mods = mod_folders(tree)
    if args.only:
        wanted = selected_mods(tree, args.only, mods)
        mods = [m for m in mods if m in wanted]
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


def selected_mods(tree, only, mods):
    """The mods the selected tests start (with the tests they continue from) and every mod beside them they need,
    directly or through others: what a run of some tests builds."""
    beside = {(m.parent, manifest_id(m)): m for m in mods}
    todo = []
    for prefix, copy in (("", tree / "tests" / "mods"), ("lumalink/", tree / "lumalink" / "tests" / "game")):
        for suite_json in sorted(copy.glob("*/suite.json")):
            label = prefix + suite_json.parent.name
            listed = json.loads((suite_json.parent / COPIED).read_text(encoding="utf-8"))["tests"]
            names = [n for n in listed if label in only or "%s/%s" % (label, n) in only]
            seen = set()
            while names:
                name = names.pop()
                folder = suite_json.parent / name
                if name in seen or not (folder / "test.json").is_file():
                    continue
                seen.add(name)
                test = json.loads((folder / "test.json").read_text(encoding="utf-8"))
                todo += test_mods(folder, test)
                if isinstance(test.get("continue_from"), str):
                    names.append(test["continue_from"])
    wanted = set()
    while todo:
        mod = todo.pop()
        if mod in wanted or not (mod / "luma.json").is_file():
            continue
        wanted.add(mod)
        todo += [beside[(mod.parent, n)] for n in needed_ids(mod) if (mod.parent, n) in beside]
    return wanted


def test_mods(folder: pathlib.Path, test: dict):
    out = []
    for m in test.get("mods", []):
        path = m.get("path") if isinstance(m, dict) else m
        if isinstance(path, str):
            out.append((folder / path).resolve())
    return out


def run_unit(args, unit, work):
    """One runner on some tests of one suite (luma_test.py run <the copy's suite.json> --test ...), with its own lock
    and log: the runner's exit code and log. Units run side by side; each test has its own run folder."""
    log = work / "logs" / ("%s%s-%s-%d%s.log" % (unit["origin"], unit["suite"].name, args.renderer, unit["index"], unit["tag"]))
    lock = work / "locks" / log.with_suffix(".lock").name
    log.parent.mkdir(parents=True, exist_ok=True)
    lock.parent.mkdir(parents=True, exist_ok=True)
    cmd = [sys.executable, str(RUNNER), "run", str(unit["suite"] / "suite.json"), "--bin", args.bin,
           "--work", str(unit["run_root"]), "--renderer", args.renderer, "--who", "mod_tests", "--commit", args.commit,
           "--lock", str(lock)]
    for name in unit["tests"]:
        cmd += ["--test", name]
    if args.pak:
        cmd += ["--pak", args.pak]
    while args.wait_while and pathlib.Path(args.wait_while).exists():
        time.sleep(30)
    with open(log, "w", encoding="utf-8") as out:
        r = subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT)
    return r.returncode, log


def run_units(args, units, work):
    """Every unit, `args.parallel` at a time, the longest first (by the seconds the last run of its tests took,
    <work>/times.json; a test never timed counts 10 s). Returns {id(unit): log}."""
    if not units:
        return {}
    times_file = work / "times.json"
    try:
        times = json.loads(times_file.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        times = {}
    for unit in units:
        unit["cost"] = sum(times.get("%s%s/%s" % (unit["origin"], unit["suite"].name, t), 10.0) for t in unit["tests"])
    logs = {}
    with concurrent.futures.ThreadPoolExecutor(max(1, args.parallel)) as pool:
        running = {pool.submit(run_unit, args, u, work): u for u in sorted(units, key=lambda u: -u["cost"])}
        for done in concurrent.futures.as_completed(running):
            logs[id(running[done])] = done.result()[1]
    for unit in units:
        if unit["tag"]:
            continue
        for name in unit["tests"]:
            result, _ = read_result(unit["run_root"], unit["suite"].name, name, args.renderer)
            if result is not None and isinstance(result.get("seconds"), (int, float)):
                times["%s%s/%s" % (unit["origin"], unit["suite"].name, name)] = result["seconds"]
    times_file.write_text(json.dumps(times, indent=1, sort_keys=True), encoding="utf-8")
    return logs


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


def chains(names, tests):
    """The suite's tests (in its order) as the units that each run in one runner: a test continuing from another
    (continue_from) runs after it in the same runner, which keeps a passed test's folder for the next; every other
    test is a unit of its own."""
    group = {name: name for name in names}

    def root(name):
        while group[name] != name:
            name = group[name]
        return name

    for name in names:
        before = tests[name].get("continue_from")
        if isinstance(before, str) and before in group:
            group[root(name)] = root(before)
    units = {}
    for name in names:
        units.setdefault(root(name), []).append(name)
    return list(units.values())


def plan_origin(args, prefix, copy, run_root, build_failed):
    """What one origin's suites run: (rows already known, units, the tests to judge). A test the selection takes
    brings the tests it continues from. Each test's run folder is emptied first."""
    rows, units, judged = [], [], []
    for suite_json in sorted(copy.glob("*/suite.json")):
        suite = suite_json.parent
        label = prefix + suite.name
        listed = json.loads((suite / COPIED).read_text(encoding="utf-8"))["tests"]
        tests = {name: json.loads((suite / name / "test.json").read_text(encoding="utf-8")) for name in listed}
        chosen = [name for name in listed if not args.only or label in args.only or "%s/%s" % (label, name) in args.only]
        wanted = set()
        for name in chosen:
            while isinstance(name, str) and name in tests and name not in wanted:
                wanted.add(name)
                name = tests[name].get("continue_from")
        to_run, once, twice = [], {}, []
        for name in listed:
            if name not in wanted:
                continue
            folder = suite / name
            test = tests[name]
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
        for name in to_run:
            shutil.rmtree(run_root / "runs" / suite.name / name, ignore_errors=True)
        second = run_root / "second"
        for name in twice:
            shutil.rmtree(second / "runs" / suite.name / name, ignore_errors=True)
        for index, chain in enumerate(chains(to_run, tests)):
            units.append({"origin": prefix.replace("/", "-"), "suite": suite, "tests": chain, "run_root": run_root,
                          "index": index, "tag": ""})
            if any(name in twice for name in chain):
                units.append({"origin": prefix.replace("/", "-"), "suite": suite, "tests": chain, "run_root": second,
                              "index": index, "tag": "-second"})
        judged += [(label, suite, name, once[name], name in twice) for name in to_run]
    return rows, units, judged


def judge(args, judged, run_root, logs_by_suite):
    """The rows of the tests run: the runner's verdict, or for a harness test its expected.json's (harness_self)."""
    rows = []
    second = run_root / "second"
    for label, suite, name, expected, twice in judged:
        result, folder = read_result(run_root, suite.name, name, args.renderer)
        if result is None:
            rows.append((label, name, "no-result", "the runner left no result.json (log: %s)" % logs_by_suite.get(suite, "?")))
            continue
        if expected is None:
            rows.append((label, name, result["verdict"], result.get("reason", "")))
            continue
        problems = harness_self.judge(expected, result, folder, suite / name)
        if twice:
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


def run_origin(args, prefix, copy, run_root, work, build_failed):
    """The rows of every suite under `copy` (the copy of this tree's tests/mods, or of LumaLink's tests/game): each
    suite's tests as the copy lists them (COPIED: its suite.json as copied), run on args.renderer, judged. The
    checkouts are not read again: a test added there while a run goes on is in the next run's copy. main() plans
    every origin first and runs all their units in one pool; this is the same for one origin."""
    rows, units, judged = plan_origin(args, prefix, copy, run_root, build_failed)
    logs = run_units(args, units, work)
    by_suite = {u["suite"]: logs.get(id(u)) for u in units}
    return rows + judge(args, judged, run_root, by_suite)


QUICK = TESTS / "quick.json"
# The label prefixes of the origins other than this tree's tests/mods.
ORIGINS = ("lumalink/",)


def quick_set(copies):
    """--set quick: tests/mods/quick.json's tests, which take one or more of every suite of the copies given
    ((label prefix, the copy's suites) pairs). Returns (selection, problems): a suite with none, or a name that is not
    a test, is a problem, so the set cannot fall behind the suites."""
    listed = json.loads(QUICK.read_text(encoding="utf-8"))["tests"]
    problems = []
    known = {}
    for prefix, copy in copies:
        for suite_json in sorted(copy.glob("*/suite.json")):
            known[prefix + suite_json.parent.name] = json.loads(suite_json.read_text(encoding="utf-8"))["tests"]
    given = {prefix for prefix, _ in copies}
    for entry in listed:
        label, _, name = entry.rpartition("/")
        if any(entry.startswith(prefix) and prefix not in given for prefix in ORIGINS):
            continue  # LumaLink's suites, judged when its tests/game is given
        if label not in known:
            problems.append("%s: no such suite" % entry)
        elif label in known and name not in known[label]:
            problems.append("%s: no such test in %s" % (entry, label))
    for label in sorted(known):
        if not any(entry.rpartition("/")[0] == label for entry in listed):
            problems.append("%s: no test of it in the quick set" % label)
    return listed, problems


LUMALINK_PIN = TREE / "tools" / "lumacore" / "lumalink.commit"


def built_in(program):
    """The LumaLink checkout a program was built in (<checkout>/target/<profile>/lumalink), whose tests/game it is
    tested with when LUMALINK_TREE is not given; None for a program built elsewhere."""
    for up in program.parents[:4]:
        if (up / "Cargo.toml").is_file() and (up / "tests" / "game").is_dir() and program.is_relative_to(up / "target"):
            return up
    return None


# The tree's paths the pinned LumaLink builds from (its nebula/): Nebula's workspace files, the two crates, the text
# table luma-validate's build script reads, and what nebula-import compiles in (include_bytes and include_str: the
# placeholder, the shader headers and program table, the store version). A build that reads any other tree path
# fails (check_nebula_inputs; a file missing from the copy fails the compile, which names it).
NEBULA_INPUTS = ("Cargo.toml", "Cargo.lock", "crates/nebula-import", "crates/luma-validate", "localization/NebulaText.tsv",
                 "data/placeholder", "runtime/include/galaxy/store_version.inc", "runtime/src/render/materials",
                 "runtime/src/render/programs/table.tsv")


def sync_by_content(tree, folder, paths=NEBULA_INPUTS):
    """Makes `folder` hold `paths` of `tree` (files or folders, recursively), writing a file only when its bytes differ
    and removing what the tree no longer has, so a file's time changes only with its content (cargo's freshness).
    Returns the number of files written or removed."""
    import shutil
    changed = 0
    wanted = set()
    for rel in paths:
        source = tree / rel
        items = [source] if source.is_file() else sorted(f for f in source.rglob("*") if f.is_file()) if source.is_dir() else []
        for f in items:
            if "target" in f.relative_to(tree).parts:
                continue
            r = f.relative_to(tree)
            wanted.add(r)
            dest = folder / r
            data = f.read_bytes()
            if dest.is_file() and dest.read_bytes() == data:
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            changed += 1
    if folder.is_dir():
        for f in sorted(folder.rglob("*")):
            r = f.relative_to(folder)
            if f.is_file() and r not in wanted and "target" not in r.parts:
                f.unlink()
                changed += 1
    for d in sorted((d for d in folder.rglob("*") if d.is_dir()), reverse=True) if folder.is_dir() else []:
        if "target" not in d.relative_to(folder).parts and not any(d.iterdir()):
            shutil.rmtree(d, ignore_errors=True)
    return changed


def check_nebula_inputs(cache, paths=NEBULA_INPUTS):
    """The tree paths the last build read (dep-info and build scripts' rerun-if-changed), each inside `paths`: None, or
    the first one outside them (the copy would then go stale where the tree changes)."""
    import os.path
    import re
    target = cache / "target" / "release"
    nebula = os.path.normcase(os.path.abspath(cache / "nebula"))
    read = set()
    for f in list(target.glob("deps/*.d")) + list(target.glob("build/*/output")):
        text = f.read_text(encoding="utf-8", errors="replace")
        if f.suffix == ".d":
            for line in text.splitlines():
                head, _, rest = line.partition(": ")
                read.update(re.split(r"(?<!\\) ", rest.strip()) if rest else [])
        else:
            read.update(l.split("=", 1)[1] for l in text.splitlines() if l.startswith("cargo:rerun-if-changed="))
    allowed = [os.path.normcase(os.path.abspath(cache / "nebula" / r)) for r in paths]
    for r in sorted(read):
        if not r:
            continue
        a = os.path.normcase(os.path.abspath(r if os.path.isabs(r) else cache / r))
        if a.startswith(nebula + os.sep) and not any(a == x or a.startswith(x + os.sep) for x in allowed):
            return a
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
    # nebula/: LumaLink builds Nebula's validator and cooker from it (its README, "Building"); here it holds this
    # tree's copy of the paths it builds from (NEBULA_INPUTS), written only where the bytes differ, so the pinned
    # LumaLink checks mods as the tree under test does and cargo, which goes by file times, rebuilds only when they
    # change (a link to the tree rebuilt all five crates at each gate: git rewrites the text table's time; N-1070).
    # The clean keeps nebula/ and target/.
    link = cache / "nebula"
    if link.is_symlink() or (hasattr(link, "is_junction") and link.is_junction()):
        os.rmdir(link) if os.name == "nt" else link.unlink()
    git("checkout", "-q", "--force", "--detach", pin)
    git("clean", "-q", "-d", "-f", "-x", "-e", "target/", "-e", "nebula/")
    written = sync_by_content(TREE, link)
    print("mod_tests: nebula/ of the pinned LumaLink: %d files written or removed" % written, flush=True)
    exe = cache / "target" / "release" / ("lumalink.exe" if os.name == "nt" else "lumalink")
    # The whole workspace with its tests, as the gate's lumalink check (cargo test --workspace) builds it: one feature
    # set, so neither build undoes the other's (-p lumalink alone resolved other features, and each of the two
    # rebuilt nebula-import, lumalink-core and lumalink at every gate; N-1070).
    print("mod_tests: building LumaLink %s (cargo build --release --workspace --all-targets)" % pin[:9], flush=True)
    r = subprocess.run(["cargo", "build", "--release", "--locked", "--workspace", "--all-targets"], cwd=str(cache),
                       capture_output=True, text=True)
    if r.returncode != 0 or not exe.is_file():
        text = (r.stderr or r.stdout).strip()
        missing = "couldn't read `nebula" in text
        raise SystemExit("building the pinned LumaLink failed%s: %s" % (
            " (it reads a tree path its nebula/ copy does not hold: add it to mod_tests.NEBULA_INPUTS)" if missing else "",
            text[-800:]))
    outside = check_nebula_inputs(cache)
    if outside is not None:
        raise SystemExit("the pinned LumaLink's build read %s, a tree path its nebula/ copy does not hold: add it to "
                         "mod_tests.NEBULA_INPUTS" % outside)
    return exe, cache


def main():
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description="The mod system's tests, one renderer at a time.")
    parser.add_argument("--renderer", required=True, choices=("d3d12", "vulkan", "d3d11"))
    parser.add_argument("--bin", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--only", action="append", default=[], help="a suite, or suite/test (repeat for several)")
    parser.add_argument("--lock", help="held for the whole run; refused while it exists (default: <work>/run.lock)")
    parser.add_argument("--pak", help="the game.pak (default: the bin folder's pak.path)")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2), help="mods built at once")
    parser.add_argument("--parallel", type=int, default=max(1, min(4, (os.cpu_count() or 4) // 4)),
                        help="tests run at once, each game in its own folder (plan 04, section 7)")
    parser.add_argument("--set", choices=("quick",),
                        help="quick: tests/mods/quick.json, one or more tests of every suite (the quick gate's)")
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
    lock = pathlib.Path(args.lock)
    if lock.exists():
        raise SystemExit("run lock held: %s: %s" % (lock, lock.read_text(encoding="utf-8", errors="replace").strip()))
    lock.parent.mkdir(parents=True, exist_ok=True)
    lock.write_text("mod_tests %s %s\n" % (args.renderer, time.strftime("%Y-%m-%d %H:%M:%S")), encoding="utf-8")
    try:
        return run_all(args, lumalink, work, ll_tree)
    finally:
        lock.unlink()


def run_all(args, lumalink, work, ll_tree):
    if args.set == "quick":
        sources = [("", TESTS)] + ([("lumalink/", ll_tree / "tests" / "game")] if ll_tree is not None else [])
        selection, problems = quick_set(sources)
        if problems:
            for problem in problems:
                print("quick-set %s" % problem)
            print("mod_tests: %s: the quick set does not match the suites" % args.renderer)
            return 1
        args.only = list(args.only) + [e for e in selection if ll_tree is not None or not e.startswith("lumalink/")]
    tree, build_failed = prepare(args, lumalink, work, ll_tree)
    if args.build_only:
        for mod, why in sorted(build_failed.items()):
            print("build-failed %s: %s" % (mod.relative_to(tree).as_posix(), why))
        return 0 if not build_failed else 1
    # (label prefix, the copy's suites, the runs' folder)
    origins = [("", tree / "tests" / "mods", work / args.renderer)]
    rows = []
    if ll_tree is not None:
        origins.append(("lumalink/", tree / "lumalink" / "tests" / "game", work / args.renderer / "lumalink"))
    elif not args.only or any(o.startswith("lumalink/") for o in args.only):
        rows.append(("lumalink", "-", "not-run", "no LUMALINK_TREE, and LUMALINK was built in no LumaLink checkout: LumaLink's tests/game did not run"))
    planned = [(run_root,) + plan_origin(args, prefix, copy, run_root, build_failed) for prefix, copy, run_root in origins]
    units = [u for _, _, origin_units, _ in planned for u in origin_units]
    logs = run_units(args, units, work)
    by_suite = {u["suite"]: logs.get(id(u)) for u in units}
    for run_root, known, _, judged in planned:
        rows += known + judge(args, judged, run_root, by_suite)
    for suite, name, verdict, reason in rows:
        print("%-40s %-7s %-14s %s" % ("%s/%s" % (suite, name), args.renderer, verdict, reason))
    passed = sum(1 for r in rows if r[2] == "pass")
    print("mod_tests: %s: %d of %d pass" % (args.renderer, passed, len(rows)))
    return 0 if rows and passed == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
