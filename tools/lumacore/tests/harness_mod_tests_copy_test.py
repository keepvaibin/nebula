"""harness_mod_tests_copy_test.py: mod_tests.py runs the tests of its copy, as the copy lists them (N-2025).

python -X utf8 -I tools/lumacore/tests/harness_mod_tests_copy_test.py [--work <folder>]

A run copies the tests into <work>/tree and builds them; the checkouts may change while it runs (a test added, a
suite.json edited). run_origin is given a copy whose suite lists one test (for another renderer, so nothing starts the
game) and must judge from the copy alone: no row, no error. Before the fix it read each suite.json from the checkout,
and a test listed there but not in the copy ended the run with no rows.
Exit 0 when the case holds.
"""
import argparse
import inspect
import json
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import mod_tests  # noqa: E402


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else None
    with tempfile.TemporaryDirectory(dir=work) as root:
        root = pathlib.Path(root)
        copy = root / "tree" / "tests" / "mods"
        (copy / "s" / "t1").mkdir(parents=True)
        (copy / "s" / "suite.json").write_text(json.dumps({"harness": 1, "tests": ["t1"]}), encoding="utf-8")
        (copy / "s" / "t1" / "test.json").write_text(json.dumps({"harness": 1, "name": "t1", "renderers": ["vulkan"]}),
                                                     encoding="utf-8")
        # The checkout meanwhile: a second test listed, its folder not in the copy.
        source = root / "checkout" / "s"
        source.mkdir(parents=True)
        (source / "suite.json").write_text(json.dumps({"harness": 1, "tests": ["t1", "t2"]}), encoding="utf-8")
        args = argparse.Namespace(only=[], renderer="d3d12")
        try:
            # (The run_origin before the fix also took the checkout's suites, `source`: given, so it shows its fault.)
            if "source" in inspect.signature(mod_tests.run_origin).parameters:
                rows = mod_tests.run_origin(args, "", copy, root / "checkout", root / "runs", root, {})
            else:
                rows = mod_tests.run_origin(args, "", copy, root / "runs", root, {})
        except Exception as e:  # noqa: BLE001
            print("FAIL run_origin: %r" % e)
            return 1
        if rows:
            print("FAIL rows for a test of another renderer: %r" % rows)
            return 1
    print("mod_tests copy: holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
