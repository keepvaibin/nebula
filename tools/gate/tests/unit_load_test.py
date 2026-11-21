"""unit_load_test.py: tools/gate/unit_load.py with fake test commands and no build. The busy programs are alive while a run
goes and gone after; ctest's summary line is read; a failing run, a run with no summary and a run that does not end are
told apart; the logs are written. Each case names what breaking the tool changes."""
import contextlib
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import unit_load  # noqa: E402

PY = sys.executable


def fake(code, text):
    """A command that prints `text` and exits with `code`."""
    return [PY, "-I", "-c", "import sys; print(%r); sys.exit(%d)" % (text, code)]


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    # The busy programs: alive inside, dead after (a load left running would spoil every run after).
    with unit_load.load(3) as children:
        check(len(children) == 3 and all(c.poll() is None for c in children), "the busy programs run inside the load")
    check(all(c.poll() is not None for c in children), "the busy programs are gone after the load")
    try:
        with unit_load.load(2) as children:
            raise RuntimeError("a run that raised")
    except RuntimeError:
        pass
    check(all(c.poll() is not None for c in children), "the busy programs are gone after a run that raised")

    # ctest's summary line.
    check(unit_load.summary("100% tests passed, 0 tests failed out of 130") == (0, 130), "a passing summary is read")
    check(unit_load.summary("98% tests passed, 3 tests failed out of 130") == (3, 130), "a failing summary is read")
    check(unit_load.summary("98% tests passed, 1 test failed out of 130") == (1, 130), "ctest's singular is read")
    check(unit_load.summary("no summary here") is None, "no summary is None")

    t = pathlib.Path(tempfile.mkdtemp())
    try:
        # The command runs inside the load, not before or after it.
        events = []

        @contextlib.contextmanager
        def recording_load(count):
            events.append("load %d" % count)
            yield []
            events.append("command ended" if (t / "ran").exists() else "load ended before the command")

        real = unit_load.load
        unit_load.load = recording_load
        try:
            command = [PY, "-I", "-c", "import pathlib; pathlib.Path(%r).write_text('x'); print('100%% tests passed, 0 tests failed out of 4')" % str(t / "ran")]
            code, failed, total = unit_load.run_once(command, t / "a.log", 7, 60)
        finally:
            unit_load.load = real
        check(events == ["load 7", "command ended"], "the command runs while the load is on: %s" % events)
        check((code, failed, total) == (0, 0, 4), "a passing run: %s" % ((code, failed, total),))
        check("100% tests passed" in (t / "a.log").read_text(encoding="utf-8"), "the run's output is in its log")

        code, failed, total = unit_load.run_once(fake(8, "98% tests passed, 3 tests failed out of 130"), t / "b.log", 0, 60)
        check((code, failed, total) == (8, 3, 130), "a failing run keeps its exit code and counts: %s" % ((code, failed, total),))

        code, failed, total = unit_load.run_once(fake(0, "nothing"), t / "c.log", 0, 60)
        check(code == 0 and failed is None, "a run with no summary has no counts")

        hang = [PY, "-I", "-c", "import time; print('start', flush=True); time.sleep(60)"]
        code, failed, total = unit_load.run_once(hang, t / "d.log", 0, 1)
        check(code == 124 and failed is None and "no end after 1 s" in (t / "d.log").read_text(encoding="utf-8"),
              "a run that does not end is code 124 and says so")

        # The whole tool, with fake commands: every run passes; one fails; one has no summary.
        runs = iter([fake(0, "100% tests passed, 0 tests failed out of 5")] * 2)
        unit_load.ctest_command = lambda build, jobs: next(runs)
        out = t / "out1"
        check(unit_load.main(["--dir", "x", "--times", "2", "--load", "0", "--out", str(out)]) == 0, "two passing runs: exit 0")
        check((out / "unit-load-1.log").exists() and (out / "unit-load-2.log").exists(), "a log for each run")

        runs = iter([fake(0, "100% tests passed, 0 tests failed out of 5"), fake(8, "80% tests passed, 1 tests failed out of 5")])
        check(unit_load.main(["--dir", "x", "--times", "2", "--load", "0", "--out", str(t / "out2")]) == 1, "one failing run: exit 1")

        runs = iter([fake(0, "a build that is not there")])
        check(unit_load.main(["--dir", "x", "--times", "2", "--load", "0", "--out", str(t / "out3")]) == 2, "no summary: exit 2")

        # A run that passes by exit code but reports a failed test is a failure (a summary and a code that disagree).
        runs = iter([fake(0, "99% tests passed, 1 tests failed out of 100")])
        check(unit_load.main(["--dir", "x", "--times", "1", "--load", "0", "--out", str(t / "out4")]) == 1,
              "exit 0 with a failed test in the summary is a failure")
    finally:
        shutil.rmtree(t, ignore_errors=True)

    for what in failures:
        print("FAILED:", what)
    print("unit_load_test: %s" % ("FAILED" if failures else "passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
