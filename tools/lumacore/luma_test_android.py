"""luma_test_android.py: the proof harness on an Android device, over adb.

    python -X utf8 -I tools/lumacore/luma_test_android.py prepare --device SERIAL --apk <test apk> --pak <game.pak>
    python -X utf8 -I tools/lumacore/luma_test_android.py remove  --device SERIAL
    python -X utf8 -I tools/lumacore/luma_test_android.py start-check --device SERIAL [--apk <test apk>] [--frames N]
    python -X utf8 -I tools/lumacore/luma_test.py run <suite.json | test> --device SERIAL --bin <test-bin-ABI> ...

Only the test app is ever touched: dev.nebula.galaxy.test, a debuggable build with its own id (platform/android/app/
build.gradle.kts), installed beside the player's game and never in its place. Nothing here can name another package.

prepare installs the test app (platform/android/build_apk.ps1 -Debuggable -Harness ...: Nebula.apk) and streams a
game.pak into its private storage (files/game/game.pak), where the game screen reads it; the pak holds no platform's
data, so the importer's on any computer serves. remove uninstalls the test app, its data with it; do it when a run of
work is over.

A run (luma_test.py --device) is luma_test.py's on this platform: the runner reads each test, lays out the run's
folder, writes the plan and judges the files that come back; this module only moves the folder and starts the game:
  1. the run's folder, with the device's paths in its plan and the test's hooks in env.txt (the test build's
     Harness.java sets them), is packed, pushed and unpacked into files/harness/<name>;
  2. the game screen starts with the intent extras nebula.harness and nebula.seconds, and is waited for: its process
     gone, or the limit and a minute (then stopped: a timeout);
  3. how the process ended comes from Android's own record (ApplicationExitInfo), which Android writes a little after
     the process is gone: it is read again until the record is there;
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
import json
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
# How long Android may take to write its exit record after the process is gone.
EXIT_RECORD_SECONDS = 5.0
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

    def pak_header(self):
        """The first 64 bytes of the test app's game.pak (empty when it has none)."""
        r = self.adb("exec-out", "run-as %s sh -c 'head -c 64 files/game/game.pak 2>/dev/null'" % PACKAGE)
        return r.stdout if r.returncode == 0 else b""

    def pid(self):
        code, out = self.shell("pidof " + GAME_PROCESS)
        return out.strip() if code == 0 else ""

    def exit_record(self, pid):
        """Android's record of the game process's end (dumpsys activity exit-info): its exit status when it ended
        itself (reason 1, EXIT_SELF), 128 + the signal when a signal ended it (reason 2), 134 for a crash (reasons 4
        and 5, CRASH and CRASH_NATIVE: the abort the crash reporter raises); None when something else ended it.
        Android writes the record after the process is gone (tens of milliseconds later, more on a busy device), and
        first with reason 0 (UNKNOWN), its real reason and status following: until the record is there with a known
        reason this asks again, for up to EXIT_RECORD_SECONDS. Returns (found, code)."""
        began = time.time()
        deadline = began + EXIT_RECORD_SECONDS
        while True:
            code, out = self.shell("dumpsys activity exit-info " + PACKAGE)
            lines = out.splitlines() if code == 0 else []
            settling = False
            for i, line in enumerate(lines):
                if (" pid=%s " % pid) not in line + " " or i + 1 >= len(lines):
                    continue
                # What was read, for the run's folder (exit-info.txt).
                self.exit_seen = "pid %s: found after %.2f s\n%s\n%s\n" % (pid, time.time() - began, line.strip(),
                                                                         lines[i + 1].strip())
                m = re.search(r"process=(\S+) reason=(\d+) .*status=(-?\d+)", lines[i + 1])
                if not m or m.group(1) != GAME_PROCESS:
                    return True, None
                reason, status = int(m.group(2)), int(m.group(3))
                if reason == 0 and time.time() < deadline:
                    settling = True  # written, its reason not yet: asked again
                    break
                return True, {1: status, 2: 128 + status, 4: 134, 5: 134}.get(reason)
            if settling:
                time.sleep(0.1)
                continue
            if time.time() >= deadline:
                self.exit_seen = "pid %s: no record after %.2f s; the last answer:\n%s" % (pid, time.time() - began, out)
                return False, None
            time.sleep(0.1)


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
            return None, "the game process %s never appeared in %d seconds" % (GAME_PROCESS, START_SECONDS)
        sleep(0.25)
    device.shell("am force-stop " + PACKAGE)
    return None, "the start check did not end in %d seconds; stopped" % limit


def device_lock(path, who):
    """The device's lock (a folder, made at once or not at all): None when taken, else who holds it."""
    if path is None:
        return None
    lock = pathlib.Path(path)
    try:
        lock.mkdir()
    except FileExistsError:
        holder = lock / "holder.txt"
        return holder.read_text(encoding="utf-8", errors="replace").strip() if holder.exists() else "unknown"
    (lock / "holder.txt").write_text("%s %s\n" % (who, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")),
                                     encoding="utf-8")
    return None


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


def device_data_folder(run_folder):
    """The game's data folder in a device run: <run>/appdata, which Harness.java gives as --data (the desktop's is
    <run>/appdata/Nebula, the system's own layout: luma_test.run_data_folder)."""
    return run_folder / "appdata"


def listening_ports(device):
    """The TCP ports something on the device listens on (/proc/net/tcp and tcp6, state 0A); empty when unreadable."""
    code, out = device.shell("cat /proc/net/tcp /proc/net/tcp6 2>/dev/null")
    ports = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) > 3 and parts[3] == "0A" and ":" in parts[1]:
            try:
                ports.add(int(parts[1].rsplit(":", 1)[1], 16))
            except ValueError:
                pass
    return ports


def device_free_ports(lt, device, test, tries=20):
    """lt.free_ports for a device run: the mods that use them run on the device, so each port is also one nothing on
    the device listens on (N-4020)."""
    taken = None
    for _ in range(tries):
        ports = lt.free_ports(test)
        if not ports:
            return ports
        if taken is None:
            taken = listening_ports(device)
        if not taken & set(ports.values()):
            return ports
    raise lt.SetupFailed("no port free on both this computer and the device in %d tries" % tries)


def run_one(lt, device, test, args, run_folder, previous):
    """luma_test.run_one on an Android device: the same layout, the start over adb. A failure while the device no
    longer answers adb (unplugged, asleep with USB off) is lt.DeviceLost: the device went, not the test."""
    try:
        return run_on_device(lt, device, test, args, run_folder, previous)
    except (lt.SetupFailed, RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        if not device.answers():
            raise lt.DeviceLost("%s left adb during the test (%s)" % (device.serial, str(error)[:160])) from error
        raise


def run_on_device(lt, device, test, args, run_folder, previous):
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
        lt.carry_data(previous, run_folder, device_data_folder)
    lt.place_saves(test, run_folder)
    # The libraries the app carries are the ones the run starts; their hashes are the bin folder's, built with them.
    frozen = {name: lt.sha256(bin_folder / name) for name in (lt.HOST["game"], lt.HOST["extension"])}
    if not test["bare"]:
        frozen[lt.HOST["half"]] = lt.sha256(bin_folder / lt.HOST["half"])
        lt.install_game_half(settings, bin_folder / lt.HOST["half"])
    ports = device_free_ports(lt, device, test)
    if ports:
        (run_folder / "ports.json").write_text(json.dumps(ports, indent=2) + "\n", encoding="utf-8")
    placed, imports = lt.place_mods(test, settings, previous, frozen, ports)
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
    began = time.time()
    code, out = device.shell("am start -W -n %s --es nebula.harness %s --ei nebula.seconds %d" % (ACTIVITY, name, limit))
    if code != 0 or "Error" in out:
        raise lt.SetupFailed("am start: %s" % out.strip()[:300])
    ended, game_pid, gone, starts = None, "", False, 1
    deadline = began + limit + 60
    while time.time() < deadline:
        now_pid = device.pid()
        if now_pid and game_pid and now_pid != game_pid:
            # The game started again (GameActivity.restartGame) between two looks: the new process is the run's.
            starts += 1
            deadline = time.time() + limit + 60
        if now_pid:
            game_pid = now_pid
        elif game_pid:
            found, ended = device.exit_record(game_pid)
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
                    time.sleep(1)
            if not new_pid:
                gone = True
                break
            starts += 1
            game_pid, ended = new_pid, None
            deadline = time.time() + limit + 60
        time.sleep(0.5)
    if not gone:
        device.shell("am force-stop " + PACKAGE)  # past its limit: stopped, and the verdict says so (code None)
    seconds = time.time() - began

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
    # The pictures are the device's screen: boxes are placed by the test's window frame in them (N-4021).
    return {"code": ended, "seconds": seconds, "frozen": frozen, "mods": placed, "exe": "GameActivity", "starts": starts,
            "fit": test["window"]}


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
        pak = pathlib.Path(args.pak)
        size = pak.stat().st_size
        # adb push (checked by size below), then copied in as the app: the app's own files take nothing from adb
        # directly. Streaming through `adb exec-in` stopped after 61 MB of 4 GB on Windows with exit 0.
        pushed = "/data/local/tmp/luma-game.pak"
        r = device.adb("push", str(pak), pushed, timeout=3600)
        if r.returncode != 0:
            device.shell("rm -f " + pushed)
            raise SystemExit("the pak did not reach the device: %s" % r.stderr.decode("utf-8", "replace").strip()[:200])
        device.shell("chmod 644 " + pushed)
        code, out = device.run_as("mkdir -p files/game && cp %s files/game/game.pak.part && "
                                  "mv files/game/game.pak.part files/game/game.pak" % pushed, timeout=1800)
        device.shell("rm -f " + pushed)
        if code != 0:
            raise SystemExit("the pak could not be copied into the app's files: %s" % out.strip()[:200])
        code, out = device.run_as("stat -c %s files/game/game.pak")
        if out.strip() != str(size):
            raise SystemExit("the device's game.pak is %s bytes, the pak %d" % (out.strip() or "no", size))
        print("pak: %s (%d bytes) in %s's files/game" % (pak, size, PACKAGE))
    finally:
        release_lock(args.device_lock)
    return 0


def remove(args):
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
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("prepare")
    p.add_argument("--device", required=True)
    p.add_argument("--apk", required=True)
    p.add_argument("--pak", required=True)
    p.add_argument("--who", default="?")
    p.add_argument("--device-lock", default=os.environ.get("LUMA_TEST_DEVICE_LOCK"))
    r = sub.add_parser("remove")
    r.add_argument("--device", required=True)
    s = sub.add_parser("start-check")
    s.add_argument("--device", required=True)
    s.add_argument("--apk", default="", help="the test app to install first")
    s.add_argument("--frames", type=int, default=300)
    s.add_argument("--limit", type=int, default=300, help="seconds the check may take")
    s.add_argument("--who", default="?")
    s.add_argument("--device-lock", default=os.environ.get("LUMA_TEST_DEVICE_LOCK"))
    args = parser.parse_args()
    return {"prepare": prepare, "remove": remove, "start-check": start_check_command}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
