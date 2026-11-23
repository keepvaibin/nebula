"""check_mod_support_package.py: whether a built package has the mod kit or not.

    python -X utf8 -I tools/check_mod_support_package.py <package> --no-mods     (a pre-release: none of it is there)
    python -X utf8 -I tools/check_mod_support_package.py <package> --mods        (a release: the kit is there)

<package> is what release.yml makes: Nebula-Setup.exe or Nebula-Beta-Setup.exe (Windows), the Linux .tar.gz, the Mac .zip, the
Android .apk (signed or not).

With --no-mods the package must hold none of these: the game module that carries the mod rows (smg_game.dll, libsmg_game.so,
libsmg_game.dylib), the game's self-description and SDK (a luma folder), LumaKit and any .luma file, the mod importer's library
(libnebula_mods.so), the Linux package's .luma type file, the menu entry's file type and link, the Mac Info.plist's .luma type,
mod archive type, nebula: link and usage texts (and the texts in the language folders), the Android manifest's .luma, archive and
nebula: filters and its microphone, camera and multicast permissions.
With --mods it must hold the game module with the rows and LumaKit.

Prints what it found, one line each; exit 0 when the package is what was asked, 1 when not, 2 when it cannot be read.
"""
import pathlib
import re
import sys
import tarfile
import zipfile

# File names the mod kit is made of.
MODS_FILES = {"smg_game.dll", "libsmg_game.so", "libsmg_game.dylib", "libnebula_mods.so", "lumakit.luma", "luma_services.json",
              "dev.nebula.galaxy.xml"}
KIT_FILE = "lumakit.luma"
ROWS_MODULES = {"smg_game.dll", "libsmg_game.so", "libsmg_game.dylib"}
# Android's binary XML keeps each string with its length in front and an end mark, as UTF-8 or UTF-16: a permission is
# found as a part of a string, the nebula: link's scheme as a whole string.
MANIFEST_PARTS = ("android.permission.RECORD_AUDIO", "android.permission.CAMERA", "android.permission.CHANGE_WIFI_MULTICAST_STATE")
MANIFEST_SCHEMES = ("nebula",)
PLIST_WORDS = ("cfbundledocumenttypes", "utexportedtypedeclarations", "cfbundleurltypes", "microphoneusage", "camerausage",
               "localnetworkusage", ".luma", "dev.nebula.luma")


def entries(path):
    """(the entry names of a package, the small files whose text is read: name -> bytes)."""
    name = path.name.lower()
    if name.endswith((".tar.gz", ".tgz")):
        with tarfile.open(path) as archive:
            members = {m.name: m for m in archive.getmembers() if m.isfile()}
            return sorted(members), {n: archive.extractfile(m).read() for n, m in members.items() if n.endswith(".desktop")}
    if name.endswith((".zip", ".apk")):
        with zipfile.ZipFile(path) as archive:
            names = [n for n in archive.namelist() if not n.endswith("/")]
            read = [n for n in names if n.endswith((".plist", ".strings", ".desktop")) or n == "AndroidManifest.xml"]
            return sorted(names), {n: archive.read(n) for n in read}
    if name.endswith(".exe"):
        # Setup carries its files as one zip in a resource; the zip's entry names are stored as they are.
        data = path.read_bytes()
        return sorted({m.decode("ascii") for m in re.findall(rb"app/[A-Za-z0-9_./\-]+", data)}), {}
    raise ValueError("a package is a Setup .exe, a .tar.gz, a .zip or an .apk: " + path.name)


def android_forms(text):
    """The text as Android's binary XML keeps a whole string (its length in front, an end mark), in its two forms."""
    return [bytes([len(text), len(text)]) + text.encode() + bytes(1),
            len(text).to_bytes(2, "little") + text.encode("utf-16-le") + bytes(2)]


def manifest_has(data):
    """What of the mods the binary manifest names."""
    found = []
    lowered = data.lower()
    if b"luma" in lowered or "luma".encode("utf-16-le") in lowered:
        found.append("a .luma filter")
    found += [part for part in MANIFEST_PARTS if part.encode() in data or part.encode("utf-16-le") in data]
    found += ["the %s: link" % scheme for scheme in MANIFEST_SCHEMES if any(form in data for form in android_forms(scheme))]
    return found


def found_in(path):
    """What of the mod kit the package holds: one line each."""
    names, files = entries(path)
    found = []
    for name in names:
        parts = [p for p in re.split(r"[/\\]", name) if p]
        if not parts:
            continue
        if parts[-1] in MODS_FILES or parts[-1].endswith(".luma"):
            found.append("file " + name)
        elif "luma" in parts[:-1] or parts[:2] == ["assets", "mods"]:
            found.append("file " + name)
    for name, data in files.items():
        base = name.rsplit("/", 1)[-1]
        lowered = data.lower()
        if name == "AndroidManifest.xml":
            found += ["the manifest has " + what for what in manifest_has(data)]
        elif base.endswith(".plist"):
            for word in PLIST_WORDS:
                if word.encode() in lowered:
                    found.append("%s has %s" % (name, word))
        elif base.endswith(".strings"):
            # InfoPlist.strings of a language: UTF-16, and its four texts are about mods.
            text = data.decode("utf-16", errors="replace").strip()
            if text:
                found.append("%s has texts: %s" % (name, text[:60].replace("\n", " ")))
        elif base.endswith(".desktop"):
            if b"mimetype=" in lowered or b"--import" in lowered:
                found.append("%s has a file type or link" % name)
    return found


def holds_kit(path):
    """(LumaKit is in it, the game module with the mod rows is in it)."""
    names, _ = entries(path)
    base = {re.split(r"[/\\]", n)[-1] for n in names}
    return KIT_FILE in base, bool(base & ROWS_MODULES)


def main(argv):
    if len(argv) != 3 or argv[2] not in ("--mods", "--no-mods"):
        print(__doc__)
        return 2
    path = pathlib.Path(argv[1])
    try:
        found = found_in(path)
        kit, rows = holds_kit(path)
    except (OSError, ValueError, zipfile.BadZipFile, tarfile.TarError) as error:
        print("check_mod_support_package: %s: %s" % (path.name, error))
        return 2
    if argv[2] == "--no-mods":
        for line in found:
            print("%s: %s" % (path.name, line))
        print("%s: %s" % (path.name, "no mod kit in it" if not found else "%d mod parts in it" % len(found)))
        return 0 if not found else 1
    missing = [what for what, present in (("LumaKit", kit), ("the game module with the mod rows", rows)) if not present]
    print("%s: %s" % (path.name, "the mod kit is in it" if not missing else "missing: " + ", ".join(missing)))
    return 0 if not missing else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
