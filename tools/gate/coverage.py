"""coverage.py: which code no test runs, and the share run (docs/plan/04-GATE-AND-MAIN.md 3.3, N-1042).

    python -X utf8 -I tools/gate/coverage.py report --binary BIN [--binary BIN...] --profile PROFDATA --work OUT [--root TREE]
                                               [--rebase BUILD...] [--history FILE] [--commit SHA] [--suites NAMES] [--llvm LLVM_BIN]

The binaries are built with -fprofile-instr-generate -fcoverage-mapping (native/CMakeLists.txt NEBULA_COVERAGE: both
game modules, smg_game and smg_game_plain; CMakeLists.txt NEBULA_COVERAGE: the programs), the profile merged from the
runs of the tests on them; a source file in several counts as run where any ran it. llvm-cov exports the counts per source line and function (lcov text), and
this writes, for the files under --root (the tree):
- OUT/coverage.tsv: each file's lines and functions, run and in all, the files with the most lines never run first;
- OUT/never-run.txt: every function no test ran, file:line and its name;
and prints the share of lines run. --rebase names a build folder the sources' paths are recorded under when a
compiler cache made them relative to the tree (build_windows.ps1's sccache with SCCACHE_BASEDIRS): those paths are read
as the tree's (one --rebase a build folder). With --history the share joins that file (one line a run: its key and its
verdict), and a share lower than the last line of the same key that passed fails: the share of code run by tests may
not fall (a change that adds code no test runs lowers it). The key is the binaries' names and --suites, the names of
the runs whose counts are merged (engine, tour, mods): another set of binaries or of runs starts its own line, since
the share of fewer runs is not the share of more (the long run of 11-22 ran no mod suite and read 16.23 % against
22.07 % of runs with them). A run that fell is written with the verdict `fell` and is never what a later run is
compared with.
Exit 0 when the export worked and the share held.
"""
import argparse
import datetime
import os
import pathlib
import subprocess
import sys

# dbghelp's UnDecorateSymbolName: no __cdecl and the like, no access, no return type, no member kind, no throw list.
UNDECORATE_FLAGS = 0x0002 | 0x0004 | 0x0080 | 0x0100 | 0x0200


def readable(name):
    """A function's name as written: the file prefix of a file's own (static) function dropped, a Microsoft-decorated
    name undecorated (Windows' dbghelp); others as they are."""
    if ":" in name and not name.startswith("?"):
        name = name.split(":", 1)[1]
    if name.startswith("?") and os.name == "nt":
        import ctypes
        out = ctypes.create_unicode_buffer(2048)
        if ctypes.windll.dbghelp.UnDecorateSymbolNameW(name, out, 2048, UNDECORATE_FLAGS):
            return out.value
    return name


HISTORY_COLUMNS = ("time", "commit", "lines_run", "lines", "share", "functions_run", "functions", "scope", "verdict")
# A line written before the verdict column existed has none and counts as a pass. The gate always names its suites, so
# the key of a line written before that (no `|` in it) is no key the gate compares under.
SUITES_SEPARATOR = "|"


def parse_lcov(text, root, rebase=None):
    """{file: [lines run, lines, functions run, functions, [(line, name) never run]]} for files under `root` (a path
    under a `rebase` folder, one path or several, read as the same path under `root`). A file in the text twice (two
    modules) adds up."""
    root = os.path.normcase(os.path.abspath(root)) + os.sep
    folders = [rebase] if isinstance(rebase, (str, os.PathLike)) else list(rebase or [])
    # The longest first: a build folder inside another is its own.
    rebases = sorted((os.path.normcase(os.path.abspath(f)) + os.sep for f in folders if f), key=len, reverse=True)
    files, current, names = {}, None, {}
    for line in text.splitlines():
        if line.startswith("SF:"):
            # Compared without case (Windows paths), kept as the export spells them.
            path = os.path.abspath(line[3:])
            # A rebased path names a tree file only when the tree has it (not a file the build made).
            folder = next((f for f in rebases if os.path.normcase(path).startswith(f)), None)
            if folder:
                path = root + path[len(folder):]
                if not os.path.exists(path):
                    path = ""
            current = None
            if path and os.path.normcase(path).startswith(root):
                current = files.setdefault(path[len(root):].replace(os.sep, "/"), [0, 0, 0, 0, []])
            names = {}
        elif current is None:
            continue
        elif line.startswith("FN:"):
            number, _, name = line[3:].partition(",")
            names[name] = int(number)
        elif line.startswith("FNDA:"):
            count, _, name = line[5:].partition(",")
            current[3] += 1
            if int(count) > 0:
                current[2] += 1
            else:
                current[4].append((names.get(name, 0), name))
        elif line.startswith("LF:"):
            current[1] += int(line[3:])
        elif line.startswith("LH:"):
            current[0] += int(line[3:])
    return files


def totals(files):
    lines_run = sum(f[0] for f in files.values())
    lines = sum(f[1] for f in files.values())
    return lines_run, lines, sum(f[2] for f in files.values()), sum(f[3] for f in files.values())


def write_lists(out, files):
    out.mkdir(parents=True, exist_ok=True)
    order = sorted(files.items(), key=lambda kv: (-(kv[1][1] - kv[1][0]), kv[0]))
    with open(out / "coverage.tsv", "w", encoding="utf-8", newline="\n") as f:
        f.write("file\tlines_run\tlines\tfunctions_run\tfunctions\n")
        for name, (lr, lf, fr, ff, _) in order:
            f.write("%s\t%d\t%d\t%d\t%d\n" % (name, lr, lf, fr, ff))
    with open(out / "never-run.txt", "w", encoding="utf-8", newline="\n") as f:
        for name, value in sorted(files.items()):
            # A header's function is in every file that includes it: listed once.
            for number, function in sorted({(n, readable(fn)) for n, fn in value[4]}):
                f.write("%s:%d %s\n" % (name, number, function))


def read_history(path):
    path = pathlib.Path(path)
    if not path.exists():
        return []
    rows = [dict(zip(HISTORY_COLUMNS, l.split("\t"))) for l in path.read_text(encoding="utf-8").splitlines()[1:] if l]
    for row in rows:
        row.setdefault("verdict", "pass")
    return rows


def compare(history, key, share):
    """What the share fell from, as "<share> % at <commit>", or None: the last line of this key that passed."""
    earlier = [row for row in read_history(history) if row.get("scope") == key and row.get("verdict") == "pass"]
    if earlier and share < float(earlier[-1]["share"]) - 0.005:
        return "%.2f %% at %s" % (float(earlier[-1]["share"]), earlier[-1]["commit"])
    return None


def history_key(binaries, suites=""):
    """The key a share is compared under: the binaries' names, and the runs whose counts it holds when they are named."""
    scope = ",".join(sorted(pathlib.Path(b).name.lower() for b in binaries))
    names = ",".join(sorted(n for n in suites.split(",") if n))
    return scope + (SUITES_SEPARATOR + names if names else "")


def append_history(path, row):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    new = not path.exists()
    with open(path, "a", encoding="utf-8") as f:
        if new:
            f.write("\t".join(HISTORY_COLUMNS) + "\n")
        f.write("\t".join(str(row.get(c, "pass") if c == "verdict" else row[c]) for c in HISTORY_COLUMNS) + "\n")


def report(a, run=subprocess.run, now=None):
    r = run([str(pathlib.Path(a.llvm) / "llvm-cov"), "export", "-format=lcov", "-instr-profile=" + str(a.profile),
             str(a.binary[0])] + [x for b in a.binary[1:] for x in ("-object", str(b))], capture_output=True, text=True)
    if r.returncode != 0:
        print("coverage: llvm-cov export failed: %s" % (r.stderr or r.stdout).strip()[-400:])
        return 1
    files = parse_lcov(r.stdout, a.root, a.rebase)
    scope = history_key(a.binary, a.suites)
    if not files:
        print("coverage: the export names no file under %s" % a.root)
        return 1
    write_lists(pathlib.Path(a.work), files)
    lines_run, lines, functions_run, functions = totals(files)
    share = 100.0 * lines_run / lines if lines else 0.0
    said = "%.2f %% of %d lines run, %d of %d functions, %d files" % (share, lines, functions_run, functions, len(files))
    fell = None
    if a.history:
        fell = compare(a.history, scope, share)
        append_history(a.history, {"time": (now or datetime.datetime.now()).isoformat(timespec="seconds"),
                                   "commit": a.commit[:9], "lines_run": lines_run, "lines": lines,
                                   "share": "%.4f" % share, "functions_run": functions_run, "functions": functions,
                                   "scope": scope, "verdict": "fell" if fell else "pass"})
    print("coverage: %s%s" % (said, ("; it fell from " + fell) if fell else ""))
    print("coverage lists: %s, %s" % (pathlib.Path(a.work) / "coverage.tsv", pathlib.Path(a.work) / "never-run.txt"))
    return 1 if fell else 0


def main(argv=None):
    # A self-test that starts this tool as a child process runs it under the live-state guard (tools/gate/selftest_guard.py).
    import pathlib as _pathlib
    import sys as _sys
    _gate = str(_pathlib.Path(__file__).resolve().parents[1] / "gate")
    _sys.path.insert(0, _gate)
    try:
        import selftest_guard
    finally:
        _sys.path.remove(_gate)
    selftest_guard.install_if_asked()
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    r = sub.add_parser("report")
    r.add_argument("--binary", required=True, action="append")
    r.add_argument("--profile", required=True)
    r.add_argument("--work", required=True)
    r.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    r.add_argument("--rebase", action="append", default=[])
    r.add_argument("--history", default="")
    r.add_argument("--commit", default="")
    r.add_argument("--suites", default="", help="the runs whose counts are merged (engine,tour,mods): part of the history's key")
    r.add_argument("--llvm", default=os.environ.get("NEBULA_LLVM_BIN", "C:/Program Files/LLVM/bin"))
    return report(p.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
