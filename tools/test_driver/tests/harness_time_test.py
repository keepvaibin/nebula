"""harness_time_test.py: the harness's clock (a test's "time") and the keys that go with a real-time run, without the game.

python -X utf8 -I tools/test_driver/tests/harness_time_test.py [--work <folder>]

1. The runner (tools/lumacore/luma_test.py): a test without "time" takes fixed steps and its plan has no time line;
   "time": "real" writes `time real`; any other value, a "visible" that is not true or false, a setting that is not
   GALAXY_NAME=value and a diagnostic that is not a name are harness errors that name the key.
2. The host half (tools/test_driver/extension/harness.cpp, built with harness_selftest.cpp as harness_wait_test.py
   builds it): no time line reads as fixed steps, `time real` as real time, `time steps` as fixed steps, and a time
   line with another word or none is refused (the run does not start).
Exit 0 only when every check holds.
"""
import json
import pathlib
import subprocess
import sys
import tempfile

TREE = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(TREE / "tools" / "lumacore"))
sys.path.insert(0, str(TREE / "tools" / "lumacore" / "tests"))
import harness_wait_test  # noqa: E402
import luma_test  # noqa: E402

BASE = {"harness": 1, "name": "t", "about": "a", "bare": True, "steps": [{"at": 10, "end": True}],
        "expect": [{"no_log": "never"}]}


def read(work, name, extra):
    folder = work / name
    folder.mkdir()
    (folder / "test.json").write_text(json.dumps(dict(BASE, **extra)), encoding="utf-8")
    return luma_test.read_test(folder)


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    # 1. The runner.
    plain = read(work, "plain", {})
    check(plain["time"] == "steps" and not plain["visible"] and plain["settings"] == [] and plain["diagnostics"] == [],
          "a test without the keys: fixed steps, hidden, no settings, no diagnostics: %r" % (
              {k: plain[k] for k in ("time", "visible", "settings", "diagnostics")}))
    check("time real" not in luma_test.plan_text(plain, work / "out").splitlines(), "a fixed-step plan has no time line")
    real = read(work, "real", {"time": "real", "visible": True, "settings": ["GALAXY_VSYNC_INTERVAL=1"],
                               "diagnostics": ["present-stats", "timeline"]})
    check(real["time"] == "real" and real["visible"] and real["settings"] == ["GALAXY_VSYNC_INTERVAL=1"] and
          real["diagnostics"] == ["present-stats", "timeline"], "the real-time keys are kept as given")
    check("time real" in luma_test.plan_text(real, work / "out").splitlines(), "a real-time plan says `time real`")
    for n, (bad, key) in enumerate((({"time": "fast"}, "time"), ({"time": 1}, "time"), ({"visible": "yes"}, "visible"),
                                    ({"settings": ["VSYNC=1"]}, "settings"), ({"settings": "GALAXY_A=1"}, "settings"),
                                    ({"settings": ["GALAXY_A=1 2"]}, "settings"),
                                    ({"diagnostics": ["Present Stats"]}, "diagnostics"),
                                    ({"diagnostics": "timeline"}, "diagnostics"))):
        try:
            read(work, "bad%d" % n, bad)
            failed.append("no harness error for %r" % bad)
        except luma_test.HarnessError as error:
            check(key in str(error), "the error for %r names %s: %s" % (bad, key, error))

    # 2. The host half.
    exe = harness_wait_test.build(work)
    for name, line, want in (("none", None, "clock steps"), ("real", "time real", "clock real"),
                             ("steps", "time steps", "clock steps"), ("other", "time fast", None),
                             ("empty", "time", None)):
        out = work / ("half-" + name)
        out.mkdir()
        plan = out / "plan.txt"
        plan.write_text("harness 1\nout %s\nbare 1\n%sstep 5 end\n" % (out.as_posix(), line + "\n" if line else ""),
                        encoding="utf-8")
        result = subprocess.run([str(exe), str(plan), "10"], capture_output=True, text=True)
        said = (result.stdout.splitlines() or [""])[0]
        if want is None:
            check(result.returncode != 0 and said == "", "`%s` is refused: exit %d, %r" % (line, result.returncode, said))
        else:
            check(result.returncode == 0 and said == want, "%s: %s, got exit %d, %r (%s)" % (
                line or "no time line", want, result.returncode, said, result.stderr.strip()[-200:]))

    for f in failed:
        print("FAIL", f)
    print("harness_time_test: %s" % ("pass" if not failed else "%d failed" % len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
