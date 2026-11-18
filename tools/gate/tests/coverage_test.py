"""coverage_test.py: tools/gate/coverage.py on a real coverage build (N-1042).

python -X utf8 -I tools/gate/tests/coverage_test.py

A small program is built with -fprofile-instr-generate -fcoverage-mapping (the flags pgo.py make --coverage gives the
game), run, and its counts merged:
- the report names the function no run reached in never-run.txt (file:line name), counts lines and functions per file
  in coverage.tsv, and gives the share of lines run;
- files outside --root are left out;
- with a history, the first run sets the share; a build that adds code no run reaches lowers it and fails, naming
  the share it fell from; the same share again passes; a line of other binaries (another scope) is not compared;
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
