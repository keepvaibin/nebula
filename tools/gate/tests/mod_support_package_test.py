"""mod_support_package_test.py: tools/check_mod_support_package.py tells a package with the mod kit from one without it.

python -X utf8 -I tools/gate/tests/mod_support_package_test.py

Packages are planted in a temporary folder: a clean one and the same with one mod part added, for each system (the Linux
tar.gz, the Mac zip, the Android apk, the Windows Setup's zip entries). The Mac Info.plist and the Android manifest are the
tree's own, with the entries between mods:begin and mods:end taken out as build_macos.sh -n and app/build.gradle.kts do: the
stripped ones are clean and the whole ones are not (so the markers sit where the mod entries are). With --mods the check wants
LumaKit and the game module with the rows. Exit 0 when every case holds.
"""
import io
import pathlib
import re
import subprocess
import sys
import tarfile
import tempfile
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
TOOL = TREE / "tools" / "check_mod_support_package.py"
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def run(package, mode):
    done = subprocess.run([sys.executable, "-X", "utf8", "-I", str(TOOL), str(package), mode], capture_output=True, text=True)
    return done.returncode, done.stdout


def tar_gz(path, files):
    with tarfile.open(path, "w:gz") as archive:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))


def zip_of(path, files):
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in files.items():
            archive.writestr(name, data)


def strip_marked(text):
    """The text with everything from a mods:begin comment to the next mods:end comment taken out (the sed and the regex of
    build_macos.sh and app/build.gradle.kts)."""
    return re.sub(r"[ \t]*<!-- mods:begin.*?<!-- mods:end -->\n", "", text.replace("\r\n", "\n"), flags=re.S)


def android_strings(*strings):
    """A binary manifest stand-in: the strings as an Android string pool keeps them (UTF-8: lengths, text, end mark)."""
    return b"".join(bytes([len(s), len(s)]) + s.encode() + bytes(1) for s in strings)


with tempfile.TemporaryDirectory(prefix="mod-support-package-") as folder:
    tmp = pathlib.Path(folder)
    check(TOOL.is_file(), "tools/check_mod_support_package.py exists")

    # ---- Linux: the package's files, its menu entry and the type file.
    desktop_clean = b"[Desktop Entry]\nType=Application\nName=Nebula\nExec=nebula-launch\nTerminal=false\n"
    desktop_mods = desktop_clean.replace(b"Exec=nebula-launch\n", b"Exec=nebula-launch --import %u\n") + b"MimeType=application/x-nebula-luma;x-scheme-handler/nebula;\n"
    top = "Nebula-2.0.0-beta.1-linux-x64/"
    linux = {top + "files/nebula": b"x", top + "files/libsmg_game_plain.so": b"x", top + "files/libnebula_import.so": b"x",
             top + "support/dev.nebula.galaxy.desktop": desktop_clean, top + "install.sh": b"#!/bin/sh\n"}
    tar_gz(tmp / "clean.tar.gz", linux)
    code, out = run(tmp / "clean.tar.gz", "--no-mods")
    check(code == 0 and "no mod kit" in out, "a Linux package with none of the kit passes --no-mods: %s" % out)
    for what, extra in (("the rows module", {top + "files/libsmg_game.so": b"x"}),
                        ("LumaKit", {top + "files/luma/mods/lumakit.luma": b"x"}),
                        ("the self-description", {top + "files/luma/sdk/luma.h": b"x"}),
                        ("the .luma type file", {top + "support/dev.nebula.galaxy.xml": b"x"}),
                        ("the menu entry's file type and link", {top + "support/dev.nebula.galaxy.desktop": desktop_mods})):
        tar_gz(tmp / "planted.tar.gz", {**linux, **extra})
        code, out = run(tmp / "planted.tar.gz", "--no-mods")
        check(code == 1, "a Linux package with %s fails --no-mods: %s" % (what, out))
    # The menu entry package-linux.sh --no-mods makes from the tree's own: the sed of that script, run on the real file.
    script = (TREE / "installer" / "package-linux.sh").read_text(encoding="utf-8").replace("\r\n", "\n")
    match = re.search(r"(sed -e .*?)\s+\"\$repo/platform/linux/dev\.nebula\.galaxy\.desktop\"", script, flags=re.S)
    check(match is not None, "package-linux.sh makes the menu entry of a package with no mod support with a sed")
    if match is not None:
        command = match.group(1).rstrip("\\ \n")
        made = subprocess.run(["sh", "-c", command + ' "$0"', str(TREE / "platform" / "linux" / "dev.nebula.galaxy.desktop")],
                              capture_output=True).stdout
        check(b"Exec=nebula-launch\n" in made.replace(b"\r\n", b"\n") and len(made) > 40, "the sed leaves the menu entry's start line")
        tar_gz(tmp / "real-desktop.tar.gz", {**linux, top + "support/dev.nebula.galaxy.desktop": made})
        code, out = run(tmp / "real-desktop.tar.gz", "--no-mods")
        check(code == 0, "the menu entry the script makes from the tree's own passes --no-mods: %s" % out)
    whole = (TREE / "platform" / "linux" / "dev.nebula.galaxy.desktop").read_bytes()
    tar_gz(tmp / "whole-desktop.tar.gz", {**linux, top + "support/dev.nebula.galaxy.desktop": whole})
    check(run(tmp / "whole-desktop.tar.gz", "--no-mods")[0] == 1, "the tree's own menu entry, with its file type and link, fails --no-mods")
    full = {**linux, top + "files/libsmg_game.so": b"x", top + "files/luma/mods/lumakit.luma": b"x"}
    tar_gz(tmp / "full.tar.gz", full)
    check(run(tmp / "full.tar.gz", "--mods")[0] == 0, "a Linux package with the kit passes --mods")
    check(run(tmp / "clean.tar.gz", "--mods")[0] == 1, "a Linux package without it fails --mods")
    tar_gz(tmp / "half.tar.gz", {**linux, top + "files/libsmg_game.so": b"x"})
    code, out = run(tmp / "half.tar.gz", "--mods")
    check(code == 1 and "LumaKit" in out, "a Linux package with the rows module and no LumaKit fails --mods: %s" % out)

    # ---- Mac: the tree's own Info.plist (stripped as build_macos.sh -n strips it) and the language folders' strings.
    plist = (TREE / "platform" / "apple" / "Info.macos.plist.in").read_text(encoding="utf-8").replace("@NEBULA_VERSION@", "2.0.0")
    stripped = strip_marked(plist)
    check(stripped != plist and "mods:begin" not in stripped, "the plist template has its mod entries between markers")
    mac = {"Nebula/Nebula.app/Contents/MacOS/Nebula": b"x", "Nebula/Nebula.app/Contents/Frameworks/libsmg_game_plain.dylib": b"x",
           "Nebula/Nebula.app/Contents/Info.plist": stripped.encode(),
           "Nebula/Nebula.app/Contents/Resources/fr.lproj/InfoPlist.strings": "".encode("utf-16")}
    zip_of(tmp / "clean-mac.zip", mac)
    code, out = run(tmp / "clean-mac.zip", "--no-mods")
    check(code == 0, "a Mac app with the stripped plist passes --no-mods: %s" % out)
    zip_of(tmp / "mac-whole-plist.zip", {**mac, "Nebula/Nebula.app/Contents/Info.plist": plist.encode()})
    code, out = run(tmp / "mac-whole-plist.zip", "--no-mods")
    check(code == 1 and "cfbundledocumenttypes" in out and "microphoneusage" in out, "the whole plist fails --no-mods: %s" % out)
    zip_of(tmp / "mac-strings.zip", {**mac, "Nebula/Nebula.app/Contents/Resources/fr.lproj/InfoPlist.strings":
                                     '"NSMicrophoneUsageDescription" = "Un mod ...";\n'.encode("utf-16")})
    check(run(tmp / "mac-strings.zip", "--no-mods")[0] == 1, "a language folder's mod texts fail --no-mods")
    zip_of(tmp / "mac-rows.zip", {**mac, "Nebula/Nebula.app/Contents/Frameworks/libsmg_game.dylib": b"x"})
    check(run(tmp / "mac-rows.zip", "--no-mods")[0] == 1, "a Mac app with the rows module fails --no-mods")
    zip_of(tmp / "mac-kit.zip", {**mac, "Nebula/Nebula.app/Contents/Resources/luma/mods/lumakit.luma": b"x"})
    check(run(tmp / "mac-kit.zip", "--no-mods")[0] == 1, "a Mac app with LumaKit fails --no-mods")

    # ---- Android: the tree's own manifest (stripped as the build strips it) in its text form; the apk's is binary, and the
    # checker reads strings of that form, so the clean and the planted stand-ins are made of the strings.
    manifest = (TREE / "platform" / "android" / "app" / "src" / "main" / "AndroidManifest.xml").read_text(encoding="utf-8")
    manifest_stripped = strip_marked(manifest)
    for word in ("luma", "RECORD_AUDIO", "android.permission.CAMERA", "CHANGE_WIFI_MULTICAST_STATE", 'android:scheme="nebula"'):
        check(word in manifest and word not in manifest_stripped,
              "the manifest holds %s between its markers, and the stripped manifest does not" % word)
    check("android.permission.INTERNET" in manifest_stripped and "<action android:name=\"android.intent.action.MAIN\"" in manifest_stripped,
          "the stripped manifest keeps the rest (the network, the launcher)")
    apk = {"lib/arm64-v8a/libmain.so": b"x", "lib/arm64-v8a/libsmg_game_plain.so": b"x", "lib/arm64-v8a/libnebula_import.so": b"x",
           "AndroidManifest.xml": android_strings("android.permission.INTERNET", "android.hardware.camera.any", "android.intent.action.MAIN")}
    zip_of(tmp / "clean.apk", apk)
    code, out = run(tmp / "clean.apk", "--no-mods")
    check(code == 0, "an apk with none of the kit passes --no-mods (a camera feature is no camera permission): %s" % out)
    for what, extra in (("the rows module", {"lib/arm64-v8a/libsmg_game.so": b"x"}),
                        ("the mod importer", {"lib/arm64-v8a/libnebula_mods.so": b"x"}),
                        ("LumaKit", {"assets/mods/lumakit.luma": b"x"}),
                        ("a .luma filter", {"AndroidManifest.xml": android_strings("android.permission.INTERNET", ".*\\.luma")}),
                        ("the nebula: link", {"AndroidManifest.xml": android_strings("android.permission.INTERNET", "nebula")}),
                        ("the microphone", {"AndroidManifest.xml": android_strings("android.permission.RECORD_AUDIO")}),
                        ("the camera", {"AndroidManifest.xml": android_strings("android.permission.CAMERA")}),
                        ("the multicast permission", {"AndroidManifest.xml": android_strings("android.permission.CHANGE_WIFI_MULTICAST_STATE")})):
        zip_of(tmp / "planted.apk", {**apk, **extra})
        code, out = run(tmp / "planted.apk", "--no-mods")
        check(code == 1, "an apk with %s fails --no-mods: %s" % (what, out))
    zip_of(tmp / "full.apk", {**apk, "lib/arm64-v8a/libsmg_game.so": b"x", "assets/mods/lumakit.luma": b"x"})
    check(run(tmp / "full.apk", "--mods")[0] == 0, "an apk with the kit passes --mods")

    # ---- Windows: Setup holds its files as a zip in a resource, the entry names stored as they are.
    def setup(*names):
        return b"MZ" + b"\0" * 64 + b"".join(b"PK\x03\x04\0\0\0" + n.encode() + b"\0\0\0" for n in names)

    base = ("app/Nebula.exe", "app/smg_game_plain.dll", "app/x86-64-v2/smg_game_plain.dll", "app/nebula_import.dll", "start/Nebula.exe")
    (tmp / "clean-Setup.exe").write_bytes(setup(*base))
    code, out = run(tmp / "clean-Setup.exe", "--no-mods")
    check(code == 0, "a Setup with none of the kit passes --no-mods: %s" % out)
    for what, extra in (("the rows module", ("app/smg_game.dll",)), ("the baseline rows module", ("app/x86-64-v2/smg_game.dll",)),
                        ("the self-description", ("app/luma/luma_types.json",)), ("LumaKit", ("app/luma/mods/lumakit.luma",))):
        (tmp / "planted-Setup.exe").write_bytes(setup(*base, *extra))
        check(run(tmp / "planted-Setup.exe", "--no-mods")[0] == 1, "a Setup with %s fails --no-mods" % what)
    (tmp / "full-Setup.exe").write_bytes(setup(*base, "app/smg_game.dll", "app/luma/mods/lumakit.luma"))
    check(run(tmp / "full-Setup.exe", "--mods")[0] == 0, "a Setup with the kit passes --mods")
    check(run(tmp / "clean-Setup.exe", "--mods")[0] == 1, "a Setup without it fails --mods")

    # ---- Not a package.
    (tmp / "notes.txt").write_text("x", encoding="utf-8")
    check(run(tmp / "notes.txt", "--no-mods")[0] == 2, "a file that is no package is not judged (exit 2)")

if failures:
    print("mod_support_package_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("mod_support_package_test.py: pass")
