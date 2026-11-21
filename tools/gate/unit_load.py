"""unit_load.py: the unit tests of a build folder, run several times in a row with every logical processor busy (ledger
N-1073: a unit test that waits a fixed time for a thread below the normal priority failed on a busy machine).

    python -X utf8 -I tools/gate/unit_load.py --dir BUILD/msvc [--times 5] [--jobs 8] [--load N] [--out FOLDER]

BUILD/msvc is build_windows.ps1's folder of the unit tests. Each run is tools/gate/ctest.ps1 (the gate's own call of
ctest) while one busy program a logical processor runs at the normal priority, as the game's test extension does with
GALAXY_TEST_CPU_LOAD for a game. Each run's output is FOLDER/unit-load-<n>.log. Prints one line a run and exits 0 when
every run passed, 1 otherwise, 2 when a run could not be made.
"""
import argparse
import contextlib
import os
import pathlib
import re
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
BUSY = "while True: pass"


@contextlib.contextmanager
def load(count):
    """`count` programs that do nothing but compute, from entering to leaving."""
    children = [subprocess.Popen([sys.executable, "-I", "-c", BUSY], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL) for _ in range(count)]
    try:
        yield children
    finally:
        for child in children:
            child.kill()
        for child in children:
            child.wait()


def summary(text):
    """ctest's own line "N% tests passed, F tests failed out of T" as (failed, total), or None when it is not there."""
    found = re.search(r"tests passed, (\d+) tests? failed out of (\d+)", text)
    return (int(found.group(1)), int(found.group(2))) if found else None


def run_once(command, log, busy, timeout):
    """(exit code, failed, total) of one run of `command` beside `busy` busy programs; the output goes to `log`."""
    with load(busy):
        began = time.monotonic()
        try:
            done = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  timeout=timeout)
            code, text = done.returncode, done.stdout.decode("utf-8", errors="replace")
        except subprocess.TimeoutExpired as error:
            code, text = 124, (error.stdout or b"").decode("utf-8", errors="replace") + "\nno end after %d s\n" % timeout
    pathlib.Path(log).write_text(text + "\nseconds: %.0f\n" % (time.monotonic() - began), encoding="utf-8")
    counts = summary(text)
    return code, counts[0] if counts else None, counts[1] if counts else None


def ctest_command(build, jobs):
    return ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(HERE / "ctest.ps1"), "-Dir",
            str(build), "-Jobs", str(jobs)]


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--dir", required=True, help="the unit tests' build folder (build_windows.ps1's ROOT/msvc)")
    p.add_argument("--times", type=int, default=5)
    p.add_argument("--jobs", type=int, default=8)
    p.add_argument("--load", type=int, default=os.cpu_count() or 1, help="busy programs (default: a logical processor each)")
    p.add_argument("--out", default=".", help="the folder of the logs")
    p.add_argument("--timeout", type=int, default=1800, help="seconds for one run")
    args = p.parse_args(argv)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    bad = 0
    for number in range(1, args.times + 1):
        log = out / ("unit-load-%d.log" % number)
        code, failed, total = run_once(ctest_command(args.dir, args.jobs), log, args.load, args.timeout)
        if failed is None:
            print("run %d: no ctest summary (exit %d), %s" % (number, code, log))
            return 2
        ok = code == 0 and failed == 0
        bad += not ok
        print("run %d: %s, %d of %d tests failed, load %d, %s" % (number, "pass" if ok else "FAIL", failed, total, args.load, log))
        sys.stdout.flush()
    print("%d of %d runs passed" % (args.times - bad, args.times))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
