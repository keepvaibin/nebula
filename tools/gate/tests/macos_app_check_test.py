"""macos_app_check_test.py: tools/check_macos_app.py tells an app a player's Mac can load from one it cannot.

python -X utf8 -I tools/gate/tests/macos_app_check_test.py

Apps are planted in a temporary folder with Mach-O files made here (fat files of an x86_64 and an arm64 half, with the load
commands, rpaths, minimum version and code signature the check reads). The clean app passes (as a folder and as a zip); then each
of these is planted alone and must be refused, naming the file and the half: a library of Xcode's folders in the x86_64 half
(the beta's fault class: it is there on GitHub's Mac and not on a player's), a Homebrew library, an @rpath library no file of
the app answers, an absolute rpath, a minimum macOS above 12.0, a missing half, a module with no signature, a program with the
hardened runtime, no team and no library-validation entitlement, a library with entitlements, a missing plain module, a rows
module in a no-mod app, no rows module in an app with mods, and an unpacking tool's "._" file in the zip. Exit 0 when every case holds.
"""
import io
import pathlib
import plistlib
import struct
import subprocess
import sys
import tempfile
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
TOOL = TREE / "tools" / "check_macos_app.py"
failures = []

X86, ARM = 0x01000007, 0x0100000C
SYSTEM = "/usr/lib/libSystem.B.dylib"


def check(ok, what):
    if not ok:
        failures.append(what)


def dylib_command(cmd, path):
    name = path.encode() + b"\0"
    size = (24 + len(name) + 7) & ~7
    return struct.pack("<IIIIII", cmd, size, 24, 0, 0, 0) + name.ljust(size - 24, b"\0")


def rpath_command(path):
    name = path.encode() + b"\0"
    size = (12 + len(name) + 7) & ~7
    return struct.pack("<III", 0x8000001C, size, 12) + name.ljust(size - 12, b"\0")


def build_version(minos):
    return struct.pack("<IIIIII", 0x32, 24, 1, (minos[0] << 16) | (minos[1] << 8), 0, 0)[:20] + b"\0" * 4


def signature_blob(flags, team="", entitlements=None):
    ident = b"dev.nebula.test\0"
    team_bytes = team.encode() + b"\0" if team else b""
    header = 52
    code_directory = struct.pack(">IIIIIIIIIBBBBIII", 0xFADE0C02, header + len(ident) + len(team_bytes), 0x20500, flags, header,
                                 header, 0, 0, 0, 32, 2, 0, 12, 0, 0, header + len(ident) if team else 0) + ident + team_bytes
    parts = [(0, code_directory)]
    if entitlements is not None:
        text = plistlib.dumps(entitlements)
        parts.append((5, struct.pack(">II", 0xFADE7171, 8 + len(text)) + text))
    table = b""
    body = b""
    base = 12 + 8 * len(parts)
    for kind, data in parts:
        table += struct.pack(">II", kind, base + len(body))
        body += data
    return struct.pack(">III", 0xFADE0CC0, base + len(body), len(parts)) + table + body


def thin(cpu, filetype, libraries=(SYSTEM,), rpaths=(), minos=(12, 0), signature=None):
    commands = b"".join(dylib_command(0x0C, path) for path in libraries) + b"".join(rpath_command(p) for p in rpaths)
    commands += build_version(minos)
    count = len(libraries) + len(rpaths) + 1
    if signature is not None:
        count += 1
        commands += struct.pack("<IIII", 0x1D, 16, 0, 0)  # filled in below
    head = struct.pack("<IiiIIIII", 0xFEEDFACF, cpu, 0, filetype, count, len(commands), 0, 0)
    body = bytearray(head + commands)
    if signature is not None:
        offset = (len(body) + 15) & ~15
        body[len(body) - 8:len(body)] = struct.pack("<II", offset, len(signature))
        body = body.ljust(offset, b"\0") + signature
    return bytes(body)


def fat(halves):
    out = struct.pack(">II", 0xCAFEBABE, len(halves))
    offset = 20 + 20 * len(halves)
    for cpu, data in halves:
        offset = (offset + 15) & ~15
        out += struct.pack(">iiIII", cpu, 0, offset, len(data), 4)
        offset += len(data)
    position = 20 + 20 * len(halves)
    blob = b""
    for cpu, data in halves:
        aligned = (position + 15) & ~15
        blob += b"\0" * (aligned - position) + data
        position = aligned + len(data)
    return out.ljust(20 + 20 * len(halves), b"\0") + blob


GOOD_ENTITLEMENTS = {"com.apple.security.cs.disable-library-validation": True}


def program(x86=None, arm=None):
    both = {"libraries": (SYSTEM, "/System/Library/Frameworks/Metal.framework/Versions/A/Metal"), "signature": signature_blob(0x10002, "", GOOD_ENTITLEMENTS)}
    x86 = dict(both, **(x86 or {}))
    arm = dict(both, **(arm or {}))
    return fat([(X86, thin(X86, 2, **x86)), (ARM, thin(ARM, 2, **arm))])


def module(x86=None, arm=None, halves=("x86_64", "arm64")):
    both = {"libraries": (SYSTEM,), "signature": signature_blob(0x10002)}
    x86 = dict(both, **(x86 or {}))
    arm = dict(both, **(arm or {}))
    chosen = []
    if "x86_64" in halves:
        chosen.append((X86, thin(X86, 6, **x86)))
    if "arm64" in halves:
        chosen.append((ARM, thin(ARM, 6, **arm)))
    return fat(chosen)


def plant(root, files):
    for name, data in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data if isinstance(data, bytes) else data.encode())


def clean(mods=False):
    files = {
        "Contents/Info.plist": "<plist/>",
        "Contents/MacOS/Nebula": program(),
        "Contents/Frameworks/libsmg_game_plain.dylib": module(),
        "Contents/Resources/programs.metallib": b"not a Mach-O",
    }
    if mods:
        files["Contents/Frameworks/libsmg_game.dylib"] = module()
    return files


def run(path, *options):
    done = subprocess.run([sys.executable, "-X", "utf8", "-I", str(TOOL), str(path), *options], capture_output=True, text=True)
    return done.returncode, done.stdout


def zipped(files, extra=(), text=True):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w") as archive:
        if text:
            archive.writestr("Nebula-macOS/How to open Nebula.txt", "open it")
        for name, data in files.items():
            archive.writestr("Nebula-macOS/Nebula.app/" + name, data)
        for name in extra:
            archive.writestr(name, b"x")
    return stream.getvalue()


def refused(files, words, what, *options, as_zip=False):
    with tempfile.TemporaryDirectory() as folder:
        folder = pathlib.Path(folder)
        if as_zip:
            target = folder / "Nebula-macOS.zip"
            target.write_bytes(zipped(files))
        else:
            target = folder / "Nebula.app"
            plant(target, files)
        code, out = run(target, *options)
    check(code == 1 and all(word in out for word in words), "%s (exit %d, %s)" % (what, code, out.strip().splitlines()[-2:]))


with tempfile.TemporaryDirectory() as folder:
    folder = pathlib.Path(folder)
    plant(folder / "Nebula.app", clean())
    code, out = run(folder / "Nebula.app", "--no-mods")
    check(code == 0 and "the app holds" in out, "a clean app passes: %s" % out)
    (folder / "Nebula-macOS.zip").write_bytes(zipped(clean()))
    code, out = run(folder / "Nebula-macOS.zip", "--no-mods")
    check(code == 0, "a clean zip passes: %s" % out)
    plant(folder / "with-mods.app", clean(True))
    code, out = run(folder / "with-mods.app", "--mods")
    check(code == 0, "an app with the rows module passes with --mods: %s" % out)
    # An @rpath library that the app has, found through a rpath inside the app.
    inside = clean()
    inside["Contents/MacOS/Nebula"] = program(x86={"libraries": (SYSTEM, "@rpath/libhelper.dylib"), "rpaths": ("@executable_path/../Frameworks",)},
                                              arm={"libraries": (SYSTEM, "@rpath/libhelper.dylib"), "rpaths": ("@executable_path/../Frameworks",)})
    inside["Contents/Frameworks/libhelper.dylib"] = module()
    plant(folder / "inside.app", inside)
    code, out = run(folder / "inside.app", "--no-mods")
    check(code == 0, "a library of the app, by @rpath, passes: %s" % out)

XCODE = "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/17/lib/darwin/libclang_rt.osx.dylib"
x86_xcode = clean()
x86_xcode["Contents/Frameworks/libsmg_game_plain.dylib"] = module(x86={"libraries": (SYSTEM, XCODE)})
refused(x86_xcode, ["libsmg_game_plain.dylib (x86_64)", "libclang_rt.osx.dylib", "FAULT"], "Xcode's library named by the x86_64 half of the module", "--no-mods")
program_xcode = clean()
program_xcode["Contents/MacOS/Nebula"] = program(x86={"libraries": (SYSTEM, XCODE)})
refused(program_xcode, ["Contents/MacOS/Nebula (x86_64)", "Xcode.app"], "Xcode's library named by the x86_64 half of the program", "--no-mods", as_zip=True)
brew = clean()
brew["Contents/Frameworks/libsmg_game_plain.dylib"] = module(arm={"libraries": (SYSTEM, "/opt/homebrew/lib/libz.1.dylib")})
refused(brew, ["(arm64)", "/opt/homebrew/lib/libz.1.dylib"], "a Homebrew library in the arm64 half", "--no-mods")
unanswered = clean()
unanswered["Contents/MacOS/Nebula"] = program(x86={"libraries": (SYSTEM, "@rpath/libgone.dylib"), "rpaths": ("@executable_path/../Frameworks",)})
refused(unanswered, ["@rpath/libgone.dylib"], "an @rpath library no file of the app answers", "--no-mods")
absolute = clean()
absolute["Contents/MacOS/Nebula"] = program(arm={"rpaths": ("/Users/builder/work/lib",)})
refused(absolute, ["rpath /Users/builder/work/lib"], "an absolute rpath", "--no-mods")
newer = clean()
newer["Contents/Frameworks/libsmg_game_plain.dylib"] = module(x86={"minos": (13, 0)})
refused(newer, ["needs macOS 13.0.0, above 12.0"], "a module for macOS 13 in the x86_64 half", "--no-mods")
lacking = clean()
lacking["Contents/Frameworks/libsmg_game_plain.dylib"] = module(halves=("arm64",))
refused(lacking, ["libsmg_game_plain.dylib has no x86_64 half"], "a module with no Intel half", "--no-mods")
unsigned = clean()
unsigned["Contents/Frameworks/libsmg_game_plain.dylib"] = module(arm={"signature": None})
refused(unsigned, ["libsmg_game_plain.dylib (arm64) has no code signature"], "a module with no signature", "--no-mods")
validated = clean()
validated["Contents/MacOS/Nebula"] = program(x86={"signature": signature_blob(0x10002, "", {})})
refused(validated, ["disable-library-validation"], "the hardened runtime, no team and no library-validation entitlement", "--no-mods")
entitled = clean()
entitled["Contents/Frameworks/libsmg_game_plain.dylib"] = module(x86={"signature": signature_blob(0x10002, "", GOOD_ENTITLEMENTS)})
refused(entitled, ["is a library with entitlements"], "a library with entitlements", "--no-mods")
no_plain = clean()
del no_plain["Contents/Frameworks/libsmg_game_plain.dylib"]
refused(no_plain, ["no Contents/Frameworks/libsmg_game_plain.dylib"], "no plain module", "--no-mods")
refused(clean(True), ["libsmg_game.dylib"], "a rows module in an app with no mod support", "--no-mods")
refused(clean(False), ["no Contents/Frameworks/libsmg_game.dylib"], "no rows module in an app with mods", "--mods")
with tempfile.TemporaryDirectory() as folder:
    target = pathlib.Path(folder) / "Nebula-macOS.zip"
    target.write_bytes(zipped(clean(), extra=("Nebula-macOS/Nebula.app/Contents/MacOS/._Nebula",)))
    code, out = run(target, "--no-mods")
    check(code == 1 and "._Nebula" in out, "an unpacking tool's ._ file in the zip: %s" % out)
    target.write_bytes(zipped(clean(), text=False))
    code, out = run(target, "--no-mods")
    check(code == 1 and "How to open Nebula.txt" in out, "a zip without How to open Nebula.txt: %s" % out)
    target.write_bytes(b"not a zip")
    code, _out = run(target, "--no-mods")
    check(code == 2, "a file that is no zip cannot be read (exit 2): %d" % code)

if failures:
    print("macos_app_check_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("macos_app_check_test.py: pass")
