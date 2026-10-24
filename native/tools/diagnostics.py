"""Lists the unique sites of chosen diagnostics in an audit build log, with source text.

    python diagnostics.py build.log -W array-bounds [-W uninitialized ...] [--counts]
"""
import argparse
import collections
import pathlib
import re

DIAG = re.compile(r"^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?:warning|error): (?P<text>.*) \[-W(?P<flag>[a-z0-9+-]+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("-W", dest="flags", action="append", default=[])
    ap.add_argument("--counts", action="store_true")
    args = ap.parse_args()
    sites = collections.OrderedDict()
    for raw in pathlib.Path(args.log).read_text(encoding="utf-8", errors="replace").splitlines():
        m = DIAG.match(raw)
        if not m or (args.flags and m["flag"] not in args.flags):
            continue
        key = (pathlib.Path(m["file"]).resolve(), int(m["line"]), int(m["col"]), m["flag"])
        sites.setdefault(key, m["text"])
    if args.counts:
        per = collections.Counter(k[3] for k in sites)
        for flag, n in per.most_common():
            print(f"{n:7} {flag}")
        return
    cache = {}
    for (path, line, col, flag), text in sorted(sites.items()):
        if path not in cache:
            try:
                cache[path] = path.read_text(encoding="utf-8", errors="replace").splitlines()
            except OSError:
                cache[path] = []
        src = cache[path][line - 1].strip() if line <= len(cache[path]) else ""
        print(f"{flag} {path.as_posix()}:{line}:{col}: {text[:140]}\n    {src[:160]}")
    print(f"sites {len(sites)}")


if __name__ == "__main__":
    main()
