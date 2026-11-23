"""linux_install_test.py: the Linux package's install.sh answers on every route and never ends without a word.

python -X utf8 -I tools/gate/tests/linux_install_test.py

A package is planted as installer/package-linux.sh lays it out (install.sh and README.txt at the top, files/ and support/ below; the
program and the importer are shell scripts that stand in for the real ones). install.sh is run by sh with no terminal (stdin is
nothing; NEBULA_TERMINAL=1 stands for a terminal), with stand-ins of the dialog tools first on the PATH that record how they are
called, and the game's folder is a planted one. The cases:
  - no terminal, no screen: the answer is in nebula-message.txt beside install.sh, for a success and for each way to fail;
  - no terminal, a screen and zenity: it asks for the disc image through zenity (--file-selection), installs, and answers in a
    window; a cancelled chooser ends it with a word;
  - a terminal: the answer is printed;
  - the installed folder starts (nebula-launch from support/ lies in the install folder) and a package missing its support folder
    says so.
Exit 0 when every case holds.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
LINUX = TREE / "installer" / "linux"
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


def read(path):
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def plant_package(top, support=True):
    write_script(top / "install.sh", (LINUX / "install.sh").read_text(encoding="utf-8"))
    (top / "README.txt").write_text("START HERE\n", encoding="utf-8")
    write_script(top / "files" / "nebula", '#!/bin/sh\necho "game: $*"\n')
    # The importer writes the game as the real one does: <output>/game.pak.
    write_script(top / "files" / "nebula-import",
                 '#!/bin/sh\n[ "$1" = import ] || exit 9\n[ -e "$2" ] || exit 8\nmkdir -p "$4" && printf pak > "$4/game.pak"\n')
    if support:
        write_script(top / "support" / "nebula-launch", (LINUX / "nebula-launch").read_text(encoding="utf-8"))
        write_script(top / "support" / "uninstall.sh", (LINUX / "uninstall.sh").read_text(encoding="utf-8"))
        (top / "support" / "VERSION").write_text("2.0.0-beta.1\n", encoding="utf-8")
        (top / "support" / "dev.nebula.galaxy.desktop").write_text("[Desktop Entry]\nExec=nebula-launch\n", encoding="utf-8")
        (top / "support" / "dev.nebula.galaxy.png").write_bytes(b"png")


def stand_ins(folder, chosen_disc=None, names=("zenity", "kdialog", "notify-send", "xmessage"), works=("zenity",)):
    """Stand-ins that record their calls in <folder>/calls.log. zenity, when it works, answers a file chooser with `chosen_disc` (none:
    the chooser is cancelled); the others in `works` show a window; those not in `works` fail as with no display (exit 5)."""
    calls = (folder / "calls.log").as_posix()
    for name in names:
        lines = ['#!/bin/sh', 'printf \'%%s %%s\\n\' "%s" "$*" >> "%s"' % (name, calls)]
        if name not in works:
            lines.append("exit 5")
        elif name == "zenity":
            lines.append('for a in "$@"; do [ "$a" = --file-selection ] && {')
            lines.append('    if [ -n "%s" ]; then printf \'%%s\\n\' "%s"; exit 0; else exit 1; fi; }; done' % (chosen_disc or "", chosen_disc or ""))
            lines.append("exit 0")
        else:
            lines.append("exit 0")
        write_script(folder / name, "\n".join(lines) + "\n")
    return folder / "calls.log"


def run(top, *args, terminal=False, display=False, tools=None, xdg):
    env = dict(os.environ)
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "NEBULA_TERMINAL"):
        env.pop(name, None)
    if terminal:
        env["NEBULA_TERMINAL"] = "1"
    if display:
        env["DISPLAY"] = ":99"
    if tools is not None:
        env["PATH"] = str(tools) + os.pathsep + env.get("PATH", "")
    env["HOME"] = (xdg.parent / "home").as_posix()
    env["XDG_DATA_HOME"] = xdg.as_posix()
    (xdg.parent / "home").mkdir(exist_ok=True)
    return subprocess.run([bash(), str(top / "install.sh"), *args], capture_output=True, text=True, stdin=subprocess.DEVNULL, env=env,
                          cwd=str(top))


check(bash() is not None and (LINUX / "install.sh").is_file(), "install.sh and a shell are there")
with tempfile.TemporaryDirectory(prefix="linux-install-") as folder:
    tmp = pathlib.Path(folder)
    disc = tmp / "game.iso"
    disc.write_bytes(b"disc")

    def fresh(name, **options):
        top = tmp / name / "Nebula-2.0.0-beta.1-linux-x64"
        plant_package(top, **options)
        return top, tmp / name / "data"

    # No terminal, no screen, no disc image: the words are in the file.
    top, xdg = fresh("a")
    done = run(top, xdg=xdg)
    message = read(top / "nebula-message.txt")
    check(done.returncode == 1 and "was not installed" in message and "--disc" in message,
          "no terminal, no screen, no disc: the end is 1 and nebula-message.txt says why: %r %r" % (done.returncode, message))
    check(not (xdg / "Nebula" / "current.json").exists(), "nothing is installed then")

    # No terminal, no screen, the disc given: it installs and the file says so.
    top, xdg = fresh("b")
    done = run(top, "--disc", str(disc), xdg=xdg)
    message = read(top / "nebula-message.txt")
    check(done.returncode == 0 and "is installed" in message and "nebula-launch" in message,
          "no terminal, no screen, --disc: installed, and the file says it: %r %r" % (done.returncode, message))
    root = xdg / "Nebula"
    check((root / "current.json").is_file() and (root / "content" / "game.pak").is_file() and (root / "nebula-launch").is_file() and
          (root / "uninstall.sh").is_file() and (root / "versions" / "2.0.0-beta.1" / "nebula").is_file() and
          (xdg / "applications" / "dev.nebula.galaxy.desktop").is_file(),
          "the install folder holds the program, the game, nebula-launch, uninstall.sh and the menu entry")
    check('Exec="' in read(xdg / "applications" / "dev.nebula.galaxy.desktop"), "the menu entry names the installed nebula-launch")
    # The installed layout starts: nebula-launch from support/ lies in the install folder.
    launched = subprocess.run([bash(), str(root / "nebula-launch")], capture_output=True, text=True, stdin=subprocess.DEVNULL,
                              env={**os.environ, "HOME": str(xdg.parent / "home")})
    check(launched.returncode == 0 and "game: --pak" in read(root / "logs" / "game.log"),
          "the installed folder starts the game and keeps its log: %r %r" % (launched.returncode, read(root / "logs" / "game.log")))

    # A terminal: the words are printed, no file.
    top, xdg = fresh("c")
    done = run(top, "--disc", str(disc), terminal=True, xdg=xdg)
    check(done.returncode == 0 and "is installed" in done.stdout and not (top / "nebula-message.txt").exists(),
          "a terminal: the answer is printed, no file: %r %r %r" % (done.returncode, done.stdout, done.stderr))

    # Portable.
    top, xdg = fresh("d")
    portable = xdg.parent / "portable"
    done = run(top, "--disc", str(disc), "--portable", str(portable), xdg=xdg)
    check(done.returncode == 0 and (portable / "portable.txt").is_file() and "Portable Nebula" in read(top / "nebula-message.txt"),
          "portable: installed, and the file says so: %r" % read(top / "nebula-message.txt"))

    # No terminal, a screen and zenity: it asks through zenity, installs, answers in a window.
    top, xdg = fresh("e")
    tools = tmp / "tools-e"
    tools.mkdir()
    calls = stand_ins(tools, chosen_disc=disc.as_posix())
    done = run(top, display=True, tools=tools, xdg=xdg)
    shown = read(calls)
    check(done.returncode == 0 and "--file-selection" in shown, "no terminal, zenity: the disc image is asked for in a window: %r %r" % (done.returncode, shown))
    check("--info" in shown and "is installed" in shown, "and the end is a window: %r" % shown)
    check((xdg / "Nebula" / "current.json").is_file() and not (top / "nebula-message.txt").exists(), "it is installed and no file is left")

    # The chooser is cancelled: a window says so, the end is 1.
    top, xdg = fresh("f")
    tools = tmp / "tools-f"
    tools.mkdir()
    calls = stand_ins(tools, chosen_disc=None)
    done = run(top, display=True, tools=tools, xdg=xdg)
    shown = read(calls)
    check(done.returncode == 1 and "--error" in shown and "no disc image was chosen" in shown,
          "a cancelled chooser: a window says so and the end is 1: %r %r" % (done.returncode, shown))

    # A disc image that is not there: the words.
    top, xdg = fresh("g")
    done = run(top, "--disc", str(tmp / "missing.iso"), xdg=xdg)
    check(done.returncode == 1 and "is not a disc image" in read(top / "nebula-message.txt"),
          "a disc image that is not there: %r" % read(top / "nebula-message.txt"))

    # An option it does not know, and a missing value.
    top, xdg = fresh("h")
    done = run(top, "--nonsense", xdg=xdg)
    check(done.returncode == 1 and "unknown option --nonsense" in read(top / "nebula-message.txt"), "an unknown option is said: %r" % read(top / "nebula-message.txt"))
    top, xdg = fresh("i")
    done = run(top, "--disc", xdg=xdg)
    check(done.returncode == 1 and "--disc needs" in read(top / "nebula-message.txt"), "--disc with no value is said: %r" % read(top / "nebula-message.txt"))

    # A package without its support folder (the folder copied alone).
    top, xdg = fresh("j", support=False)
    done = run(top, "--disc", str(disc), xdg=xdg)
    check(done.returncode == 1 and "support" in read(top / "nebula-message.txt"), "no support folder is said: %r" % read(top / "nebula-message.txt"))

    # A command that fails with no word of its own (the importer ends with an error after install began): the trap speaks.
    top, xdg = fresh("k")
    (top / "support" / "nebula-launch").unlink()  # the copy to the install folder fails with no message of its own
    done = run(top, "--disc", str(disc), xdg=xdg)
    message = read(top / "nebula-message.txt")
    check(done.returncode != 0 and "was not installed" in message and "stopped with an error" in message,
          "a command that fails with no word of its own: the end says it stopped: %r %r" % (done.returncode, message))

if failures:
    print("linux_install_test.py: FAILED:\n  " + "\n  ".join(f for f in failures if f))
    sys.exit(1)
print("linux_install_test.py: pass")
