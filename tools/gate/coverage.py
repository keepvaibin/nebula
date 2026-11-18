"""coverage.py: which game code no test runs, and the share run (docs/plan/04-GATE-AND-MAIN.md 3.3, N-1042).

    python -X utf8 -I tools/gate/coverage.py report --binary DLL [--binary DLL...] --profile PROFDATA --work OUT [--root TREE]
                                               [--history FILE] [--commit SHA] [--llvm LLVM_BIN]

The binaries are built with -fprofile-instr-generate -fcoverage-mapping (native/CMakeLists.txt NEBULA_COVERAGE: both
game modules, smg_game and smg_game_plain), the profile merged from the runs of the tests on them; a source file in
both counts as run where either ran it. llvm-cov exports the counts per source line and function (lcov text), and
this writes, for the files under --root (the tree):
- OUT/coverage.tsv: each file's lines and functions, run and in all, the files with the most lines never run first;
- OUT/never-run.txt: every function no test ran, file:line and its name;
and prints the share of lines run. With --history the share joins that file (one line a run), and a share lower than
the last line's fails: the share of code run by tests may not fall (a change that adds code no test runs lowers it).
Exit 0 when the export worked and the share held.
"""
import argparse
import datetime
import os
import pathlib
import subprocess
import sys

HISTORY_COLUMNS = ("time", "commit", "lines_run", "lines", "share", "functions_run", "functions")


def parse_lcov(text, root):
    """{file: [lines run, lines, functions run, functions, [(line, name) never run]]} for files under `root`."""
    root = os.path.normcase(os.path.abspath(root)) + os.sep
    files, current, names = {}, None, {}
    for line in text.splitlines():
        if line.startswith("SF:"):
            path = os.path.normcase(os.path.abspath(line[3:]))
            current = [0, 0, 0, 0, []] if path.startswith(root) else None
            if current is not None:
                files[path[len(root):].replace(os.sep, "/")] = current
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
            current[1] = int(line[3:])
        elif line.startswith("LH:"):
            current[0] = int(line[3:])
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
            for number, function in sorted(value[4]):
                f.write("%s:%d %s\n" % (name, number, function))


def read_history(path):
    path = pathlib.Path(path)
    if not path.exists():
        return []
    return [dict(zip(HISTORY_COLUMNS, l.split("\t"))) for l in path.read_text(encoding="utf-8").splitlines()[1:] if l]


def append_history(path, row):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    new = not path.exists()
    with open(path, "a", encoding="utf-8") as f:
        if new:
            f.write("\t".join(HISTORY_COLUMNS) + "\n")
        f.write("\t".join(str(row[c]) for c in HISTORY_COLUMNS) + "\n")


def report(a, run=subprocess.run, now=None):
    r = run([str(pathlib.Path(a.llvm) / "llvm-cov"), "export", "-format=lcov", "-instr-profile=" + str(a.profile),
             str(a.binary[0])] + [x for b in a.binary[1:] for x in ("-object", str(b))], capture_output=True, text=True)
    if r.returncode != 0:
        print("coverage: llvm-cov export failed: %s" % (r.stderr or r.stdout).strip()[-400:])
        return 1
    files = parse_lcov(r.stdout, a.root)
    if not files:
        print("coverage: the export names no file under %s" % a.root)
        return 1
    write_lists(pathlib.Path(a.work), files)
    lines_run, lines, functions_run, functions = totals(files)
    share = 100.0 * lines_run / lines if lines else 0.0
    said = "%.2f %% of %d lines run, %d of %d functions, %d files" % (share, lines, functions_run, functions, len(files))
    fell = None
    if a.history:
        earlier = read_history(a.history)
        if earlier and share < float(earlier[-1]["share"]) - 0.005:
            fell = "%.2f %% at %s" % (float(earlier[-1]["share"]), earlier[-1]["commit"])
        append_history(a.history, {"time": (now or datetime.datetime.now()).isoformat(timespec="seconds"),
                                   "commit": a.commit[:9], "lines_run": lines_run, "lines": lines,
                                   "share": "%.4f" % share, "functions_run": functions_run, "functions": functions})
    print("coverage: %s%s" % (said, ("; it fell from " + fell) if fell else ""))
    print("coverage lists: %s, %s" % (pathlib.Path(a.work) / "coverage.tsv", pathlib.Path(a.work) / "never-run.txt"))
    return 1 if fell else 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    r = sub.add_parser("report")
    r.add_argument("--binary", required=True, action="append")
    r.add_argument("--profile", required=True)
    r.add_argument("--work", required=True)
    r.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    r.add_argument("--history", default="")
    r.add_argument("--commit", default="")
    r.add_argument("--llvm", default=os.environ.get("NEBULA_LLVM_BIN", "C:/Program Files/LLVM/bin"))
    return report(p.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
