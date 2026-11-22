"""cmake_copy_guard_test.py: the guard at the end of native/CMakeLists.txt (P-341), on a small project of its own.

    python -X utf8 -I tools/gate/tests/cmake_copy_guard_test.py [--work FOLDER]

The rows module compiles generated copies of the game's sources. A COMPILE_OPTIONS, COMPILE_DEFINITIONS or COMPILE_FLAGS set on a source as
written and missing on its copy stops the configure with a sentence that names the file and the property. The test takes the guard's text
from native/CMakeLists.txt itself (from its comment to the end of the file) and runs it in a project with two sources and their copies:
  the copy has what its source has (and an -iquote of its own)    configure passes
  the copy lacks the option                                         stops, naming the source and COMPILE_OPTIONS
  the copy lacks one of two definitions                             stops, naming COMPILE_DEFINITIONS
  the copy lacks the flags                                          stops, naming COMPILE_FLAGS
  the source has no property                                        passes, whatever the copy has
"""
import pathlib
import shutil
import subprocess
import sys
import tempfile

TREE = pathlib.Path(__file__).resolve().parents[3]
CMAKE_CANDIDATES = [
    "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe",
    "C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe",
    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe",
]
MARK = "# The rows module compiles the generated copies of the game's sources"

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def guard_text():
    text = (TREE / "native" / "CMakeLists.txt").read_text(encoding="utf-8").replace("\r\n", "\n")
    at = text.find(MARK)
    return text[at:] if at >= 0 else ""


def configure(cmake, folder, source_lines, copy_lines, guard):
    """Configure a project with the source a.cpp and its copy b.cpp (the lines set their properties); (exit code, output)."""
    project = ["cmake_minimum_required(VERSION 3.24)", "project(guard_test LANGUAGES NONE)",
               "set(luma_copied_sources \"${CMAKE_CURRENT_SOURCE_DIR}/a.cpp\")", "set(luma_copied_copies \"${CMAKE_CURRENT_BINARY_DIR}/luma/b.cpp\")"]
    project += [l.replace("SRC", "\"${CMAKE_CURRENT_SOURCE_DIR}/a.cpp\"") for l in source_lines]
    project += [l.replace("COPY", "\"${CMAKE_CURRENT_BINARY_DIR}/luma/b.cpp\"") for l in copy_lines]
    project.append(guard)
    (folder / "CMakeLists.txt").write_text("\n".join(project) + "\n", encoding="utf-8")
    (folder / "a.cpp").write_text("", encoding="utf-8")
    r = subprocess.run([cmake, "-S", str(folder), "-B", str(folder / "out")], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    cmake = next((c for c in CMAKE_CANDIDATES if pathlib.Path(c).is_file()), shutil.which("cmake"))
    guard = guard_text()
    check(bool(guard), "the guard is at the end of native/CMakeLists.txt")
    check(cmake is not None, "cmake is found")
    if guard and cmake:
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            opt = "set_property(SOURCE %s APPEND PROPERTY COMPILE_OPTIONS \"-fno-sanitize=address\")"
            iquote = "set_property(SOURCE COPY APPEND PROPERTY COMPILE_OPTIONS \"-iquote/x\")"
            cases = [
                ("same", [opt % "SRC"], [opt % "COPY", iquote], True, ""),
                ("no option on the copy", [opt % "SRC"], [iquote], False, "COMPILE_OPTIONS"),
                ("one definition of two", ["set_property(SOURCE SRC APPEND PROPERTY COMPILE_DEFINITIONS \"A=1;B=1\")"],
                 ["set_property(SOURCE COPY APPEND PROPERTY COMPILE_DEFINITIONS \"A=1\")"], False, "COMPILE_DEFINITIONS"),
                ("no flags on the copy", ["set_property(SOURCE SRC PROPERTY COMPILE_FLAGS \"-O0\")"], [], False, "COMPILE_FLAGS"),
                ("flags kept", ["set_property(SOURCE SRC PROPERTY COMPILE_FLAGS \"-O0\")"], ["set_property(SOURCE COPY PROPERTY COMPILE_FLAGS \"-O0 -g\")"], True, ""),
                ("nothing on the source", [], [iquote], True, ""),
            ]
            for n, (name, src, copy, passes, word) in enumerate(cases):
                folder = tmp / ("case%d" % n)
                folder.mkdir()
                code, out = configure(cmake, folder, src, copy, guard)
                if passes:
                    check(code == 0, "%s: the configure passes: %s" % (name, out[-300:]))
                else:
                    check(code != 0 and "a.cpp" in out and word in out and "its copy for the rows module" in out,
                          "%s: the configure stops, naming the source and %s: %s" % (name, word, out[-400:]))

    if failures:
        print("cmake_copy_guard_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("cmake_copy_guard_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
