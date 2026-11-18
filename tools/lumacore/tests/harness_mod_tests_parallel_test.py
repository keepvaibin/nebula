"""harness_mod_tests_parallel_test.py: mod_tests.py runs tests side by side and keeps the quick set whole (plan 04,
sections 3.1 and 7; N-2041).

python -X utf8 -I tools/lumacore/tests/harness_mod_tests_parallel_test.py [--work <folder>]

1. chains: a test continuing from another (continue_from) runs after it in one runner; every other test is a runner
   of its own, in the suite's order.
2. plan_origin: a test the selection takes brings the tests it continues from (a quick set naming the last of a chain
   runs the chain); a test of another renderer is left out.
3. run_units: N runners at once and never more, each with its own lock and log (the runner refuses a held lock: one
   shared lock made runs wait in turn); the longest first by the last run's times.
4. quick_set: a suite with no test in the set, or a name that is not a test, is a problem; tests/mods/quick.json as
   committed covers every suite of tests/mods.
The runner is not started: subprocess.run is replaced by one that records what it was given.
Exit 0 when every case holds.
"""
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import mod_tests  # noqa: E402

COPIED = mod_tests.COPIED


def suite(root, name, tests):
    """A suite of a copy as prepare leaves it: {test name: test.json}."""
    folder = root / name
    for test, body in tests.items():
        (folder / test).mkdir(parents=True)
        (folder / test / "test.json").write_text(json.dumps(dict({"harness": 1, "name": test}, **body)), encoding="utf-8")
    text = json.dumps({"harness": 1, "tests": list(tests)})
    (folder / "suite.json").write_text(text, encoding="utf-8")
    (folder / COPIED).write_text(text, encoding="utf-8")
    return folder


def case_chains(_root):
    tests = {"a": {}, "b": {"continue_from": "a"}, "c": {}, "d": {"continue_from": "b"}, "e": {"continue_from": "c"},
             "f": {"continue_from": "gone"}}
    got = mod_tests.chains(list(tests), tests)
    if got != [["a", "b", "d"], ["c", "e"], ["f"]]:
        return "chains %r" % got
    return None


def case_plan(root):
    copy = root / "copy"
    suite(copy, "s", {"t1": {}, "t2": {"continue_from": "t1"}, "t3": {"continue_from": "t2"}, "t4": {},
                      "t5": {"renderers": ["vulkan"]}})
    args = argparse.Namespace(only=["s/t3", "s/t5"], renderer="d3d12")
    rows, units, judged = mod_tests.plan_origin(args, "", copy, root / "runs", {})
    if rows or [u["tests"] for u in units] != [["t1", "t2", "t3"]]:
        return "only s/t3: rows %r, units %r" % (rows, [u["tests"] for u in units])
    if [j[2] for j in judged] != ["t1", "t2", "t3"]:
        return "judged %r" % [j[2] for j in judged]
    args.only = []
    _, units, _ = mod_tests.plan_origin(args, "", copy, root / "runs", {})
    if [u["tests"] for u in units] != [["t1", "t2", "t3"], ["t4"]]:
        return "all: units %r" % [u["tests"] for u in units]
    return None


def case_side_by_side(root):
    copy = root / "copy"
    suite(copy, "s", {"t%d" % i: {} for i in range(8)})
    work = root / "work"
    work.mkdir()
    (work / "times.json").write_text(json.dumps({"s/t5": 50.0, "s/t2": 40.0}), encoding="utf-8")
    seen = {"now": 0, "peak": 0, "locks": [], "logs": [], "order": []}
    guard = threading.Lock()

    def runner(cmd, **_kw):
        with guard:
            seen["now"] += 1
            seen["peak"] = max(seen["peak"], seen["now"])
            seen["locks"].append(cmd[cmd.index("--lock") + 1])
            seen["order"].append(cmd[cmd.index("--test") + 1])
            seen["logs"].append(_kw["stdout"].name)
        time.sleep(0.2)
        with guard:
            seen["now"] -= 1
        return subprocess.CompletedProcess(cmd, 0)

    real = mod_tests.subprocess.run
    mod_tests.subprocess.run = runner
    try:
        args = argparse.Namespace(only=[], renderer="d3d12", bin="", commit="", pak=None, wait_while=None, parallel=3)
        mod_tests.run_origin(args, "", copy, root / "runs", work, {})
    finally:
        mod_tests.subprocess.run = real
    if seen["peak"] != 3:
        return "%d runners at once, not 3" % seen["peak"]
    if len(set(seen["locks"])) != 8 or len(set(seen["logs"])) != 8:
        return "locks %r, logs %r: not one each" % (seen["locks"], seen["logs"])
    if not {"t5", "t2"} <= set(seen["order"][:3]):
        return "the longest were not in the first three started: %r" % seen["order"]
    return None


def case_quick(root):
    copy = root / "copy"
    suite(copy, "a", {"a1": {}, "a2": {}})
    suite(copy, "b", {"b1": {}})
    real = mod_tests.QUICK
    try:
        for listed, want in ((["a/a1", "b/b1"], []), (["a/a1"], ["b: no test of it in the quick set"]),
                             (["a/a1", "b/b1", "a/zz"], ["a/zz: no such test in a"]),
                             (["a/a1", "b/b1", "c/c1"], ["c/c1: no such suite"])):
            mod_tests.QUICK = root / ("quick-%d.json" % len(listed))
            mod_tests.QUICK.write_text(json.dumps({"harness": 1, "tests": listed}), encoding="utf-8")
            _, problems = mod_tests.quick_set([("", copy)])
            if problems != want:
                return "%r gave %r, not %r" % (listed, problems, want)
    finally:
        mod_tests.QUICK = real
    _, problems = mod_tests.quick_set([("", mod_tests.TESTS)])
    if problems:
        return "tests/mods/quick.json: %s" % "; ".join(problems)
    return None


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else None
    if work is not None:
        work.mkdir(parents=True, exist_ok=True)
    failed = []
    for case in (case_chains, case_plan, case_side_by_side, case_quick):
        with tempfile.TemporaryDirectory(dir=work) as root:
            try:
                why = case(pathlib.Path(root))
            except Exception as e:  # noqa: BLE001
                why = "%s: %r" % (type(e).__name__, e)
            if why is not None:
                failed.append("%s: %s" % (case.__name__, why))
    if failed:
        print("FAIL " + "\n  ".join(failed))
        return 1
    print("mod_tests side by side: holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
