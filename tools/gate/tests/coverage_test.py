"""coverage_test.py: tools/gate/coverage.py on a real coverage build (N-1042).

python -X utf8 -I tools/gate/tests/coverage_test.py

A small program is built with -fprofile-instr-generate -fcoverage-mapping (the flags pgo.py make --coverage gives the
game), run, and its counts merged:
- the report names the function no run reached in never-run.txt (file:line name), counts lines and functions per file
  in coverage.tsv, and gives the share of lines run;
- files outside --root are left out;
- with a history, the first run sets the share; a build that adds code no run reaches lowers it and fails, naming
  the share it fell from; the same share again passes; a line of other binaries (another scope) is not compared;
- the key holds the suites that ran: fewer suites start their own line (a lower share is not a fall); a run that fell
  is written as one and is not what the next run is compared with (the same low share fails again, the old share
  passes); a line with no verdict (the file before the column) counts as a pass; the recorded rows of the long runs
  of 11-18 to 11-22 judged by the key (data below, no build);
- paths under each of several build folders (--rebase) read as the tree's.
Needs clang, llvm-profdata and llvm-cov (C:/Program Files/LLVM/bin, or NEBULA_LLVM_BIN).
"""
import os
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
LLVM = pathlib.Path(os.environ.get("NEBULA_LLVM_BIN", "C:/Program Files/LLVM/bin"))
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


try:
    import coverage
except ImportError:
    coverage = None
check(coverage is not None, "tools/gate/coverage.py exists")

SOURCE = "int used(int x) {\n    return x + 1;\n}\nint never(int x) {\n    return x * 2;\n}\n%s" \
         "int main(int c, char** v) {\n    return used(c) - 2;\n}\n"
EXTRA = "int also_never(int x) {\n    int y = x * 3;\n    y += 4;\n    y -= 1;\n    return y;\n}\n"


def build_and_run(folder, extra=""):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "a.c").write_text(SOURCE % extra, encoding="utf-8")
    exe = folder / ("a.exe" if os.name == "nt" else "a")
    subprocess.run([str(LLVM / "clang"), "-fprofile-instr-generate", "-fcoverage-mapping", "-O0", str(folder / "a.c"),
                    "-o", str(exe)], check=True, capture_output=True)
    subprocess.run([str(exe)], env=dict(os.environ, LLVM_PROFILE_FILE=str(folder / "a.profraw")), check=False)
    subprocess.run([str(LLVM / "llvm-profdata"), "merge", "-o", str(folder / "a.profdata"), str(folder / "a.profraw")],
                   check=True, capture_output=True)
    return exe, folder / "a.profdata"


def report(exe, profile, out, root, history=""):
    r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(HERE.parent / "coverage.py"), "report", "--binary",
                        str(exe), "--profile", str(profile), "--work", str(out), "--root", str(root), "--history",
                        str(history), "--commit", "abcdef123", "--llvm", str(LLVM)], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def report_keyed(exe, profile, out, root, history, suites):
    """The same report, with the suites that ran named (the history's key)."""
    r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(HERE.parent / "coverage.py"), "report", "--binary",
                        str(exe), "--profile", str(profile), "--work", str(out), "--root", str(root), "--history",
                        str(history), "--commit", "abcdef123", "--suites", suites, "--llvm", str(LLVM)],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


if coverage is not None:
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        exe, profile = build_and_run(t / "one")
        code, out = report(exe, profile, t / "out1", t)
        never = (t / "out1" / "never-run.txt").read_text(encoding="utf-8")
        rows = (t / "out1" / "coverage.tsv").read_text(encoding="utf-8").splitlines()
        check(code == 0 and "one/a.c:4 never" in never and "used" not in never, "never-run names the function: %s" % never)
        check(rows[1].startswith("one/a.c\t") and rows[1].split("\t")[3:] == ["2", "3"],
              "coverage.tsv counts the file's functions: %s" % rows)
        check("% of" in out and "lines run, 2 of 3 functions, 1 files" in out, "the share is printed: %s" % out)
        code, out = report(exe, profile, t / "out2", t / "elsewhere")
        check(code == 1 and "names no file under" in out, "files outside the root are left out: %s" % out)

        history = t / "coverage-history.tsv"
        code, out = report(exe, profile, t / "out3", t, history)
        check(code == 0 and len(coverage.read_history(history)) == 1, "the first run sets the share: %s" % out)
        code, out = report(exe, profile, t / "out4", t, history)
        check(code == 0, "the same share again passes: %s" % out)
        exe2, profile2 = build_and_run(t / "one", EXTRA)
        code, out = report(exe2, profile2, t / "out5", t, history)
        check(code == 1 and "it fell from" in out, "code no run reaches lowers the share and fails: %s" % out)
        # A higher share under other binaries' names: not this scope's last line.
        other = t / "other-history.tsv"
        coverage.append_history(other, {"time": "x", "commit": "c", "lines_run": 1, "lines": 1, "share": "100.0000",
                                        "functions_run": 1, "functions": 1, "scope": "nebula.exe,smg_game.dll"})
        code, out = report(exe, profile, t / "out6", t, other)
        check(code == 0 and len(coverage.read_history(other)) == 2, "another scope's line is not compared: %s" % out)

        # The key holds the suites. One history, three runs of the same binaries: engine and tour with every test (the
        # high share), then with the engine suite alone (the low share: the first line of its key, not a fall), then
        # the low share again after a run of the key that fell.
        exe, profile = build_and_run(t / "keyed")
        keyed = t / "keyed-history.tsv"
        code, out = report_keyed(exe, profile, t / "k1", t, keyed, "engine,tour,mods")
        check(code == 0 and coverage.read_history(keyed)[-1]["scope"].endswith("|engine,mods,tour"), "the key holds the suites, sorted: %s" % coverage.read_history(keyed))
        exe_big, profile_big = build_and_run(t / "keyed_big", EXTRA)
        code, out = report_keyed(exe_big, profile_big, t / "k2", t, keyed, "engine,tour")
        check(code == 0 and "it fell" not in out, "fewer suites start their own line, a lower share is not a fall: %s" % out)
        code, out = report_keyed(exe_big, profile_big, t / "k3", t, keyed, "engine,tour,mods")
        check(code == 1 and "it fell from" in out, "the same suites with a lower share fall: %s" % out)
        rows = coverage.read_history(keyed)
        check([r["verdict"] for r in rows] == ["pass", "pass", "fell"], "the fall is written as one: %s" % [r["verdict"] for r in rows])
        code, out = report_keyed(exe_big, profile_big, t / "k4", t, keyed, "engine,tour,mods")
        check(code == 1 and "it fell from" in out, "a run that fell does not lower the next comparison: %s" % out)
        code, out = report_keyed(exe, profile, t / "k5", t, keyed, "engine,tour,mods")
        check(code == 0, "the share of the last run that passed holds: %s" % out)
        # A line from before the verdict column: a pass.
        old = t / "old-history.tsv"
        tab = chr(9)
        old.write_text(tab.join(["time", "commit", "lines_run", "lines", "share", "functions_run", "functions", "scope"]) + chr(10)
                       + tab.join(["x", "c1", "9", "9", "100.0000", "1", "1", "nebula.exe"]) + chr(10), encoding="utf-8")
        check(coverage.compare(old, "nebula.exe", 50.0) == "100.00 % at c1", "a line with no verdict counts as a pass")

    # The recorded lines of the long runs (gate-runs/coverage-history.tsv, key `<binaries>|<suites>`): the run of
    # 11-22 played no mod suite and read 16.23 % where the runs before it, with the mod suites, read 22.07 %.
    BINARIES = "nebula.exe,smg_game.dll,smg_game_plain.dll"
    RECORDED = [("2026-11-18T20:30:23", "cce47fc21", 95644, 440250, "21.7249", 17218, 258663, "engine,mods,tour"),
                ("2026-11-19T03:58:50", "5dc7169c7", 95927, 440382, "21.7827", 17245, 258683, "engine,mods,tour"),
                ("2026-11-19T12:30:55", "d9efee551", 95932, 440496, "21.7782", 17257, 258731, "engine,mods,tour"),
                ("2026-11-19T16:53:57", "8675cd623", 95824, 440294, "21.7636", 17265, 258733, "engine,mods,tour"),
                ("2026-11-20T08:48:43", "790626cb5", 100011, 453344, "22.0607", 17639, 259712, "engine,mods,tour"),
                ("2026-11-21T03:36:51", "c693079e8", 100044, 453363, "22.0671", 17642, 259716, "engine,mods,tour")]
    with tempfile.TemporaryDirectory() as tmp:
        recorded = pathlib.Path(tmp) / "coverage-history.tsv"
        for time, commit, lr, lines, share, fr, functions, suites in RECORDED:
            coverage.append_history(recorded, {"time": time, "commit": commit, "lines_run": lr, "lines": lines, "share": share,
                                               "functions_run": fr, "functions": functions, "scope": BINARIES + "|" + suites,
                                               "verdict": "pass"})
        with_mods, without = BINARIES + "|engine,mods,tour", BINARIES + "|engine,tour"
        check(coverage.compare(recorded, without, 16.2257) is None, "the run of 11-22 under its own key is no fall")
        check(coverage.compare(recorded, with_mods, 16.2257) == "22.07 % at c693079e8", "under the key with the mod suites it is one: %s" % coverage.compare(recorded, with_mods, 16.2257))
        coverage.append_history(recorded, {"time": "2026-11-22T18:19:44", "commit": "c89e4e551", "lines_run": 73853, "lines": 455160,
                                           "share": "16.2257", "functions_run": 13952, "functions": 259955, "scope": without,
                                           "verdict": "pass"})
        check(coverage.compare(recorded, without, 16.20) == "16.23 % at c89e4e551", "a drop planted within the key is refused")
        check(coverage.compare(recorded, without, 16.2257) is None and coverage.compare(recorded, without, 16.5) is None,
              "the same share and a higher one pass")
        coverage.append_history(recorded, {"time": "t", "commit": "bad", "lines_run": 1, "lines": 100, "share": "15.0000", "functions_run": 1,
                                           "functions": 1, "scope": without, "verdict": "fell"})
        check(coverage.compare(recorded, without, 16.0) == "16.23 % at c89e4e551", "a failed line does not lower the next comparison")
        check(coverage.compare(recorded, without, 16.2257) is None, "the last pass's share still holds")

    # A cache's paths (the build folder joined with paths relative to the tree) read as the tree's; a file in two
    # modules adds up; a file's own function and a decorated name read as written.
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        record = "SF:%s\nFN:4,%s\nFNDA:0,%s\nLF:3\nLH:1\nend_of_record\n"
        lcov = (record % (t / "build" / "game" / "src" / "B.cpp", "B.cpp:local", "B.cpp:local")
                + record % (t / "build" / "game" / "src" / "B.cpp", "?run@Actor@@QEAAXXZ", "?run@Actor@@QEAAXXZ")
                + record % (t / "build" / "game" / "gen" / "made.cpp", "made", "made"))
        (t / "tree" / "src").mkdir(parents=True)
        (t / "tree" / "src" / "B.cpp").write_text("", encoding="utf-8")
        files = coverage.parse_lcov(lcov, t / "tree", t / "build" / "game")
        check(list(files) == ["src/B.cpp"] and files["src/B.cpp"][:4] == [2, 6, 0, 2],
              "rebased, kept case, two modules add up: %s" % files)
        # Two build folders, each rebased: the programs' and the game's.
        two = (record % (t / "build" / "msvc" / "src" / "B.cpp", "f", "f")
               + record % (t / "build" / "game" / "src" / "B.cpp", "g", "g"))
        both = coverage.parse_lcov(two, t / "tree", [t / "build" / "game", t / "build" / "msvc"])
        check(list(both) == ["src/B.cpp"] and both["src/B.cpp"][:4] == [2, 6, 0, 2],
              "each build folder rebased: %s" % both)
        names = sorted(coverage.readable(n) for _, n in files["src/B.cpp"][4])
        check(names[1] == "local" and (os.name != "nt" or names[0].startswith("Actor::run")),
              "names read as written: %s" % names)

if failures:
    print("coverage_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("coverage_test.py: pass")
