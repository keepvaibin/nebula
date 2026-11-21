"""measure_wait_test.py: a build or a test run started by hand waits while a speed measure holds measure.lock (tools/gate/measure_wait.py,
build_windows.ps1, luma_test.py run, measure.py take_lock). Planted lock folders, fake clocks; no game, no build.

    python -X utf8 -I tools/gate/tests/measure_wait_test.py

  free_lock_does_not_wait         no lock folder: nothing said, nothing slept
  held_lock_waits_and_says        a lock held by a running process: one line, a look every 10 s, the end of the lock ends the wait
  waits_fifteen_minutes_then_goes_on   a lock that stays: one line at the start, one at the end, 90 looks, `went on`
  ended_holder_does_not_count     a holder.txt "pid N" whose process has ended: no wait (and measure.py takes the lock over)
  holder_without_pid_counts       a holder.txt with no pid: held
  callers_that_never_wait         NEBULA_NO_MEASURE_WAIT set: skipped
  real_process_check              this process is alive, a process that ended is not
  measure_take_lock_writes_pid    measure.py's lock names the process; a lock of an ended holder is taken over; a live one is refused
  command_line                    measure_wait.py as a program: waits and says it; free: silent
  build_script_waits              build_windows.ps1 (stopped by a Tree that does not exist, right after the wait): says it waits, with a
                                  live holder; not with the gate's variable set, nor with an ended holder
  luma_test_run_waits             luma_test.py run by hand waits; with --device it does not
Exit 0 when every case holds.
"""
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
GATE = HERE.parent
TREE = GATE.parents[1]
sys.path.insert(0, str(GATE))
sys.path.insert(0, str(TREE / "tools" / "measure"))
import measure_wait  # noqa: E402

failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)
    counter = [0]

    def plant(text, name=None):
        counter[0] += 1
        lock = work / (name or "lock%d" % counter[0]) / "measure.lock"
        lock.mkdir(parents=True)
        if text is not None:
            (lock / "holder.txt").write_text(text, encoding="utf-8")
        return lock

    class Clock:
        def __init__(self):
            self.now, self.sleeps = 0.0, []

        def time(self):
            return self.now

        def sleep(self, seconds):
            self.sleeps.append(seconds)
            self.now += seconds

    def waiting(lock, alive=lambda pid: True, minutes=15.0, free_after=None, environ=None):
        clock, said = Clock(), []

        def sleep(seconds):
            clock.sleep(seconds)
            if free_after is not None and len(clock.sleeps) >= free_after:
                import shutil
                shutil.rmtree(lock, ignore_errors=True)
        result = measure_wait.wait(lock, minutes, said.append, sleep, clock.time, 10.0, environ or {}, alive)
        return result, clock, said

    # free_lock_does_not_wait
    result, clock, said = waiting(work / "nothing" / "measure.lock")
    check(result == "free" and said == [] and clock.sleeps == [], "no lock: %s %s %s" % (result, said, clock.sleeps))

    # held_lock_waits_and_says
    lock = plant("measure.py abc D:/w 2026-11-21 08:00:00 pid 4242\n")
    result, clock, said = waiting(lock, free_after=3)
    check(result == "waited" and len(said) == 1 and "pid 4242" in said[0] and "speed measure" in said[0] and "15 minutes" in said[0]
          and clock.sleeps == [10.0, 10.0, 10.0], "a held lock: waits, one line, a look every 10 s: %s %s %s" % (result, said, clock.sleeps))

    # waits_fifteen_minutes_then_goes_on
    lock = plant("measure.py abc D:/w 2026-11-21 08:00:00 pid 4242\n")
    result, clock, said = waiting(lock)
    check(result == "went on" and len(said) == 2 and "going on" in said[1] and len(clock.sleeps) == 90 and
          abs(clock.now - 900.0) < 1e-9, "a lock that stays: goes on within 15 minutes: %s %s %d" % (
              result, said, len(clock.sleeps)))
    lock = plant("measure.py abc D:/w 2026-11-21 08:00:00 pid 4242\n")
    result, clock, said = waiting(lock, minutes=0.0)
    check(result == "went on" and clock.sleeps == [], "no minutes: goes on at once")

    # ended_holder_does_not_count
    lock = plant("measure.py abc D:/w 2026-11-21 08:00:00 pid 4242\n")
    result, clock, said = waiting(lock, alive=lambda pid: False)
    check(result == "free" and said == [], "a holder whose process ended: no wait: %s %s" % (result, said))
    lock = plant("measure.py abc D:/w 2026-11-21 08:00:00 pid 4242\n")
    asked = []
    result, clock, said = waiting(lock, alive=lambda pid: asked.append(pid) or False)
    check(asked == [4242], "the process asked about is the one in holder.txt: %s" % asked)

    # holder_without_pid_counts
    for text in ("someone 2026-11-21 00:00:00\n", None, ""):
        lock = plant(text)
        result, clock, said = waiting(lock, alive=lambda pid: False, free_after=1)
        check(result == "waited" and len(said) == 1, "a holder with no pid (%r) counts as held: %s" % (text, result))

    # callers_that_never_wait
    lock = plant("measure.py abc D:/w 2026-11-21 08:00:00 pid 4242\n")
    result, clock, said = waiting(lock, environ={"NEBULA_NO_MEASURE_WAIT": "1"})
    check(result == "skipped" and said == [] and clock.sleeps == [], "the gate's variable: never waits: %s" % result)

    # real_process_check
    done = subprocess.run([sys.executable, "-X", "utf8", "-I", "-c", "import os; print(os.getpid())"], capture_output=True, text=True)
    ended = int(done.stdout.strip())
    check(measure_wait.process_alive(os.getpid()) and not measure_wait.process_alive(ended),
          "this process is alive, one that ended is not (%d)" % ended)

    # measure_take_lock_writes_pid
    import measure
    lock = work / "take" / "measure.lock"
    lock.parent.mkdir(parents=True, exist_ok=True)
    measure.take_lock(lock, "measure.py abc D:/w")
    text = (lock / "holder.txt").read_text(encoding="utf-8")
    check(text.rstrip().endswith("pid %d" % os.getpid()) and text.startswith("measure.py abc D:/w "), "the lock names the process: %r" % text)
    try:
        measure.take_lock(lock, "again")
        check(False, "a lock held by a live process is refused")
    except SystemExit as error:
        check("is held" in str(error) and "pid %d" % os.getpid() in str(error), "refused, the holder said: %s" % error)
    (lock / "holder.txt").write_text("measure.py old D:/w 2026-11-21 00:00:00 pid %d\n" % ended, encoding="utf-8")
    measure.take_lock(lock, "new holder")
    check("new holder" in (lock / "holder.txt").read_text(encoding="utf-8"), "a lock whose holder ended is taken over")

    # command_line
    # What a person starting these by hand has: no gate variable, and not inside a self-test (the gate runs this test inside one).
    env = {k: v for k, v in os.environ.items() if k not in ("NEBULA_NO_MEASURE_WAIT", "NEBULA_SELF_TEST", "NEBULA_SELF_TEST_LOG")}
    live = plant("measure.py abc D:/w 2026-11-21 08:00:00 pid %d\n" % os.getpid())
    dead = plant("measure.py abc D:/w 2026-11-21 08:00:00 pid %d\n" % ended)
    program = [sys.executable, "-X", "utf8", "-I", str(GATE / "measure_wait.py")]
    out = subprocess.run(program + ["--lock", str(live), "--minutes", "0.05", "--every", "0.1"], capture_output=True, text=True, env=env)
    check(out.returncode == 0 and "waiting while a speed measure runs" in out.stdout and "going on" in out.stdout,
          "the program waits and goes on: %r %r" % (out.stdout, out.stderr))
    out = subprocess.run(program + ["--lock", str(dead), "--minutes", "0.05"], capture_output=True, text=True, env=env)
    check(out.returncode == 0 and out.stdout == "", "an ended holder: silent: %r" % out.stdout)

    # build_script_waits: build_windows.ps1 stops at the Tree that is not there, right after the wait.
    script = str(GATE / "build_windows.ps1")
    shell = ["powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", script, "-Tree",
             str(work / "no-such-tree"), "-Root", str(work / "root"), "-Bin", str(work / "bin"), "-MeasureWaitMinutes", "0.05"]
    out = subprocess.run(shell + ["-MeasureLock", str(live)], capture_output=True, text=True, env=env)
    check("waiting while a speed measure runs" in out.stdout and "pid %d" % os.getpid() in out.stdout and out.returncode != 0,
          "build_windows.ps1 by hand, measure running: says it waits: %r %r" % (out.stdout, out.stderr[:200]))
    out = subprocess.run(shell + ["-MeasureLock", str(dead)], capture_output=True, text=True, env=env)
    check("waiting" not in out.stdout and out.returncode != 0, "an ended holder: no wait: %r" % out.stdout)
    out = subprocess.run(shell + ["-MeasureLock", str(live)], capture_output=True, text=True, env=dict(env, NEBULA_NO_MEASURE_WAIT="1"))
    check("waiting" not in out.stdout and out.returncode != 0, "started by the gate: no wait: %r" % out.stdout)

    # luma_test_run_waits: stops at once after the wait (no such test folder).
    luma = [sys.executable, "-X", "utf8", "-I", str(TREE / "tools" / "lumacore" / "luma_test.py"), "run", str(work / "no-such-test"),
            "--bin", str(work / "bin"), "--work", str(work / "luma-work"), "--lock", str(work / "luma.lock")]
    wait_env = dict(env, NEBULA_MEASURE_LOCK=str(live), NEBULA_MEASURE_WAIT_MINUTES="0.05")
    out = subprocess.run(luma, capture_output=True, text=True, env=wait_env, timeout=120)
    check("waiting while a speed measure runs" in out.stdout, "luma_test.py run by hand: waits: %r %r" % (out.stdout[:200], out.stderr[-200:]))
    out = subprocess.run(luma + ["--device", "SERIAL"], capture_output=True, text=True, env=wait_env, timeout=120)
    check("waiting while" not in out.stdout, "a run on the tablet does not wait: %r" % out.stdout[:200])
    out = subprocess.run(luma, capture_output=True, text=True, env=dict(wait_env, NEBULA_NO_MEASURE_WAIT="1"), timeout=120)
    check("waiting while" not in out.stdout, "started by the gate or the measure: does not wait")
    out = subprocess.run(luma, capture_output=True, text=True, env=dict(wait_env, NEBULA_SELF_TEST="1"), timeout=120)
    check("waiting while" not in out.stdout, "inside a self-test: does not look at the lock")

    print("\n".join(failed) if failed else "measure wait: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
