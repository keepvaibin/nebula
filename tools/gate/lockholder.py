"""lockholder.py: how the lock folders (gate.lock, long.lock, move.lock, landing-runner.lock, the queue's lock, measure.lock) name their
holder in holder.txt, and when that holder is gone. One reader for every lock, so that a form one lock writes is a form every reader reads.

A holder line holds `pid N` anywhere in it (`full gate of Tree@abc pid 67700; covers main x` is one), and may hold `since T pid N`: T is
the start of process N, in whole seconds since 1970 (written by this module's `named`). The last `pid N` of the line is the holder's.
A holder is gone when its process is not running, or when it runs but started at another time than `since` says (the number was used
again). A line with no `pid` is a lock taken by hand: it is never gone.
"""
import os
import re

HOLDER = re.compile(r"(?:\bsince (\d+) )?\bpid (\d+)\b")


def parse(text):
    """(pid, since) of a holder line: the last `pid N` of it and its `since T`, None for what the line does not give."""
    found = HOLDER.findall(text or "")
    if not found:
        return None, None
    since, pid = found[-1]
    return int(pid), (int(since) if since else None)


def process_started(pid):
    """When process `pid` started, in whole seconds since 1970; None when it cannot be read (not Windows, no such process, no right)."""
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes as wt
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = wt.HANDLE
    k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    k32.GetProcessTimes.argtypes = [wt.HANDLE, ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME),
                                    ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME)]
    k32.GetProcessTimes.restype = wt.BOOL
    k32.CloseHandle.argtypes = [wt.HANDLE]
    handle = k32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return None
    try:
        created, ended, kernel, user = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
        if not k32.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(ended), ctypes.byref(kernel), ctypes.byref(user)):
            return None
        ticks = (created.dwHighDateTime << 32) | created.dwLowDateTime
        return int((ticks - 116444736000000000) / 1e7)
    finally:
        k32.CloseHandle(handle)


def named(pid=None):
    """`since T pid N` for process N (this one when none is given): `pid N` alone where its start cannot be read."""
    pid = os.getpid() if pid is None else pid
    since = process_started(pid)
    return ("since %d pid %d" % (since, pid)) if since is not None else ("pid %d" % pid)


def gone(text, alive, started=process_started):
    """The pid the line names when that holder is gone (its process is not running, or runs from another start than `since`), else
    None (it lives, or the line names no process: held by hand)."""
    pid, since = parse(text)
    if pid is None:
        return None
    if not alive(pid):
        return pid
    if since is not None:
        now = started(pid)
        if now is not None and abs(now - since) > 2:
            return pid
    return None
