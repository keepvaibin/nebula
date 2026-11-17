"""harness_steps_test.py: the host half's window, lose_device and key steps, without the game.

python -X utf8 -I tools/lumacore/tests/harness_steps_test.py [--work <folder>]

Builds the host half with harness_selftest.cpp (as harness_wait_test.py does) and runs short plans:
- with a host that has the call, window and lose_device reach it, at their frames, and the run ends;
- with a host that lacks it, window, lose_device and key are harness errors that say so (a step the host cannot do is
  never passed over in silence);
- a window of 0 pixels or with one size is refused when the plan is read;
- the runner (luma_test.py) writes the plan lines for "window" and "lose_device", and refuses malformed ones.
Exit 0 when every case holds.
"""
import json
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import harness_wait_test  # noqa: E402
import luma_test  # noqa: E402


def run(exe, work, name, steps, host):
    out = work / name
    out.mkdir()
    plan = out / "plan.txt"
    plan.write_text("harness 1\nout %s\nbare 1\n%s" % (out.as_posix(), steps), encoding="utf-8")
    result = subprocess.run([str(exe)] + (["--host"] if host else []) + [str(plan), "40"], capture_output=True, text=True)
    facts_file = out / "facts.jsonl"
    facts = [json.loads(l) for l in facts_file.read_text(encoding="utf-8").splitlines() if l] if facts_file.exists() else []
    ends = [f for f in facts if f["key"] in ("harness.end", "harness.stop")]
    printed = [l for l in result.stdout.splitlines() if not l.startswith("clock ")]
    return ends[-1] if ends else None, printed


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)
    exe = harness_wait_test.build(work)
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    end, printed = run(exe, work, "host_has", "step 5 window 640 360\nstep 10 lose_device\nstep 20 end\n", True)
    check(end is not None and end["key"] == "harness.end", "with the calls, the run ends: %s" % end)
    check(printed == ["window 640 360", "lose_device"], "with the calls, both reach the host: %s" % printed)
    for name, step, words in (("no_window", "window 640 360", "cannot size its window"),
                              ("no_lose", "lose_device", "cannot lose its graphics device"),
                              ("no_key", "key enter", "takes no menu keys")):
        end, printed = run(exe, work, name, "step 5 %s\nstep 20 end\n" % step, False)
        check(end is not None and end["key"] == "harness.stop" and end["value"]["verdict"] == "harness-error"
              and words in end["value"]["reason"], "%s without the call: a harness error saying so: %s" % (step, end))
    for name, step in (("window_zero", "window 0 360"), ("window_one", "window 640")):
        end, printed = run(exe, work, name, "step 5 %s\nstep 20 end\n" % step, True)
        check(end is not None and end["value"].get("verdict") == "harness-error" and printed == [],
              "%s refused when the plan is read: %s %s" % (step, end, printed))

    steps = luma_test.read_steps({"steps": [{"at": 5, "window": [640, 360]}, {"at": 10, "lose_device": True},
                                            {"at": 20, "end": True}]}, True, None)
    check([s["line"] for s in steps] == ["window 640 360", "lose_device", "end"], "the runner's plan lines: %s" % steps)
    for bad in ({"at": 5, "window": [0, 360]}, {"at": 5, "window": [640]}, {"at": 5, "window": [640.5, 360]},
                {"at": 5, "lose_device": 1}):
        try:
            luma_test.read_steps({"steps": [bad, {"at": 20, "end": True}]}, True, None)
            check(False, "the runner took %s" % bad)
        except luma_test.HarnessError:
            pass

    print("\n".join(failed) if failed else "harness steps: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
