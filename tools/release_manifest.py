"""release_manifest.py: writes <package>.manifest beside a release package for the Linux and macOS updates
(runtime/src/update/update.cpp): the release's version, the package's name (the release asset an update downloads),
its size and its SHA-256, one `name=value` a line. An update installs the package only when the manifest's signature
(<package>.manifest.sig, made by installer/sign-release.ps1 with the release key) verifies, the version is the
release's and newer than the running one, and the download has that name, size and SHA-256.

    python tools/release_manifest.py <package> <version>"""
import hashlib
import os
import re
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.split("\n\n")[1].strip())
    package, version = sys.argv[1], sys.argv[2]
    if not re.fullmatch(r"\d+\.\d+\.\d+(-beta\.\d+)?", version):
        sys.exit(f"the version must be MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-beta.N: {version}")
    digest = hashlib.sha256()
    with open(package, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    lines = [f"version={version}", f"asset={os.path.basename(package)}", f"size={os.path.getsize(package)}",
             f"sha256={digest.hexdigest()}"]
    with open(package + ".manifest", "w", encoding="ascii", newline="\n") as file:
        file.write("\n".join(lines) + "\n")
    print(f"{package}.manifest: {', '.join(lines)}")


if __name__ == "__main__":
    main()
