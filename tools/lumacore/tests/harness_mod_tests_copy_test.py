"""harness_mod_tests_copy_test.py: mod_tests.py runs the tests of its copy, as the copy lists them (N-2025, N-2031).

python -X utf8 -I tools/lumacore/tests/harness_mod_tests_copy_test.py [--work <folder>]

A run copies the tests into <work>/tree and builds them; the checkouts may change while it runs (a test added, a
suite.json edited), and a later run reuses the copy while nothing changed.
1. run_origin is given a copy whose suite lists one test (for another renderer, so nothing starts the game) and must
   judge from the copy alone: no row, no error. Before the N-2025 fix it read each suite.json from the checkout, and a
   test listed there but not in the copy ended the run with no rows.
2. The same copy run twice, first for one test (--only), then for the whole suite: the second run takes both tests.
   Before the N-2031 fix a run rewrote the copy's suite.json to the tests it took and run_origin read that file, so the
   second run took only the first run's test. The runner is not started: subprocess.run is replaced by one that records
   the tests it was given (--test, one runner per test or chain since runs go side by side).
Exit 0 when both hold.
"""
import argparse
import inspect
import json
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import mod_tests  # noqa: E402

COPIED = getattr(mod_tests, "COPIED", "suite.copied.json")


def write_suite(suite, tests):
    """A suite of the copy as prepare leaves it: its suite.json and, beside it, its list as copied."""
    text = json.dumps({"harness": 1, "tests": tests})
    (suite / "suite.json").write_text(text, encoding="utf-8")
    (suite / COPIED).write_text(text, encoding="utf-8")


def run_origin(args, copy, root):
    # (The run_origin before the N-2025 fix also took the checkout's suites, `source`: given, so it shows its fault.)
    if "source" in inspect.signature(mod_tests.run_origin).parameters:
        return mod_tests.run_origin(args, "", copy, root / "checkout", root / "runs", root, {})
    return mod_tests.run_origin(args, "", copy, root / "runs", root, {})


def case_checkout(root):
    copy = root / "tree" / "tests" / "mods"
    (copy / "s" / "t1").mkdir(parents=True)
    write_suite(copy / "s", ["t1"])
    (copy / "s" / "t1" / "test.json").write_text(json.dumps({"harness": 1, "name": "t1", "renderers": ["vulkan"]}),
                                                 encoding="utf-8")
    # The checkout meanwhile: a second test listed, its folder not in the copy.
    source = root / "checkout" / "s"
    source.mkdir(parents=True)
    (source / "suite.json").write_text(json.dumps({"harness": 1, "tests": ["t1", "t2"]}), encoding="utf-8")
    try:
        rows = run_origin(argparse.Namespace(only=[], renderer="d3d12"), copy, root)
    except Exception as e:  # noqa: BLE001
        return "run_origin: %r" % e
    if rows:
        return "rows for a test of another renderer: %r" % rows
    return None


def case_reused(root):
    copy = root / "tree" / "tests" / "mods"
    for name in ("t1", "t2"):
        (copy / "s" / name).mkdir(parents=True)
        (copy / "s" / name / "test.json").write_text(json.dumps({"harness": 1, "name": name}), encoding="utf-8")
    write_suite(copy / "s", ["t1", "t2"])
    given = []

    def runner(cmd, **_kw):
        named = [cmd[i + 1] for i, a in enumerate(cmd) if a == "--test"]
        given.append(named or json.loads(pathlib.Path(cmd[3]).read_text(encoding="utf-8"))["tests"])
        return subprocess.CompletedProcess(cmd, 0)

    real = mod_tests.subprocess.run
    mod_tests.subprocess.run = runner
    try:
        runs = []
        for only in (["s/t1"], []):
            before = len(given)
            run_origin(argparse.Namespace(only=only, renderer="d3d12", bin="", commit="", lock="", pak=None, wait_while=None,
                                          parallel=2), copy, root)
            runs.append(sorted(t for g in given[before:] for t in g))
    except Exception as e:  # noqa: BLE001
        return "run_origin: %r" % e
    finally:
        mod_tests.subprocess.run = real
    if runs != [["t1"], ["t1", "t2"]]:
        return "the runs took %r, not [['t1'], ['t1', 't2']]" % runs
    return None


def case_no_kit(root):
    """Without a LumaLink tree the examples are not copied (there is no LumaKit for them) and their row says they did
    not run; with one, they and the kit beside them are. Before N-4018 they were copied without the kit and every one
    of them failed to build."""
    places = [place for _, place in mod_tests.copies(None)]
    if "examples" in places:
        return "the examples copied without a LumaLink tree: %r" % places
    if not hasattr(mod_tests, "no_tree_rows"):
        return "no row for the examples without a LumaLink tree"
    rows = mod_tests.no_tree_rows([])
    if [r[0] for r in rows] != ["lumalink", "examples"] or any(r[2] != "not-run" for r in rows):
        return "the rows without a LumaLink tree: %r" % rows
    if [r[0] for r in mod_tests.no_tree_rows(["s/t1"])]:
        return "a run of one test of tests/mods has a row for the other origins"
    with_tree = [place for _, place in mod_tests.copies(root)]
    if "examples" not in with_tree or "examples/lumakit" not in with_tree:
        return "with a LumaLink tree, the examples or their kit are not copied: %r" % with_tree
    return None


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else None
    failed = []
    for case in (case_checkout, case_reused, case_no_kit):
        with tempfile.TemporaryDirectory(dir=work) as root:
            why = case(pathlib.Path(root))
            if why is not None:
                failed.append("%s: %s" % (case.__name__, why))
    if failed:
        print("FAIL " + "\n  ".join(failed))
        return 1
    print("mod_tests copy: holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
