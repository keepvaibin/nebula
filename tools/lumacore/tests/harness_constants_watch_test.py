"""harness_constants_watch_test.py: the runner's reading of Direct3D 11's constants watch (ledger P-332), without the game.

python -X utf8 -I tools/lumacore/tests/harness_constants_watch_test.py [--work <folder>]

Judges run folders written here (run.log and facts.jsonl, as a run leaves them) with luma_test.evaluate:
- a run whose log has no "[constants] watch" line passes, with no watch check;
- a run whose log has the watch's line fails: the check "the constants watch saw every read right" fails with the line,
  and the verdict's reason names it;
- the line fails the run wherever it stands in the log line (after a time stamp too);
- a run the test driver stopped with its own "pass" still fails on the line.
Exit 0 when every case holds.
"""
import json
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import luma_test  # noqa: E402

NAME = "the constants watch saw every read right"
ZERO = "[loss] draws-no-material=0 draws-no-pipeline=0 textures-blank=0 sound-reads-missing=0"
WATCH = ("[constants] watch: wrote number 812 generation 1 offset 4096, read number 811 generation 1 offset 3840 "
         "(ring generation now 1)")


def judge(work, name, log_lines, stopped=False):
    folder = work / name
    folder.mkdir()
    (folder / "test.json").write_text(json.dumps({"harness": 1, "name": name, "about": "constants watch", "bare": True,
                                                  "steps": [{"at": 10, "end": True}], "expect": []}), encoding="utf-8")
    test = luma_test.read_test(folder)
    run = folder / "run"
    run.mkdir()
    (run / "run.log").write_text("\n".join(log_lines) + "\n", encoding="utf-8")
    fact = ({"frame": 10, "key": "harness.stop", "value": {"verdict": "pass", "reason": "the driver's own end"}}
            if stopped else {"frame": 10, "key": "harness.end", "value": {"frame": 10}})
    (run / "facts.jsonl").write_text(json.dumps(fact) + "\n", encoding="utf-8")
    verdict, reason, checks, _ = luma_test.evaluate(test, "d3d11", run, {"code": 0, "seconds": 1.0, "mods": []})
    return verdict, reason, [c for c in checks if c["check"] == NAME]


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    verdict, reason, found = judge(work, "quiet", ["host: started", ZERO])
    check(verdict == "pass" and found == [], "a run with no watch line passes with no watch check: %s %s %s"
          % (verdict, reason, found))

    verdict, reason, found = judge(work, "watched", ["host: started", WATCH, ZERO])
    check(len(found) == 1 and found[0]["result"] == "fail" and found[0]["actual"] == [WATCH],
          "the watch's line fails its check, which names it: %s" % found)
    check(verdict == "fail" and WATCH in reason, "the watch's line fails the run and its reason names it: %s %s"
          % (verdict, reason))

    stamped = "[  41.250] " + WATCH
    verdict, reason, found = judge(work, "stamped", ["host: started", stamped, ZERO])
    check(verdict == "fail" and len(found) == 1, "the line after a time stamp fails the run: %s %s" % (verdict, found))

    verdict, reason, found = judge(work, "stopped", ["host: started", WATCH, ZERO], stopped=True)
    check(verdict == "fail" and WATCH in reason, "a run the driver stopped with a pass fails on the line: %s %s"
          % (verdict, reason))

    print("\n".join(failed) if failed else "harness constants watch: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
