"""noprompt.py: nothing the step tool or the gate starts may wait for a person (docs/plan/native/AGENT.md section 9).

    import noprompt; noprompt.install()     # first thing in main() of plan.py and gate.py

  git      GIT_TERMINAL_PROMPT=0 and GCM_INTERACTIVE=never: a push or a fetch whose sign-in is refused fails with a line, it never opens
           the credential manager's window or asks on a terminal.
  Windows  the error mode SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, which every child process inherits:
           a program that crashes ends with its exception code (0xC0000005) and shows no "has stopped working" box, so a run nobody
           watches does not hang on one. A debug-runtime assert box is not covered by it (the C runtime asks for it by its own call):
           the game host switches those to a file (native/host/host_main.cpp, _CrtSetReportMode) and the gate builds the release runtime.
Python standard library only.
"""
import ctypes
import os

ENV = {"GIT_TERMINAL_PROMPT": "0", "GCM_INTERACTIVE": "never"}
SEM_FAILCRITICALERRORS = 0x0001
SEM_NOGPFAULTERRORBOX = 0x0002
SEM_NOOPENFILEERRORBOX = 0x8000
MODE = SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX


def error_mode():
    """The process's error mode (0 where there is none to read)."""
    if os.name != "nt":
        return 0
    return ctypes.windll.kernel32.GetErrorMode()


def install():
    """Sets the environment for git and the error mode of this process (and so of every child). Returns the mode now in force."""
    for key, value in ENV.items():
        os.environ[key] = value
    if os.name == "nt":
        ctypes.windll.kernel32.SetErrorMode(error_mode() | MODE)
    return error_mode()
