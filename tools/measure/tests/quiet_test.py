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

# The desktop's compositor draws the game's window too and alone reached 11 %: it is left out of other programs' use.
percent, detail = quiet.card_use(lambda: counters([0] * 6, [0, 0, 0, 0, 0, 11.0]), lambda: procs, ME)
check(percent == 0.0 and "dwm" not in detail, "dwm.exe at 11 %% is left out of the sum: %s %s" % (percent, detail))
percent, detail = quiet.card_use(lambda: counters([0] * 6, [3.0, 2.5, 0, 0, 0, 11.0]), lambda: procs, ME)
check(abs(percent - 5.5) < 1e-9 and "chrome.exe 5.5%" in detail, "other programs are still counted beside it: %s %s" % (percent, detail))
procs_upper = procs[:3] + [proc(9, "DWM.EXE", 50)]
check(quiet.card_use(lambda: counters([0] * 6, [0, 0, 0, 0, 0, 11.0]), lambda: procs_upper, ME)[0] == 0.0,
      "the name is matched in any case")

# ---- The processors: what other programs used over a run (CpuWatch) ----
# A run on a machine with 8 processors for 100 seconds: 800 processor-seconds. The system's busy time is read at the start
# and the end; the process list gives each process's CPU seconds. The game (below the measure) used 200, chrome 100.


class Cpu:
    """Fake readings: a clock of the machine's busy and total time, and a process list that changes."""
    def __init__(self):
        self.busy, self.total, self.procs = 1000.0, 5000.0, []

    def times(self):
        return self.busy, self.total

    def reader(self):
        return [dict(p) for p in self.procs]


def cpu_proc(pid, ppid, name, cpu, created=1.0):
    return {"pid": pid, "ppid": ppid, "name": name, "created": created, "cpu": cpu}


def watched(own_cpu, other_cpu, system_busy, ended_game=False):
    """The percent and detail for a run whose own processes used `own_cpu` seconds, chrome `other_cpu`, with the machine
    busy for `system_busy` of 800 seconds."""
    cpu = Cpu()
    cpu.procs = [cpu_proc(ME, 50, "python.exe", 1.0), cpu_proc(7, 50, "chrome.exe", 40.0)]
    watch = quiet.CpuWatch(cpu.reader, cpu.times, own_pid=ME)
    watch.begin(threaded=False)
    # The run: the harness (a child of the measure) starts, the game (its child) starts; a program that started later too.
    cpu.procs = [cpu_proc(ME, 50, "python.exe", 3.0), cpu_proc(7, 50, "chrome.exe", 40.0 + other_cpu),
                 cpu_proc(101, ME, "python.exe", 2.0, 2.0), cpu_proc(102, 101, "NebulaVk.exe", own_cpu - 5.0, 2.0)]
    watch.sample()
    if ended_game:  # the game ends before the last look: it keeps the time of the look before
        cpu.procs = [p for p in cpu.procs if p["pid"] != 102]
    cpu.busy += system_busy
    cpu.total += 800.0
    return watch.end()


percent, detail = watched(own_cpu=200.0, other_cpu=100.0, system_busy=310.0)
# busy 310 - own (the measure 2 + the harness 2 + the game 195 = 199 plus ... see below) is computed from the readings.
check(abs(percent - (310.0 - (2.0 + 2.0 + 195.0)) / 800.0 * 100.0) < 1e-9, "others = the machine's busy time less the own processes' CPU: %s" % percent)
check("chrome.exe 12.5%" in detail and detail.startswith("other programs use the processors 13.9%"),
      "the biggest named program is said: %s" % detail)
percent, detail = watched(own_cpu=200.0, other_cpu=0.0, system_busy=199.0)
check(percent == 0.0 and "(" not in detail, "only the own processes busy: others 0, nothing named: %s %s" % (percent, detail))
percent, detail = watched(own_cpu=200.0, other_cpu=0.0, system_busy=100.0)
check(percent == 0.0, "own CPU above the machine's busy time (a look a little late) is never negative: %s" % percent)
percent, detail = watched(own_cpu=200.0, other_cpu=0.0, system_busy=199.0 + 90.0)
check(abs(percent - 90.0 / 800.0 * 100.0) < 1e-9 and "(" not in detail,
      "busy time no readable program explains (the system's own, protected programs) still counts: %s %s" % (percent, detail))
percent, detail = watched(own_cpu=200.0, other_cpu=100.0, system_busy=310.0, ended_game=True)
check(abs(percent - (310.0 - 199.0) / 800.0 * 100.0) < 1e-9, "a game that ended keeps the CPU of its last look: %s" % percent)
# The limit's two sides, as the measure applies them.
check(quiet.CPU_LIMIT == 10.0 and watched(200.0, 0.0, 199.0 + 80.0)[0] <= quiet.CPU_LIMIT < watched(200.0, 0.0, 199.0 + 80.5)[0],
      "exactly 10 %% of 800 processor-seconds (80) is not more than 10 %%")
# A process that was already running at the start counts only what it used since: chrome's 40 s before the start are not its run.
percent, detail = watched(own_cpu=200.0, other_cpu=0.0, system_busy=199.0)
check("chrome" not in detail, "CPU used before the watch began is not counted: %s" % detail)
# A game the host started in its place (its parent, the first game, has ended): still below the measure.
cpu = Cpu()
cpu.procs = [cpu_proc(ME, 50, "python.exe", 1.0)]
watch = quiet.CpuWatch(cpu.reader, cpu.times, own_pid=ME)
watch.begin(threaded=False)
cpu.procs = [cpu_proc(ME, 50, "python.exe", 1.0), cpu_proc(101, ME, "NebulaVk.exe", 50.0, 2.0)]
watch.sample()
cpu.procs = [cpu_proc(ME, 50, "python.exe", 1.0), cpu_proc(103, 101, "NebulaVk.exe", 40.0, 3.0)]  # 101 has ended
watch.sample()
cpu.busy += 90.0
cpu.total += 800.0
percent, detail = watch.end()
check(abs(percent) < 1e-9, "a restarted game (its parent ended) is still the measure's own: %s" % percent)
# Unreadable: no process list, or no system times: not read (the measure trusts the run and says so).
dead = quiet.CpuWatch(lambda: None, cpu.times, own_pid=ME)
dead.begin(threaded=False)
check(dead.end() == (None, "processor use not read"), "no process list: processor use not read")
dead = quiet.CpuWatch(cpu.reader, lambda: None, own_pid=ME)
dead.begin(threaded=False)
check(dead.end() == (None, "processor use not read"), "no system times: processor use not read")
still = Cpu()
still.procs = [cpu_proc(ME, 50, "python.exe", 1.0)]
frozen = quiet.CpuWatch(still.reader, still.times, own_pid=ME)
frozen.begin(threaded=False)
check(frozen.end() == (None, "processor use not read"), "no time passed: not read, not a division by zero")
# The thread looks while the run goes on, and stops at end().
looks = []
live = quiet.CpuWatch(lambda: looks.append(1) or [cpu_proc(ME, 50, "python.exe", 1.0)], still.times, own_pid=ME, every=0.01)
live.begin()
import time  # noqa: E402
time.sleep(0.3)
live.end()
seen = len(looks)
time.sleep(0.1)
check(seen >= 5 and len(looks) == seen and not live.thread.is_alive(), "the thread looks every few seconds and ends with the watch: %d looks" % seen)
# The Machine hands out a watch with its fakes.
mach = quiet.Machine(own_pid=ME, cpu_reader=cpu.reader, system_times=cpu.times)
w = mach.cpu_watch()
check(isinstance(w, quiet.CpuWatch) and w.reader == cpu.reader and w.own_pid == ME, "Machine.cpu_watch uses the fakes")
# The real readings work on this computer (Windows only): the process list holds this process, the system times grow.
if quiet.os.name == "nt":
    real = quiet.read_cpu_processes()
    me = [p for p in real if p["pid"] == quiet.os.getpid()] if real else []
    check(len(me) == 1 and me[0]["cpu"] >= 0.0 and me[0]["name"].lower().startswith("python"),
          "the real process list names this process: %s" % me)
    first = quiet.read_system_times()
    check(first is not None and first[1] >= first[0] > 0, "the real system times: busy no more than total: %s" % (first,))

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
