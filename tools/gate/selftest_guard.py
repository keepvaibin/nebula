"""selftest_guard.py: a self-test must never reach the live state (package T13c).

The live state is what the machine's tools keep for the whole project: D:/NebulaWork/plan (the board, the locks, the landing queue and
its runner lock, the failure store, the step tool's folder with its claims, landings and repairs) and the files directly in
D:/NebulaWork/build/gate-runs (the history, the measure's history, the logs). A self-test that reads or writes them behaves
differently inside a real landing (the runner's lock is held, the queue has rows) and, later, when an agent has filled them: a false
`fail harness/self` for an agent that may not touch the tools. This makes it impossible, not unlikely.

    install(log)        a sys.addaudithook hook: every audit event that takes a path (open, list, make, rename, copy, remove a file or
                        folder) is looked at; a path inside D:/NebulaWork/plan, or a file directly in D:/NebulaWork/build/gate-runs
                        (a run folder, and what is below it, is fine), is refused: one line `<event> <path>` is appended to `log` (a handle kept from
                        install time, so the hook does not open anything itself) and RuntimeError("a self-test reached the live state:
                        <event> <path>") is raised. Paths are compared absolute, lower case, with forward slashes. A number is a file
                        handle: skipped. There is no exception list: a test that trips gets temporary paths.
    install_if_asked()  installs when the environment says NEBULA_SELF_TEST=1 (the log is NEBULA_SELF_TEST_LOG). Every tool a self-test
                        starts as a child process calls this first in its main(), so the guard follows the test into its children.

The gate's harness/self check starts each script through tools/gate/tests/guarded.py with both variables set, one log file for each
script; a script whose log is not empty fails whatever its exit code (code that swallows the exception cannot hide it).
"""
import os
import sys

LIVE_FOLDER = "d:/nebulawork/plan"
RUNS_FOLDER = "d:/nebulawork/build/gate-runs"

# The audit events that take a path, and which of their arguments are paths (Python's audit event table). Listing, walking and
# removing the runs folder itself is reading or deleting its history, so for those the folder itself counts too.
PATH_ARGS = {
    "open": (0,), "os.listdir": (0,), "os.scandir": (0,), "os.mkdir": (0,), "os.remove": (0,), "os.rename": (0, 1),
    "os.rmdir": (0,), "os.chmod": (0,), "os.chown": (0,), "os.chdir": (0,), "os.link": (0, 1), "os.symlink": (0, 1),
    "os.truncate": (0,), "os.utime": (0,), "os.walk": (0,), "os.fwalk": (0,), "os.removexattr": (0,), "os.setxattr": (0,),
    "shutil.copyfile": (0, 1), "shutil.copymode": (0, 1), "shutil.copystat": (0, 1), "shutil.copytree": (0, 1),
    "shutil.move": (0, 1), "shutil.rmtree": (0,), "shutil.make_archive": (0, 2), "shutil.unpack_archive": (0, 1),
    "glob.glob": (0,), "pathlib.Path.glob": (0,), "pathlib.Path.rglob": (0,),
}
WHOLE_FOLDER_EVENTS = ("os.listdir", "os.scandir", "os.walk", "os.fwalk", "os.rmdir", "shutil.rmtree", "glob.glob",
                       "pathlib.Path.glob", "pathlib.Path.rglob", "shutil.copytree", "shutil.move", "os.rename")

FOLDER_ONLY_EVENTS = ("os.listdir", "os.scandir", "os.mkdir", "os.rmdir", "os.walk", "os.fwalk", "shutil.rmtree", "glob.glob",
                      "pathlib.Path.glob", "pathlib.Path.rglob")

_STATE = {"installed": False, "log": None}


def normal(path):
    """The path as it is compared: absolute, lower case, forward slashes (no trailing slash). None for a number or anything that is
    not a path."""
    if isinstance(path, int) or path is None:
        return None
    try:
        text = os.fsdecode(os.fspath(path))
    except (TypeError, ValueError):
        return None
    if not text:
        return None
    return os.path.abspath(text).replace("\\", "/").lower().rstrip("/")


def reaches_live_state(event, path):
    """Whether this event on this path touches the live state."""
    p = normal(path)
    if p is None:
        return False
    if p == LIVE_FOLDER or p.startswith(LIVE_FOLDER + "/"):
        return True
    if p.startswith(RUNS_FOLDER + "/"):
        if "/" in p[len(RUNS_FOLDER) + 1:]:
            return False                                 # below a run folder: free
        # Directly in the runs folder: a file (the history, a log) is the live state, a run folder is not. A run folder is what an
        # event that only works on folders names, or what is a folder already.
        return event not in FOLDER_ONLY_EVENTS and not os.path.isdir(p)
    return p == RUNS_FOLDER and event in WHOLE_FOLDER_EVENTS


def _hook(event, args):
    which = PATH_ARGS.get(event)
    if which is None:
        return
    for i in which:
        if i < len(args) and reaches_live_state(event, args[i]):
            shown = os.fsdecode(os.fspath(args[i]))
            line = "%s %s\n" % (event, shown)
            handle = _STATE["log"]
            try:
                if handle is not None:
                    handle.write(line)
                    handle.flush()
                else:
                    sys.stderr.write(line)
            except (OSError, ValueError):
                pass
            raise RuntimeError("a self-test reached the live state: %s %s" % (event, shown))


def install(log=None):
    """Installs the guard once in this process; `log` is the file the refused events are appended to (opened here, before the hook)."""
    if _STATE["installed"]:
        return
    _STATE["log"] = open(log, "a", encoding="utf-8") if log else None
    sys.addaudithook(_hook)
    _STATE["installed"] = True


def install_if_asked():
    """Installs the guard when NEBULA_SELF_TEST is 1 (the gate's harness/self check sets it for every script and its children)."""
    if os.environ.get("NEBULA_SELF_TEST") == "1":
        install(os.environ.get("NEBULA_SELF_TEST_LOG") or None)
