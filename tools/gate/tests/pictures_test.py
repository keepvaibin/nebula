"""pictures_test.py: tools/gate/pictures.py on run folders written here, without the game.

python -X utf8 -I tools/gate/tests/pictures_test.py [--work <folder>]

- approved pictures, drawn again the same: check passes;
- one pixel changed by 1: check fails, names 1 pixel and difference 1, and writes the review page;
- a picture with none approved: "new" (it does not fail the check; pictures_new_test.py has the rest); an approved one
  the runs did not take: "missing" with --complete only;
- a failed run's pictures are refused by approve;
- runs on another driver: check fails on the machine row, approve refuses them without --new-machine, and runs of
  two drivers together always.
Exit 0 when every case holds.
"""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
TOOL = HERE.parent / "pictures.py"


def make_run(work, test, pixels, verdict="pass", driver="1.2.3", pictures=("arrive", "after")):
    run = work / "runs" / "tour" / test / "d3d12"
    if run.exists():
        shutil.rmtree(run)
    (run / "pictures").mkdir(parents=True)
    (run / "result.json").write_text(json.dumps({"test": test, "renderer": "d3d12", "verdict": verdict,
                                                 "gpu": '"Test GPU" vram-mb=1', "driver": driver}), encoding="utf-8")
    for name in pictures:
        image = Image.new("RGB", (8, 4), (10, 20, 30))
        for (x, y), colour in pixels.items():
            image.putpixel((x, y), colour)
        image.save(run / "pictures" / (name + ".png"))
        image.save(run / "pictures" / (name + ".ppm.display.png"))


def tool(*args):
    r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(TOOL)] + [str(a) for a in args],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    given = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    root = given / "pictures-case"
    if root.exists():
        shutil.rmtree(root)
    work, store, manifest = root / "work", root / "store", root / "approved.tsv"
    common = ["--runs", work, "--store", store, "--manifest", manifest]
    failed = []

    def check(ok, what, out=""):
        if not ok:
            failed.append("%s\n%s" % (what, out))

    make_run(work, "001-A-1", {})
    make_run(work, "002-B-1", {(1, 1): (200, 0, 0)})
    code, out = tool("approve", *common)
    check(code == 0 and "approved 4 pictures" in out, "approve takes the four shown pictures", out)
    check(".display" not in manifest.read_text(encoding="utf-8"), "the display copies are not approved")
    code, out = tool("check", *common, "--complete")
    check(code == 0 and "pictures: 4 of 4 the same" in out, "the same pictures pass", out)

    make_run(work, "002-B-1", {(1, 1): (200, 0, 0), (5, 2): (11, 20, 30)})
    code, out = tool("check", *common)
    check(code == 1 and "1 of 32 pixels, largest difference 1" in out, "one pixel off by one is seen", out)
    check((work / "pictures-review" / "review.html").exists(), "the review page is written", out)

    make_run(work, "002-B-1", {(1, 1): (200, 0, 0)}, pictures=("arrive",))
    make_run(work, "003-C-1", {})
    code, out = tool("check", *common)
    check(code == 0 and "new      tour/003-C-1/d3d12/arrive" in out and "missing" not in out,
          "a new picture is listed and does not fail (the gate approves it at the landing); without --complete one not taken "
          "is not missing", out)
    code, out = tool("check", *common, "--complete")
    check(code == 1 and "missing  tour/002-B-1/d3d12/after" in out, "with --complete one not taken is missing", out)

    shutil.rmtree(work / "runs" / "tour" / "003-C-1")
    make_run(work, "002-B-1", {(1, 1): (200, 0, 0)})
    make_run(work, "001-A-1", {(0, 0): (0, 0, 0)}, verdict="fail")
    code, out = tool("approve", *common)
    check(code == 1 and "refused  001-A-1/d3d12: the run did not pass" in out, "a failed run is refused", out)

    make_run(work, "001-A-1", {})
    make_run(work, "002-B-1", {(1, 1): (200, 0, 0)}, driver="9.9.9")
    code, out = tool("check", *common)
    check(code == 1 and "approved on Test GPU driver 1.2.3" in out, "another driver fails the machine row", out)
    code, out = tool("approve", *common, "--only", "002-B-1")
    check(code == 1 and "--new-machine" in out, "approve refuses another driver", out)
    code, out = tool("approve", *common, "--new-machine")
    check(code == 1 and "more than one adapter or driver" in out, "runs of two drivers are never approved together", out)
    make_run(work, "001-A-1", {}, driver="9.9.9")
    code, out = tool("approve", *common, "--new-machine")
    check(code == 0, "--new-machine takes them", out)
    code, out = tool("check", *common)
    check(code == 0, "approved on the new driver, they pass", out)

    # Places (N-1040): the same runs drawn on the tablet are checked against the tablet's rows, never the PC's.
    make_run(work, "001-A-1", {}, driver="tablet 1")
    make_run(work, "002-B-1", {(1, 1): (200, 0, 0)}, driver="tablet 1")
    code, out = tool("check", *common, "--place", "tablet")
    check(code == 1 and "no approved pictures for this renderer at tablet" in out,
          "a tablet run checked where only the PC's rows are fails by machine", out)
    code, out = tool("approve", *common, "--place", "tablet")
    check(code == 0 and "approved 4 pictures" in out, "approve --place tablet takes them beside the PC's", out)
    code, out = tool("check", *common, "--place", "tablet", "--complete")
    check(code == 0 and "pictures: 4 of 4 the same" in out, "against the tablet's rows they pass", out)
    code, out = tool("check", *common)
    check(code == 1 and "approved on Test GPU driver 9.9.9" in out,
          "the PC's rows are untouched: tablet runs checked as the PC's fail by machine", out)
    rows = manifest.read_text(encoding="utf-8").splitlines()
    check(any(r.startswith("machine\tpc\td3d12\tTest GPU\t9.9.9") for r in rows)
          and any(r.startswith("machine\ttablet\td3d12\tTest GPU\ttablet 1") for r in rows),
          "one machine row per place and renderer", "\n".join(rows[:8]))

    # A manifest written before places existed (no place column) is the PC's.
    old = root / "old.tsv"
    old.write_text("".join(r.replace("\tpc\t", "\t", 1) + "\n" for r in rows
                           if r.startswith(("machine\tpc\t", "picture\tpc\t"))), encoding="utf-8")
    make_run(work, "001-A-1", {}, driver="9.9.9")
    make_run(work, "002-B-1", {(1, 1): (200, 0, 0)}, driver="9.9.9")
    code, out = tool("check", "--runs", work, "--store", store, "--manifest", old, "--complete")
    check(code == 0 and "pictures: 4 of 4 the same" in out, "rows without a place read as the PC's", out)

    print("\n".join(failed) if failed else "pictures: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
