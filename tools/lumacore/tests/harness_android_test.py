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
- the platform names follow the device's ABI.
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
