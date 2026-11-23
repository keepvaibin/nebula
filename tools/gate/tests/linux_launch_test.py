"""linux_launch_test.py: the Linux menu entry's start script (installer/linux/nebula-launch) keeps a log.

python -X utf8 -I tools/gate/tests/linux_launch_test.py

The script is run by sh in a planted install folder (current.json, versions/<version>/nebula, content/game.pak) whose "program" is a
shell script that prints a line on each of its two outputs and what it was given. A start whose outputs are no terminal writes both to
<data>/logs/game.log and the run before to game.previous.log; a portable folder (portable.txt) keeps them in <folder>/data/logs; the
arguments still reach the program; a folder that cannot take a log still starts the program. Exit 0 when every case holds.
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


def plant(root, portable=False):
    (root / "versions" / "2.0.0-beta.1").mkdir(parents=True)
    (root / "content").mkdir()
    (root / "content" / "game.pak").write_bytes(b"pak")
    (root / "current.json").write_text('{\n  "version": "2.0.0-beta.1",\n  "previous": null\n}\n', encoding="utf-8")
    shutil.copy(SCRIPT, root / "nebula-launch")
    program = root / "versions" / "2.0.0-beta.1" / "nebula"
    program.write_text('#!/bin/sh\necho "out: $*"\necho "err: the log line" >&2\n', encoding="utf-8", newline="\n")
    os.chmod(program, 0o755)
    os.chmod(root / "nebula-launch", 0o755)
    if portable:
        (root / "portable.txt").write_text("portable", encoding="utf-8")


def read(path):
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def run(root, *args):
    return subprocess.run([bash(), str(root / "nebula-launch"), *args], capture_output=True, text=True, stdin=subprocess.DEVNULL)


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

if failures:
    print("linux_launch_test.py: FAILED:\n  " + "\n  ".join(f for f in failures if f))
    sys.exit(1)
print("linux_launch_test.py: pass")
