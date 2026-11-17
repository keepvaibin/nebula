"""pgo_test.py: the profile's staleness is seen, not hidden (P-014; tools/gate/pgo.py, native/CMakeLists.txt NEBULA_PGO).

1. The compiler, given the warning flags the game's profile build uses (read from native/CMakeLists.txt) and -Werror
   as the game builds, compiles a file against a profile whose counts no longer match it: the build goes on (not an
   error) and its log says the function is out of date. With the warnings switched off (the build before P-014) the
   log says nothing, and this fails.
2. pgo.py check: a log that is not a profile build's fails; a fresh one passes; more than the limit out of date fails;
   a file the profile does not know fails; "has" and "have" are both read.
Needs clang and llvm-profdata (C:/Program Files/LLVM/bin, or NEBULA_LLVM/bin).
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
import pgo  # noqa: E402


def llvm_tool(name):
    exe = name + (".exe" if os.name == "nt" else "")
    for folder in (os.environ.get("NEBULA_LLVM") and pathlib.Path(os.environ["NEBULA_LLVM"]) / "bin",
                   pathlib.Path("C:/Program Files/LLVM/bin")):
        if folder and (folder / exe).is_file():
            return str(folder / exe)
    return shutil.which(name)


def profile_flags():
    """The warning flags of the NEBULA_PGO block's target_compile_options."""
    text = (TREE / "native" / "CMakeLists.txt").read_text(encoding="utf-8")
    block = text[text.index("if(NEBULA_PGO)"):]
    options = block[block.index("target_compile_options(smg_game_common INTERFACE"):]
    options = options[:options.index(")")]
    return [w for w in options.split() if w.startswith("-W")]


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    clang, profdata = llvm_tool("clang"), llvm_tool("llvm-profdata")
    if not clang or not profdata:
        print("pgo_test.py: FAILED: no clang or llvm-profdata")
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        (t / "game.c").write_text("int stepped(int x) {\n    return x > 3 ? x * 2 : x - 1;\n}\n", encoding="utf-8")
        # A front-end profile of stepped() whose hash belongs to other code: the counts no longer match it.
        (t / "old.proftext").write_text("stepped\n# Func Hash:\n1234567\n# Num Counters:\n2\n# Counter Values:\n100\n40\n\n",
                                        encoding="utf-8")
        merged = subprocess.run([profdata, "merge", "-o", str(t / "old.profdata"), str(t / "old.proftext")],
                                capture_output=True, text=True)
        check(merged.returncode == 0, "llvm-profdata merge: %s" % merged.stderr.strip()[-300:])
        flags = profile_flags()
        check("-Wprofile-instr-out-of-date" in flags and "-Wno-profile-instr-out-of-date" not in flags,
              "the profile build keeps the out-of-date warning: %s" % flags)
        built = subprocess.run([clang, "-c", "-O2", "-Werror", "-fprofile-instr-use=%s" % (t / "old.profdata")] + flags
                               + [str(t / "game.c"), "-o", str(t / "game.o")], capture_output=True, text=True)
        log = "-- Nebula game profile: old.profdata\n" + built.stdout + built.stderr
        check(built.returncode == 0, "a profile out of date is not an error: exit %d %s" % (built.returncode, log[-300:]))
        functions, mismatched, unknown = pgo.staleness(log)
        check(functions == 1 and mismatched == 1, "the log says 1 of 1 out of date: %d of %d (%s)" % (
            mismatched, functions, re.sub(r"\s+", " ", log)[-300:]))
        (t / "build.log").write_text(log, encoding="utf-8")
        check(pgo.check(t / "build.log", 5.0) == 1, "check fails it")

        def judged(text, limit=5.0):
            (t / "x.log").write_text(text, encoding="utf-8")
            return pgo.check(t / "x.log", limit)

        marker = "-- Nebula game profile: native/pgo/game.profdata\n"
        check(judged("[1/2] Building CXX object a.obj\n") == 1, "a log that is not a profile build's fails")
        check(judged(marker + "[1/2] Building CXX object a.obj\n") == 0, "a fresh profile passes")
        some = (marker + "a.cpp: warning: profile data may be out of date: of 100 functions, 3 have mismatched data that"
                " will be ignored [-Wprofile-instr-out-of-date]\nb.cpp: warning: profile data may be out of date: of 1"
                " function, 1 has mismatched data that will be ignored\n")
        check(pgo.staleness(some)[:2] == (101, 4), "has and have both read: %s" % (pgo.staleness(some),))
        check(judged(some) == 0, "4 of 101 is within 5 %")
        check(judged(some, 3.0) == 1, "4 of 101 is over 3 %")
        check(judged(marker + 'warning: no profile data available for file "C:\\src\\new.cpp" [-Wprofile-instr-unprofiled]\n')
              == 1, "a file the profile does not know fails")

    if failures:
        print("pgo_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("pgo_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
