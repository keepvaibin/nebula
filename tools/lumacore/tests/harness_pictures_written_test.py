"""harness_pictures_written_test.py: the every-test check "every picture asked was written", without the game.

python -X utf8 -I tools/lumacore/tests/harness_pictures_written_test.py [--work <folder>]

Judges run folders written here with luma_test.evaluate, for a test with two picture steps and no check of its own on
either (as the gate's tours and engine tests, whose pictures pictures.py compares):
- both pictures in the run's pictures folder: the check passes;
- one never written (the host gave up on the GPU at its exit): the check fails and names it;
- a test that ends in a crash is not held to it.
Exit 0 when every case holds.
"""
import json
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import luma_test  # noqa: E402

NAME = "every picture asked was written"


def judge(work, name, written, ends="end"):
    folder = work / name
    folder.mkdir()
    steps = [{"at": 4, "picture": "title"}, {"at": 8, "picture": "after"}, {"at": 10, "end": True}]
    (folder / "test.json").write_text(json.dumps({"harness": 1, "name": name, "about": "pictures", "bare": True,
                                                  "ends": ends, "steps": steps,
                                                  "expect": [{"log": "crash: "}] if ends == "crash" else []}), encoding="utf-8")
    test = luma_test.read_test(folder)
    run = folder / "run"
    (run / "pictures").mkdir(parents=True)
    for picture in written:
        (run / "pictures" / (picture + ".png")).write_bytes(b"")
    (run / "run.log").write_text("host: started\n", encoding="utf-8")
    (run / "facts.jsonl").write_text(json.dumps({"frame": 10, "key": "harness.end", "value": {"frame": 10}}) + "\n",
                                     encoding="utf-8")
    _, _, checks, _ = luma_test.evaluate(test, "d3d12", run, {"code": 0, "seconds": 1.0, "mods": []})
    return [c for c in checks if c["check"] == NAME]


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    found = judge(work, "both", ["title", "after"])
    check(len(found) == 1 and found[0]["result"] == "pass", "both written passes: %s" % found)
    found = judge(work, "one", ["title"])
    check(len(found) == 1 and found[0]["result"] == "fail" and found[0]["actual"] == ["after"],
          "a picture never written fails and is named: %s" % found)
    check(judge(work, "crash", [], ends="crash") == [], "a crash test is not held to the check")

    print("\n".join(failed) if failed else "harness pictures written: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
