"""Lists the Petari files a game module build uses: every compiled source and every header its units include, from
ninja's dependency log. native/petari holds exactly this set.

    python petari_files.py <build dir> <petari dir> [ninja]
"""
import os
import pathlib
import re
import subprocess
import sys


def main():
    build, petari = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]).resolve()
    ninja = sys.argv[3] if len(sys.argv) > 3 else "ninja"
    deps = subprocess.run([ninja, "-C", str(build), "-t", "deps"], capture_output=True, text=True, check=True).stdout
    root = str(petari).replace("\\", "/").lower() + "/"
    files = set()
    for line in deps.splitlines():
        line = line.strip().replace("\\", "/")
        if not line or line.startswith("CMakeFiles/") or " #deps " in line:
            continue
        path = os.path.normpath(line).replace("\\", "/")
        if path.lower().startswith(root):
            files.add(path[len(root):])
    for f in sorted(files):
        print(f)


if __name__ == "__main__":
    main()
