"""Lists the sites clang-query matched (output of `set output diag`), one per line with its bind name and source.

    python query_sites.py out1 [out2 ...]
"""
import collections
import pathlib
import re
import sys

NOTE = re.compile(r'^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): note: "(?P<bind>\w+)" binds here')


def main():
    sites = collections.Counter()
    for name in sys.argv[1:]:
        for raw in pathlib.Path(name).read_text(encoding="utf-8", errors="replace").splitlines():
            m = NOTE.match(raw.strip())
            if m:
                sites[(pathlib.Path(m["file"]).resolve(), int(m["line"]), m["bind"])] += 1
    cache = {}
    for (path, line, bind), count in sorted(sites.items()):
        if path not in cache:
            cache[path] = path.read_text(encoding="utf-8", errors="replace").splitlines()
        text = cache[path][line - 1].strip()
        print(f"{bind:16} {path.as_posix()}:{line} x{count}: {text[:150]}")
    print(f"sites {len(sites)}")


if __name__ == "__main__":
    main()
