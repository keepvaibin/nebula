"""same_runs_test.py: tools/gate/same_runs.py on gate run folders made here: two runs with the same pictures and steps
agree; a changed pixel, a changed step hash and a place only one run holds are each named and fail."""
import json
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import same_runs  # noqa: E402


def make_run(root, pixel=(10, 20, 30), step_hash="aa", extra=False):
    from PIL import Image
    tests = ["001-A", "002-B"] + (["003-C"] if extra else [])
    for test in tests:
        run = root / "runs-tour-d3d12" / "runs" / "tour" / test / "d3d12"
        (run / "pictures").mkdir(parents=True)
        (run / "result.json").write_text(json.dumps({"test": test, "verdict": "pass", "renderer": "d3d12"}), encoding="utf-8")
        image = Image.new("RGB", (4, 4), (1, 2, 3))
        if test == "002-B":
            image.putpixel((2, 2), pixel)
        image.save(run / "pictures" / "arrive.png")
        image.save(run / "pictures" / "arrive.display.png")
        facts = [{"key": "hash.step", "frame": f, "value": {"h": step_hash if (test == "001-A" and f == 2) else "s%d" % f,
                                                           "actors": 3}} for f in range(4)]
        (run / "facts.jsonl").write_text("\n".join(json.dumps(x) for x in facts) + "\n", encoding="utf-8")


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    t = pathlib.Path(tempfile.mkdtemp())
    try:
        make_run(t / "a")
        make_run(t / "b")
        ok, lines = same_runs.compare(t / "a", t / "b")
        check(ok and lines[-1].startswith("same_runs: 2 of 2"), "the same runs agree: %s" % lines)
        make_run(t / "c", pixel=(10, 20, 31))
        ok, lines = same_runs.compare(t / "a", t / "c")
        check(not ok and any("002-B" in l and "pictures differ: arrive.png" in l for l in lines), "a pixel: %s" % lines)
        make_run(t / "d", step_hash="ab")
        ok, lines = same_runs.compare(t / "a", t / "d")
        check(not ok and any("001-A" in l and "differs from step 2" in l for l in lines), "a step: %s" % lines)
        make_run(t / "e", extra=True)
        ok, lines = same_runs.compare(t / "a", t / "e")
        check(not ok and any("003-C" in l and "only in the second" in l for l in lines), "one run only: %s" % lines)
        (t / "empty").mkdir()
        check(not same_runs.compare(t / "empty", t / "empty")[0], "nothing compared cannot pass")
    finally:
        shutil.rmtree(t, ignore_errors=True)
    if failures:
        print("same_runs_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("same_runs_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
