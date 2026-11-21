"""quiet.py: is the machine fit for a measure (T7a). A measure is trusted only when nothing else uses the computer's
speed: no compiler or build tool, no other copy of the game, the graphics card used by no other program, and the
processors used by no other program.

  wait_quiet   looks every 30 seconds, for up to 30 minutes, until no compiler, build tool or other copy of the game runs
  card_use     the card's use by programs other than this measure's own, from Windows' performance counters
               (\\GPU Engine(*)\\Utilization Percentage, summed for each process); the desktop's compositor (dwm.exe) is
               left out: it draws the game's window too and alone reached 11 %
  CpuWatch     the processors' time used over a run by everything but the measure's own processes (the game and what
               started it): the whole machine's busy time less the own processes' CPU time, read every two seconds

Every reading of the machine (the process list, the counters, the clock) is a function the caller can replace, so the
tests run with fakes and no game.
"""
import csv
import io
import json
import os
import re
import subprocess
import threading
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
CARD_LEFT_OUT = ("dwm.exe",)  # the desktop's compositor: not counted in other programs' use of the card
CPU_LIMIT = 10.0  # percent of all processors' time: other programs' use above this makes a measure not trusted
CPU_LOOK_SECONDS = 2.0
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
    others = {pid: v for pid, v in per.items()
              if pid in alive and pid not in own and v > 0 and alive[pid].lower() not in CARD_LEFT_OUT}
    total = sum(others.values())
    top = sorted(others.items(), key=lambda kv: -kv[1])[:3]
    detail = "other programs use the card %.1f%%" % total
    if top:
        detail += " (" + ", ".join("%s %.1f%%" % (alive[pid], v) for pid, v in top) + ")"
    return total, detail


def _filetime_seconds(ft):
    return (ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 1e7


def read_cpu_processes():
    """[{pid, ppid, name, created, cpu}] of this computer's processes (Windows): cpu is the process's kernel and user time in
    seconds, created its start (a time stamp; with the pid it names one process). Processes that cannot be opened (some of
    the system's own) are left out. None when the list cannot be read."""
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes

    class Entry(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD), ("th32ProcessID", wintypes.DWORD),
                    ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wintypes.DWORD),
                    ("cntThreads", wintypes.DWORD), ("th32ParentProcessID", wintypes.DWORD),
                    ("pcPriClassBase", wintypes.LONG), ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260)]
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    k32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    k32.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    k32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    k32.OpenProcess.restype = wintypes.HANDLE
    k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    k32.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    snapshot = k32.CreateToolhelp32Snapshot(2, 0)  # TH32CS_SNAPPROCESS
    if not snapshot or snapshot == ctypes.c_void_p(-1).value:
        return None
    out = []
    try:
        entry = Entry()
        entry.dwSize = ctypes.sizeof(Entry)
        more = k32.Process32FirstW(snapshot, ctypes.byref(entry))
        while more:
            handle = k32.OpenProcess(0x1000, False, entry.th32ProcessID)  # PROCESS_QUERY_LIMITED_INFORMATION
            if handle:
                made, ended, kernel, user = (wintypes.FILETIME() for _ in range(4))
                if k32.GetProcessTimes(handle, ctypes.byref(made), ctypes.byref(ended), ctypes.byref(kernel),
                                       ctypes.byref(user)):
                    out.append({"pid": int(entry.th32ProcessID), "ppid": int(entry.th32ParentProcessID),
                                "name": entry.szExeFile, "created": _filetime_seconds(made),
                                "cpu": _filetime_seconds(kernel) + _filetime_seconds(user)})
                k32.CloseHandle(handle)
            more = k32.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        k32.CloseHandle(snapshot)
    return out


def read_system_times():
    """(busy, total) in seconds since the system started (Windows): the time all processors together were not idle, and
    the time all together have run, idle included. None when it cannot be read."""
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.GetSystemTimes.argtypes = [ctypes.POINTER(wintypes.FILETIME)] * 3
    idle, kernel, user = (wintypes.FILETIME() for _ in range(3))
    if not k32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
        return None
    total = _filetime_seconds(kernel) + _filetime_seconds(user)  # the kernel's time includes the idle time
    return total - _filetime_seconds(idle), total


class CpuWatch:
    """The processors' time that programs other than this measure's own used between begin() and end(), as a percentage of
    all the processors' time. The own processes are this process and everything below it (the harness, the game, a game the
    host started in its place); their CPU time is read every `every` seconds by a thread (a process that ends keeps the
    time of its last look). The rest of the machine's busy time is the others': it includes the system's own work and the
    programs the process list cannot open, which it cannot name. The readings are functions the caller can replace, so the
    tests run with fakes."""

    def __init__(self, reader=read_cpu_processes, system_times=read_system_times, own_pid=None, every=CPU_LOOK_SECONDS):
        self.reader, self.system_times = reader, system_times
        self.own_pid = os.getpid() if own_pid is None else own_pid
        self.every = every
        self.seen = {}  # (pid, created) -> {name, own, base, last}
        self.own_pids = {self.own_pid}
        self.first_times = None
        self.begun = False
        self.stop, self.thread, self.lock = threading.Event(), None, threading.Lock()

    def sample(self):
        """One look at the processes."""
        procs = self.reader()
        if procs is None:
            return
        with self.lock:
            self.own_pids |= descendants(procs, self.own_pid)
            grew = True
            while grew:  # a process whose parent is an own process that ended (the list is not in order)
                grew = False
                for p in procs:
                    if p["pid"] not in self.own_pids and p["ppid"] in self.own_pids:
                        self.own_pids.add(p["pid"])
                        grew = True
            for p in procs:
                key = (p["pid"], p["created"])
                known = self.seen.get(key)
                if known is None:
                    # A process seen at the first look has run before it; one that came later started inside the watch.
                    known = self.seen[key] = {"name": p["name"], "own": False, "base": 0.0 if self.begun else p["cpu"],
                                              "last": p["cpu"]}
                known["last"] = max(known["last"], p["cpu"])
                if p["pid"] in self.own_pids:
                    known["own"] = True

    def begin(self, threaded=True):
        self.first_times = self.system_times()
        self.sample()
        self.begun = True
        if threaded:
            self.thread = threading.Thread(target=self._loop, daemon=True)
            self.thread.start()

    def _loop(self):
        while not self.stop.wait(self.every):
            try:
                self.sample()
            except Exception:  # a look that failed is a look missed
                pass

    def end(self):
        """(percent, detail), or (None, "processor use not read")."""
        self.stop.set()
        if self.thread is not None:
            self.thread.join(10)
        self.sample()
        last_times = self.system_times()
        if self.first_times is None or last_times is None or not self.seen:
            return None, "processor use not read"
        busy = last_times[0] - self.first_times[0]
        total = last_times[1] - self.first_times[1]
        if total <= 0:
            return None, "processor use not read"
        own = sum(max(0.0, k["last"] - k["base"]) for k in self.seen.values() if k["own"])
        percent = max(0.0, busy - own) / total * 100.0
        named = {}
        for k in self.seen.values():
            if not k["own"] and k["last"] > k["base"]:
                named[k["name"]] = named.get(k["name"], 0.0) + (k["last"] - k["base"]) / total * 100.0
        top = [(n, v) for n, v in sorted(named.items(), key=lambda kv: -kv[1])[:3] if v >= 0.05]
        detail = "other programs use the processors %.1f%%" % percent
        if top:
            detail += " (" + ", ".join("%s %.1f%%" % (n, v) for n, v in top) + ")"
        return percent, detail


class Machine:
    """What the measure asks of the computer: wait for quiet, read the card, watch the processors. The tests give it fakes."""

    def __init__(self, minutes=WAIT_MINUTES, own_pid=None, **fakes):
        self.minutes, self.own_pid, self.fakes = minutes, own_pid, fakes

    def wait_quiet(self):
        return wait_quiet(minutes=self.minutes, own_pid=self.own_pid, **{k: v for k, v in self.fakes.items()
                                                                     if k in ("lister", "sleep", "clock", "say")})

    def card_use(self):
        return card_use(own_pid=self.own_pid, **{k: v for k, v in self.fakes.items() if k in ("reader", "lister")})

    def cpu_watch(self):
        """A watch of the processors for one run (call begin() before it, end() after). The fakes: `cpu_reader`,
        `system_times`, `every`."""
        fakes = {"reader": self.fakes["cpu_reader"]} if "cpu_reader" in self.fakes else {}
        fakes.update({k: v for k, v in self.fakes.items() if k in ("system_times", "every")})
        return CpuWatch(own_pid=self.own_pid, **fakes)
