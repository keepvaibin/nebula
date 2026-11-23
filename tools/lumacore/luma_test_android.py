"""luma_test_android.py: the proof harness on an Android device, over adb.

    python -X utf8 -I tools/lumacore/luma_test_android.py prepare --device SERIAL --apk <test apk> --pak <game.pak>
    python -X utf8 -I tools/lumacore/luma_test_android.py push-data --device SERIAL --pak <game.pak>
    python -X utf8 -I tools/lumacore/luma_test_android.py remove  --device SERIAL [--data]
    python -X utf8 -I tools/lumacore/luma_test_android.py start-check --device SERIAL [--apk <test apk>] [--frames N]
    python -X utf8 -I tools/lumacore/luma_test.py run <suite.json | test> --device SERIAL --bin <test-bin-ABI> ...

Only the test app is ever touched: dev.nebula.galaxy.test, a debuggable build with its own id (platform/android/app/
build.gradle.kts), installed beside the player's game and never in its place. Nothing here can name another package.

prepare installs the test app (platform/android/build_apk.ps1 -Debuggable -Harness ...: Nebula.apk) and puts a
game.pak into its private storage (files/game/game.pak), where the game screen reads it; the pak holds no platform's
data, so the importer's on any computer serves. remove uninstalls the test app, its data with it. push-data puts the
game.pak again into a test app that is already installed (the gate does it when the store version of the data on the
tablet is not its tree's).

The game data is kept on the tablet between runs (B14 part 2): /data/local/tmp/nebula-test/game-<key>.pak with
game-<key>.pak.info beside it (the size and a hash), the key being the one the gate gives the data (gamedata.py's
registry: RMGE01-37-...). It crosses the cable once for each version of the data: prepare and push-data fill
files/game/game.pak by a copy on the tablet. One kept copy at a time: another key removes the old one first. A kept
file whose size is not the recorded one, or with no info file, is pushed again (adb push, then the hash made on the
tablet, sha256sum or md5sum, and compared with the PC's; unequal: pushed once more, then exit 3, kind tool). With less
free room than the kept copy, the app's copy and 1 GB the data goes the old way for that run (pushed to a temporary
file, copied in, the temporary file removed; nothing kept). A run never fails for room. The test app is still removed
after each use; only the cable's part goes. `remove --data` removes the kept copy and nothing else (do it when a
run of work is over); only /data/local/tmp/nebula-test/ and the package dev.nebula.galaxy.test are ever touched.

A run (luma_test.py --device) is luma_test.py's on this platform: the runner reads each test, lays out the run's
folder, writes the plan and judges the files that come back; this module only moves the folder and starts the game:
  1. the run's folder, with the device's paths in its plan and the test's hooks in env.txt (the test build's
     Harness.java sets them), is packed, pushed and unpacked into files/harness/<name>;
  2. the game screen starts with the intent extras nebula.harness and nebula.seconds, and is waited for: its process
     gone, or the limit (then stopped). While it runs, the game's step lines are read from the device's log every
     PROGRESS_POLL_SECONDS (luma_test.Progress): a process that took no step for luma_test.HANG_SECONDS is stopped as hung
     (verdict hung, kind fail); a limit that passes in a pause waits for steps or for the hang; one that passes with the
     game stepping is judged by the game's speed (not-reached, kind fail, at 80% of full speed or more; else too-slow, kind
     env, which the gate runs again alone);
  3. how the process ended comes from Android's own record (ApplicationExitInfo), which Android writes a little after
     the process is gone: it is waited for until the record is there, at most EXIT_RECORD_SECONDS; a record that never
     comes is the verdict no-exit-record (kind tool), never a verdict on the game;
  4. the folder comes back over the local one, the app's log of the run is run.log, the device copy is removed.
start-check is the push workflow's start check (host_main.cpp run_start_check) on a device or an emulator: the test
app starts with the intent extra nebula.start_check (its Harness.java gives the game a run folder of its own and no
game data); the settings, the window and the Vulkan renderer start and N cleared pictures are shown. Its exit status
(0: pass; 3: no renderer; 4: no picture shown; 5: a frame held 10 seconds) is read from Android's exit record, as a
run's is. --apk installs the test app first (adb installs by the APK's own id: dev.nebula.galaxy.test only).

The bin folder is build_apk.ps1's test-bin-<ABI>: libsmg_game.so, libsmg_game_plain.so, libnebula_test_extension.so,
luma-test.so, luma_types.json. A test's "disc" runs only when the device's game is of that disc.
"""
import argparse
import datetime
import math
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import time

PACKAGE = "dev.nebula.galaxy.test"
ACTIVITY = PACKAGE + "/dev.nebula.galaxy.GameActivity"
GAME_PROCESS = PACKAGE + ":game"  # AndroidManifest.xml: GameActivity android:process=":game"
ABIS = {"arm64-v8a": "android-arm64", "x86_64": "android-x64"}
# The game's own log tags in logcat (platform/sdl: __android_log_print) and the system's for a crash.
LOG_TAGS = ("Nebula", "NebulaGame", "libc", "DEBUG", "AndroidRuntime")
# The longest the runner waits for Android's exit record after the process is gone. The record is waited for, not for a
# time: it usually comes within tens of milliseconds, and a busy device can take far longer. A record that is not there
# after this long is no verdict on the game: the run's kind is tool ("the tablet gave no exit record").
EXIT_RECORD_SECONDS = 60.0
# How often the runner reads the game's step lines from the device's log while the game runs (an adb call each).
PROGRESS_POLL_SECONDS = 2.0
# The kept game data (B14 part 2): where, how much room must be left over, what a hash tool is called, the exit code of a hash that
# stays unequal (the gate's kind tool).
KEPT_DIR = "/data/local/tmp/nebula-test"
KEPT_SPARE_BYTES = 1 << 30
HASH_TOOLS = (("sha256", "sha256sum", 64), ("md5", "md5sum", 32))
EXIT_TOOL = 3
DATA_REGISTRY = os.environ.get("LUMA_TEST_DATA_REGISTRY", "D:/NebulaWork/build/game-data/registry.tsv")
# The start check's run folder (files/harness/<it>) and how long the game process may take to appear after am start.
START_CHECK = "start-check"
START_SECONDS = 60.0


class Device:
    def __init__(self, serial):
        sdk = os.environ.get("ANDROID_HOME") or os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk")
        self.adb_path = os.path.join(sdk, "platform-tools", "adb")
        self.serial = serial
        self.exit_seen = ""  # what the last exit_record read, for the run's folder
        self.env = dict(os.environ, MSYS_NO_PATHCONV="1")

    def adb(self, *args, timeout=600, stdin=None):
        return subprocess.run([self.adb_path, "-s", self.serial] + list(args), capture_output=True, env=self.env,
                              timeout=timeout, stdin=stdin)

    def shell(self, command, timeout=600):
        r = self.adb("shell", command, timeout=timeout)
        return r.returncode, r.stdout.decode("utf-8", "replace")

    def run_as(self, command, timeout=600):
        return self.shell("run-as %s sh -c '%s'" % (PACKAGE, command.replace("'", "'\\''")), timeout=timeout)

    def answers(self):
        return self.adb("get-state").stdout.strip() == b"device"

    def installed(self):
        code, out = self.shell("pm list packages %s" % PACKAGE)
        return ("package:%s" % PACKAGE) in out.split()

    def files(self):
        """The test app's files folder (Context.getFilesDir), as run-as sees it."""
        code, out = self.run_as("pwd")
        folder = out.strip().splitlines()[-1] if out.strip() else ""
        if code != 0 or not folder.startswith("/"):
            raise RuntimeError("run-as %s gave no folder (is the test app installed, and debuggable?): %s"
                               % (PACKAGE, out.strip()[:200]))
        return folder + "/files"

    def abi(self):
        return self.shell("getprop ro.product.cpu.abi")[1].strip()

    def free_bytes(self, path="/data"):
        """Free room of the partition `path` is in (df), or None when df said nothing readable."""
        code, out = self.shell("df -k %s" % path)
        for line in reversed(out.strip().splitlines()):
            cells = line.split()
            if len(cells) >= 4 and cells[3].isdigit():
                return int(cells[3]) * 1024
        return None

    def size_of(self, path):
        """Size of a file on the device (the shell's view), or None when there is none."""
        code, out = self.shell("stat -c %%s %s 2>/dev/null" % path)
        text = out.strip()
        return int(text) if code == 0 and text.isdigit() else None

    def read_text(self, path):
        code, out = self.shell("cat %s 2>/dev/null" % path)
        return out if code == 0 else None

    def write_text(self, path, text):
        """A small text file (no quote in it) written on the device."""
        escaped = text.replace("\\", "\\\\").replace("\n", "\\n")     # printf reads the backslash escapes of its format
        code, out = self.shell("printf '" + escaped + "' > " + path)
        return code == 0

    def list_names(self, folder):
        code, out = self.shell("ls %s 2>/dev/null" % folder)
        return out.split() if code == 0 else []

    def remove_files(self, *paths):
        self.shell("rm -f " + " ".join(paths))

    def make_folder(self, folder):
        code, out = self.shell("mkdir -p %s && chmod 755 %s" % (folder, folder))
        return code == 0

    def move_file(self, source, target):
        code, out = self.shell("mv %s %s && chmod 644 %s" % (source, target, target))
        return code == 0

    def push_file(self, local, remote):
        """adb push: (ok, what adb said)."""
        r = self.adb("push", str(local), remote, timeout=3600)
        return r.returncode == 0, r.stderr.decode("utf-8", "replace").strip()[:200]

    def hash_of(self, path):
        """(algorithm, hex digest) of a file on the device, made by the first of HASH_TOOLS it has; (None, None) when it has none."""
        for algorithm, tool, width in HASH_TOOLS:
            code, out = self.shell("%s %s 2>/dev/null" % (tool, path), timeout=3600)
            words = out.split()
            if code == 0 and words and len(words[0]) == width and re.fullmatch(r"[0-9a-f]+", words[0]):
                return algorithm, words[0]
        return None, None

    def copy_into_app(self, source):
        """The file `source` (on the device, readable by the app) copied to the app's files/game/game.pak, on the device: (ok, what it said).
        First as the app itself (run-as cp); where that is refused, one shell line that reads the file and pipes it into the app."""
        code, out = self.run_as("mkdir -p files/game && cp %s files/game/game.pak.part && mv files/game/game.pak.part files/game/game.pak"
                                % source, timeout=3600)
        if code == 0:
            return True, ""
        first = out.strip()[:200]
        code, out = self.shell("cat %s | run-as %s sh -c 'mkdir -p files/game && cat > files/game/game.pak.part && "
                               "mv files/game/game.pak.part files/game/game.pak'" % (source, PACKAGE), timeout=3600)
        return code == 0, ("cp: %s; pipe: %s" % (first, out.strip()[:200])) if code != 0 else ""

    def app_pak_size(self):
        code, out = self.run_as("stat -c %s files/game/game.pak")
        text = out.strip()
        return int(text) if code == 0 and text.isdigit() else None

    def pak_header(self):
        """The first 64 bytes of the test app's game.pak (empty when it has none)."""
        r = self.adb("exec-out", "run-as %s sh -c 'head -c 64 files/game/game.pak 2>/dev/null'" % PACKAGE)
        return r.stdout if r.returncode == 0 else b""

    def pid(self):
        code, out = self.shell("pidof " + GAME_PROCESS)
        return out.strip() if code == 0 else ""

    def log_tail(self, pid):
        """The last lines the game process `pid` wrote to the device's log under its tag, the host's step lines among them
        (luma_test.PROGRESS_LINE): what the runner sees of the game's progress while it runs. The game's stdout reaches
        logcat as tag Nebula (host_window_sdl.cpp); --pid keeps a process that took the place of another apart."""
        r = self.adb("logcat", "-d", "-v", "raw", "--pid=%s" % pid, "-t", "200", "-s", "Nebula")
        return r.stdout.decode("utf-8", "replace").replace("\r\n", "\n") if r.returncode == 0 else ""

    def exit_record(self, pid, clock=time.time, sleep=time.sleep):
        """Android's record of the game process's end (dumpsys activity exit-info): its exit status when it ended
        itself (reason 1, EXIT_SELF), 128 + the signal when a signal ended it (reason 2), 134 for a crash (reasons 4
        and 5, CRASH and CRASH_NATIVE: the abort the crash reporter raises); None when something else ended it.
        Android writes the record after the process is gone (tens of milliseconds later, more on a busy device), and
        first with reason 0 (UNKNOWN), its real reason and status following: until the record is there with a known
        reason this asks again, for up to EXIT_RECORD_SECONDS (read on `clock`, waited with `sleep`). Returns (found,
        code): found False is a record that never came, which is the tool's fault and no verdict on the game."""
        began = clock()
        deadline = began + EXIT_RECORD_SECONDS
        pause = 0.1
        while True:
            code, out = self.shell("dumpsys activity exit-info " + PACKAGE)
            lines = out.splitlines() if code == 0 else []
            settling = False
            for i, line in enumerate(lines):
                if (" pid=%s " % pid) not in line + " " or i + 1 >= len(lines):
                    continue
                # What was read, for the run's folder (exit-info.txt).
                self.exit_seen = "pid %s: found after %.2f s\n%s\n%s\n" % (pid, clock() - began, line.strip(),
                                                                         lines[i + 1].strip())
                m = re.search(r"process=(\S+) reason=(\d+) .*status=(-?\d+)", lines[i + 1])
                if not m or m.group(1) != GAME_PROCESS:
                    return True, None
                reason, status = int(m.group(2)), int(m.group(3))
                if reason == 0 and clock() < deadline:
                    settling = True  # written, its reason not yet: asked again
                    break
                return True, {1: status, 2: 128 + status, 4: 134, 5: 134}.get(reason)
            if settling:
                sleep(0.1)
                continue
            if clock() >= deadline:
                self.exit_seen = "pid %s: no record after %.2f s; the last answer:\n%s" % (pid, clock() - began, out)
                return False, None
            # Asked again a little less often the longer it takes: adb calls do not hurry a record that is slow.
            sleep(pause)
            pause = min(1.0, pause * 1.5)


# What a start check that ended without the game's end shows: the system's lines about starting the app and the
# app's own, from the log since the check's am start.
START_LOG_TAGS = ("ActivityManager", "ActivityTaskManager", "Zygote", "linker", "nativeloader") + LOG_TAGS
START_LOG_LINES = 150


def device_state(device):
    """The device at a start check's time-out: the test app's processes, the activity on top, and the log's lines about
    the start, so a red names whether Android started the process at all and why not."""
    code, ps = device.shell("ps -A -o PID,PPID,S,NAME")
    mine = [line for line in ps.splitlines()[1:] if PACKAGE in line]
    code, top = device.shell("dumpsys activity activities")
    resumed = [line.strip() for line in top.splitlines() if "ResumedActivity" in line][:4]
    log = device.adb("logcat", "-d", "-v", "brief", "-s", *START_LOG_TAGS).stdout.decode("utf-8", "replace")
    lines = log.strip().splitlines()[-START_LOG_LINES:]
    return ("the test app's processes: %s\nthe activity on top: %s\nthe log since am start (%d lines, the last %d):\n%s"
            % ("; ".join(" ".join(line.split()) for line in mine) or "none",
               "; ".join(resumed) or "none", len(log.strip().splitlines()), len(lines), "\n".join(lines)))


def start_check(device, frames, limit, clock=time.time, sleep=time.sleep):
    """The start check on the device (start-check above): (code, what was seen). code is the game process's exit code
    from Android's record (0 passes), None when no end was read: the test app missing, a start refused, a process that
    never appeared, one still running at the limit (stopped), or an end Android gave no code for."""
    if not device.installed():
        return None, "the test app %s is not installed" % PACKAGE
    folder = "files/harness/" + START_CHECK
    code, out = device.run_as("rm -rf %s && mkdir -p %s/settings %s/appdata" % (folder, folder, folder))
    if code != 0:
        return None, "the start check's folder could not be made: %s" % out.strip()[:200]
    device.shell("am force-stop " + PACKAGE)
    device.adb("logcat", "-c")
    began = clock()
    code, out = device.shell("am start -W -n %s --es nebula.harness %s --ei nebula.start_check %d"
                             % (ACTIVITY, START_CHECK, frames))
    if code != 0 or "Error" in out:
        return None, "am start: %s" % out.strip()[:300]
    game_pid = ""
    while clock() < began + limit:
        now_pid = device.pid()
        if now_pid:
            game_pid = now_pid
        elif game_pid:
            found, ended = device.exit_record(game_pid)
            if not found and device.pid() == game_pid:
                continue  # pidof's answer lost, not the process
            return ended, device.exit_seen
        elif clock() > began + START_SECONDS:
            return None, "the game process %s never appeared in %d seconds\n%s" % (GAME_PROCESS, START_SECONDS,
                                                                                    device_state(device))
        sleep(0.25)
    seen = device_state(device)
    device.shell("am force-stop " + PACKAGE)
    return None, "the start check did not end in %d seconds; stopped\n%s" % (limit, seen)


def device_lock(path, who):
    """The device's lock (a folder, made at once or not at all): None when taken, else who holds it. The holder line ends with this
    process's `since T pid N` (tools/gate/lockholder.py); a holder whose process has ended loses the lock, with one printed line, so that a
    run that died with it does not keep the tablet away for ever. A line with no pid (taken by hand) holds until it is freed."""
    if path is None:
        return None
    gate = str(pathlib.Path(__file__).resolve().parents[1] / "gate")
    sys.path.insert(0, gate)
    try:
        import lockholder
        import measure_wait
    finally:
        sys.path.remove(gate)
    lock = pathlib.Path(path)
    for _ in range(3):
        try:
            lock.mkdir()
        except FileExistsError:
            holder = lock / "holder.txt"
            text = holder.read_text(encoding="utf-8", errors="replace").strip() if holder.exists() else "unknown"
            old = lockholder.gone(text, measure_wait.process_alive, lockholder.process_started)
            if old is not None and old != os.getpid():
                print("luma_test_android.py: the device lock %s was held by pid %d, which has ended: taken over (%s)" % (lock, old, text),
                      flush=True)
                shutil.rmtree(lock, ignore_errors=True)
                continue
            return text
        (lock / "holder.txt").write_text("%s %s %s\n" % (who, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), lockholder.named()),
                                         encoding="utf-8")
        return None
    return "the device lock could not be taken"


def release_lock(path):
    if path is not None and pathlib.Path(path).is_dir():
        shutil.rmtree(path)


def host_for(abi):
    """luma_test.py's HOST for an Android device of this ABI."""
    platform_name = ABIS.get(abi)
    if platform_name is None:
        raise SystemExit("the device's ABI %s is not one the app is built for (%s)" % (abi, ", ".join(ABIS)))
    host = {"platform": platform_name, "exes": {"vulkan": "GameActivity"}, "game": "libsmg_game.so",
            "plain": "libsmg_game_plain.so", "extension": "libnebula_test_extension.so", "importer": None,
            "importer_exe": "nebula-import", "half": "luma-test.so", "ending": ".so"}
    host["code"] = "code/%s/mod%s" % (host["platform"], host["ending"])
    return host


def run_one(lt, device, test, args, run_folder, previous, clock=time.time, sleep=time.sleep):
    """luma_test.run_one on an Android device: the same layout, the start over adb. A failure while the device no
    longer answers adb (unplugged, asleep with USB off) is lt.DeviceLost: the device went, not the test."""
    try:
        return run_on_device(lt, device, test, args, run_folder, previous, clock, sleep)
    except (lt.SetupFailed, RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        if not device.answers():
            raise lt.DeviceLost("%s left adb during the test (%s)" % (device.serial, str(error)[:160])) from error
        raise


def run_on_device(lt, device, test, args, run_folder, previous, clock=time.time, sleep=time.sleep):
    for m in test["mods"]:
        if m["install"] == "import":
            raise lt.HarnessError("install import: the Android app imports with its own screen, not a start argument")
    if test["hidden"]:
        raise lt.HarnessError("hide_game_files: the device's game is the app's own")
    if test["safe_start"] == "asked":
        raise lt.HarnessError("safe_start asked: the Android game screen takes no start arguments from a run")
    disc = test.get("disc")
    if disc is not None:
        have = lt.pak_disc_of(device.pak_header())
        if have != disc:
            raise lt.NotRun("needs disc %s: the device's game is %s" % (disc, have or "of no known disc"))
    bin_folder = pathlib.Path(args.bin).resolve()
    for need in (lt.HOST["game"], lt.HOST["extension"]):
        if not (bin_folder / need).is_file():
            raise lt.HarnessError("the bin folder has no %s" % need)
    if run_folder.exists():
        shutil.rmtree(run_folder)
    for sub in ("pictures", "saves", "settings", "appdata"):
        (run_folder / sub).mkdir(parents=True)
    settings = run_folder / "settings"
    if previous is not None:
        for sub in ("saves", "settings"):
            shutil.rmtree(run_folder / sub)
            shutil.copytree(previous / sub, run_folder / sub)
        lt.carry_data(previous, run_folder)
    lt.place_saves(test, run_folder)
    # The libraries the app carries are the ones the run starts; their hashes are the bin folder's, built with them.
    frozen = {name: lt.sha256(bin_folder / name) for name in (lt.HOST["game"], lt.HOST["extension"])}
    if not test["bare"]:
        frozen[lt.HOST["half"]] = lt.sha256(bin_folder / lt.HOST["half"])
        lt.install_game_half(settings, bin_folder / lt.HOST["half"])
    placed, imports = lt.place_mods(test, settings, previous, frozen, {})
    assert not imports, imports
    lt.write_run_settings(test, settings)
    hooks = [a for a in test.get("settings", []) if a.partition("=")[0] in lt.TEST_HOOKS]
    if hooks:
        (run_folder / "env.txt").write_text("".join(h + "\n" for h in hooks), encoding="utf-8", newline="\n")
    # The game's --diagnostics, which the game screen reads from diagnostics.txt (Harness.java): a timeline is
    # written to the run's folder as on the desktop.
    diagnostics = lt.run_diagnostics(test, args)
    if diagnostics:
        (run_folder / "diagnostics.txt").write_text(",".join(diagnostics) + "\n", encoding="utf-8", newline="\n")
    name = "".join(c if c.isalnum() or c in "._-" else "_" for c in
                   ("%s-%s" % (test["name"], datetime.datetime.now().strftime("%H%M%S")))[:64])
    app_files = device.files()
    remote = "%s/harness/%s" % (app_files, name)
    (run_folder / "plan.txt").write_text(lt.plan_text(test, remote), encoding="utf-8", newline="\n")
    limit = int(math.ceil(test["limit"]))

    archive = run_folder.parent / (run_folder.name + ".out.tar")
    with tarfile.open(archive, "w") as tar:
        tar.add(run_folder, arcname=name)
    pushed = "/data/local/tmp/luma-%s.tar" % name
    r = device.adb("push", str(archive), pushed)
    archive.unlink()
    if r.returncode != 0:
        raise lt.SetupFailed("adb push: %s" % r.stderr.decode("utf-8", "replace").strip()[:200])
    code, out = device.run_as("mkdir -p %s/harness && cd %s/harness && rm -rf %s && tar -xf %s" % (app_files, app_files, name, pushed))
    device.shell("rm -f " + pushed)
    if code != 0:
        raise lt.SetupFailed("the run's folder could not be unpacked on the device: %s" % out.strip()[:200])

    device.shell("am force-stop " + PACKAGE)
    device.adb("logcat", "-c")
    began = clock()
    code, out = device.shell("am start -W -n %s --es nebula.harness %s --ei nebula.seconds %d" % (
        ACTIVITY, name, limit + int(lt.HANG_SECONDS) + 30))
    if code != 0 or "Error" in out:
        raise lt.SetupFailed("am start: %s" % out.strip()[:300])
    ended, game_pid, gone, starts = None, "", False, 1
    # The game's progress is read from the device's log (lt.Progress). The limit is when the runner looks at how the run
    # stands, judged by the steps as on a PC (luma_test.py watch_run, evaluate): the game's own --seconds is set beyond it.
    # Hung when no step came for lt.HANG_SECONDS while the process lived; a limit that passes in a pause waits for steps to
    # come again or for the hang; one that passes with the game stepping, or with no step ever, ends the game (limit_hit).
    progress = lt.Progress(None, clock)
    looked, hung, no_record, limit_hit = None, None, False, False
    deadline = began + limit
    while True:
        past = clock() >= deadline
        if past and not game_pid:
            break  # the game never appeared
        now_pid = device.pid()
        if now_pid and game_pid and now_pid != game_pid:
            # The game started again (GameActivity.restartGame) between two looks: the new process is the run's.
            starts += 1
            deadline = clock() + limit
            past = False
            progress.new_process()
            looked = None
        if now_pid:
            game_pid = now_pid
            if looked is None or past or clock() - looked >= PROGRESS_POLL_SECONDS:
                looked = clock()
                progress.feed_lines(device.log_tail(now_pid).split("\n"))
                quiet = progress.quiet()
                if quiet is not None and quiet >= lt.HANG_SECONDS:
                    # No step for that long, the process alive: hung. The game writes no dump on Android.
                    hung = {"seconds": int(quiet), "frame": progress.frame, "dumps": []}
                    break
                if past and not (quiet is not None and quiet > lt.STALL_SECONDS):
                    limit_hit = True
                    break
        elif game_pid:
            found, ended = device.exit_record(game_pid, clock, sleep)
            (run_folder / "exit-info.txt").write_text(device.exit_seen, encoding="utf-8")
            if not found and device.pid() == game_pid:
                # pidof answered nothing for a process that is still there (adb's answer lost, not the process):
                # still running.
                continue
            # restartGame ends its process with SIGKILL (137) after starting the new one with the same extras.
            new_pid = ""
            if ended == 137:
                for _ in range(15):
                    new_pid = device.pid()
                    if new_pid:
                        break
                    sleep(1)
            if not new_pid:
                gone = True
                no_record = not found  # Android never wrote its record of the end: the tool's fault (kind tool)
                break
            starts += 1
            game_pid, ended = new_pid, None
            deadline = clock() + limit
            progress.new_process()
            looked = None
        sleep(0.5)
    if not gone:
        if hung is None and not limit_hit and game_pid:
            progress.feed_lines(device.log_tail(game_pid).split("\n"))  # how the game stood
        device.shell("am force-stop " + PACKAGE)  # past its limit, or hung: stopped, and the verdict says so (code None)
        ended = None
    seconds = clock() - began

    back = run_folder.parent / (run_folder.name + ".back.tar")
    r = device.adb("exec-out", "run-as %s sh -c 'cd %s/harness && tar -cf - %s'" % (PACKAGE, app_files, name))
    if r.returncode != 0 or not r.stdout:
        raise lt.SetupFailed("the run's folder could not be read back from the device")
    back.write_bytes(r.stdout)
    with tarfile.open(back) as tar:
        for member in tar.getmembers():
            # Only the run's own files, under its own name (no absolute path, no "..").
            parts = pathlib.PurePosixPath(member.name).parts
            if not parts or parts[0] != name or ".." in parts or not (member.isfile() or member.isdir()):
                continue
            target = run_folder.joinpath(*parts[1:])
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with tar.extractfile(member) as source, open(target, "wb") as out_file:
                    shutil.copyfileobj(source, out_file)
    back.unlink()
    device.run_as("rm -rf %s/harness/%s" % (app_files, name))
    log = device.adb("logcat", "-d", "-v", "raw", "-s", *LOG_TAGS).stdout
    (run_folder / "run.log").write_bytes(log.replace(b"\r\n", b"\n"))
    return {"code": ended, "seconds": seconds, "frozen": frozen, "mods": placed, "exe": "GameActivity", "starts": starts,
            "no_exit_record": no_record, **dict(progress.snapshot(), hung=hung, limit_hit=limit_hit)}


def prepare(args):
    device = Device(args.device)
    if not device.answers():
        raise SystemExit("%s does not answer adb" % args.device)
    taken = device_lock(args.device_lock, args.who)
    if taken is not None:
        raise SystemExit("the device's lock %s is held: %s" % (args.device_lock, taken))
    try:
        r = device.adb("install", "-r", args.apk)
        said = (r.stdout + r.stderr).decode("utf-8", "replace").strip().splitlines()
        print("install:", said[-1] if said else "adb install said nothing")
        if r.returncode != 0 or not device.installed():
            # The APK is not the test app (a build without -Debuggable is dev.nebula.galaxy): it went nowhere, as
            # adb installs by the APK's own id; nothing of the player's game was touched.
            raise SystemExit("the test app %s is not on the device after the install" % PACKAGE)
        push_pak(device, args.pak, getattr(args, "key", ""))
    finally:
        release_lock(args.device_lock)
    return 0


def kept_key(pak, given=""):
    """The key the game data goes by: `given`, else the registry's key for this file (tools/gate/gamedata.py: RMGE01-37-3aa6533b1f08),
    else the name of the folder it is in; only letters, digits, dot, dash and underscore."""
    key = given
    if not key:
        try:
            want = os.path.normcase(os.path.abspath(str(pak)))
            for line in pathlib.Path(DATA_REGISTRY).read_text(encoding="utf-8").splitlines()[1:]:
                cells = line.split("\t")
                if len(cells) >= 2 and os.path.normcase(os.path.abspath(cells[1])) == want:
                    key = cells[0]
                    break
        except OSError:
            pass
    key = key or pathlib.Path(pak).resolve().parent.name or "data"
    return re.sub(r"[^A-Za-z0-9._-]", "_", key)


def kept_paths(key):
    """(the kept file, its info file) of a key on the device."""
    return "%s/game-%s.pak" % (KEPT_DIR, key), "%s/game-%s.pak.info" % (KEPT_DIR, key)


def pc_hash(pak, algorithm):
    import hashlib
    digest = hashlib.new(algorithm)
    with open(pak, "rb") as f:
        for chunk in iter(lambda: f.read(8 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_info(text):
    """{'size': int, 'hash': (algorithm, hex) or None} of an info file, or None when it is not one."""
    if not text:
        return None
    found = {}
    for line in text.splitlines():
        cells = line.split()
        if len(cells) >= 2 and cells[0] == "size" and cells[1].isdigit():
            found["size"] = int(cells[1])
        elif len(cells) >= 2 and cells[0] == "hash":
            found["hash"] = (cells[1], cells[2]) if len(cells) >= 3 and cells[1] != "none" else None
    return found if "size" in found else None


def gb(n):
    return "%.1f GB" % (n / 1e9)


def ensure_kept(device, pak, key, say=print, pc_digest=pc_hash):
    """The game data kept on the device for `key`. Returns (how, kept path or None): `kept` (it was there, of the recorded size, with its
    info file), `pushed` (put there now, its hash made on the device and equal to the PC's, or its size only where the device has no hash
    tool), `streamed` (no room to keep it: nothing was kept, the caller pushes it the old way). A copy of another key is removed first.
    SystemExit(EXIT_TOOL-worded) when the hash stays unequal after a second push or a push fails."""
    size = pathlib.Path(pak).stat().st_size
    path, info_path = kept_paths(key)
    names = device.list_names(KEPT_DIR)
    stale = [n for n in names if n.startswith("game-") and n not in (path.rsplit("/", 1)[1], info_path.rsplit("/", 1)[1])]
    if stale:
        device.remove_files(*["%s/%s" % (KEPT_DIR, n) for n in stale])
        say("machine: removed the kept game data of another key: %s" % ", ".join(sorted(stale)))
    info = read_info(device.read_text(info_path)) if info_path.rsplit("/", 1)[1] in names else None
    on_device = device.size_of(path) if path.rsplit("/", 1)[1] in names else None
    if info is not None and on_device == info["size"] == size:
        return "kept", path
    if on_device is not None or info is not None:
        why = ("its size is %s, the recorded one %s, the pak's %d" % (on_device, info["size"] if info else "none", size)
               if info is not None else "it has no info file")
        say("machine: the kept game data %s is wrong (%s): it is pushed again" % (key, why))
        device.remove_files(path, info_path)
    free = device.free_bytes("/data")
    need = 2 * size + KEPT_SPARE_BYTES
    if free is not None and free < need:
        say("machine: not enough room to keep the game data on the tablet (%s free, the kept copy, the app's copy and 1 GB need %s): "
            "it goes into the app the old way for this run, nothing is kept" % (gb(free), gb(need)))
        return "streamed", None
    if not device.make_folder(KEPT_DIR):
        raise SystemExit("%s could not be made on the device" % KEPT_DIR)
    part = path + ".part"
    for attempt in (1, 2):
        ok, said = device.push_file(pak, part)
        if not ok:
            device.remove_files(part)
            raise SystemExit("the pak did not reach the device: %s" % said)
        algorithm, theirs = device.hash_of(part)
        if algorithm is None:
            say("machine: the tablet has no sha256sum and no md5sum: the kept game data %s is checked by its size only" % key)
            mine = None
        else:
            mine = pc_digest(pak, algorithm)
        got = device.size_of(part)
        if got != size or (algorithm is not None and theirs != mine):
            device.remove_files(part)
            if attempt == 1:
                say("machine: the pushed game data is not the PC's (size %s of %d, %s %s against %s): pushed once more" % (
                    got, size, algorithm or "no hash", theirs, mine))
                continue
            print("tool: the game data on the tablet is not the PC's after two pushes: size %s of %d, %s %s against %s" % (
                got, size, algorithm or "no hash", theirs, mine))
            raise SystemExit(EXIT_TOOL)
        break
    if not device.move_file(part, path):
        raise SystemExit("the kept game data could not be put in place on the device")
    record = "size %d\nhash %s %s\n" % (size, algorithm or "none", theirs or "") if algorithm else "size %d\nhash none\n" % size
    if not device.write_text(info_path, record):
        raise SystemExit("the info file of the kept game data could not be written on the device")
    say("pushed %s (%d bytes) to %s%s" % (pak, size, path, (", %s %s" % (algorithm, theirs)) if algorithm else ", size only"))
    return "pushed", path


def push_pak(device, pak, key="", say=print, clock=time.time):
    """The game.pak into the test app's files/game, checked by size (SystemExit with the reason when it did not get there). The data is
    kept on the device between runs and copied into the app there (ensure_kept); with no room to keep it, it is pushed to a temporary
    file and copied in, as it always was."""
    pak = pathlib.Path(pak)
    size = pak.stat().st_size
    key = kept_key(pak, key)
    began = clock()
    how, kept = ensure_kept(device, pak, key, say)
    if how == "streamed":
        # adb push (checked by size below), then copied in as the app: the app's own files take nothing from adb directly.
        # Streaming through `adb exec-in` stopped after 61 MB of 4 GB on Windows with exit 0.
        pushed = "/data/local/tmp/luma-game.pak"
        ok, said = device.push_file(pak, pushed)
        if not ok:
            device.remove_files(pushed)
            raise SystemExit("the pak did not reach the device: %s" % said)
        device.shell("chmod 644 " + pushed)
        ok, said = device.copy_into_app(pushed)
        device.remove_files(pushed)
    else:
        ok, said = device.copy_into_app(kept)
    if not ok:
        raise SystemExit("the pak could not be copied into the app's files: %s" % said)
    got = device.app_pak_size()
    if got != size:
        raise SystemExit("the device's game.pak is %s bytes, the pak %d" % (got if got is not None else "no", size))
    say("pak: %s (%d bytes) in %s's files/game (%s, %.1f s)" % (pak, size, PACKAGE, {"kept": "copied on the tablet",
                                                                                 "pushed": "pushed, then copied on the tablet",
                                                                                 "streamed": "pushed to a temporary file"}[how],
                                                              clock() - began))
    if how != "streamed":
        say("machine: game data kept on the tablet: %s, %s" % (key, gb(size)))


def push_data(args):
    """Puts the game.pak again into the test app that is already installed (the gate's check of the tablet's data, N-1079)."""
    device = Device(args.device)
    if not device.answers():
        raise SystemExit("%s does not answer adb" % args.device)
    taken = device_lock(args.device_lock, args.who)
    if taken is not None:
        raise SystemExit("the device's lock %s is held: %s" % (args.device_lock, taken))
    try:
        if not device.installed():
            raise SystemExit("%s is not installed: there is no app to push the data into" % PACKAGE)
        push_pak(device, args.pak, getattr(args, "key", ""))
    finally:
        release_lock(args.device_lock)
    return 0


def kept_report(device):
    """(key, size) of the game data kept on the device, or None: what the gate says of it at a landing's start."""
    names = device.list_names(KEPT_DIR)
    for name in sorted(names):
        if name.startswith("game-") and name.endswith(".pak"):
            info = read_info(device.read_text("%s/%s.info" % (KEPT_DIR, name)))
            size = device.size_of("%s/%s" % (KEPT_DIR, name))
            if info is not None and size == info["size"]:
                return name[len("game-"):-len(".pak")], size
    return None


def remove_kept(args):
    """remove --data: the kept game data (every game-*.pak and game-*.pak.info in KEPT_DIR) removed from the device, and nothing else."""
    device = Device(args.device)
    if not device.answers():
        print("%s does not answer adb: the kept game data could not be removed" % args.device)
        return 1
    names = [n for n in device.list_names(KEPT_DIR) if n.startswith("game-")]
    if not names:
        print("no game data is kept on the tablet")
        return 0
    device.remove_files(*["%s/%s" % (KEPT_DIR, n) for n in names])
    left = [n for n in device.list_names(KEPT_DIR) if n.startswith("game-")]
    print("kept game data removed: %s%s" % (", ".join(sorted(names)), ("; still there: " + ", ".join(left)) if left else ""))
    return 0 if not left else 1


def remove(args):
    if getattr(args, "data", False):
        return remove_kept(args)
    device = Device(args.device)
    if not device.answers():
        # Not "not installed": nothing is known of a device that does not answer, and the app may still be on it.
        print("%s does not answer adb: %s could not be removed" % (args.device, PACKAGE))
        return 1
    if not device.installed():
        print("%s is not installed" % PACKAGE)
        return 0
    r = device.adb("uninstall", PACKAGE)
    print("uninstall %s: %s" % (PACKAGE, (r.stdout + r.stderr).decode("utf-8", "replace").strip()))
    return 0 if not device.installed() else 1


def start_check_command(args):
    device = Device(args.device)
    if not device.answers():
        raise SystemExit("%s does not answer adb" % args.device)
    taken = device_lock(args.device_lock, args.who)
    if taken is not None:
        raise SystemExit("the device's lock %s is held: %s" % (args.device_lock, taken))
    try:
        if args.apk:
            r = device.adb("install", "-r", args.apk)
            said = (r.stdout + r.stderr).decode("utf-8", "replace").strip().splitlines()
            print("install:", said[-1] if said else "adb install said nothing")
        code, seen = start_check(device, args.frames, args.limit)
        log = device.adb("logcat", "-d", "-v", "brief", "-s", *LOG_TAGS).stdout.decode("utf-8", "replace")
        print(log.strip())
        print(seen.strip())
        print("start check: %s" % ("pass" if code == 0 else "FAILED (exit %s)" % code))
        return 0 if code == 0 else 1
    finally:
        release_lock(args.device_lock)


def main():
    # A self-test that starts this tool as a child process runs it under the live-state guard (tools/gate/selftest_guard.py).
    import pathlib as _pathlib
    import sys as _sys
    _gate = str(_pathlib.Path(__file__).resolve().parents[1] / "gate")
    _sys.path.insert(0, _gate)
    try:
        import selftest_guard
    finally:
        _sys.path.remove(_gate)
    selftest_guard.install_if_asked()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("prepare")
    p.add_argument("--device", required=True)
    p.add_argument("--apk", required=True)
    p.add_argument("--pak", required=True)
    p.add_argument("--key", default="", help="the key of the game data (default: the registry's, else the pak's folder name)")
    p.add_argument("--who", default="?")
    p.add_argument("--device-lock", default=os.environ.get("LUMA_TEST_DEVICE_LOCK"))
    d = sub.add_parser("push-data")
    d.add_argument("--device", required=True)
    d.add_argument("--pak", required=True)
    d.add_argument("--key", default="")
    d.add_argument("--who", default="?")
    d.add_argument("--device-lock", default=os.environ.get("LUMA_TEST_DEVICE_LOCK"))
    r = sub.add_parser("remove")
    r.add_argument("--device", required=True)
    r.add_argument("--data", action="store_true", help="remove the game data kept on the tablet (and nothing else)")
    s = sub.add_parser("start-check")
    s.add_argument("--device", required=True)
    s.add_argument("--apk", default="", help="the test app to install first")
    s.add_argument("--frames", type=int, default=300)
    s.add_argument("--limit", type=int, default=300, help="seconds the check may take")
    s.add_argument("--who", default="?")
    s.add_argument("--device-lock", default=os.environ.get("LUMA_TEST_DEVICE_LOCK"))
    args = parser.parse_args()
    return {"prepare": prepare, "push-data": push_data, "remove": remove,
            "start-check": start_check_command}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
