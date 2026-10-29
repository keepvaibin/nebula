"""divide_audit.py <build dir> [--jobs N] [--list]: every integer division and remainder in the game module whose divisor
is not a nonzero constant (the build's compile_commands.json; configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON).

PowerPC's divw and divwu never trap; x86's div and idiv do, for a zero divisor and for INT_MIN / -1. Each listed site
either has a divisor that cannot be zero (or -1 with an INT_MIN dividend) on any path, or states its result for those
cases in source. Prints each site once (a header's site once for all its units) with --list, and the totals."""
import argparse
import collections
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

CONSTANT = (r"anyOf(integerLiteral(unless(equals(0))), characterLiteral(), unaryExprOrTypeTraitExpr(), "
            r"declRefExpr(to(enumConstantDecl())), declRefExpr(to(varDecl(hasType(isConstQualified()), "
            r"hasInitializer(ignoringParenImpCasts(integerLiteral(unless(equals(0)))))))))")
QUERY = f"""set output diag
set bind-root false
match binaryOperator(hasAnyOperatorName("/", "%"), hasType(isInteger()), \
unless(hasRHS(ignoringParenImpCasts({CONSTANT})))).bind("div")
match compoundAssignOperator(hasAnyOperatorName("/=", "%="), hasType(isInteger()), \
unless(hasRHS(ignoringParenImpCasts({CONSTANT})))).bind("div")
"""
NOTE = re.compile(r'^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): note: "div" binds here')


def clang_query():
    found = shutil.which("clang-query")
    if found:
        return found
    for candidate in (r"C:\Program Files\LLVM\bin\clang-query.exe", "/usr/bin/clang-query"):
        if os.path.exists(candidate):
            return candidate
    sys.exit("divide_audit: clang-query not found")


def run(tool, build, query, files):
    result = subprocess.run([tool, "-p", build, "-f", query, *files], capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    return result.stdout + result.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    database = pathlib.Path(args.build) / "compile_commands.json"
    if not database.exists():
        sys.exit(f"divide_audit: {database} missing (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)")
    files = [e["file"] for e in json.loads(database.read_text(encoding="utf-8"))
             if e["file"].endswith((".c", ".cpp", ".cc"))]
    tool = clang_query()
    with tempfile.TemporaryDirectory() as tmp:
        query = os.path.join(tmp, "divide.query")
        pathlib.Path(query).write_text(QUERY, encoding="utf-8")
        batches = [files[i:i + 16] for i in range(0, len(files), 16)]
        sites = set()
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            for output in pool.map(lambda b: run(tool, args.build, query, b), batches):
                for raw in output.splitlines():
                    m = NOTE.match(raw.strip())
                    if m:
                        sites.add((pathlib.Path(m["file"]).resolve().as_posix(), int(m["line"]), int(m["col"])))
    by_area = collections.Counter()
    cache = {}
    for path, line, col in sorted(sites):
        area = re.search(r"/native/(petari/src/\w+|petari/libs/\w+|petari/include|sdk|msl)", path)
        by_area[area.group(1) if area else "other"] += 1
        if args.list:
            if path not in cache:
                cache[path] = pathlib.Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
            print(f"{path}:{line}:{col}: {cache[path][line - 1].strip()[:140]}")
    print(f"divide_audit: {len(files)} files, {len(sites)} sites: " +
          ", ".join(f"{a} {n}" for a, n in by_area.most_common()))


if __name__ == "__main__":
    sys.exit(main())
