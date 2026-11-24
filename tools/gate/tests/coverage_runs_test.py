"""coverage_runs_test.py: the coverage check does not judge counts of runs that did not play, and a build folder keeps no object
another tree made (R-004, 11-23). No game, no build, no compiler; fake runs, fake `run`, planted folders.

    python -X utf8 -I tools/gate/tests/coverage_runs_test.py [--work FOLDER]

What each part plants (and what it was seen to catch):
  missing_runs      the engine list and the tour list both empty; every test ended `tool` (the game did not open its data, as on 11-23);
                    one test of a run ended `env`; the right case (every test passed, a test that FAILED is the game's) says nothing.
                    Seen failing with missing_runs returning [] (the old check read no result).
  coverage_check    through gate.py with the same planted runs: the check ends `owed` with one line that names what is missing,
                    the counts are not merged, coverage.py report is not called, no history line is written; the right case
                    merges and reports. Seen failing with the old coverage_check (it merged and reported 0.15 %).
  foreign_objects   a folder whose objects read Nebula-A1h's files while its tree is Nebula-long: listed (exit 1) and dropped, and only
                    those object files; a sound folder (own tree, the SDK, the folder's own generated files): nothing listed, nothing
                    dropped. Seen failing with the tree compared to nothing (every object foreign) and with the other tree ignored.
  build_windows.ps1 calls the detector for the MSVC folder (a text check); not for the game folder, whose deps take 16 s to read.
  fail-safe          a ninja that fails, no ninja, a ninja that cannot be started, an answer that cannot be read: one line, exit 0 for `drop`
                    (2 for `list`), every object file still there; the planted foreign folder is the only one that loses a file.
"""
import argparse
import pathlib
import shutil
import sys
import tempfile
import types

HERE = pathlib.Path(__file__).resolve().parent
GATE = HERE.parent
sys.path.insert(0, str(GATE))
import coverage  # noqa: E402
import foreign_objects  # noqa: E402
import gate  # noqa: E402


class Cases:
    """What the harness check hands the gate: results {test: (kind, message)}."""

    def __init__(self, results):
        self.results = dict(results)


def result(**kinds):
    """(passed, detail, cases) as the harness check returns it."""
    cases = Cases({name: (kind, "" if kind == "pass" else "the game.pak did not open") for name, kind in kinds.items()})
    return all(k == "pass" for k in kinds.values()), "", cases


DEPS_SOUND = """\
CMakeFiles/a.dir/runtime/src/a.cpp.obj: #deps 3, deps mtime 1 (VALID)
    ../../../MarioGalaxyRecomp/Nebula-long/runtime/include/galaxy/a.h
    ../../../MarioGalaxyRecomp/Nebula-long/runtime/include/galaxy/store_version.inc
    D:/NebulaWork/deps/ffx-fg/FidelityFX-SDK-1.1.4/sdk/include/ffx.h

CMakeFiles/b.dir/b.cpp.obj: #deps 1, deps mtime 1 (VALID)
    generated/programs_dxil.rc

libx.so: #deps 1, deps mtime 1 (VALID)
    CMakeFiles/x.dir/D_/NebulaWork/MarioGalaxyRecomp/Nebula-A1h/runtime/src/x.cpp.o
"""
DEPS_FOREIGN = DEPS_SOUND + """
CMakeFiles/c.dir/runtime/src/asset_store.cpp.obj: #deps 2, deps mtime 1 (VALID)
    ../../../MarioGalaxyRecomp/Nebula-A1h/runtime/include/galaxy/asset_store.h
    ../../../MarioGalaxyRecomp/Nebula-A1h/runtime/include/galaxy/store_version.inc
"""


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("--work", default="")
    args = p.parse_args(argv)
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    # --- missing_runs -----------------------------------------------------------------------------------------------------
    good = [("engine", result(boot="pass", home="pass")), ("tour", result(a="pass", b="pass"))]
    check(coverage.missing_runs(good) == [], "the right case (every test passed) says nothing")
    check(coverage.missing_runs([("engine", result(boot="fail", home="pass"))]) == [],
          "a test that failed played the game: its counts are there, and its failure is another check's business")
    empty = coverage.missing_runs([("engine", (True, "", Cases({}))), ("tour", (True, "", Cases({})))])
    check(len(empty) == 2 and "engine: no test ran" in empty[0] and "tour: no test ran" in empty[1],
          "both lists empty: one sentence for each run: %s" % empty)
    bare = coverage.missing_runs([("engine", result(**{"boot": "tool", "home": "tool"})), ("tour", result(a="tool", b="tool", c="tool"))])
    check(len(bare) == 2 and "2 of 2 tests did not play (first: boot: the game.pak did not open)" in bare[0] and "3 of 3" in bare[1],
          "every test ended `tool`: named with the count and the first reason: %s" % bare)
    part = coverage.missing_runs([("tour", result(a="pass", b="env", c="pass"))])
    check(len(part) == 1 and "1 of 3 tests did not play (first: b" in part[0], "one test of three ended `env`: counts are missing: %s" % part)
    check(coverage.missing_runs([("mods", (True, "ok"))]) == [], "a result with no cases is not judged here")

    # --- coverage_check through gate.py ------------------------------------------------------------------------------------
    work = pathlib.Path(args.work) if args.work else pathlib.Path(tempfile.mkdtemp())
    own_work = not args.work
    calls = []
    real_run = gate.run
    try:
        def fake_run(cmd, log, env=None, cwd=None, timeout=None):
            calls.append([str(c) for c in cmd])
            with open(log, "ab") as f:
                if any("coverage.py" in str(c) for c in cmd):
                    f.write(b"coverage: 20.00 % of 10 lines run, 2 of 4 functions, 1 files\n")
            return 0
        gate.run = fake_run

        def play(name, parts):
            folder = work / name
            counts = folder / "coverage" / "counts"
            counts.mkdir(parents=True)
            (counts / "counts-x-1.profraw").write_bytes(b"raw")
            g = types.SimpleNamespace(work=folder, commit="abcdef012", mods="off", args=types.SimpleNamespace(work=str(folder)))
            calls.clear()
            out = gate.coverage_check(g, str(folder / "bins"), folder / "tour", parts)(folder / ("%s.log" % name))
            return out, folder, list(calls)

        out, folder, made = play("right", {"engine": lambda log: result(boot="pass"), "tour": lambda log: result(a="pass", b="pass")})
        check(out[0] is True and any("llvm-profdata" in c[0] for c in made) and any("coverage.py" in " ".join(c) for c in made),
              "the right case merges and reports: %s %s" % (out, made))
        for name, parts, word in (
                ("lists", {"engine": lambda log: (True, "", Cases({})), "tour": lambda log: (True, "", Cases({}))}, "no test ran"),
                ("bare", {"engine": lambda log: result(boot="tool", home="tool"), "tour": lambda log: result(a="tool")}, "did not play"),
                ("one", {"engine": lambda log: result(boot="pass"), "tour": lambda log: result(a="pass", b="env")}, "1 of 2 tests did not play")):
            out, folder, made = play(name, parts)
            check(len(out) == 4 and out[0] is False and out[3] == "owed", "%s: ends `owed`, not fail: %s" % (name, out))
            check("could not measure" in out[1] and word in out[1] and "no history line" in out[1], "%s: one line names what is missing: %s" % (name, out[1]))
            check(not made, "%s: nothing is merged and coverage.py is not called: %s" % (name, made))
            check(not (folder / "coverage-history.tsv").exists() and not (folder / "coverage" / "counts").exists(),
                  "%s: no history line, the counts are cleared" % name)
            check("could not measure" in (folder / ("%s.log" % name)).read_text(encoding="utf-8"), "%s: the log says so" % name)
    finally:
        gate.run = real_run

    # --- foreign_objects --------------------------------------------------------------------------------------------------
    t = pathlib.Path(tempfile.mkdtemp())
    try:
        check(foreign_objects.tree_of("D:/NebulaWork/MarioGalaxyRecomp/Nebula-A1h/runtime/a.h") == "d:/nebulawork/mariogalaxyrecomp/nebula-a1h",
              "a path in a work tree names that tree")
        check(foreign_objects.tree_of("D:/NebulaWork/deps/ffx/a.h") is None, "a path outside every work tree names none")
        folder = t / "NebulaWork" / "build" / "x" / "msvc"
        own = t / "NebulaWork" / "MarioGalaxyRecomp" / "Nebula-long"
        folder.mkdir(parents=True)
        (folder / ".ninja_deps").write_bytes(b"x")
        tree = str(own).replace("\\", "/")
        (folder / "CMakeCache.txt").write_text("CMAKE_HOME_DIRECTORY:INTERNAL=%s\n" % tree, encoding="utf-8")
        for obj in ("CMakeFiles/a.dir/runtime/src/a.cpp.obj", "CMakeFiles/b.dir/b.cpp.obj", "CMakeFiles/c.dir/runtime/src/asset_store.cpp.obj"):
            (folder / obj).parent.mkdir(parents=True, exist_ok=True)
            (folder / obj).write_bytes(b"obj")

        def fake_ninja(text):
            return lambda cmd, **kw: types.SimpleNamespace(returncode=0, stdout=text, stderr="")
        shutil_ninja = str(pathlib.Path(sys.executable))      # any file: only its existence is read, `run` is the fake

        # The folder as the build left it: relative paths, as `ninja -t deps` prints them, from build/x/msvc up to NebulaWork.
        sound = foreign_objects.check(folder, tree, shutil_ninja, fake_ninja(DEPS_SOUND))
        check(sound[1] == 3 and sound[2] == {}, "a sound folder: 3 objects, none foreign (a link step names objects of the folder itself after the sources' paths): %s" % (sound,))
        planted = foreign_objects.check(folder, tree, shutil_ninja, fake_ninja(DEPS_FOREIGN))
        check(list(planted[2]) == ["CMakeFiles/c.dir/runtime/src/asset_store.cpp.obj"] and planted[2][list(planted[2])[0]] == {"nebula-a1h"},
              "the planted folder: the one object that read Nebula-A1h is found, and the tree it read: %s" % (planted,))
        line = foreign_objects.describe("F", planted[0], planted[1], planted[2])
        check("1 of 4 objects read another tree's files (nebula-a1h)" in line and "tree nebula-long" in line, "the line says it: %s" % line)
        # The folder's own tree from its CMakeCache when none is given.
        from_cache = foreign_objects.check(folder, None, shutil_ninja, fake_ninja(DEPS_FOREIGN))
        check(from_cache[0] == foreign_objects.tree_of(tree) and list(from_cache[2]) == list(planted[2]),
              "the folder's tree is read from its CMakeCache.txt: %s" % (from_cache[0],))
        check(foreign_objects.drop(folder, planted[2]) == 1, "drop deletes the one object file")
        check(not (folder / "CMakeFiles/c.dir/runtime/src/asset_store.cpp.obj").exists()
              and (folder / "CMakeFiles/a.dir/runtime/src/a.cpp.obj").exists() and (folder / "CMakeFiles/b.dir/b.cpp.obj").exists(),
              "only that object's file went; the others stay")
        # The fail-safe: a detector that cannot answer says one line, deletes nothing and lets the build go on (exit 0 for `drop`).
        import contextlib
        import io

        def run_main(*argv):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = foreign_objects.main(list(argv))
            return code, out.getvalue()
        before = sorted(str(f) for f in folder.rglob("*.obj"))
        code, said = run_main("drop", str(folder), "--tree", tree, "--ninja", sys.executable)
        check(code == 0 and said.count("\n") == 1 and "not checked (Doubt: ninja -t deps ended with" in said and "nothing deleted, the build goes on" in said,
              "a ninja that fails: one line, exit 0, nothing deleted: %r" % said)
        real_find = foreign_objects.find_ninja
        foreign_objects.find_ninja = lambda given=None: None
        try:
            code, said = run_main("drop", str(folder), "--tree", tree)
        finally:
            foreign_objects.find_ninja = real_find
        check(code == 0 and said.count("\n") == 1 and "not checked (Doubt: no ninja program found)" in said,
              "no ninja: one line, exit 0, nothing deleted: %r" % said)
        code, said = run_main("list", str(folder), "--tree", tree, "--ninja", sys.executable)
        check(code == 2, "`list` says exit 2 when it cannot answer: %s" % code)

        def raising(cmd, **kw):
            raise OSError("the program is gone")
        try:
            foreign_objects.deps_text(folder, shutil_ninja, raising)
            check(False, "a ninja that cannot be started raises Doubt")
        except foreign_objects.Doubt as error:
            check("could not be run" in str(error), "a ninja that cannot be started raises Doubt: %s" % error)
        check(foreign_objects.parse_deps("garbage\nlines only") == {} and foreign_objects.foreign({}, folder, "x") == {},
              "an answer that cannot be read lists no object")
        check(sorted(str(f) for f in folder.rglob("*.obj")) == before, "after every case of doubt every object file is still there")
        none = t / "empty"
        none.mkdir()
        check(foreign_objects.check(none, tree, shutil_ninja, fake_ninja(DEPS_FOREIGN))[2] == {}, "a folder with no .ninja_deps: nothing found")
    finally:
        shutil.rmtree(t, ignore_errors=True)
        if own_work:
            shutil.rmtree(work, ignore_errors=True)

    # --- build_windows.ps1 -------------------------------------------------------------------------------------------------
    ps1 = (GATE / "build_windows.ps1").read_text(encoding="utf-8")
    check("Drop-Foreign-Objects $msvc" in ps1 and "foreign_objects.py" in ps1 and "Drop-Foreign-Objects $game" not in ps1,
          "build_windows.ps1 drops foreign objects of the MSVC folder before it builds (the game folder: `ninja -t deps` takes 16 s there)")

    for what in failures:
        print("FAILED:", what)
    print("coverage_runs_test: %s" % ("FAILED" if failures else "pass"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
