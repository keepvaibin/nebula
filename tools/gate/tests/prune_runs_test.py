"""prune_runs_test.py: tools/gate/prune_runs.py on gate run folders made here. A passed test keeps its verdict, log and
step hashes and loses the rest; a failed test stays whole; a run that failed, the newest kept runs and a run that
waits for GitHub are not touched; a second prune changes nothing; same_runs.py names a pruned place."""
import json
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import prune_runs  # noqa: E402
import same_runs  # noqa: E402


def make_test(run, test, verdict):
    folder = run / "runs-tour-d3d12" / "runs" / "tour" / test / "d3d12"
    for sub in ("pictures", "appdata/nand", "saves"):
        (folder / sub).mkdir(parents=True)
    (folder / "result.json").write_text(json.dumps({"test": test, "verdict": verdict}), encoding="utf-8")
    (folder / "run.log").write_text("log\n", encoding="utf-8")
    (folder / "facts.jsonl").write_text('{"key": "hash.step", "frame": 0, "value": {"h": "s0"}}\n', encoding="utf-8")
    (folder / "audio.wav").write_bytes(b"\0" * 1000)
    (folder / "pictures" / "arrive.png").write_bytes(b"\0" * 500)
    (folder / "appdata" / "nand" / "big.bin").write_bytes(b"\0" * 4000)
    return folder


def make_run(work, name, passed, verdicts):
    run = work / name
    run.mkdir(parents=True)
    (run / "result.json").write_text(json.dumps({"passed": passed}), encoding="utf-8")
    (run / "tour-d3d12.log").write_text("log\n", encoding="utf-8")
    return {test: make_test(run, test, v) for test, v in verdicts.items()}


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    t = pathlib.Path(tempfile.mkdtemp())
    try:
        work = t / "gate-runs"
        old = make_run(work, "20261117-100000-aaaaaaaaa-main", True, {"001-A": "pass", "002-B": "fail"})
        red = make_run(work, "20261117-110000-bbbbbbbbb-quick", False, {"001-A": "pass"})
        held = make_run(work, "20261117-120000-ccccccccc-main", True, {"001-A": "pass"})
        new1 = make_run(work, "20261117-130000-ddddddddd-quick", True, {"001-A": "pass"})
        new2 = make_run(work, "20261117-140000-eeeeeeeee-main", True, {"001-A": "pass"})
        (work / "waiting.tsv").write_text("ccccccccc0123456789\tgate/20261117-120000-ccccccccc\t1\n", encoding="utf-8")

        runs, tests, freed = prune_runs.prune_work(work, keep=2)
        check((runs, tests, freed) == (1, 1, 5500), "one run, one test, 5500 bytes: %r" % ((runs, tests, freed),))
        a = old["001-A"]
        check(sorted(p.name for p in a.iterdir()) == ["facts.jsonl", "pruned.txt", "result.json", "run.log"],
              "a passed test keeps its verdict, log and step hashes: %s" % sorted(p.name for p in a.iterdir()))
        check((old["002-B"] / "appdata" / "nand" / "big.bin").exists() and (old["002-B"] / "audio.wav").exists(),
              "a failed test stays whole")
        check((work / "20261117-100000-aaaaaaaaa-main" / "tour-d3d12.log").exists(), "the run's logs stay")
        for name, folders in (("a run that failed", red), ("a run that waits for GitHub", held),
                              ("the newest passed runs", new1), ("the newest passed runs", new2)):
            check((folders["001-A"] / "audio.wav").exists(), "%s is not touched" % name)

        again = prune_runs.prune_work(work, keep=2)
        check(again == (0, 0, 0), "a second prune changes nothing: %r" % (again,))

        whole = prune_runs.prune(work / "20261117-140000-eeeeeeeee-main")
        check(whole == (1, 5500), "gate.py's prune of the run that passed: %r" % (whole,))
        ok, lines = same_runs.compare(work / "20261117-130000-ddddddddd-quick", work / "20261117-140000-eeeeeeeee-main")
        check(not ok and any("pruned" in line for line in lines), "same_runs names a pruned place: %s" % lines)
    finally:
        shutil.rmtree(t, ignore_errors=True)

    for f in failures:
        print("FAIL:", f)
    print("prune_runs_test: %s" % ("FAIL" if failures else "pass"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
