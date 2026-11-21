"""quiet.py: is the machine fit for a measure (T7a). A measure is trusted only when nothing else uses the computer's
speed: no compiler or build tool, no other copy of the game, and the graphics card used by no other program.

  wait_quiet   looks every 30 seconds, for up to 30 minutes, until no compiler, build tool or other copy of the game runs
  card_use     the card's use by programs other than this measure's own, from Windows' performance counters
               (\\GPU Engine(*)\\Utilization Percentage, summed for each process)

Every reading of the machine (the process list, the counters, the clock) is a function the caller can replace, so the
tests run with fakes and no game.
"""
import csv
import io
import json
import os
import re
import subprocess
import time

# A compiler or build tool: while one runs, the machine's speed is not the game's alone.
BUILD_TOOLS = ("cl.exe", "clang.exe", "clang++.exe", "clang-cl.exe", "link.exe", "lld-link.exe", "ninja.exe",
               "cmake.exe", "msbuild.exe", "cargo.exe", "rustc.exe")
# The game's programs as the repository names them (luma_test.py's table of the Windows programs, gate.py EXES, and the
# installer's list: each with its x86-64-v2 twin).
GAME_PROGRAMS = ("nebula.exe", "nebulavk.exe", "nebulad11.exe", "nebula-x86-64-v2.exe", "nebulavk-x86-64-v2.exe",
                 "nebulad11-x86-64-v2.exe")
WAIT_MINUTES = 30.0
LOOK_SECONDS = 30.0
CARD_LIMIT = 10.0  # percent: other programs' use of the card above this makes a measure not trusted
COUNTER = r"\GPU Engine(*)\Utilization Percentage"


def list_processes():
    """[{pid, ppid, name, cmd}] of this computer's processes (Windows), or None when it cannot be read."""
    if os.name != "nt":
        return None
    script = ("Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name,CommandLine | "
              "ConvertTo-Json -Compress")
    try:
        done = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
                              capture_output=True, text=True, timeout=60)
        data = json.loads(done.stdout)
    except (OSError, ValueError, subprocess.SubprocessError):
        return None
    if isinstance(data, dict):
        data = [data]
    return [{"pid": int(p["ProcessId"]), "ppid": int(p["ParentProcessId"] or 0), "name": p["Name"] or "",
             "cmd": p["CommandLine"] or ""} for p in data]


def descendants(procs, root):
    """The pids below `root` (its children, their children, ...), root itself not included."""
    below, grew = set(), True
    while grew:
        grew = False
        for p in procs:
            if p["pid"] not in below and p["pid"] != root and (p["ppid"] == root or p["ppid"] in below):
                below.add(p["pid"])
                grew = True
    return below


def busy_programs(procs, own_pid):
    """What runs that makes the machine not quiet: ["cl.exe (pid 12)", ...] (a build tool, a java running gradle, a game
    program that is not below this measure's own process)."""
    own = descendants(procs, own_pid) | {own_pid}
    found = []
    for p in procs:
        name = p["name"].lower()
        if p["pid"] in own:
            continue
        if name in BUILD_TOOLS or name in GAME_PROGRAMS or (name == "java.exe" and "gradle" in p["cmd"].lower()):
            found.append("%s (pid %d)" % (p["name"], p["pid"]))
    return found


def wait_quiet(lister=list_processes, sleep=time.sleep, clock=time.monotonic, own_pid=None, minutes=WAIT_MINUTES,
               every=LOOK_SECONDS, say=print):
    """Look now and every `every` seconds until nothing is running that makes the machine busy, up to `minutes`.
    (True, "") when quiet, (False, why) when the time passed (why names what still ran, or that the list was not
    readable)."""
    own_pid = os.getpid() if own_pid is None else own_pid
    began, told = clock(), None
    while True:
        procs = lister()
        if procs is None:
            return False, "the process list could not be read"
        busy = busy_programs(procs, own_pid)
        if not busy:
            return True, ""
        what = ", ".join(busy[:6]) + (" and %d more" % (len(busy) - 6) if len(busy) > 6 else "")
        if what != told:
            say("measure.py: waiting for a quiet machine: %s" % what)
            told = what
        if clock() - began + every > minutes * 60.0 + 1e-9:
            return False, "still busy after %g minutes: %s" % (minutes, what)
        sleep(every)


def parse_card_counters(text):
    """{pid: percent summed over the process's engines} from typeperf's CSV (a header, then one row a sample; the last
    row is read). Empty when the text holds no rows."""
    rows = [r for r in csv.reader(io.StringIO(text)) if r]
    header = next((r for r in rows if r[0].startswith("(PDH-CSV")), None)
    if header is None:
        return {}
    data = [r for r in rows if r is not header and len(r) == len(header) and re.match(r"\d", r[0])]
    if not data:
        return {}
    out = {}
    for name, cell in zip(header[1:], data[-1][1:]):
        m = re.search(r"\(pid_(\d+)_", name)
        try:
            value = float(cell)
        except ValueError:
            continue
        if m:
            out[int(m.group(1))] = out.get(int(m.group(1)), 0.0) + value
    return out


def read_card_counters():
    """The performance counters' text (typeperf, two samples a second apart), or None when they cannot be read."""
    if os.name != "nt":
        return None
    try:
        done = subprocess.run(["typeperf", COUNTER, "-sc", "2"], capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    return done.stdout if done.returncode == 0 and "(PDH-CSV" in done.stdout else None


def card_use(reader=read_card_counters, lister=list_processes, own_pid=None):
    """(percent, detail): the card's use by programs other than this measure's own (every process below `own_pid`, and
    itself), summed over the engines of each. A process that no longer exists is left out (it is not using the card).
    percent None with detail "card use not read" when the counters or the process list cannot be read."""
    own_pid = os.getpid() if own_pid is None else own_pid
    text = reader()
    procs = lister()
    if text is None or procs is None:
        return None, "card use not read"
    per = parse_card_counters(text)
    if not per:
        return None, "card use not read"
    alive = {p["pid"]: p["name"] for p in procs}
    own = descendants(procs, own_pid) | {own_pid}
    others = {pid: v for pid, v in per.items() if pid in alive and pid not in own and v > 0}
    total = sum(others.values())
    top = sorted(others.items(), key=lambda kv: -kv[1])[:3]
    detail = "other programs use the card %.1f%%" % total
    if top:
        detail += " (" + ", ".join("%s %.1f%%" % (alive[pid], v) for pid, v in top) + ")"
    return total, detail


class Machine:
    """What the measure asks of the computer: wait for quiet, and read the card. The tests give it fakes."""

    def __init__(self, minutes=WAIT_MINUTES, own_pid=None, **fakes):
        self.minutes, self.own_pid, self.fakes = minutes, own_pid, fakes

    def wait_quiet(self):
        return wait_quiet(minutes=self.minutes, own_pid=self.own_pid, **{k: v for k, v in self.fakes.items()
                                                                     if k in ("lister", "sleep", "clock", "say")})

    def card_use(self):
        return card_use(own_pid=self.own_pid, **{k: v for k, v in self.fakes.items() if k in ("reader", "lister")})
