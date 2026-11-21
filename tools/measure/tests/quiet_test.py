"""quiet_test.py: the speed measure's quiet-machine rules (T7a) with a fake process list and fake counters: no game.

python -X utf8 -I tools/measure/tests/quiet_test.py
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import quiet  # noqa: E402

failures = []
checks = [0]


def check(ok, what):
    checks[0] += 1
    if not ok:
        failures.append(what)


def proc(pid, name, ppid=1, cmd=""):
    return {"pid": pid, "ppid": ppid, "name": name, "cmd": cmd}


ME = 100
quiet_list = [proc(4, "System", 0), proc(ME, "python.exe", 50), proc(7, "chrome.exe", 50), proc(8, "java.exe", 50, "java -jar ide.jar")]

# ---- What makes the machine busy ----
for tool in quiet.BUILD_TOOLS:
    check(quiet.busy_programs(quiet_list + [proc(9, tool.upper())], ME) == ["%s (pid 9)" % tool.upper()],
          "%s makes the machine busy (any case)" % tool)
check(quiet.BUILD_TOOLS == ("cl.exe", "clang.exe", "clang++.exe", "clang-cl.exe", "link.exe", "lld-link.exe", "ninja.exe",
                            "cmake.exe", "msbuild.exe", "cargo.exe", "rustc.exe"), "the design's list of build tools")
check(quiet.busy_programs(quiet_list + [proc(9, "java.exe", 50, "java -cp x org.gradle.launcher.daemon.bootstrap")], ME)
      == ["java.exe (pid 9)"], "a java running gradle is a build tool")
check(quiet.busy_programs(quiet_list, ME) == [], "a java that is not running gradle, a browser: not busy")
for game in ("Nebula.exe", "NebulaVk.exe", "NebulaD11.exe"):
    check(quiet.busy_programs(quiet_list + [proc(9, game)], ME) == ["%s (pid 9)" % game],
          "another copy of the game (%s) makes it busy" % game)
# The measure's own programs (below its process) do not.
own = [proc(ME, "python.exe", 50), proc(101, "python.exe", ME), proc(102, "NebulaVk.exe", 101), proc(103, "cl.exe", 102)]
check(quiet.busy_programs(own, ME) == [], "the game this measure started (and what it started) is not 'another copy'")
check(quiet.busy_programs(own + [proc(104, "NebulaVk.exe", 50)], ME) == ["NebulaVk.exe (pid 104)"],
      "another copy beside the measure's own is found")
check(quiet.descendants(own, ME) == {101, 102, 103}, "descendants of the measure")


# ---- The wait: looks now, then every 30 seconds, up to 30 minutes ----
class Clock:
    def __init__(self):
        self.now, self.sleeps = 0.0, []

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.now += seconds


def waits(lists, minutes=30.0):
    """wait_quiet over a script of process lists (the last repeats): (result, clock, looks, said)."""
    clock, looks, said = Clock(), [], []

    def lister():
        looks.append(clock.now)
        return lists[min(len(looks) - 1, len(lists) - 1)]
    result = quiet.wait_quiet(lister, clock.sleep, clock.time, ME, minutes, 30.0, say=said.append)
    return result, clock, looks, said


result, clock, looks, said = waits([quiet_list])
check(result == (True, "") and looks == [0.0] and clock.sleeps == [], "a quiet machine: no waiting")
busy = quiet_list + [proc(9, "cl.exe")]
result, clock, looks, said = waits([busy, busy, busy, quiet_list])
check(result == (True, "") and looks == [0.0, 30.0, 60.0, 90.0], "busy, then clear after a while: looks every 30 s: %s" % looks)
check(len(said) == 1 and "cl.exe" in said[0], "what it waits for is said once: %s" % said)
result, clock, looks, said = waits([busy])
check(result[0] is False and "still busy after 30 minutes" in result[1] and "cl.exe (pid 9)" in result[1],
      "never clears: not quiet, the reason names what ran: %s" % (result,))
check(looks[0] == 0.0 and looks[-1] == 1800.0 and len(looks) == 61 and set(clock.sleeps) == {30.0},
      "it looked every 30 s up to the 30 minutes (%d looks, last at %s)" % (len(looks), looks[-1]))
result, clock, looks, said = waits([busy] * 59 + [quiet_list])
check(result == (True, "") and looks[-1] == 1770.0, "clear just inside the 30 minutes: quiet")
result, clock, looks, said = waits([busy], minutes=0.0)
check(result[0] is False and looks == [0.0], "no wait asked: one look, then not quiet")
result, clock, looks, said = waits([None])
check(result == (False, "the process list could not be read") and looks == [0.0], "an unreadable list is not 'quiet'")
many = quiet_list + [proc(20 + i, "cl.exe") for i in range(9)]
result, clock, looks, said = waits([many], minutes=0.0)
check("and 3 more" in result[1], "a long list of what runs is cut: %s" % result[1])

# ---- The card: other programs' use, from the counters ----
HEADER = '"(PDH-CSV 4.0)",' + ",".join('"\\\\PC\\GPU Engine(pid_%d_luid_0x0_0x1_phys_0_eng_%d_engtype_%s)\\Utilization Percentage"' % x
                                       for x in ((7, 0, "3D"), (7, 1, "Copy"), (ME, 0, "3D"), (102, 0, "3D"), (555, 0, "3D"), (9, 0, "3D")))


def counters(values_first, values_last):
    return "\n".join([HEADER, '"11/21/2026 01:00:00.000",' + ",".join('"%s"' % v for v in values_first),
                      '"11/21/2026 01:00:01.000",' + ",".join('"%s"' % v for v in values_last), "", "The command completed successfully."])


per = quiet.parse_card_counters(counters([50, 50, 50, 50, 50, 50], [3.0, 2.5, 40.0, 60.0, 90.0, 0.0]))
check(per == {7: 5.5, ME: 40.0, 102: 60.0, 555: 90.0, 9: 0.0}, "summed for each process, the last sample: %s" % per)
check(quiet.parse_card_counters("") == {} and quiet.parse_card_counters("garbage\nlines") == {}, "no rows: nothing")
procs = [proc(ME, "python.exe", 50), proc(102, "NebulaVk.exe", ME), proc(7, "chrome.exe", 50), proc(9, "dwm.exe", 50)]
# Process 555 has ended (not in the list): leaving out the measure's own (100 and its child 102): chrome 5.5 + dwm 0.
percent, detail = quiet.card_use(lambda: counters([0] * 6, [3.0, 2.5, 40.0, 60.0, 90.0, 0.0]), lambda: procs, ME)
check(abs(percent - 5.5) < 1e-9 and "chrome.exe 5.5%" in detail, "the measure's own game and an ended process are left out: %s %s" % (percent, detail))
percent, detail = quiet.card_use(lambda: counters([0] * 6, [8.0, 4.0, 0, 0, 0, 0.0]), lambda: procs, ME)
check(percent > quiet.CARD_LIMIT and "chrome.exe 12.0%" in detail, "busy: other programs use more than 10 %%: %s %s" % (percent, detail))
check(quiet.CARD_LIMIT == 10.0, "the limit is 10 %")
percent, detail = quiet.card_use(lambda: counters([0] * 6, [0, 0, 70.0, 80.0, 0, 0.0]), lambda: procs, ME)
check(percent == 0.0, "only the measure's own use: other programs 0: %s" % percent)
# Unreadable counters or an unreadable process list: card use not read (the run is trusted by its caller).
check(quiet.card_use(lambda: None, lambda: procs, ME) == (None, "card use not read"), "no counters: card use not read")
check(quiet.card_use(lambda: counters([0] * 6, [1] * 6), lambda: None, ME) == (None, "card use not read"),
      "no process list: card use not read")
check(quiet.card_use(lambda: "nothing useful", lambda: procs, ME) == (None, "card use not read"), "counters with no rows: not read")

# ---- The machine object the measure uses ----
mach = quiet.Machine(minutes=0.0, own_pid=ME, lister=lambda: busy, sleep=lambda s: None, clock=lambda: 0.0, say=lambda s: None,
                     reader=lambda: counters([0] * 6, [8.0, 4.0, 0, 0, 0, 0.0]))
ok, why = mach.wait_quiet()
check(ok is False and "cl.exe" in why, "Machine.wait_quiet: %s" % why)
percent, detail = mach.card_use()
check(percent is not None and abs(percent - 12.0) < 1e-9, "Machine.card_use reads through the fakes: %s" % percent)

if failures:
    print("quiet_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("quiet_test.py: pass (%d checks)" % checks[0])
