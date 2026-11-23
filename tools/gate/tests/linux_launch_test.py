"""linux_launch_test.py: the Linux start script (installer/linux/nebula-launch) keeps a log and never ends without a word.

python -X utf8 -I tools/gate/tests/linux_launch_test.py

The script is run by sh in a planted install folder (current.json, versions/<version>/nebula, content/game.pak) whose "program" is a
shell script that prints a line on each of its two outputs and what it was given. A start whose outputs are no terminal writes both to
<data>/logs/game.log and the run before to game.previous.log; a portable folder (portable.txt) keeps them in <folder>/data/logs; the
arguments still reach the program; a folder that cannot take a log still starts the program.

A start that cannot go on says why where a person sees it: in the terminal when there is one (NEBULA_TERMINAL=1 stands for it here),
else in a window of the desktop through a dialog tool (stand-ins of zenity, kdialog, notify-send and xmessage are put first on the
PATH here and record what they are called with), else in nebula-message.txt beside the script. The cases: no install at all (the
words name install.sh), a game that ends with an error (the words name the log), a game ended by the system's own signal (no word),
and each route of the words. Exit 0 when every case holds.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
SCRIPT = TREE / "installer" / "linux" / "nebula-launch"
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def bash():
    for candidate in (r"C:\Program Files\Git\bin\bash.exe", "/bin/bash", "/usr/bin/bash"):
        if pathlib.Path(candidate).is_file():
            return candidate
    return shutil.which("bash")


def write_script(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")
    os.chmod(path, 0o755)


def plant(root, portable=False, exit_code=0):
    (root / "versions" / "2.0.0-beta.1").mkdir(parents=True)
    (root / "content").mkdir()
    (root / "content" / "game.pak").write_bytes(b"pak")
    (root / "current.json").write_text('{\n  "version": "2.0.0-beta.1",\n  "previous": null\n}\n', encoding="utf-8")
    write_script(root / "nebula-launch", SCRIPT.read_text(encoding="utf-8"))
    write_script(root / "versions" / "2.0.0-beta.1" / "nebula",
                 '#!/bin/sh\necho "out: $*"\necho "err: the log line" >&2\nexit %d\n' % exit_code)
    if portable:
        (root / "portable.txt").write_text("portable", encoding="utf-8")


def read(path):
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def stand_ins(folder, names=("zenity", "kdialog", "notify-send", "xmessage"), works=()):
    """Dialog tools that record how they were called, in <folder>/calls.log: those in `works` show their window (exit 0), the others
    fail as a tool with no display would (exit 5)."""
    for name in names:
        status = 0 if name in works else 5
        write_script(folder / name, '#!/bin/sh\nprintf \'%%s %%s\\n\' "%s" "$*" >> "%s"\nexit %d\n' % (
            name, (folder / "calls.log").as_posix(), status))
    return folder / "calls.log"


def run(root, *args, terminal=False, display=False, tools=None):
    env = dict(os.environ)
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "NEBULA_TERMINAL"):
        env.pop(name, None)
    if terminal:
        env["NEBULA_TERMINAL"] = "1"
    if display:
        env["DISPLAY"] = ":99"
    if tools is not None:
        env["PATH"] = str(tools) + os.pathsep + env.get("PATH", "")
    env["HOME"] = str(root.parent / "home")
    return subprocess.run([bash(), str(root / "nebula-launch"), *args], capture_output=True, text=True, stdin=subprocess.DEVNULL,
                          env=env)


check(SCRIPT.is_file() and bash() is not None, "the script and a shell are there")
with tempfile.TemporaryDirectory(prefix="linux-launch-") as folder:
    tmp = pathlib.Path(folder)
    root = tmp / "install"
    plant(root)
    done = run(root)
    log = root / "logs" / "game.log"
    check(done.returncode == 0, "the start ends as the program ends: %s" % done.stderr)
    check(log.is_file() and "out: --pak" in read(log) and "err: the log line" in read(log),
          "both outputs of the program are in <data>/logs/game.log: %s" % (read(log) or "no file"))
    check(done.stdout == "" and done.stderr == "", "and nothing goes to the outputs the menu gives it")
    check(not (root / "nebula-message.txt").exists(), "a start that went well leaves no message file")
    done = run(root, "--import", "mod.luma")
    check((root / "logs" / "game.previous.log").is_file() and "err: the log line" in read(root / "logs" / "game.previous.log"),
          "the run before is game.previous.log")
    check("--import mod.luma" in read(log), "the arguments reach the program and the log is the new run's")
    # The menu entry's bare --import is dropped.
    run(root, "--import")
    check("--import" not in read(log).partition("--saves")[2] and "--saves" in read(log), "a bare --import is dropped")

    portable = tmp / "portable"
    plant(portable, portable=True)
    run(portable)
    check((portable / "data" / "logs" / "game.log").is_file() and not (portable / "logs").exists(),
          "a portable folder keeps the log in <folder>/data/logs")

    blocked = tmp / "blocked"
    plant(blocked)
    (blocked / "logs").write_text("a file where the folder should be", encoding="utf-8")
    done = run(blocked)
    check(done.returncode == 0 and "err: the log line" in done.stderr, "a folder that cannot take a log still starts the program: %s" % done.stderr)

    # ---- No install: the words, and the end with 1, on every route.
    empty = tmp / "empty"
    empty.mkdir()
    write_script(empty / "nebula-launch", SCRIPT.read_text(encoding="utf-8"))
    done = run(empty, terminal=True)
    check(done.returncode == 1, "no install: the end is 1 (got %d)" % done.returncode)
    check("not installed yet" in done.stderr and "install.sh" in done.stderr, "no install, in a terminal: the words are there: %r" % done.stderr)
    check(not (empty / "nebula-message.txt").exists(), "in a terminal no file is left")

    tools = tmp / "tools-zenity"
    calls = stand_ins(tools, works=("zenity",))
    done = run(empty, display=True, tools=tools)
    check(done.returncode == 1, "no install, no terminal: the end is 1 (got %d)" % done.returncode)
    shown = read(calls)
    check(shown.startswith("zenity --error") and "not installed yet" in shown and "install.sh" in shown,
          "no install, no terminal, zenity: it is called with the words: %r" % shown)
    check(not (empty / "nebula-message.txt").exists(), "a window that showed the words leaves no file")

    tools = tmp / "tools-kdialog"
    calls = stand_ins(tools, works=("kdialog",))
    run(empty, display=True, tools=tools)
    shown = read(calls)
    check("kdialog --title Nebula --error" in shown and "not installed yet" in shown and "zenity" in shown,
          "kdialog is tried after zenity, which did not show: %r" % shown)

    tools = tmp / "tools-notify"
    calls = stand_ins(tools, works=("notify-send",))
    run(empty, display=True, tools=tools)
    check("notify-send" in read(calls) and "not installed yet" in read(calls), "notify-send is tried after both: %r" % read(calls))
    tools = tmp / "tools-xmessage"
    calls = stand_ins(tools, works=("xmessage",))
    run(empty, display=True, tools=tools)
    check("xmessage" in read(calls) and not (empty / "nebula-message.txt").exists(), "xmessage is the last window tried: %r" % read(calls))

    tools = tmp / "tools-none"
    calls = stand_ins(tools, works=())
    done = run(empty, display=True, tools=tools)
    message = read(empty / "nebula-message.txt")
    check(done.returncode == 1 and "not installed yet" in message and "install.sh" in message,
          "no tool shows a window: the words are in nebula-message.txt beside the script: %r" % message)
    (empty / "nebula-message.txt").unlink(missing_ok=True)
    done = run(empty, display=False, tools=tools)
    message = read(empty / "nebula-message.txt")
    check(done.returncode == 1 and "not installed yet" in message, "no screen at all: the file again: %r" % message)

    # The words name the folder install.sh is in: beside the script, or above it (the download's support folder).
    package = tmp / "package"
    (package / "support").mkdir(parents=True)
    write_script(package / "support" / "nebula-launch", SCRIPT.read_text(encoding="utf-8"))
    write_script(package / "install.sh", "#!/bin/sh\n")
    done = run(package / "support", terminal=True)
    check(done.returncode == 1 and str(package).replace("\\", "/")[-12:] in done.stderr.replace("\\", "/") and "./install.sh" in done.stderr,
          "an unpacked download: the words name its folder: %r" % done.stderr)

    # A current.json whose program is gone.
    broken = tmp / "broken"
    plant(broken)
    (broken / "versions" / "2.0.0-beta.1" / "nebula").unlink()
    done = run(broken, terminal=True)
    check(done.returncode == 1 and "install.sh" in done.stderr and "missing" in done.stderr, "a program that is gone: the words, and 1: %r" % done.stderr)

    # ---- The game ends with an error: the words name the log. A system signal's end is no error.
    failing = tmp / "failing"
    plant(failing, exit_code=3)
    tools = tmp / "tools-error"
    calls = stand_ins(tools, works=("zenity",))
    done = run(failing, display=True, tools=tools)
    shown = read(calls)
    check(done.returncode == 3, "the end is the program's own: 3 (got %d)" % done.returncode)
    check(shown.startswith("zenity --error") and "code 3" in shown and "game.log" in shown and str(failing).replace("\\", "/")[-12:] in shown.replace("\\", "/"),
          "an error ends the game: zenity is called with the code and the log's place: %r" % shown)
    check("out: --pak" in read(failing / "logs" / "game.log"), "and the log holds what it wrote")
    quiet = tmp / "quiet"
    plant(quiet, exit_code=143)
    tools = tmp / "tools-quiet"
    calls = stand_ins(tools, works=("zenity",))
    done = run(quiet, display=True, tools=tools)
    check(done.returncode == 143 and read(calls) == "", "the system's own terminate signal ends it with no window: %r" % read(calls))
    nowindow = tmp / "nowindow"
    plant(nowindow, exit_code=2)
    done = run(nowindow)
    check(done.returncode == 2 and "code 2" in read(nowindow / "nebula-message.txt") and "game.log" in read(nowindow / "nebula-message.txt"),
          "an error with no terminal and no window: the file beside the script names the log: %r" % read(nowindow / "nebula-message.txt"))

if failures:
    print("linux_launch_test.py: FAILED:\n  " + "\n  ".join(f for f in failures if f))
    sys.exit(1)
print("linux_launch_test.py: pass")
