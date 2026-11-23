"""check_fsr3_package.py: whether the programs of a package hold AMD's FSR 3.

    python -X utf8 -I tools/check_fsr3_package.py <file> [<file> ...]

Each <file> is one of
  the Linux package (.tar.gz): its program, <folder>/files/nebula;
  the Android app (.apk): the program of every processor it holds, lib/<abi>/libmain.so;
  any other file: a program as it is (Nebula.exe and NebulaVk.exe of the Windows Setup's folder).
A program holds FSR 3 when it holds the text its FSR 3 code prints on a failure ("[fsr3] shared resource context failed",
temporal_upscaler.cpp and vk/temporal_upscaler_vk.cpp): the text is in the code only where FSR 3 is built in. The Windows Setup
packs its programs as a compressed archive, so release.yml runs this on the programs it packs, not on the Setup.

Prints one line per program; exit 0 when each holds FSR 3, 1 when one does not, 2 when a file cannot be read.
"""
import pathlib
import sys
import tarfile
import zipfile

MARK = b"[fsr3] shared resource context failed"


def programs(path):
    """[(name, bytes)] of the programs in a package, or the file itself."""
    name = path.name.lower()
    if name.endswith((".tar.gz", ".tgz")):
        with tarfile.open(path) as archive:
            found = [m for m in archive.getmembers() if m.isfile() and m.name.endswith("/files/nebula")]
            if not found:
                raise ValueError("the package has no files/nebula")
            return [(m.name, archive.extractfile(m).read()) for m in found]
    if name.endswith(".apk"):
        with zipfile.ZipFile(path) as archive:
            found = [n for n in archive.namelist() if n.startswith("lib/") and n.endswith("/libmain.so")]
            if not found:
                raise ValueError("the app has no lib/<abi>/libmain.so")
            return [(n, archive.read(n)) for n in found]
    return [(path.name, path.read_bytes())]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    status = 0
    for argument in argv[1:]:
        path = pathlib.Path(argument)
        try:
            found = programs(path)
        except (OSError, ValueError, zipfile.BadZipFile, tarfile.TarError) as error:
            print("check_fsr3_package: %s: %s" % (path.name, error))
            return 2
        for name, data in found:
            held = MARK in data
            print("%s: %s: %s" % (path.name, name, "holds FSR 3" if held else "has no FSR 3: the build left it out"))
            if not held:
                status = 1
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
