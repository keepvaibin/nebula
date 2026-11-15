"""harness_wait_test.py: the host half's after-step waits ("after"), without the game.

python -X utf8 -I tools/lumacore/tests/harness_wait_test.py [--work <folder>]

Builds tools/test_driver/extension/harness.cpp with harness_selftest.cpp (MSVC on Windows, found by vswhere; c++
elsewhere), then runs one plan for each meaning of a wait on a fact:
- a fact written before the wait began holds at once (`exists`, `is`, `has`);
- `has` holds on an object that has every member asked for, and not on one that lacks one;
- `next` (the runner's "when": "next") holds only on a write made after the wait began;
- a check the half does not know is a harness error that names it.
Exit 0 when every case gives its verdict.
"""
import json
import os
import pathlib
import subprocess
import sys
import tempfile

TREE = pathlib.Path(__file__).resolve().parents[3]
SOURCES = [TREE / "tools/test_driver/extension/harness.cpp", TREE / "tools/test_driver/extension/harness_selftest.cpp"]
INCLUDES = [TREE / "native/include", TREE / "runtime/include", TREE / "tools/test_driver/extension"]


def build(work):
    exe = work / ("harness_selftest.exe" if os.name == "nt" else "harness_selftest")
    if os.name == "nt":
        vswhere = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        root = subprocess.run([str(vswhere), "-latest", "-property", "installationPath"], capture_output=True, text=True, check=True).stdout.strip()
        vcvars = pathlib.Path(root) / "VC/Auxiliary/Build/vcvars64.bat"
        flags = ["/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", "/permissive-", "/Fe:" + str(exe), "/Fo:" + work.as_posix() + "/"]
        flags += ["/I" + str(i) for i in INCLUDES] + [str(s) for s in SOURCES]
        script = work / "build.cmd"
        script.write_text('@call "%s" >nul\r\n@cl %s\r\n' % (vcvars, " ".join('"%s"' % f for f in flags)), encoding="utf-8")
        result = subprocess.run(["cmd", "/c", str(script)], capture_output=True, text=True)
    else:
        result = subprocess.run(["c++", "-std=c++20", "-Wall", "-Werror", "-o", str(exe)] + ["-I" + str(i) for i in INCLUDES]
                                + [str(s) for s in SOURCES] + ["-pthread"], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("build failed:\n" + result.stdout + result.stderr)
    return exe


def run(exe, work, name, cond, lines, frames=30):
    out = work / name
    out.mkdir()
    plan = out / "plan.txt"
    plan.write_text("harness 1\nout %s\nbare 1\nstep after end\ncond %s\ntimeout 10\n" % (out.as_posix(), cond), encoding="utf-8")
    code = subprocess.run([str(exe), str(plan), str(frames)] + lines, capture_output=True, text=True).returncode
    facts = [json.loads(l) for l in (out / "facts.jsonl").read_text(encoding="utf-8").splitlines() if l]
    ends = [f for f in facts if f["key"] in ("harness.end", "harness.stop")]
    return code, ends[-1] if ends else None


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)
    exe = build(work)
    ready = 'game: [mod m] fact ready {"frame":60,"stage":"RedBlueExGalaxy"}'
    again = '@5:game: [mod m] fact ready {"frame":61,"stage":"RedBlueExGalaxy"}'
    cases = [
        # name, cond, log lines, what ends the run: "end" (the step ran), "fail" (its wait timed out), or "error"
        ("exists_already", "fact mod.m.ready exists - true", [ready], "end"),
        ("is_already", 'fact mod.m.ready is - {"stage":"RedBlueExGalaxy","frame":60}', [ready], "end"),
        ("has_already", 'fact mod.m.ready has - {"frame":60}', [ready], "end"),
        ("has_lacking", 'fact mod.m.ready has - {"frame":60,"zone":1}', [ready], "fail"),
        ("has_other_value", 'fact mod.m.ready has - {"frame":61}', [ready], "fail"),
        ("has_not_object", 'fact mod.m.ready has - {"frame":60}', ["game: [mod m] fact ready 60"], "fail"),
        ("has_later", 'fact mod.m.ready has - {"frame":61}', [ready, again], "end"),
        ("next_old_write", 'next mod.m.ready has - {"frame":60}', [ready], "fail"),
        ("next_new_write", 'next mod.m.ready exists - true', [ready, again], "end"),
        ("unknown_check", 'fact mod.m.ready contains - {"frame":60}', [ready], "error"),
    ]
    failures = []
    for name, cond, lines, want in cases:
        code, end = run(exe, work, name, cond, lines)
        if end is None:
            got = "none"
        elif end["key"] == "harness.end":
            got = "end"
        else:
            got = "error" if end["value"]["verdict"] == "harness-error" else end["value"]["verdict"]
        if got != want:
            failures.append("%s: %s, expected %s (%s; exit %s)" % (name, got, want, end, code))
        if name == "unknown_check" and end is not None and "contains" not in end["value"].get("reason", ""):
            failures.append("%s: the reason does not name the check: %s" % (name, end))
    print("\n".join(failures) if failures else "harness waits: %d cases hold" % len(cases))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
