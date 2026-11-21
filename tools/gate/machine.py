"""machine.py: what a gate run asks of the machine before it starts the game, and what it leaves behind (package T5).

gate.py's machine_ready(g, ...) uses this file; every reading of the machine is a function that a test replaces with a
fake (the Probes class), so the rules are proved without the machine.

  Job object      the gate puts itself in one Windows job object that ends everything in it when the gate ends or is
                  killed. A program that must outlive the run is started with start_outliving (the leave-the-job flag).
  Leftovers       a game process under D:/NebulaWork/ whose parent is gone and that is older than ten minutes is ended,
                  unless a lock that is held (a gate, the long run, a measure) may own it.
  Disk            drive D: needs a floor of free space by level; old run folders are thinned first (thin_runs).
  The tablet      answers, is authorised, holds the test data of this tree's store version.
  The card        which graphics card the game uses is read from DXGI the way the game reads it (the first adapter of
                  IDXGIFactory6::EnumAdapterByGpuPreference with DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, or the one
                  LUMA_TEST_GPU names): its total memory is DXGI's DedicatedVideoMemory (SharedSystemMemory too for a
                  card with no memory of its own); what every program holds of it is Windows' performance counter
                  "GPU Adapter Memory(luid_..)\\Dedicated Usage", read through PDH by its English name (so it works in
                  every language and for every maker's card); each game process's use is "GPU Process Memory(pid_N_luid_..)".
  Copies at once  copies_at_once(): the largest number of game copies that fit in 70% of the card's free memory and in
                  70% of the PC's free memory at 2 GB a copy, at most what the lanes ask for. What one copy needs is
                  learned (Learned, <plan>/native/machine.json): the largest of the last 20 runs for each renderer, 3 GB
                  until known.
"""
import ctypes
import datetime
import json
import math
import os
import pathlib
import re
import shutil
import socket
import subprocess
import threading
import time

NT = os.name == "nt"
GB = 2 ** 30

# The root every work folder of the project lies under, and the game's programs by renderer (luma_test.py HOST["exes"]).
ROOT = "D:/NebulaWork/"
GAME_EXES = {"nebula.exe": "d3d12", "nebulavk.exe": "vulkan", "nebulad11.exe": "d3d11"}
LEFTOVER_AGE_SECONDS = 600.0

# Drive D: free space a run needs, by level. A landing's own level is "land"; the quick gate is as small as a landing.
DISK_NEED_GB = {"quick": 30, "land": 30, "main": 80, "long": 150, "final": 150}
KEEP_WHOLE = 3
KEEP_FILE = "keep.txt"      # a run folder with this file in it is kept whole (a stress lane's evidence): never thinned, never pruned
RUN_NAME = re.compile(r"^\d{8}-\d{6}-([0-9a-f]{6,40})-(quick|main|long|final|land)$")

# The graphics card and the PC's memory.
CARD_SHARE = 70             # percent of the card's free memory, and of the PC's, the copies may take
MEMORY_PER_COPY = 2 * GB    # what a game copy takes of the PC's own memory
DEFAULT_NEED = 3 * GB       # what a copy takes of the card until a run has shown it
LEARN_RUNS = 20
CARD_SHARED_ABOVE = 50      # percent of the card other programs hold above which a run says "card shared"
WAIT_LOOK_SECONDS = 60.0
WAIT_SAY_SECONDS = 600.0
WAIT_LIMIT_SECONDS = 4 * 3600.0
SAMPLE_SECONDS = 5.0

CREATE_BREAKAWAY_FROM_JOB = 0x01000000


# The job object -------------------------------------------------------------------------------------------------------------------


if NT:
    from ctypes import wintypes as wt

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wt.LPCWSTR]
    kernel32.CreateJobObjectW.restype = wt.HANDLE
    kernel32.SetInformationJobObject.argtypes = [wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD]
    kernel32.SetInformationJobObject.restype = wt.BOOL
    kernel32.AssignProcessToJobObject.argtypes = [wt.HANDLE, wt.HANDLE]
    kernel32.AssignProcessToJobObject.restype = wt.BOOL
    kernel32.GetCurrentProcess.restype = wt.HANDLE
    kernel32.CloseHandle.argtypes = [wt.HANDLE]
    kernel32.CloseHandle.restype = wt.BOOL
    kernel32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    kernel32.OpenProcess.restype = wt.HANDLE

    class _BasicLimits(ctypes.Structure):
        _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                    ("LimitFlags", wt.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                    ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wt.DWORD),
                    ("Affinity", ctypes.c_size_t), ("PriorityClass", wt.DWORD), ("SchedulingClass", wt.DWORD)]

    class _IoCounters(ctypes.Structure):
        _fields_ = [(n, ctypes.c_uint64) for n in ("ReadOps", "WriteOps", "OtherOps", "ReadBytes", "WriteBytes", "OtherBytes")]

    class _ExtendedLimits(ctypes.Structure):
        _fields_ = [("Basic", _BasicLimits), ("Io", _IoCounters), ("ProcessMemoryLimit", ctypes.c_size_t),
                    ("JobMemoryLimit", ctypes.c_size_t), ("PeakProcessMemoryUsed", ctypes.c_size_t),
                    ("PeakJobMemoryUsed", ctypes.c_size_t)]

JOB_OBJECT_LIMIT_BREAKAWAY_OK = 0x800
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000
JOB_OBJECT_EXTENDED_LIMIT_INFORMATION = 9


class Job:
    """A Windows job object that ends every process in it when its last handle closes (the gate's own exit or kill closes it),
    and lets a child leave it on request (CREATE_BREAKAWAY_FROM_JOB)."""

    def __init__(self, handle):
        self.handle = handle

    @classmethod
    def make(cls):
        """A new job object. OSError (with the Windows error) when it cannot be made."""
        handle = kernel32.CreateJobObjectW(None, None)
        if not handle:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = _ExtendedLimits()
        limits.Basic.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK
        if not kernel32.SetInformationJobObject(handle, JOB_OBJECT_EXTENDED_LIMIT_INFORMATION, ctypes.byref(limits),
                                                ctypes.sizeof(limits)):
            error = ctypes.WinError(ctypes.get_last_error())
            kernel32.CloseHandle(handle)
            raise error
        return cls(handle)

    def add_self(self):
        """Puts this process in the job (its children, and theirs, join it as they start). OSError when Windows refuses."""
        if not kernel32.AssignProcessToJobObject(self.handle, kernel32.GetCurrentProcess()):
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        """Closes the job: every process still in it ends."""
        handle, self.handle = self.handle, None
        if handle:
            kernel32.CloseHandle(handle)


# The run's job, kept so that its handle stays open as long as the gate does.
RUN_JOB = [None]


def join_new_job(say=print):
    """At the gate's start: makes the run's job object and puts the gate in it. Returns the Job, or None when nothing is
    done: not Windows (nothing is done, no line), or Windows refused (one `tool` line says so, and the run goes on)."""
    if not NT:
        return None
    try:
        job = Job.make()
        job.add_self()
    except OSError as error:
        say("gate.py: tool: the run's job object could not be made or joined (%s): programs the run starts may stay behind "
            "if it is killed" % str(error).strip())
        return None
    RUN_JOB[0] = job
    return job


def outliving_flags(flags=0):
    """`flags` (creation flags) with the leave-the-job flag added; unchanged where there is no job object."""
    return flags | CREATE_BREAKAWAY_FROM_JOB if NT else flags


def start_outliving(popen, command, say=print, **kwargs):
    """Starts a program that must outlive the run (the detached speed measure, the nightly run, the adb server): with the
    leave-the-job flag on Windows. Where Windows refuses the flag (a job around the gate that does not allow it) the program is
    started without it and one `tool` line says it will end with the run. Returns what popen returns."""
    if not NT:
        return popen(command, **kwargs)
    flagged = dict(kwargs, creationflags=outliving_flags(kwargs.get("creationflags", 0)))
    try:
        return popen(command, **flagged)
    except OSError as error:
        if getattr(error, "winerror", None) != 5:   # ERROR_ACCESS_DENIED: breakaway is not allowed here
            raise
        say("gate.py: tool: a program meant to outlive the run could not leave the run's job (%s): it ends with the run"
            % str(error).strip())
        return popen(command, **kwargs)


# The adb server -------------------------------------------------------------------------------------------------------------------

ADB_PORT = 5037
_ADB_READY = [False]


def adb_server_up(port=ADB_PORT, connect=socket.create_connection):
    """Whether an adb server answers on its port (a connection only: no adb program is run)."""
    try:
        connect(("127.0.0.1", port), timeout=1.0).close()
        return True
    except OSError:
        return False


def ensure_adb_server(adb_path, say=print, up=adb_server_up, popen=subprocess.Popen, wait=time.sleep):
    """Before the first adb call of a run: when no adb server runs, the gate starts it itself with the leave-the-job flag, so
    the end of a run never takes down the server under another program that uses the tablet. Once a process. Returns True when
    a server answers afterwards."""
    if _ADB_READY[0]:
        return True
    if up():
        _ADB_READY[0] = True
        return True
    try:
        process = start_outliving(popen, [adb_path, "start-server"], say, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL, creationflags=0x08000000 if NT else 0)  # CREATE_NO_WINDOW
        try:
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            pass
    except OSError as error:
        say("gate.py: tool: the adb server could not be started: %s" % error)
        return False
    for _ in range(10):
        if up():
            _ADB_READY[0] = True
            return True
        wait(0.5)
    return False


# The compiler cache's server ----------------------------------------------------------------------------------------------------

# What tools/gate/build_windows.ps1 gives the cache (its defaults and the environment it sets); a test keeps the two equal. The
# server reads these when it starts, so the one the gate starts here is the one the build would have started.
SCCACHE_PROGRAM = "D:/NebulaWork/tools/sccache/sccache.exe"
SCCACHE_PORT = 4227
SCCACHE_ENV = {"SCCACHE_DIR": "D:/NebulaWork/sccache", "SCCACHE_CACHE_SIZE": "60G", "SCCACHE_SERVER_PORT": str(SCCACHE_PORT)}
_SCCACHE_READY = [False]


def ensure_sccache_server(program=SCCACHE_PROGRAM, say=print, exists=os.path.exists, up=None, popen=subprocess.Popen,
                          wait=time.sleep, env=None):
    """Before the gate's Windows build: when the compiler cache is in use (its program is there) and no server answers on the
    build's port, the gate starts the server itself with the leave-the-job flag, so it is never inside a run's job: a build of
    another folder using it is not cut off when this run ends. Once a process. Returns True when a server answers (or the cache is
    not in use)."""
    if _SCCACHE_READY[0] or not exists(program):
        return True
    up = up or (lambda: adb_server_up(SCCACHE_PORT))
    if up():
        _SCCACHE_READY[0] = True
        return True
    environment = dict(os.environ if env is None else env)
    environment.update(SCCACHE_ENV)
    environment.pop("SCCACHE_BASEDIRS", None)
    try:
        process = start_outliving(popen, [program, "--start-server"], say, env=environment, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=0x08000000 if NT else 0)
        try:
            process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            pass
    except OSError as error:
        say("gate.py: tool: the compiler cache's server could not be started: %s" % error)
        return False
    for _ in range(20):
        if up():
            _SCCACHE_READY[0] = True
            return True
        wait(0.5)
    return False


# Processes ------------------------------------------------------------------------------------------------------------------------


class Proc:
    """One process: its id, its parent's id, program name, full program path (forward slashes) and start (epoch seconds)."""

    def __init__(self, pid, ppid, name, path, started):
        self.pid, self.ppid, self.name, self.path, self.started = pid, ppid, name, path, started

    def __repr__(self):
        return "Proc(%s, %s, %r)" % (self.pid, self.ppid, self.path or self.name)


if NT:
    class _ProcessEntry(ctypes.Structure):
        _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                    ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD),
                    ("th32ParentProcessID", wt.DWORD), ("pcPriClassBase", wt.LONG), ("dwFlags", wt.DWORD),
                    ("szExeFile", wt.WCHAR * 260)]

    kernel32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
    kernel32.CreateToolhelp32Snapshot.restype = wt.HANDLE
    kernel32.Process32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(_ProcessEntry)]
    kernel32.Process32FirstW.restype = wt.BOOL
    kernel32.Process32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(_ProcessEntry)]
    kernel32.Process32NextW.restype = wt.BOOL
    kernel32.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)]
    kernel32.QueryFullProcessImageNameW.restype = wt.BOOL
    kernel32.GetProcessTimes.argtypes = [wt.HANDLE, ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME),
                                         ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME)]
    kernel32.GetProcessTimes.restype = wt.BOOL


def list_processes():
    """Every process of this computer as Proc (Windows: the tool-help snapshot, the path and start by the process's handle;
    those the user may not open have no path and no start). Elsewhere: none."""
    if not NT:
        return []
    out = []
    snapshot = kernel32.CreateToolhelp32Snapshot(2, 0)   # TH32CS_SNAPPROCESS
    if not snapshot or snapshot == wt.HANDLE(-1).value:
        return out
    try:
        entry = _ProcessEntry()
        entry.dwSize = ctypes.sizeof(entry)
        more = kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
        while more:
            pid = entry.th32ProcessID
            path, started = "", None
            handle = kernel32.OpenProcess(0x1000, False, pid) if pid else None   # PROCESS_QUERY_LIMITED_INFORMATION
            if handle:
                try:
                    size = wt.DWORD(1024)
                    buffer = ctypes.create_unicode_buffer(1024)
                    if kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
                        path = buffer.value.replace("\\", "/")
                    created, ended, kernel_time, user_time = (wt.FILETIME() for _ in range(4))
                    if kernel32.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(ended), ctypes.byref(kernel_time),
                                                ctypes.byref(user_time)):
                        ticks = (created.dwHighDateTime << 32) | created.dwLowDateTime
                        started = (ticks - 116444736000000000) / 1e7
                finally:
                    kernel32.CloseHandle(handle)
            out.append(Proc(pid, entry.th32ParentProcessID, entry.szExeFile, path, started))
            more = kernel32.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        kernel32.CloseHandle(snapshot)
    return out


def end_process_tree(pid):
    """Ends a process and the processes it started. True when taskkill said it did."""
    if NT:
        r = subprocess.run(["taskkill", "/T", "/F", "/PID", str(pid)], capture_output=True)
        return r.returncode == 0
    try:
        os.kill(pid, 9)
        return True
    except OSError:
        return False


def under(path, root):
    """Whether a path (any slashes, any case) lies under a root folder."""
    return path.replace("\\", "/").lower().startswith(root.replace("\\", "/").lower().rstrip("/") + "/")


def is_game(proc, root=ROOT):
    return bool(proc.path) and proc.name.lower() in GAME_EXES and under(proc.path, root)


class Holder:
    """A lock folder that is held: its text, the process it names (None: held by hand) and that process's start."""

    def __init__(self, lock, text, pid, started):
        self.lock, self.text, self.pid, self.started = lock, text, pid, started


def read_holders(locks, procs, own_pid=None):
    """The held locks that may own a game process: for each lock folder with a holder.txt, the holder's text and the `pid N` it
    names. A holder whose process has ended is stale and owns nothing; this run's own lock owns nothing yet (it has started no
    game); a lock with no process in its holder line was taken by hand and may own anything."""
    own_pid = os.getpid() if own_pid is None else own_pid
    by_pid = {p.pid: p for p in procs}
    out = []
    for lock in locks:
        holder = pathlib.Path(lock) / "holder.txt"
        if not pathlib.Path(lock).is_dir():
            continue
        try:
            text = holder.read_text(encoding="utf-8", errors="replace").strip()
        except OSError:
            text = "unknown"
        m = re.search(r"\bpid (\d+)", text)
        pid = int(m.group(1)) if m else None
        if pid == own_pid:
            continue
        if pid is not None:
            if pid not in by_pid:
                continue
            out.append(Holder(str(lock), text, pid, by_pid[pid].started))
        else:
            out.append(Holder(str(lock), text, None, None))
    return out


def find_leftovers(procs, now, holders=(), root=ROOT, min_age=LEFTOVER_AGE_SECONDS):
    """The game processes under `root` that were left behind: their parent no longer exists (no such process, or one that
    began after the child: the number was used again) and they started more than `min_age` seconds ago. A process that a held
    lock may own (it started after that lock's holder did, or the lock was taken by hand) is not one. Returns (leftovers,
    [(process, why it was kept)])."""
    by_pid = {p.pid: p for p in procs}
    leftovers, kept = [], []
    for p in procs:
        if not is_game(p, root) or p.started is None:
            continue
        parent = by_pid.get(p.ppid)
        if parent is not None and (parent.started is None or parent.started <= p.started + 1.0):
            continue     # its parent is there
        if now - p.started <= min_age:
            continue
        owner = next((h for h in holders if h.started is None or p.started >= h.started - 1.0), None)
        if owner is not None:
            kept.append((p, "the lock %s is held (%s)" % (owner.lock, owner.text)))
            continue
        leftovers.append(p)
    return leftovers, kept


# Disk -----------------------------------------------------------------------------------------------------------------------------


def disk_need(level):
    """The free bytes drive D: needs for a run of this level."""
    return DISK_NEED_GB.get(level, DISK_NEED_GB["main"]) * GB


def folder_size(path):
    path = pathlib.Path(path)
    if path.is_file():
        return path.stat().st_size
    total = 0
    for f in path.rglob("*"):
        try:
            if f.is_file():
                total += f.stat().st_size
        except OSError:
            continue
    return total


def run_folders(work):
    """The finished gate runs in a work folder: [(name, folder, commit, level, passed)], oldest first. A folder that is not a run
    by its name, or has no readable result.json (a run still going, or one that never wrote its result), is not listed."""
    out = []
    try:
        entries = sorted(pathlib.Path(work).iterdir(), key=lambda p: p.name)
    except OSError:
        return out
    for d in entries:
        m = RUN_NAME.match(d.name)
        if m is None or not d.is_dir():
            continue
        try:
            passed = json.loads((d / "result.json").read_text(encoding="utf-8")).get("passed") is True
        except (OSError, ValueError, AttributeError):
            continue
        out.append((d.name, d, m.group(1), m.group(2), passed))
    return out


def runs_to_thin(runs, keep=KEEP_WHOLE, held=()):
    """Which runs stay whole: for each level the `keep` newest that passed and the `keep` newest that did not, any run whose
    commit is in `held` (a run still waiting for GitHub) and any run with a keep.txt in its folder (a stress result's evidence).
    Returns (whole names, names to thin)."""
    whole = set()
    for level in {r[3] for r in runs}:
        for passed in (True, False):
            same = [r for r in runs if r[3] == level and r[4] is passed]
            whole.update(r[0] for r in same[-keep:])
    whole.update(r[0] for r in runs if r[2][:9] in held or any(h.startswith(r[2][:9]) for h in held))
    whole.update(r[0] for r in runs if (pathlib.Path(r[1]) / KEEP_FILE).is_file())
    return whole, [r[0] for r in runs if r[0] not in whole]


def thin_run(folder):
    """Thins one run folder: every folder inside it goes, the files at its top level stay. Returns the bytes freed."""
    freed = 0
    for entry in list(pathlib.Path(folder).iterdir()):
        try:
            if entry.is_dir() and not entry.is_symlink():
                freed += folder_size(entry)
                shutil.rmtree(entry, ignore_errors=True)
        except OSError:
            continue
    return freed


def thin_runs(work, keep=KEEP_WHOLE, held=()):
    """Thins the old run folders of a work folder (runs_to_thin says which stay whole). Returns (runs thinned, bytes freed)."""
    runs = run_folders(work)
    _, thin = runs_to_thin(runs, keep, held)
    by_name = {r[0]: r[1] for r in runs}
    count, freed = 0, 0
    for name in thin:
        got = thin_run(by_name[name])
        if got:
            count += 1
            freed += got
    return count, freed


# The graphics card and the PC's memory ------------------------------------------------------------------------------------------


class Card:
    """The graphics card the game uses: its name, its total memory, what every program holds of it, and where the reading
    came from."""

    def __init__(self, name, total, used, luid=None, source="", luids=None):
        self.name, self.total, self.used, self.luid, self.source = name, total, used, luid, source
        self.luids = set(luids or ([luid] if luid else ()))      # every LUID Windows lists the card under

    @property
    def free(self):
        return max(0, self.total - self.used)

    def shared(self):
        """Other programs hold more than half of the card."""
        return self.total > 0 and self.used * 100 > self.total * CARD_SHARED_ABOVE


if NT:
    pdh = ctypes.WinDLL("pdh")
    pdh.PdhOpenQueryW.argtypes = [wt.LPCWSTR, ctypes.c_size_t, ctypes.POINTER(wt.HANDLE)]
    pdh.PdhAddEnglishCounterW.argtypes = [wt.HANDLE, wt.LPCWSTR, ctypes.c_size_t, ctypes.POINTER(wt.HANDLE)]
    pdh.PdhCollectQueryData.argtypes = [wt.HANDLE]
    pdh.PdhGetFormattedCounterArrayW.argtypes = [wt.HANDLE, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.POINTER(wt.DWORD),
                                                 ctypes.c_void_p]
    pdh.PdhCloseQuery.argtypes = [wt.HANDLE]
    for _f in (pdh.PdhOpenQueryW, pdh.PdhAddEnglishCounterW, pdh.PdhCollectQueryData, pdh.PdhGetFormattedCounterArrayW,
               pdh.PdhCloseQuery):
        _f.restype = wt.DWORD

    class _CounterItem(ctypes.Structure):
        _fields_ = [("szName", wt.LPWSTR), ("CStatus", wt.DWORD), ("largeValue", ctypes.c_int64)]

PDH_FMT_LARGE = 0x400
PDH_MORE_DATA = 0x800007D2


class Counters:
    """Windows performance counters by their English paths, read through PDH: one query for several wildcard paths. read()
    returns {path: {instance: value}}. Raises OSError when PDH or a counter set is not there (an old Windows, no graphics
    counters)."""

    def __init__(self, paths):
        if not NT:
            raise OSError("no performance counters here")
        self.query = wt.HANDLE()
        self.counters = {}
        if pdh.PdhOpenQueryW(None, 0, ctypes.byref(self.query)) != 0:
            raise OSError("PdhOpenQuery failed")
        try:
            for path in paths:
                counter = wt.HANDLE()
                status = pdh.PdhAddEnglishCounterW(self.query, path, 0, ctypes.byref(counter))
                if status != 0:
                    raise OSError("the counter %s is not there (PDH 0x%08X)" % (path, status))
                self.counters[path] = counter
        except OSError:
            self.close()
            raise

    def read(self):
        pdh.PdhCollectQueryData(self.query)
        out = {}
        for path, counter in self.counters.items():
            size, count = wt.DWORD(0), wt.DWORD(0)
            status = pdh.PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, ctypes.byref(size), ctypes.byref(count), None)
            if status != PDH_MORE_DATA:
                out[path] = {}
                continue
            buffer = ctypes.create_string_buffer(size.value)
            status = pdh.PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, ctypes.byref(size), ctypes.byref(count), buffer)
            values = {}
            if status == 0:
                items = ctypes.cast(buffer, ctypes.POINTER(_CounterItem))
                for i in range(count.value):
                    if items[i].CStatus in (0, 1):    # PDH_CSTATUS_VALID_DATA, PDH_CSTATUS_NEW_DATA
                        values[items[i].szName] = items[i].largeValue
            out[path] = values
        return out

    def close(self):
        if getattr(self, "query", None):
            pdh.PdhCloseQuery(self.query)
            self.query = None


ADAPTER_DEDICATED = r"\GPU Adapter Memory(*)\Dedicated Usage"
ADAPTER_SHARED = r"\GPU Adapter Memory(*)\Shared Usage"
PROCESS_DEDICATED = r"\GPU Process Memory(*)\Dedicated Usage"
_LUID_PART = re.compile(r"luid_0x([0-9a-f]+)_0x([0-9a-f]+)_phys_\d+", re.IGNORECASE)
_PID_PART = re.compile(r"pid_(\d+)_", re.IGNORECASE)


def luid_of(instance):
    """(high, low) of the adapter a counter instance names (`luid_0x00000000_0x0001a050_phys_0`), or None."""
    m = _LUID_PART.search(instance)
    return (int(m.group(1), 16), int(m.group(2), 16)) if m else None


if NT:
    class _Luid(ctypes.Structure):
        _fields_ = [("LowPart", wt.DWORD), ("HighPart", wt.LONG)]

    class _AdapterDesc1(ctypes.Structure):
        _fields_ = [("Description", wt.WCHAR * 128), ("VendorId", wt.UINT), ("DeviceId", wt.UINT), ("SubSysId", wt.UINT),
                    ("Revision", wt.UINT), ("DedicatedVideoMemory", ctypes.c_size_t), ("DedicatedSystemMemory", ctypes.c_size_t),
                    ("SharedSystemMemory", ctypes.c_size_t), ("AdapterLuid", _Luid), ("Flags", wt.UINT)]

    class _Guid(ctypes.Structure):
        _fields_ = [("Data1", wt.DWORD), ("Data2", wt.WORD), ("Data3", wt.WORD), ("Data4", ctypes.c_ubyte * 8)]


def _guid(text):
    import uuid
    u = uuid.UUID(text)
    g = _Guid()
    g.Data1, g.Data2, g.Data3 = u.time_low, u.time_mid, u.time_hi_version
    for i, b in enumerate(u.bytes[8:]):
        g.Data4[i] = b
    return g


def _call(pointer, index, restype, *argtypes):
    """COM method `index` of the interface at `pointer`, as a function that takes the interface first."""
    table = ctypes.cast(ctypes.cast(pointer, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
    return ctypes.WINFUNCTYPE(restype, ctypes.c_void_p, *argtypes)(table[index])


def dxgi_adapters():
    """The graphics adapters in the order Windows ranks them for performance, the order the game takes its card in
    (IDXGIFactory6::EnumAdapterByGpuPreference, high performance; the factory's own order where DXGI 1.6 is missing): a list of
    dicts name, vendor, device, subsystem, luid (high, low), dedicated, shared, software. Raises OSError when DXGI cannot be read."""
    if not NT:
        raise OSError("DXGI is Windows'")
    dxgi = ctypes.WinDLL("dxgi")
    create = dxgi.CreateDXGIFactory1
    create.argtypes = [ctypes.POINTER(_Guid), ctypes.POINTER(ctypes.c_void_p)]
    create.restype = ctypes.c_long
    factory = ctypes.c_void_p()
    if create(ctypes.byref(_guid("770aae78-f26f-4dba-a829-253c83d1b387")), ctypes.byref(factory)) != 0:   # IDXGIFactory1
        raise OSError("CreateDXGIFactory1 failed")
    adapters = []
    iid_adapter1 = _guid("29038f61-3839-4626-91fd-086879011a05")
    factory6 = ctypes.c_void_p()
    query = _call(factory, 0, ctypes.c_long, ctypes.POINTER(_Guid), ctypes.POINTER(ctypes.c_void_p))
    if query(factory, ctypes.byref(_guid("c1b6694f-ff09-44a9-b03c-77900a0a1d17")), ctypes.byref(factory6)) != 0:   # IDXGIFactory6
        factory6 = None

    def describe(adapter):
        get_desc = _call(adapter, 10, ctypes.c_long, ctypes.POINTER(_AdapterDesc1))     # IDXGIAdapter1::GetDesc1
        desc = _AdapterDesc1()
        if get_desc(adapter, ctypes.byref(desc)) != 0:
            return None
        return {"name": desc.Description, "vendor": desc.VendorId, "device": desc.DeviceId, "subsystem": desc.SubSysId,
                "luid": (desc.AdapterLuid.HighPart & 0xFFFFFFFF, desc.AdapterLuid.LowPart),
                "dedicated": desc.DedicatedVideoMemory, "shared": desc.SharedSystemMemory,
                "software": bool(desc.Flags & 2)}                                        # DXGI_ADAPTER_FLAG_SOFTWARE

    try:
        index = 0
        while True:
            adapter = ctypes.c_void_p()
            if factory6 is not None:
                enum = _call(factory6, 29, ctypes.c_long, wt.UINT, ctypes.c_int, ctypes.POINTER(_Guid),
                             ctypes.POINTER(ctypes.c_void_p))                # IDXGIFactory6::EnumAdapterByGpuPreference
                code = enum(factory6, index, 2, ctypes.byref(iid_adapter1), ctypes.byref(adapter))   # 2: high performance
            else:
                enum = _call(factory, 12, ctypes.c_long, wt.UINT, ctypes.POINTER(ctypes.c_void_p))   # EnumAdapters1
                code = enum(factory, index, ctypes.byref(adapter))
            if code != 0 or not adapter.value:
                break
            try:
                info = describe(adapter)
                if info is not None:
                    adapters.append(info)
            finally:
                _call(adapter, 2, wt.ULONG)(adapter)                                      # Release
            index += 1
    finally:
        if factory6 is not None:
            _call(factory6, 2, wt.ULONG)(factory6)
        _call(factory, 2, wt.ULONG)(factory)
    return adapters


def game_adapter(adapters, gpu_env=""):
    """The adapter the game plays on: the one LUMA_TEST_GPU names (vendor:device:subsystem in hex, as the game's config.ini gpu)
    when it is there, else the first adapter that is not a software one, in the performance order dxgi_adapters gives, which is the
    order the game takes. None when there is none."""
    real = [a for a in adapters if not a["software"]]
    if gpu_env:
        for a in real:
            if gpu_env.lower() == "%04x:%04x:%08x" % (a["vendor"], a["device"], a["subsystem"]):
                return a
    return real[0] if real else None


def card_total(adapter):
    """The memory a game can use on the adapter: its own (dedicated) memory; a card with next to none (an integrated one) shares
    the PC's, so its own and the shared amount DXGI names."""
    return adapter["dedicated"] if adapter["dedicated"] >= GB else adapter["dedicated"] + adapter["shared"]


def card_used(adapter, values, same_card=None):
    """What every program holds of the adapter, from the adapter counters' values {path: {instance: value}}: the instances of its
    LUID, or of any LUID in `same_card` (Windows may list one card under two LUIDs), summed over their physical parts; an
    integrated card's shared use too. None when no counter instance names the card at all (then nothing is known of its use)."""
    luids = set(same_card or ()) | {adapter["luid"]}
    paths = [ADAPTER_DEDICATED] + ([ADAPTER_SHARED] if adapter["dedicated"] < GB else [])
    used, seen = 0, False
    for path in paths:
        for instance, value in values.get(path, {}).items():
            if luid_of(instance) in luids:
                used += value
                seen = True
    return used if seen else None


def read_card(gpu_env=None, adapters=dxgi_adapters, counters=Counters):
    """The card the game uses, read now: a Card. None when the machine has no real graphics adapter; OSError when the card's
    memory or what is held of it cannot be read."""
    every = adapters()
    adapter = game_adapter(every, os.environ.get("LUMA_TEST_GPU", "") if gpu_env is None else gpu_env)
    if adapter is None:
        return None
    reader = counters([ADAPTER_DEDICATED, ADAPTER_SHARED])
    try:
        values = reader.read()
    finally:
        reader.close()
    same = [a["luid"] for a in every if (a["vendor"], a["device"], a["subsystem"]) ==
            (adapter["vendor"], adapter["device"], adapter["subsystem"])]
    used = card_used(adapter, values, same)
    if used is None:
        raise OSError("no performance counter names the card %s (luid %s)" % (adapter["name"], adapter["luid"]))
    return Card(adapter["name"], card_total(adapter), used, adapter["luid"],
                "DXGI adapter %04x:%04x, counters GPU Adapter Memory" % (adapter["vendor"], adapter["device"]), same)


if NT:
    class _MemoryStatus(ctypes.Structure):
        _fields_ = [("dwLength", wt.DWORD), ("dwMemoryLoad", wt.DWORD), ("ullTotalPhys", ctypes.c_uint64),
                    ("ullAvailPhys", ctypes.c_uint64), ("ullTotalPageFile", ctypes.c_uint64),
                    ("ullAvailPageFile", ctypes.c_uint64), ("ullTotalVirtual", ctypes.c_uint64),
                    ("ullAvailVirtual", ctypes.c_uint64), ("ullAvailExtendedVirtual", ctypes.c_uint64)]


def memory_free():
    """The PC's free (available) physical memory in bytes, or None when it cannot be read."""
    if NT:
        status = _MemoryStatus()
        status.dwLength = ctypes.sizeof(status)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return status.ullAvailPhys
        return None
    try:
        return os.sysconf("SC_AVPHYS_PAGES") * os.sysconf("SC_PAGE_SIZE")
    except (ValueError, OSError, AttributeError):
        return None


def copies_at_once(card_free, memory_free_bytes, need, asked):
    """(copies, by the card, by the memory): the largest number of game copies that fit in 70% of the card's free memory
    (`need` bytes each) and in 70% of the PC's free memory (2 GB each), at most `asked`. A reading that is None sets no limit."""
    by_card = asked if card_free is None else (card_free * CARD_SHARE // 100) // max(1, need)
    by_memory = asked if memory_free_bytes is None else (memory_free_bytes * CARD_SHARE // 100) // MEMORY_PER_COPY
    return int(min(by_card, by_memory, asked)), int(by_card), int(by_memory)


def gb(n):
    return "%.1f GB" % (n / GB)


# What one copy needs -------------------------------------------------------------------------------------------------------------


class Learned:
    """<plan>/native/machine.json: for each renderer the card memory the biggest game process of each of the last 20 runs held
    at most. need() is the largest of them (3 GB for a renderer none has shown)."""

    def __init__(self, path):
        self.path = pathlib.Path(path)

    def read(self):
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
            peaks = data["peaks"]
            return {r: [int(x) for x in v][-LEARN_RUNS:] for r, v in peaks.items() if isinstance(v, list)}
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            return {}

    def need(self, renderers):
        """What one copy takes: the largest of what the renderers have shown, 3 GB for one with no figure yet."""
        peaks = self.read()
        return max([max(peaks[r]) if peaks.get(r) else DEFAULT_NEED for r in renderers] or [DEFAULT_NEED])

    def add(self, run_peaks, now=None):
        """Adds one run's largest use per renderer ({renderer: bytes}, a renderer that was not seen left out) and keeps the last
        20 of each. Written whole and moved into place."""
        run_peaks = {r: int(v) for r, v in run_peaks.items() if v and v > 0}
        if not run_peaks:
            return
        peaks = self.read()
        for r, v in run_peaks.items():
            peaks[r] = (peaks.get(r, []) + [v])[-LEARN_RUNS:]
        self.path.parent.mkdir(parents=True, exist_ok=True)
        body = {"about": "the card memory the largest game process of each run held, per renderer, the last %d runs; written by "
                         "tools/gate/machine.py" % LEARN_RUNS, "peaks": peaks,
                "updated": (now or datetime.datetime.now()).isoformat(timespec="seconds")}
        tmp = self.path.with_name(self.path.name + ".tmp")
        tmp.write_text(json.dumps(body, indent=2) + "\n", encoding="utf-8")
        os.replace(tmp, self.path)


# Copies at once, and the wait ----------------------------------------------------------------------------------------------------


class Room:
    """The answer of the wait: copies that fit, whether the card is shared, the reading it came from, and (when no copy fitted
    for four hours) the line that says why."""

    def __init__(self, copies, shared, card, by_card, by_memory, memory, need, env=None, card_error=""):
        self.copies, self.shared, self.card, self.by_card, self.by_memory = copies, shared, card, by_card, by_memory
        self.memory, self.need, self.env, self.card_error = memory, need, env, card_error


def look(probes, need, asked):
    """One look at the card and the memory: a Room (env None)."""
    card_error = ""
    try:
        card = probes.card()
    except OSError as error:        # the card cannot be read: it sets no limit (the PC's memory still does)
        card, card_error = None, str(error)
    memory = probes.memory_free()
    copies, by_card, by_memory = copies_at_once(None if card is None else card.free, memory, need, asked)
    return Room(copies, card is not None and card.shared(), card, by_card, by_memory, memory, need, card_error=card_error)


def wait_for_room(probes, need, asked, say=print):
    """Looks at the card and the memory; when not even one copy fits waits, looking every minute and saying so every ten, up to
    four hours. Returns a Room; env is the line when no copy fitted in time."""
    began = probes.clock()
    next_say = began + WAIT_SAY_SECONDS
    said_first = False
    room = look(probes, need, asked)
    while room.copies < 1:
        now = probes.clock()
        if now - began >= WAIT_LIMIT_SECONDS:
            room.env = no_room_line(room)
            return room
        if not said_first:
            say("gate.py: not even one copy of the game fits now (%s); looking again every minute, up to %.0f hours" % (
                no_room_line(room, short=True), WAIT_LIMIT_SECONDS / 3600))
            said_first = True
        elif now >= next_say:
            say("gate.py: still waiting for room for one copy of the game (%s); %.0f minutes so far, up to %.0f" % (
                no_room_line(room, short=True), (now - began) / 60, WAIT_LIMIT_SECONDS / 60))
            next_say += WAIT_SAY_SECONDS
        probes.sleep(WAIT_LOOK_SECONDS)
        room = look(probes, need, asked)
    return room


def no_room_line(room, short=False):
    """Why no copy fits, in plain words."""
    if room.by_card < 1 and room.card is not None:
        held = gb(room.card.used)
        if short:
            return "other programs hold %s of the card's %s, one copy needs %s" % (held, gb(room.card.total), gb(room.need))
        return "the graphics card has no room for one copy of the game: other programs hold %s" % held
    memory = gb(room.memory) if room.memory is not None else "no reading"
    if short:
        return "the PC has %s of memory free, a copy needs %s" % (memory, gb(MEMORY_PER_COPY))
    return "the PC's memory has no room for one copy of the game: %s free, a copy needs %s" % (memory, gb(MEMORY_PER_COPY))


class Slots:
    """How many game copies run at once: a lane takes `weight` slots before it starts the game and gives them back after. A weight
    above the whole is the whole."""

    def __init__(self, total):
        self.total = max(1, int(total))
        self.free = self.total
        self.cond = threading.Condition()

    def take(self, weight=1):
        weight = min(max(1, weight), self.total)
        with self.cond:
            while self.free < weight:
                self.cond.wait()
            self.free -= weight
        return weight

    def give(self, weight):
        with self.cond:
            self.free += weight
            self.cond.notify_all()

    def holding(self, weight=1):
        slots = self

        class Hold:
            def __enter__(self):
                self.weight = slots.take(weight)
                return self

            def __exit__(self, *exc):
                slots.give(self.weight)
        return Hold()


# The machine's probes (tests put fakes in these) ------------------------------------------------------------------------------


class Probes:
    """Every reading and action machine_ready makes of the machine, in one place."""

    def __init__(self):
        self.clock = time.time
        self.sleep = time.sleep

    def free_disk(self, path="D:/"):
        return shutil.disk_usage(path).free

    def processes(self):
        return list_processes()

    def end(self, pid):
        return end_process_tree(pid)

    def card(self):
        return read_card()

    def memory_free(self):
        return memory_free()

    def sampler(self, work, luids, say=print):
        """The thread that reads the game processes' use of the card during the run (not started)."""
        return Sampler(work, luids, say)


# The sampler: what the game processes of a run hold of the card -----------------------------------------------------------------


class Sampler:
    """A light thread: every few seconds it reads what each game process of this run holds of the card and keeps the largest
    for each renderer. A game process of the run is one under the run's work folder, or one that descends from the gate. It
    never fails or slows a run: any error ends the reading after one warning line."""

    def __init__(self, work, card_luids=None, say=print, interval=SAMPLE_SECONDS, processes=list_processes, counters=Counters,
                 gate_pid=None, sleep=None):
        self.work, self.luids, self.say, self.interval = str(work), set(card_luids) if card_luids else None, say, interval
        self.processes, self.counters_factory = processes, counters
        self.gate_pid = os.getpid() if gate_pid is None else gate_pid
        self.peaks = {}
        self.stop_event = threading.Event()
        self.thread = None
        self.failed = False
        self.reads = 0

    def start(self):
        self.thread = threading.Thread(target=self.run, name="machine-sampler", daemon=True)
        self.thread.start()
        return self

    def stop(self):
        self.stop_event.set()
        if self.thread is not None:
            self.thread.join(timeout=10.0)
        return dict(self.peaks)

    def run(self):
        if not NT:
            return          # no performance counters here: nothing to read, nothing to say
        reader = None
        try:
            reader = self.counters_factory([PROCESS_DEDICATED])
            while not self.stop_event.is_set():
                self.sample(reader)
                self.stop_event.wait(self.interval)
        except Exception as error:  # noqa: BLE001 - a reading that fails must never fail the run
            self.failed = True
            try:
                self.say("gate.py: tool: the card's use by the game was not read (%s: %s); what one copy takes is not updated "
                         "from this run" % (type(error).__name__, error))
            except Exception:  # noqa: BLE001
                pass
        finally:
            if reader is not None:
                try:
                    reader.close()
                except Exception:  # noqa: BLE001
                    pass

    def sample(self, reader):
        """One reading: the use of each game process of the run, kept per renderer when it is the largest yet."""
        procs = self.processes()
        mine = descendants(procs, self.gate_pid)
        games = {p.pid: GAME_EXES[p.name.lower()] for p in procs
                 if p.name.lower() in GAME_EXES and (p.pid in mine or (p.path and under(p.path, self.work)))}
        self.reads += 1
        if not games:
            return
        values = reader.read().get(PROCESS_DEDICATED, {})
        for instance, value in values.items():
            m = _PID_PART.match(instance)
            if m is None or int(m.group(1)) not in games:
                continue
            if self.luids is not None and luid_of(instance) not in self.luids:
                continue
            renderer = games[int(m.group(1))]
            if value > self.peaks.get(renderer, 0):
                self.peaks[renderer] = value


def descendants(procs, root_pid):
    """The ids of the processes that descend from `root_pid` through parents that exist now."""
    children = {}
    for p in procs:
        children.setdefault(p.ppid, []).append(p.pid)
    found, todo = set(), [root_pid]
    while todo:
        for child in children.get(todo.pop(), ()):
            if child not in found and child != root_pid:
                found.add(child)
                todo.append(child)
    return found
