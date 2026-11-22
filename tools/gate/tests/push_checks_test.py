"""push_checks_test.py: tools/gate/gate.py's push_checks (the pre-push hook's checks of main, run before the gate) on
folders whose answer is known: a census that fails refuses with its files, one that passes with no ratchet passes, a
ratchet that refuses refuses, and the ratchet's baseline is never written by the check."""
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import gate  # noqa: E402

CENSUS_FAIL = 'import sys\nprint("fifo  1 closed")\nprint("    1 runtime/src/x.h")\nsys.exit(1)\n'
CENSUS_PASS = "import sys\nsys.exit(0)\n"
RATCHET_FAIL = ('import pathlib, sys\n'
                'pathlib.Path(__file__).with_name("ratchet_baseline.json").write_text("rewritten")\n'
                'print("ratchet: PUSH REFUSED: test_seam 0 -> 3")\nsys.exit(1)\n')
RATCHET_PASS = ('import pathlib, sys\n'
                'pathlib.Path(__file__).with_name("ratchet_baseline.json").write_text("rewritten")\nsys.exit(0)\n')


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        tree, guards = t / "tree", t / "guards"
        (tree / "tools" / "verify").mkdir(parents=True)
        guards.mkdir()
        census = tree / "tools" / "verify" / "console_census.py"

        census.write_text(CENSUS_FAIL, encoding="utf-8")
        why = gate.push_checks("c0ffee", tree=tree, guards=guards)
        check(why is not None and "census" in why and "runtime/src/x.h" in why, "a failing census refuses: %r" % why)
        check(why is not None and "fifo 1 closed" in why, "it names the group that is not as it must be, with its count: %r" % why)

        census.write_text(CENSUS_PASS, encoding="utf-8")
        check(gate.push_checks("c0ffee", tree=tree, guards=guards) is None, "a passing census, no ratchet: passes")

        baseline = guards / "ratchet_baseline.json"
        baseline.write_text("kept", encoding="utf-8")
        (guards / "ratchet_prepush.py").write_text(RATCHET_FAIL, encoding="utf-8")
        why = gate.push_checks("c0ffee", tree=tree, guards=guards)
        check(why is not None and "ratchet" in why and "test_seam" in why, "a refusing ratchet refuses: %r" % why)
        check(baseline.read_text(encoding="utf-8") == "kept", "a refusing ratchet's copy leaves the baseline")

        (guards / "ratchet_prepush.py").write_text(RATCHET_PASS, encoding="utf-8")
        check(gate.push_checks("c0ffee", tree=tree, guards=guards) is None, "a passing ratchet passes")
        check(baseline.read_text(encoding="utf-8") == "kept", "a passing ratchet's copy leaves the baseline")

    for f in failures:
        print("FAIL:", f)
    print("push_checks_test: %s" % ("FAIL" if failures else "pass"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
