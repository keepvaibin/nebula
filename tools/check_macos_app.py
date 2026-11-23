"""check_macos_app.py: whether Nebula.app (or the Mac zip that holds it) can run on a Mac that has nothing of ours but the app.

    python -X utf8 -I tools/check_macos_app.py <Nebula.app | Nebula-macOS.zip> [--mods | --no-mods] [--minos 12.0]

Reads the files only (no Mac is needed; build_macos.sh and release.yml run it on the packed app). For every Mach-O file in
Contents/MacOS and Contents/Frameworks, and for each processor in it (the app is for Intel and Apple processors, so both
x86_64 and arm64 must be there):
  - every library it loads (LC_LOAD_DYLIB and its weak, lazy, upward and re-export forms) is one of the system (/usr/lib,
    /System/Library) or a file inside the app that its @rpath, @loader_path or @executable_path names. A path into a build
    machine's folders (an Xcode or Homebrew library) is refused: it is there on GitHub's Mac and not on a player's.
  - every LC_RPATH stays inside the app (@executable_path or @loader_path), never an absolute path.
  - the minimum macOS it asks for is not above --minos (12.0, the app's LSMinimumSystemVersion).
  - it has a code signature. A library has no entitlements (macOS 13 refuses a library that carries them). The program's
    signature has the hardened runtime's flag with no team: then its entitlements must hold
    com.apple.security.cs.disable-library-validation, or the system will not load a library signed by no team (the game's
    module is ad hoc signed).
The app holds the plain game module (libsmg_game_plain.dylib) and, with --mods, the rows module (libsmg_game.dylib); with
--no-mods the rows module is not there. The zip holds no "__MACOSX" folder or "._" file (an unpacking tool writes those into the
app and breaks its signature) and has "How to open Nebula.txt".

Prints what it found, one line each; exit 0 when the app is as asked, 1 when not, 2 when it cannot be read.
"""
import os
import pathlib
import plistlib
import struct
import sys
import zipfile

CPUS = {0x01000007: "x86_64", 0x0100000C: "arm64"}
REQUIRED_CPUS = ("x86_64", "arm64")
LOAD_COMMANDS = {0x0C: "LC_LOAD_DYLIB", 0x80000018: "LC_LOAD_WEAK_DYLIB", 0x80000023: "LC_LOAD_UPWARD_DYLIB",
                 0x8000001F: "LC_REEXPORT_DYLIB", 0x20: "LC_LAZY_LOAD_DYLIB"}
LC_ID_DYLIB = 0x0D
LC_RPATH = 0x8000001C
LC_CODE_SIGNATURE = 0x1D
LC_BUILD_VERSION = 0x32
LC_VERSION_MIN_MACOSX = 0x24
PLATFORM_MACOS = 1
SYSTEM_FOLDERS = ("/usr/lib/", "/System/Library/")
FLAG_ADHOC = 0x2
FLAG_RUNTIME = 0x10000
LIBRARY_VALIDATION_KEY = "com.apple.security.cs.disable-library-validation"


class Unreadable(Exception):
    pass


def version_text(packed):
    return "%d.%d.%d" % (packed >> 16, (packed >> 8) & 0xFF, packed & 0xFF)


def version_tuple(text):
    parts = [int(p) for p in text.split(".")]
    return tuple((parts + [0, 0, 0])[:3])


def is_macho(data):
    return len(data) >= 4 and data[:4] in (b"\xca\xfe\xba\xbe", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbf")


def slices(data):
    """[(cpu name or hex, the slice's bytes)] of a fat or a thin Mach-O file."""
    if data[:4] == b"\xca\xfe\xba\xbe":
        count = struct.unpack(">I", data[4:8])[0]
        out = []
        for i in range(count):
            cpu, _sub, offset, size, _align = struct.unpack(">iiIII", data[8 + 20 * i:28 + 20 * i])
            out.append((CPUS.get(cpu & 0xFFFFFFFF, hex(cpu & 0xFFFFFFFF)), data[offset:offset + size]))
        return out
    if data[:4] == b"\xcf\xfa\xed\xfe":
        cpu = struct.unpack("<I", data[4:8])[0]
        return [(CPUS.get(cpu, hex(cpu)), data)]
    raise Unreadable("a fat file with 64-bit offsets")


def commands(piece):
    """(filetype, [(cmd, the command's bytes)]) of one slice."""
    if len(piece) < 32 or piece[:4] != b"\xcf\xfa\xed\xfe":
        raise Unreadable("a slice that is not a 64-bit Mach-O")
    _magic, _cpu, _sub, filetype, count, _size, _flags, _reserved = struct.unpack("<IiiIIIII", piece[:32])
    out = []
    offset = 32
    for _ in range(count):
        cmd, size = struct.unpack("<II", piece[offset:offset + 8])
        if size < 8 or offset + size > len(piece):
            raise Unreadable("a load command that runs past the file")
        out.append((cmd, piece[offset:offset + size]))
        offset += size
    return filetype, out


def c_string(body, offset):
    end = body.index(b"\0", offset) if b"\0" in body[offset:] else len(body)
    return body[offset:end].decode("utf-8", "replace")


def signature_of(piece, cmds):
    """{'flags', 'team', 'entitlements' (a dict or None)} of the slice's code signature, or None."""
    for cmd, body in cmds:
        if cmd != LC_CODE_SIGNATURE:
            continue
        offset, size = struct.unpack("<II", body[8:16])
        blob = piece[offset:offset + size]
        magic, _length, count = struct.unpack(">III", blob[:12])
        if magic != 0xFADE0CC0:
            raise Unreadable("a code signature that is not a SuperBlob")
        found = {"flags": None, "team": "", "entitlements": None}
        for i in range(count):
            _kind, at = struct.unpack(">II", blob[12 + 8 * i:20 + 8 * i])
            part_magic, part_length = struct.unpack(">II", blob[at:at + 8])
            if part_magic == 0xFADE0C02:
                fields = struct.unpack(">IIIIIIIIIBBBBIII", blob[at:at + 52])
                found["flags"] = fields[3]
                if fields[2] >= 0x20200 and fields[15] != 0:
                    found["team"] = c_string(blob, at + fields[15])
            elif part_magic == 0xFADE7171:
                text = blob[at + 8:at + part_length]
                try:
                    found["entitlements"] = plistlib.loads(text)
                except Exception:
                    raise Unreadable("entitlements that are not a property list")
        return found
    return None


def app_files(path):
    """{name below Nebula.app: loader of its bytes}, the zip's own entry names, the app's folder name in the zip or on disk."""
    if path.is_dir():
        root = path
        files = {}
        for folder, _dirs, names in os.walk(root):
            for name in names:
                full = pathlib.Path(folder) / name
                files[full.relative_to(root).as_posix()] = (lambda f=full: f.read_bytes())
        return files, [], None
    if path.suffix.lower() == ".zip":
        archive = zipfile.ZipFile(path)
        names = [n for n in archive.namelist()]
        apps = sorted({n.split("Nebula.app/")[0] + "Nebula.app/" for n in names if "Nebula.app/" in n})
        if len(apps) != 1:
            raise Unreadable("the zip holds %d Nebula.app (it should hold one)" % len(apps))
        prefix = apps[0]
        files = {n[len(prefix):]: (lambda n=n: archive.read(n)) for n in names if n.startswith(prefix) and not n.endswith("/")}
        return files, names, prefix[:-len("Nebula.app/")]
    raise Unreadable("a Nebula.app folder or a .zip: " + path.name)


def resolve(target, image, rpaths, files):
    """The name below Nebula.app that an @-path of `image` (a name below Nebula.app) points to, or None."""
    here = pathlib.PurePosixPath(image).parent
    bases = {"@executable_path": pathlib.PurePosixPath("Contents/MacOS"), "@loader_path": here}

    def normal(p):
        parts = []
        for part in pathlib.PurePosixPath(p).parts:
            if part == "..":
                if not parts:
                    return None
                parts.pop()
            elif part != ".":
                parts.append(part)
        return "/".join(parts)

    candidates = []
    if target.startswith("@rpath/"):
        for rpath in rpaths:
            candidates.append(rpath + "/" + target[len("@rpath/"):])
    else:
        candidates.append(target)
    for candidate in candidates:
        for prefix, base in bases.items():
            if candidate.startswith(prefix + "/") or candidate == prefix:
                name = normal(str(base) + candidate[len(prefix):])
                if name is not None and name in files:
                    return name
    return None


def expand_rpath(rpath, image):
    """An LC_RPATH made absolute for resolve(): left as it is (an @ path) or None (outside the app)."""
    if rpath.startswith(("@executable_path", "@loader_path")):
        return rpath
    return None


def check(path, mods, minos):
    """(lines, number of faults)."""
    lines = []
    faults = [0]

    def fault(text):
        faults[0] += 1
        lines.append("FAULT: " + text)

    files, zip_names, folder = app_files(path)
    for name in zip_names:
        if name.startswith("__MACOSX/") or "/._" in name or name.startswith("._"):
            fault("the zip holds %s: an unpacking tool's own file, which breaks the app's signature" % name)
    if zip_names and not any(n.endswith("How to open Nebula.txt") for n in zip_names):
        fault("the zip has no \"How to open Nebula.txt\"")
    for needed in ("Contents/Info.plist", "Contents/MacOS/Nebula", "Contents/Frameworks/libsmg_game_plain.dylib"):
        if needed not in files:
            fault("the app has no " + needed)
    rows_there = "Contents/Frameworks/libsmg_game.dylib" in files
    if mods is True and not rows_there:
        fault("the app has no Contents/Frameworks/libsmg_game.dylib (the game module with the mod rows)")
    if mods is False and rows_there:
        fault("the app has Contents/Frameworks/libsmg_game.dylib (the game module with the mod rows): no mod support was asked")
    image_names = sorted(n for n in files if n.startswith(("Contents/MacOS/", "Contents/Frameworks/")))
    checked = 0
    any_unsigned_team = False
    program_entitled = {}
    for name in image_names:
        data = files[name]()
        if not is_macho(data):
            continue
        checked += 1
        found = {c: s for c, s in slices(data)}
        for cpu in REQUIRED_CPUS:
            if cpu not in found:
                fault("%s has no %s half" % (name, cpu))
        for cpu, piece in sorted(found.items()):
            try:
                filetype, cmds = commands(piece)
            except Unreadable as why:
                fault("%s (%s): %s" % (name, cpu, why))
                continue
            rpaths = []
            for cmd, body in cmds:
                if cmd == LC_RPATH:
                    rpath = c_string(body, struct.unpack("<I", body[8:12])[0])
                    rpaths.append(rpath)
                    if expand_rpath(rpath, name) is None:
                        fault("%s (%s) has the rpath %s, outside the app" % (name, cpu, rpath))
            libraries = []
            for cmd, body in cmds:
                if cmd in LOAD_COMMANDS:
                    target = c_string(body, struct.unpack("<I", body[8:12])[0])
                    libraries.append(target)
                    if target.startswith(SYSTEM_FOLDERS):
                        continue
                    if target.startswith("@") and resolve(target, name, rpaths, files) is not None:
                        continue
                    fault("%s (%s) loads %s: not a system library and not a file of the app" % (name, cpu, target))
            minimum = None
            for cmd, body in cmds:
                if cmd == LC_BUILD_VERSION:
                    platform, packed = struct.unpack("<II", body[8:16])
                    if platform == PLATFORM_MACOS:
                        minimum = packed
                elif cmd == LC_VERSION_MIN_MACOSX:
                    minimum = struct.unpack("<I", body[8:12])[0]
            if minimum is None:
                fault("%s (%s) names no macOS minimum version" % (name, cpu))
            elif (minimum >> 16, (minimum >> 8) & 0xFF, minimum & 0xFF) > version_tuple(minos):
                fault("%s (%s) needs macOS %s, above %s" % (name, cpu, version_text(minimum), minos))
            try:
                signature = signature_of(piece, cmds)
            except Unreadable as why:
                fault("%s (%s): %s" % (name, cpu, why))
                continue
            if signature is None:
                fault("%s (%s) has no code signature" % (name, cpu))
                continue
            if name == "Contents/MacOS/Nebula":
                program_entitled[cpu] = signature
            elif signature["entitlements"] is not None:
                fault("%s (%s) is a library with entitlements: macOS 13 refuses it" % (name, cpu))
            if signature["team"] == "":
                any_unsigned_team = True
            lines.append("%s %s: %d libraries, minimum macOS %s, signature flags %#x%s" % (
                name, cpu, len(libraries), version_text(minimum) if minimum else "none", signature["flags"] or 0,
                " (ad hoc)" if (signature["flags"] or 0) & FLAG_ADHOC else ""))
    if checked == 0:
        fault("no Mach-O file in Contents/MacOS or Contents/Frameworks")
    for cpu in REQUIRED_CPUS:
        signature = program_entitled.get(cpu)
        if signature is None:
            continue
        hardened = bool((signature["flags"] or 0) & FLAG_RUNTIME)
        entitlements = signature["entitlements"] or {}
        if hardened and any_unsigned_team and entitlements.get(LIBRARY_VALIDATION_KEY) is not True:
            fault("the program (%s) has the hardened runtime and no team, and no %s: the system would refuse the game module"
                  % (cpu, LIBRARY_VALIDATION_KEY))
    return lines, faults[0]


def main(argv):
    arguments = [a for a in argv[1:] if not a.startswith("--")]
    mods = True if "--mods" in argv else (False if "--no-mods" in argv else None)
    minos = "12.0"
    if "--minos" in argv:
        minos = argv[argv.index("--minos") + 1]
        arguments = [a for a in arguments if a != minos]
    if len(arguments) != 1:
        print(__doc__)
        return 2
    try:
        lines, faults = check(pathlib.Path(arguments[0]), mods, minos)
    except (Unreadable, OSError, zipfile.BadZipFile, struct.error, ValueError) as why:
        print("check_macos_app.py: cannot read %s: %s" % (arguments[0], why))
        return 2
    for line in lines:
        print(line)
    print("check_macos_app.py: %s" % ("%d faults" % faults if faults else "the app holds"))
    return 1 if faults else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
