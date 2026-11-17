"""harness_loss_test.py: the every-test check "nothing the game asked for was lost", without the game.

python -X utf8 -I tools/lumacore/tests/harness_loss_test.py [--work <folder>]

Judges run folders written here (run.log and facts.jsonl, as a run leaves them) with luma_test.evaluate:
- the host's [loss] line at its exit with every count 0: the check passes;
- one count that is not 0: the check fails and names the line;
- no [loss] line at all (the host did not report): the check fails;
- only the first-loss line ([loss] first ...) and no counts: the check fails;
- a later [loss] line wins over an earlier one;
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

NAME = "nothing the game asked for was lost"
ZERO = "[loss] draws-no-material=0 draws-no-pipeline=0 textures-blank=0 sound-reads-missing=0"


def judge(work, name, log_lines, ends="end"):
    folder = work / name
    folder.mkdir()
    steps = [{"at": 10, "end": True}]
    (folder / "test.json").write_text(json.dumps({"harness": 1, "name": name, "about": "loss", "bare": True,
                                                  "ends": ends, "steps": steps,
                                                  "expect": [{"log": "crash: "}] if ends == "crash" else []}), encoding="utf-8")
    test = luma_test.read_test(folder)
    run = folder / "run"
    run.mkdir()
    (run / "run.log").write_text("\n".join(log_lines) + "\n", encoding="utf-8")
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

    def one(found, result, what):
        check(len(found) == 1 and found[0]["result"] == result, "%s: %s" % (what, found))

    one(judge(work, "zero", ["host: started", ZERO]), "pass", "every count 0 passes")
    lost = ZERO.replace("textures-blank=0", "textures-blank=3")
    found = judge(work, "lost", ["host: started", lost])
    one(found, "fail", "a count that is not 0 fails")
    check(bool(found) and found[0]["actual"] == lost, "the failure names the line: %s" % found)
    one(judge(work, "silent", ["host: started"]), "fail", "no [loss] line fails")
    one(judge(work, "first_only", ["[loss] first textures-blank: texture 12"]), "fail", "only the first-loss line fails")
    one(judge(work, "later_wins", [lost, ZERO]), "pass", "the last [loss] line is the one judged")
    one(judge(work, "first_then_zero", [ZERO, "[loss] first textures-blank: texture 12"]), "pass",
        "the first-loss line is not the count line")
    check(judge(work, "crash", ["host: started"], ends="crash") == [], "a crash test is not held to the check")

    print("\n".join(failed) if failed else "harness loss: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
