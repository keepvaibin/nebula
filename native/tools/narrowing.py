"""Lists every implicit 64-to-32-bit conversion in a build log (an audit build with
-DNEBULA_AUDIT_FLAGS=-Wshorten-64-to-32), one line per site with its source text, grouped by conversion kind.

    python narrowing.py build.log [--kind REGEX] [--file REGEX]
"""
import argparse
import collections
import pathlib
import re

WARN = re.compile(r"^(?P<file>[^\s:][^:]*(?::[\\/][^:]*)?):(?P<line>\d+):(?P<col>\d+): warning: implicit conversion "
                  r"loses integer precision: '(?P<src>[^']+)'(?: \(aka '[^']+'\))? to '(?P<dst>[^']+)'")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--kind", default="")
    ap.add_argument("--file", default="")
    args = ap.parse_args()
    sites = {}
    for raw in pathlib.Path(args.log).read_text(encoding="utf-8", errors="replace").splitlines():
        m = WARN.match(raw)
        if not m:
            continue
        key = (pathlib.Path(m["file"]).resolve(), int(m["line"]), int(m["col"]))
        sites[key] = f"{m['src']} -> {m['dst']}"
    groups = collections.defaultdict(list)
    for (path, line, col), kind in sites.items():
        if args.kind and not re.search(args.kind, kind):
            continue
        if args.file and not re.search(args.file, str(path).replace("\\", "/")):
            continue
        groups[kind].append((path, line, col))
    cache = {}
    total = 0
    for kind in sorted(groups, key=lambda k: -len(groups[k])):
        print(f"== {kind}: {len(groups[kind])}")
        for path, line, col in sorted(groups[kind]):
            if path not in cache:
                cache[path] = path.read_text(encoding="utf-8", errors="replace").splitlines()
            text = cache[path][line - 1].strip() if line <= len(cache[path]) else ""
            print(f"{path.as_posix()}:{line}:{col}: {text[:160]}")
            total += 1
    print(f"total {total}")


if __name__ == "__main__":
    main()
