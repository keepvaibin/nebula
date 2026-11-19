"""harness_close_test.py: a run whose end step waits for a picture ends after that picture's present, not during it.

python -X utf8 -I tools/lumacore/tests/harness_close_test.py [--work <folder>]

Builds the host half with harness_selftest.cpp (as harness_wait_test.py does) and runs it with a present after each
frame (--presents). The host takes a picture's file at the present that shows it, and the end closes the window: an end
asked inside that present closed the window under it (a Vulkan queue then waited for good on the swap chain's image, so
no picture was written at exit). Cases:
- the end step at the frame of the last picture: "quit" only after a present that came after the picture's;
- the end step after the picture was taken: "quit" at once, with no present needed;
- no picture: the end at its own frame.
Exit 0 when every case holds.
"""
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import harness_wait_test  # noqa: E402


def run(exe, work, name, steps):
    out = work / name
    out.mkdir()
    (out / "pictures").mkdir()
    plan = out / "plan.txt"
    plan.write_text("harness 1\nout %s\nbare 1\n%s" % (out.as_posix(), steps), encoding="utf-8")
    result = subprocess.run([str(exe), "--presents", str(plan), "40"], capture_output=True, text=True)
    return [l.split(" ")[0] for l in result.stdout.splitlines() if not l.startswith("clock ")]


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)
    exe = harness_wait_test.build(work)
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    lines = run(exe, work, "end_with_picture", "step 5 picture first\nstep 5 end\n")
    check("picture" in lines and "quit" in lines, "the picture taken and the run ended: %s" % lines)
    # A line is printed once its present returns; "quit" is printed inside the present that ends the run.
    check("picture" in lines and "quit" in lines and lines.index("quit") > lines.index("picture"),
          "the end inside the present after the picture's, not inside the picture's own: %s" % lines)
    lines = run(exe, work, "end_later", "step 5 picture first\nstep 10 end\n")
    check(lines.count("picture") == 1 and lines[-1] == "quit" and lines.index("picture") < len(lines) - 2,
          "an end after the picture was taken: at its own frame: %s" % lines)
    lines = run(exe, work, "no_picture", "step 10 end\n")
    check(lines[-1] == "quit" and "picture" not in lines, "no picture: the end at its own frame: %s" % lines)

    print("\n".join(failed) if failed else "harness close: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
