"""harness_far_test.py: the `far` fact check, without the game.

python -X utf8 -I tools/lumacore/tests/harness_far_test.py [--work <folder>]

- a test whose expect has {"fact", "far": [x, y, z], "beyond": D} is read; the check holds for a value more than D away
  and fails for one D or nearer, and for a number the same by difference;
- far without beyond, beyond with near, within with far, far with a value that is not numbers, and far in a step's
  `after` (a wait, which the game's own code answers) are refused when the test is read;
- a value of another shape (a string) is neither near nor far.
Exit 0 when every case holds.
"""
import json
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import luma_test  # noqa: E402


def write_test(folder, expect, steps=None):
    folder.mkdir(parents=True)
    (folder / "test.json").write_text(json.dumps({
        "harness": 1, "name": folder.name, "about": "far", "stage": {"name": "EggStarGalaxy", "scenario": 1},
        "steps": steps or [{"at": 10, "end": True}], "expect": expect}), encoding="utf-8")
    return folder


def refused(work, name, expect, steps=None):
    try:
        luma_test.read_test(write_test(work / name, expect, steps))
    except luma_test.HarnessError as e:
        return str(e)
    return None


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else None
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        base = (work / "harness-far-case") if work else pathlib.Path(tmp)
        test = luma_test.read_test(write_test(base / "good", [{"fact": "end_pos", "far": [0, 0, 0], "beyond": 300}]))
        check = test["expect"][0]
        op, value, within = check["op"], check["value"], check["within"]
        if (op, within) != ("far", 300):
            failures.append("read: op %r within %r" % (op, within))
        for actual, want in (([400, 0, 0], True), ([0, 300, 0], False), ([100, 100, 100], False),
                             ([200, 200, 200], True)):
            if luma_test.check_value(actual, op, value, within) != want:
                failures.append("far %s from the origin beyond 300: not %s" % (actual, want))
        if luma_test.check_value(10.5, "far", 10.0, 0.25) is not True or luma_test.check_value(10.2, "far", 10.0, 0.25):
            failures.append("far by a number's difference")
        if luma_test.check_value("x", "far", [0, 0, 0], 1) or luma_test.check_value("x", "near", [0, 0, 0], 1):
            failures.append("a string is neither near nor far")
        if luma_test.check_value([1, 2], "far", [0, 0, 0], 1):
            failures.append("vectors of different lengths are not far")

        for name, expect, steps, says in (
            ("no-beyond", [{"fact": "p", "far": [0, 0, 0]}], None, "far needs beyond"),
            ("beyond-near", [{"fact": "p", "near": [0, 0, 0], "within": 1, "beyond": 2}], None, "beyond belongs with far"),
            ("within-far", [{"fact": "p", "far": [0, 0, 0], "beyond": 2, "within": 1}], None, "within belongs with near"),
            ("not-numbers", [{"fact": "p", "far": ["a"], "beyond": 2}], None, "takes a number or a list of numbers"),
            ("empty-list", [{"fact": "p", "far": [], "beyond": 2}], None, "takes a number or a list of numbers"),
            ("in-a-wait", [], [{"after": {"fact": "p", "far": [0, 0, 0], "beyond": 2}, "end": True}], "far"),
        ):
            said = refused(base, name, expect, steps)
            if said is None or says not in said:
                failures.append("%s: refused with %r, expected to say %r" % (name, said, says))

    for f in failures:
        print("FAILED:", f)
    print("harness far: %s" % ("every case holds" if not failures else "%d cases failed" % len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
