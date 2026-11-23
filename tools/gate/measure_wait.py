"""measure_wait.py: a build or a test run started by hand waits while a speed measure runs (T7a).

  python -X utf8 -I tools/gate/measure_wait.py [--lock PATH] [--minutes 15] [--stop]

A speed measure (tools/measure/measure.py) holds the measurement lock, a folder with holder.txt in it that ends "pid N" (the
measure's own process). While it is held, `wait` says so in one line, looks every 10 seconds, and goes on when the lock is
free, or after `minutes` (15) anyway. A lock whose holder's process has ended does not count (a measure that died). A lock
whose holder file names no process counts as held.

The gate, a landing and the measure itself never wait: they set NEBULA_NO_MEASURE_WAIT=1 for what they start (gate.py main,
measure.py run). Used by tools/gate/build_windows.ps1 (through
this command) and tools/lumacore/luma_test.py run.
"""
import argparse
import os
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import lockholder  # noqa: E402

LOCK = "D:/NebulaWork/plan/measure.lock"
SKIP_ENV = "NEBULA_NO_MEASURE_WAIT"
MINUTES = 15.0
AGENT_MINUTES = 40.0      # what the agent's own commands wait (AGENT.md section 9): a game never starts beside a running measure
LOOK_SECONDS = 10.0


def process_alive(pid):
    """Whether a process with this id is running."""
    if os.name == "nt":
        import ctypes
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.OpenProcess.restype = ctypes.c_void_p
        k32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        k32.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
        k32.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = k32.OpenProcess(0x1000, 0, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_uint32(0)
        ok = k32.GetExitCodeProcess(handle, ctypes.byref(code))
        k32.CloseHandle(handle)
        return bool(ok) and code.value == 259  # STILL_ACTIVE
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def holder_of(lock, alive=process_alive, started=lockholder.process_started):
    """The text of the lock's holder.txt when the lock is held by a process that runs (or by one that is not named), else None. The
    holder is read as tools/gate/lockholder.py reads it: `pid N` anywhere in the line, and a `since` that is not the process's start."""
    lock = pathlib.Path(lock)
    if not lock.is_dir():
        return None
    try:
        text = (lock / "holder.txt").read_text(encoding="utf-8", errors="replace").strip()
    except OSError:
        text = ""
    if lockholder.gone(text, alive, started) is not None:
        return None
    return text or "unknown"


def wait(lock=None, minutes=None, say=print, sleep=time.sleep, clock=time.monotonic, every=LOOK_SECONDS, environ=None,
         alive=process_alive):
    """Waits while the measurement lock is held. Returns `skipped` (the caller never waits), `free` (not held), `waited` (it
    was held and became free) or `went on` (still held after `minutes`). The lock and the minutes are the given ones, else
    NEBULA_MEASURE_LOCK and NEBULA_MEASURE_WAIT_MINUTES (for the self-tests), else the defaults."""
    environ = os.environ if environ is None else environ
    if environ.get(SKIP_ENV):
        return "skipped"
    lock = lock or environ.get("NEBULA_MEASURE_LOCK") or LOCK
    minutes = float(minutes if minutes is not None else environ.get("NEBULA_MEASURE_WAIT_MINUTES") or MINUTES)
    began = clock()
    held = holder_of(lock, alive)
    if held is None:
        return "free"
    say("waiting while a speed measure runs (%s), at most %g minutes" % (held, minutes))
    while True:
        if clock() - began + every > minutes * 60.0 + 1e-9:
            say("the speed measure still runs after %g minutes: going on" % minutes)
            return "went on"
        sleep(every)
        if holder_of(lock, alive) is None:
            return "waited"


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--lock", default=None)
    p.add_argument("--minutes", type=float, default=None)
    p.add_argument("--every", type=float, default=LOOK_SECONDS)
    p.add_argument("--stop", action="store_true", help="a measure that still runs after the minutes: exit 3, so that the caller starts nothing")
    args = p.parse_args(argv)
    result = wait(args.lock, args.minutes, every=args.every)
    if args.stop and result == "went on":
        print("the speed measure still runs: nothing is started; run the command again when it has ended")
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
