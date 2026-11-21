"""prune_runs.py: the drive's room after a gate run (N-1060). A passed run keeps its logs and every test's verdict, and
loses the working files of the tests that passed: their game folders, sound, pictures, saves and program copies.

    python -X utf8 -I tools/gate/prune_runs.py [--work gate-runs] [--keep 2]

gate.py prunes a run that passed when it ends (--keep-runs keeps it whole, for same_runs.py). This command prunes the
passed runs in the work folder but the newest --keep, and never a run that still waits for GitHub (waiting.tsv).
A test folder is one that holds result.json under a runs-* folder; one whose verdict is not "pass" stays whole.
"""
import argparse
import json
import pathlib
import shutil
import sys

KEPT = frozenset(("result.json", "run.log", "facts.jsonl", "test.json", "plan.txt"))
MARK = "pruned.txt"
KEEP = "keep.txt"       # a run folder with this file in it is kept whole (a stress lane's evidence, tools/gate/stress.py)


def size(path):
    path = pathlib.Path(path)
    if path.is_file():
        return path.stat().st_size
    return sum(f.stat().st_size for f in path.rglob("*") if f.is_file())


def passed_tests(run):
    """The test folders of a run's harness runs whose verdict is pass."""
    for result in pathlib.Path(run).glob("runs-*/**/result.json"):
        try:
            verdict = json.loads(result.read_text(encoding="utf-8")).get("verdict")
        except (OSError, ValueError, AttributeError):
            continue
        if verdict == "pass":
            yield result.parent


def prune(run):
    """Removes the working files of a run's passed tests: (tests, bytes freed). A run folder with a keep.txt is left whole."""
    tests, freed = 0, 0
    if (pathlib.Path(run) / KEEP).is_file():
        return tests, freed
    for folder in list(passed_tests(run)):
        if (folder / MARK).exists():
            continue
        for entry in folder.iterdir():
            if entry.name in KEPT:
                continue
            freed += size(entry)
            if entry.is_dir():
                shutil.rmtree(entry, ignore_errors=True)
            else:
                entry.unlink(missing_ok=True)
        (folder / MARK).write_text("the test passed: its working files were removed (gate.py --keep-runs keeps them)\n",
                                   encoding="utf-8")
        tests += 1
    return tests, freed


def run_passed(run):
    try:
        result = json.loads((pathlib.Path(run) / "result.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    return result.get("passed") is True


def waiting(work):
    """The commits of the runs that wait for GitHub (gate.py's waiting.tsv: commit, candidate, pid)."""
    try:
        lines = (pathlib.Path(work) / "waiting.tsv").read_text(encoding="utf-8").splitlines()
    except OSError:
        return set()
    return {line.split("\t", 1)[0][:9] for line in lines if line.strip()}


def prune_work(work, keep):
    """Prunes the passed runs in a gate work folder but the newest `keep` passed ones: (runs, tests, bytes freed)."""
    work = pathlib.Path(work)
    held = waiting(work)
    runs = sorted((d for d in work.iterdir() if d.is_dir() and run_passed(d)), key=lambda d: d.name)
    old = runs[:-keep] if keep > 0 else runs
    total = [0, 0, 0]
    for run in old:
        parts = run.name.split("-")
        if len(parts) >= 4 and parts[2] in held:
            continue
        tests, freed = prune(run)
        if tests:
            total[0] += 1
        total[1] += tests
        total[2] += freed
    return tuple(total)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--work", default="D:/NebulaWork/build/gate-runs")
    p.add_argument("--keep", type=int, default=2, help="the newest passed runs left whole")
    args = p.parse_args(argv)
    runs, tests, freed = prune_work(args.work, args.keep)
    print("prune_runs: %d runs, %d passed tests, %.1f GB freed" % (runs, tests, freed / 1e9))
    return 0


if __name__ == "__main__":
    sys.exit(main())
