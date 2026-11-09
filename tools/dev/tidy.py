"""Runs clang-tidy over a compile_commands.json and prints the number of findings for each check.

tidy.py COMPILE_COMMANDS [--filter TEXT] [--clang-tidy PATH] [--jobs N] [--log FILE]

COMPILE_COMMANDS: a compile_commands.json (or the folder that holds it). Only the files whose path holds --filter
(default native/petari) are checked, each with its nearest .clang-tidy (the repo root's; runtime/ and native/host add the
host's checks). --log writes the full output; the counts
go to stdout, most frequent first.
"""
import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys

CHECK_NAME = re.compile(r"\[([A-Za-z0-9_.,-]+)\]$")
FINDING = re.compile(r"^(.+?):(\d+):(\d+): (warning|error): ")


def source_path(entry):
    path = entry["file"]
    if not os.path.isabs(path):
        path = os.path.join(entry["directory"], path)
    return os.path.normpath(path)


def run_one(tidy, build_dir, path):
    result = subprocess.run([tidy, "-p", build_dir, "--quiet", path], capture_output=True, text=True, errors="replace")
    return path, result.stdout + result.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("compile_commands")
    parser.add_argument("--filter", default="native/petari")
    parser.add_argument("--clang-tidy", dest="tidy", default=None)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--log", default=None)
    args = parser.parse_args()

    database = args.compile_commands
    if os.path.isdir(database):
        database = os.path.join(database, "compile_commands.json")
    if not os.path.isfile(database):
        sys.exit(f"tidy.py: {database} not found")
    tidy = args.tidy or shutil.which("clang-tidy")
    if tidy is None:
        sys.exit("tidy.py: clang-tidy not found; pass --clang-tidy")
    build_dir = os.path.dirname(os.path.abspath(database))

    with open(database, encoding="utf-8") as f:
        entries = json.load(f)
    wanted = args.filter.replace("\\", "/")
    files = sorted({source_path(e) for e in entries if wanted in source_path(e).replace("\\", "/")})
    if not files:
        sys.exit(f"tidy.py: no file in {database} matches {args.filter!r}")

    counts = {}
    seen = set()
    log = open(args.log, "w", encoding="utf-8") if args.log else None
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for path, output in pool.map(lambda p: run_one(tidy, build_dir, p), files):
            if log:
                log.write(f"== {path}\n{output}\n")
            for line in output.splitlines():
                finding = FINDING.match(line)
                name = CHECK_NAME.search(line)
                if finding is None or name is None:
                    continue
                # A header finding repeats in every file that includes it: counted once by place and check.
                key = (finding.group(1), finding.group(2), finding.group(3), name.group(1))
                if key in seen:
                    continue
                seen.add(key)
                for check in name.group(1).split(","):
                    counts[check] = counts.get(check, 0) + 1
    if log:
        log.close()

    print(f"{len(files)} files checked")
    for check, count in sorted(counts.items(), key=lambda item: (-item[1], item[0])):
        print(f"{count:6d}  {check}")
    print(f"{sum(counts.values()):6d}  total")


if __name__ == "__main__":
    main()
