"""harness_android_test.py: the Android runner's parts that need no device (luma_test_android.py).

python -X utf8 -I tools/lumacore/tests/harness_android_test.py [--work <folder>]

- Android's exit record arrives after the process is gone: the runner asks again until it is there (the old runner
  read it once, tens of milliseconds too early, and took a clean exit for an unknown end); a record that never comes
  is "not found", after EXIT_RECORD_SECONDS;
- Android writes the record first with reason 0 (UNKNOWN) and its real reason a little later (seen on a tablet: a
  clean exit read 0.11 s after the process went said UNKNOWN, and EXIT_SELF status 0 afterwards): the runner asks
  again until the reason is known, and a reason still unknown after the wait is an end with no code;
- the record's reasons map to codes: an exit's status, a signal's 128 + n, a crash's 134;
- the package is the test app's and nothing else;
- the device lock is made at once or not at all, and names its holder;
- the platform names follow the device's ABI;
- the start check (the push workflow's, on an emulator) starts the test app with its own extras and no game data, and
  its verdict is the game process's exit record: 0 passes, a status or a crash is that code, a process that never
  ends is stopped at the limit, one that never starts is reported;
- a device that leaves adb during a test is luma_test.DeviceLost (verdict `device-lost`, the suite's other tests not
  run), not the test's own failure; a failure while the device still answers stays the test's; remove on a device
  that does not answer fails instead of saying the test app is not installed (seen 11-17: the tablet left adb mid-tour,
  the runner fell over and remove said "not installed").
Exit 0 when every case holds.
"""
import importlib.util
import pathlib
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("luma_test_android", HERE.parent / "luma_test_android.py")
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)
sys.path.insert(0, str(HERE.parent))
import luma_test  # noqa: E402

RECORD = ("ApplicationExitInfo #0:\n"
          "  timestamp=2026-11-17 10:00:00.000 pid=4321 realUid=10345 packageUid=10345 definingUid=10345 user=0\n"
          "    process=dev.nebula.galaxy.test:game reason=%d (X) subreason=0 (UNKNOWN) status=%d importance=100\n")


class FakeDevice(android.Device):
    """Answers dumpsys with no record for the first `late` asks, then with `record`."""

    def __init__(self, record, late):
        super().__init__("fake")
        self.record, self.late, self.asked = record, late, 0

    def shell(self, command, timeout=600):
        assert command == "dumpsys activity exit-info " + android.PACKAGE, command
        self.asked += 1
        return 0, ("ApplicationExitInfo for nothing yet\n" if self.asked <= self.late else self.record)


class ScriptedDevice(android.Device):
    """Answers dumpsys with each of `answers` in turn, then the last one for good."""

    def __init__(self, answers):
        super().__init__("fake")
        self.answers, self.asked = answers, 0

    def shell(self, command, timeout=600):
        self.asked += 1
        return 0, self.answers[min(self.asked, len(self.answers)) - 1]


class StartDevice(android.Device):
    """A device for the start check: the game process is there for `alive` looks, then gone with `record`; or never
    there (alive None: it never started) or there for good (alive -1)."""

    def __init__(self, alive, record, installed=True):
        super().__init__("fake")
        self.alive, self.record, self.is_installed = alive, record, installed
        self.commands, self.looks, self.now = [], 0, 0.0

    def installed(self):
        return self.is_installed

    def adb(self, *args, timeout=600, stdin=None):
        self.commands.append(" ".join(args))

        class Done:
            returncode, stdout, stderr = 0, b"", b""
        return Done()

    def shell(self, command, timeout=600):
        self.commands.append(command)
        if command.startswith("pidof "):
            self.looks += 1
            there = self.alive == -1 or (self.alive is not None and self.looks <= self.alive)
            return (0, "4321\n") if there else (1, "")
        if command.startswith("dumpsys activity exit-info"):
            return 0, self.record
        if command.startswith("am start"):
            return 0, "Status: ok\nActivity: %s\n" % android.ACTIVITY
        return 0, ""

    def clock(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class GoneDevice(android.Device):
    """A device that answers adb or not, as told."""

    def __init__(self, there):
        super().__init__("fake")
        self.there = there

    def answers(self):
        return self.there

    def installed(self):
        return False


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    d = FakeDevice(RECORD % (1, 0), late=3)
    found, code = d.exit_record("4321")
    check(found and code == 0 and d.asked == 4, "a late record is waited for: %s %s after %d asks" % (found, code, d.asked))
    for reason, status, want in ((1, 3, 3), (2, 9, 137), (4, 0, 134), (5, 0, 134), (10, 0, None)):
        found, code = FakeDevice(RECORD % (reason, status), late=0).exit_record("4321")
        check(found and code == want, "reason %d status %d gives %s: %s" % (reason, status, want, code))
    settling = ScriptedDevice([RECORD % (0, 0)] * 3 + [RECORD % (1, 0)])
    found, code = settling.exit_record("4321")
    check(found and code == 0 and settling.asked == 4,
          "an UNKNOWN reason is asked again until it is known: %s %s after %d asks" % (found, code, settling.asked))
    android.EXIT_RECORD_SECONDS = 0.3
    never = ScriptedDevice([RECORD % (0, 0)])
    found, code = never.exit_record("4321")
    check(found and code is None and never.asked > 1, "a reason never known: found, no code (%s %s)" % (found, code))
    android.EXIT_RECORD_SECONDS = 5.0
    other = FakeDevice((RECORD % (1, 0)).replace("pid=4321", "pid=999"), late=0)
    android.EXIT_RECORD_SECONDS = 0.3
    began = time.time()
    found, code = other.exit_record("4321")
    check(not found and code is None and time.time() - began >= 0.3, "no record for the pid: not found, after the wait")

    check(android.PACKAGE == "dev.nebula.galaxy.test" and android.GAME_PROCESS == "dev.nebula.galaxy.test:game"
          and android.ACTIVITY.startswith("dev.nebula.galaxy.test/"), "only the test app is named")

    lock = work / "device.lock"
    if lock.exists():
        android.release_lock(lock)
    check(android.device_lock(lock, "first") is None, "a free lock is taken")
    held = android.device_lock(lock, "second")
    check(held is not None and held.startswith("first "), "a held lock names its holder: %s" % held)
    android.release_lock(lock)
    check(not lock.exists() and android.device_lock(lock, "third") is None, "a released lock is free again")
    android.release_lock(lock)

    d = StartDevice(alive=3, record=RECORD % (1, 0))
    code, seen = android.start_check(d, 300, 120, clock=d.clock, sleep=d.sleep)
    started = [c for c in d.commands if c.startswith("am start")]
    check(code == 0 and "pid=4321" in seen, "a clean start check passes: %s %s" % (code, seen))
    check(len(started) == 1 and "--es nebula.harness %s" % android.START_CHECK in started[0]
          and "--ei nebula.start_check 300" in started[0] and "nebula.seconds" not in started[0],
          "the start check starts the test app with its own extras: %s" % started)
    check(any(c.startswith("run-as %s " % android.PACKAGE) and "files/harness/%s" % android.START_CHECK in c
              for c in d.commands), "the start check gets a run folder of its own in the test app's files")
    check(not any("dev.nebula.galaxy " in c + " " or "dev.nebula.galaxy/" in c for c in d.commands),
          "the start check never names the player's game")
    for reason, status, want in ((1, 3, 3), (1, 4, 4), (5, 0, 134)):
        code, _ = android.start_check(StartDevice(alive=2, record=RECORD % (reason, status)), 300, 120,
                                      clock=lambda: 0.0, sleep=lambda s: None)
        check(code == want, "a start check that ends with reason %d status %d gives %s: %s" % (reason, status, want, code))
    hung = StartDevice(alive=-1, record="")
    code, seen = android.start_check(hung, 300, 120, clock=hung.clock, sleep=hung.sleep)
    hung_seen = seen
    check(code is None and "did not end" in seen and ("am force-stop " + android.PACKAGE) in hung.commands[-1],
          "a start check past its limit is stopped and fails: %s %s" % (code, seen))
    never = StartDevice(alive=None, record="")
    code, seen = android.start_check(never, 300, 120, clock=never.clock, sleep=never.sleep)
    check(code is None and "never" in seen, "a game process that never starts fails: %s %s" % (code, seen))
    check("the test app's processes: none" in seen and any(c.startswith("ps -A") for c in never.commands)
          and any(c.startswith("logcat -d") and "ActivityManager" in c for c in never.commands),
          "a game process that never starts: the red names the app's processes and the log of its start: %s" % seen)
    check(any(c.startswith("ps -A") for c in hung.commands) and "the log since am start" in hung_seen,
          "a start check past its limit: the red names the device's state: %s" % hung_seen)
    code, seen = android.start_check(StartDevice(alive=3, record="", installed=False), 300, 120,
                                     clock=lambda: 0.0, sleep=lambda s: None)
    check(code is None and "not installed" in seen, "no test app: no start: %s" % seen)

    def failing(*a):
        raise RuntimeError("run-as dev.nebula.galaxy.test gave no folder")
    real = android.run_on_device
    android.run_on_device = failing
    try:
        android.run_one(luma_test, GoneDevice(False), {}, None, work, None)
        check(False, "a device gone mid-test raises DeviceLost")
    except Exception as error:  # noqa: BLE001
        check(type(error).__name__ == "DeviceLost" and isinstance(error, getattr(luma_test, "DeviceLost", ())),
              "a device gone mid-test is DeviceLost: %r" % error)
    try:
        android.run_one(luma_test, GoneDevice(True), {}, None, work, None)
        check(False, "a failure on a device still there is raised")
    except Exception as error:  # noqa: BLE001
        check(type(error) is RuntimeError, "a failure on a device still there stays the test's: %r" % error)
    android.run_on_device = real
    real_device = android.Device
    android.Device = lambda serial: GoneDevice(False)

    class Args:
        device = "fake"
    check(android.remove(Args()) == 1, "remove on a device that does not answer fails")
    android.Device = real_device

    check(android.host_for("arm64-v8a")["code"] == "code/android-arm64/mod.so", "arm64 code path")
    check(android.host_for("x86_64")["platform"] == "android-x64", "x86_64 platform")
    try:
        android.host_for("armeabi-v7a")
        check(False, "an ABI the app is not built for is refused")
    except SystemExit:
        pass

    print("\n".join(failed) if failed else "harness android: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
