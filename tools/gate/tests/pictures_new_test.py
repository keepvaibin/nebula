"""pictures_new_test.py: the pictures' kinds, their approval at a landing, and the one list (tools/gate/pictures.py and
tools/gate/gate.py), without the game, a graphics card or the real picture store.

    python -X utf8 -I tools/gate/tests/pictures_new_test.py [--work FOLDER]

Everything is in temporary folders: the tree under test is a temporary git repository holding its own
tests/pictures/approved.tsv, the store is a temporary folder (gate.PICTURE_STORE is replaced), the runs are written here the
way luma_test.py writes them. Each case below plants one situation and reads the result back:

  new_picture_is_not_a_failure     a picture with no approved row is kind new: the check does not fail, the run is
                                   VERDICT pass, the picture is listed (new-pictures.tsv) with a sheet to look at
  approval_commit                  at the landing one commit on top of the tested one touches only approved.tsv (and removes
                                   PICTURES-CHANGE.txt), says "pictures: N approved for <tests>", puts the files in the
                                   store, and the picture checks run again on the same runs are all pass
  second_check_fails_moves_nothing a second check that is not all pass leaves the tree at the tested commit, the
                                   manifest and the declaration file as they were, and the gate's call returns the fault
  undeclared_difference_fails      a differing picture that nothing declares is fail: VERDICT fail
  declared_difference_approved     a differing picture the change declares is new; it is approved and its old row replaced
  unused_declaration_is_caught     a declaration that names no differing picture is tool (env when the tablet was owed),
                                   and so is a line that is not a declaration
  carried                          a step's landing (12 stops) passes a line no picture of its stops used, keeps the file on main
                                   with the used line marked; the section's check takes the marked line as used, removes the
                                   file at its approval, and still calls an unmarked line nothing used stale
  one_list_for_every_suite_name    a row approved from one run is found from a run that gave its suite another name, and
                                   for a test kept under tests/ by its own folder; rows written as "tour-tablet" still count
  places_do_not_collide            the PC's and the tablet's "vulkan" rows are two sets: each run is checked against its own
  tablet_pictures_are_compared     the tablet's pictures differ: fail; the tablet on another card: env, not judged
  driver_update                    after a driver change a picture within 1 of 255 per channel is approved for the new
                                   machine, one beyond it is listed as FAIL, another card maker approves nothing
  zero_pictures_is_tool            runs with no picture compared nothing: tool
  runner_names_the_test_path       luma_test.py's test_path_of: the folder under tests/, None outside
Exit 0 when every case holds.
"""
import contextlib
import importlib.util
import io
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent.parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402
import pictures  # noqa: E402

TOOL = HERE.parent / "pictures.py"
MANIFEST = "tests/pictures/approved.tsv"
CHANGES = "PICTURES-CHANGE.txt"
PC = ("Test GPU", "1.0")
TABLET = ("Tab GPU", "9")
A, B, C = (10, 20, 30), (200, 0, 0), (0, 90, 0)


class World:
    """A temporary world: the tree under test (a git repository with an approved list that names the machines), the picture
    store, and the gate's own folders."""

    def __init__(self, tmp):
        self.t = pathlib.Path(tmp)
        self.tree = self.t / "tree"
        self.store = self.t / "store"
        self.work = self.t / "work"
        self.build = self.t / "build"
        self.gates = 0
        for d in (self.tree / "tests" / "pictures", self.store, self.work, self.build / "bins"):
            d.mkdir(parents=True)
        pictures.write_manifest(self.tree / MANIFEST,
                                {("pc", "d3d12"): PC, ("pc", "vulkan"): PC, ("tablet", "vulkan"): TABLET}, {})
        (self.tree / "README.txt").write_text("the tree\n", encoding="utf-8")
        self.sh("init", "-q", "-b", "main")
        for k, v in (("user.name", "keepvaibin"), ("user.email", "122556909+keepvaibin@users.noreply.github.com"),
                     ("core.hooksPath", ".git/hooks"), ("commit.gpgsign", "false")):
            self.sh("config", k, v)
        self.commit("tests")
        self.saved = gate.PICTURE_STORE
        gate.PICTURE_STORE = str(self.store)

    def close(self):
        gate.PICTURE_STORE = self.saved

    def sh(self, *a):
        return subprocess.run([gate.GIT, "-C", str(self.tree), *a], capture_output=True, text=True)

    def commit(self, message):
        self.sh("add", "-A")
        self.sh("commit", "-q", "-m", message)
        return self.sh("rev-parse", "HEAD").stdout.strip()

    def head(self):
        return self.sh("rev-parse", "HEAD").stdout.strip()

    def declare(self, *lines):
        (self.tree / CHANGES).write_text("\n".join(lines) + "\n", encoding="utf-8")
        return self.commit("declare")

    def gate(self, level="main"):
        args = gate.parser().parse_args(["--" + level, "--tablet", "T", "--work", str(self.work), "--board", "", "--status", "",
                                         "--build-root", str(self.build), "--paks", "", "--keep-runs", "--wait", "0",
                                         "--gate-lock", str(self.t / "gate.lock")])
        g = gate.Gate(args)
        g.tree = self.tree
        self.gates += 1
        g.work = self.work / ("run-%d" % self.gates)
        g.work.mkdir()
        return g

    def run(self, g, test, colour, label="tour", renderer="d3d12", suite_dir=None, names=("arrive", "after"), machine=PC,
            platform="windows-x64", test_path=None, pixels=None, verdict="pass"):
        """A test's run as luma_test.py leaves it: runs-<label>-<renderer>/runs/<suite>/<test>/<renderer>."""
        folder = g.work / ("runs-%s-%s" % (label, renderer)) / "runs" / (suite_dir or label) / test / renderer
        shutil.rmtree(folder, ignore_errors=True)
        (folder / "pictures").mkdir(parents=True)
        result = {"test": test, "renderer": renderer, "verdict": verdict, "kind": "pass" if verdict == "pass" else "fail",
                  "gpu": '"%s" api=x' % machine[0], "driver": machine[1], "platform": platform}
        if test_path:
            result["test_path"] = test_path
        (folder / "result.json").write_text(json.dumps(result), encoding="utf-8")
        for name in names:
            image = Image.new("RGB", (8, 4), colour)
            for (x, y), c in (pixels or {}).items():
                image.putpixel((x, y), c)
            image.save(folder / "pictures" / (name + ".png"))
            image.save(folder / "pictures" / (name + ".ppm.display.png"))
        return folder

    def check(self, g, label="tour", renderer="d3d12", place="pc", name=None):
        """The pictures check of the gate on the runs; the row it left."""
        with contextlib.redirect_stdout(io.StringIO()):
            g.check(name or "pictures/%s/%s" % (label, renderer), gate.pictures(g, label, renderer, place))
        return g.results[-1]

    def finish(self, g):
        """The run's end (its verdict, new-pictures.tsv); (verdict, exit code, the result text)."""
        with contextlib.redirect_stdout(io.StringIO()):
            g.write()
        return g.verdict, g.code, (g.work / "result.txt").read_text(encoding="utf-8")

    def approve(self, g, *more):
        code, out = tool("approve", "--runs", g.work, "--manifest", self.tree / MANIFEST, "--store", self.store, *more)
        self.commit("approved")
        return code, out

    def rows(self):
        return pictures.read_manifest(self.tree / MANIFEST)[1]


def remove(path):
    """A folder removed, git's read-only files too."""
    def writable(function, name, _):
        os.chmod(name, 0o700)
        function(name)
    shutil.rmtree(path, onerror=writable)


def tool(*args):
    r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(TOOL)] + [str(a) for a in args], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def kinds_of(row):
    return sorted({r["kind"] for r in row["records"]})


def main():
    given = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    root = given / "pictures-new-case"
    if root.exists():
        remove(root)
    root.mkdir(parents=True)
    failed = []

    def check(ok, what, out=""):
        if not ok:
            failed.append("%s\n%s" % (what, out))

    def case(n):
        w = World(root / n)
        return w

    # --- a new picture is not a failure ---------------------------------------------------------------------------
    w = case("new")
    try:
        g = w.gate()
        w.run(g, "001-AAA-1", A)
        row = w.check(g)
        check(row["state"] == "new" and row["passed"] and kinds_of(row) == ["new"], "a picture with no row is new, not fail",
              str(row))
        verdict, code, text = w.finish(g)
        check(verdict == "pass" and code == 0, "a run with only new pictures passes", text)
        lines = (g.work / "new-pictures.tsv").read_text(encoding="utf-8").splitlines()
        check(len(lines) == 3 and lines[1].split("\t")[:6] == ["new", "pc", "tour", "001-AAA-1", "d3d12", "after"],
              "the new pictures are listed, complete enough to become rows", "\n".join(lines))
        check("2 new pictures" in text and "KIND new pictures/tour/d3d12 tour/001-AAA-1/d3d12/arrive" in text
              and (g.work / "new-pictures" / "sheet" / "review.html").exists(),
              "the summary names the count, the new ones, and the sheet", text)
        check(json.loads((g.work / "verdict.json").read_text(encoding="utf-8"))["new_pictures"]["count"] == 2,
              "verdict.json counts them")

        # --- the approval at the landing ------------------------------------------------------------------------
        tested = w.head()
        before = w.rows()
        landed, fault = gate.approve_pictures(g, tested, say=lambda text: None)
        check(fault is None and landed != tested and w.head() == landed, "the approval makes one commit", fault)
        files = w.sh("diff", "--name-only", tested, landed).stdout.split()
        check(files == [MANIFEST], "it touches only the approved list", str(files))
        check(w.sh("rev-list", "--count", tested + ".." + landed).stdout.strip() == "1", "and is one commit on the tested one")
        message = w.sh("log", "-1", "--format=%B", landed).stdout
        check(message.strip() == "pictures: 2 approved for 001-AAA-1" and "Co-Authored" not in message,
              "plain message: N approved for the tests", message)
        author = w.sh("log", "-1", "--format=%an <%ae>", landed).stdout.strip()
        check(author == "keepvaibin <122556909+keepvaibin@users.noreply.github.com>", "with the repository's own identity", author)
        rows = w.rows()
        check(len(rows) == len(before) + 2 and all(pictures.store_path(w.store, v[2]).exists() for v in rows.values()),
              "two rows added and their files in the store", str(rows))
        check(w.check(g)["state"] == "pass" and gate.picture_recheck(g) == [("pictures/tour/d3d12", "pass")],
              "the picture checks run again on the same runs are all pass")
        again = w.gate()
        check(gate.approve_pictures(again, landed) == (landed, None), "a run with nothing new approves nothing")
    finally:
        w.close()

    # --- a second check that is not all pass moves nothing --------------------------------------------------------
    w = case("second")
    try:
        tested = w.declare("tour/001-AAA-1 * pc d3d12 RMGE01 :: not needed")
        g = w.gate()
        w.run(g, "001-AAA-1", A)
        w.check(g)
        w.finish(g)
        saved = gate.picture_recheck
        gate.picture_recheck = lambda g: [("pictures/tour/d3d12", "fail")]
        try:
            landed, fault = gate.approve_pictures(g, tested, say=lambda text: None)
        finally:
            gate.picture_recheck = saved
        check(landed == tested and fault and "not all pass" in fault, "the gate's call returns the fault, not a commit", str(fault))
        check(w.head() == tested and w.sh("status", "--porcelain").stdout.strip() == "", "the tree is at the tested commit, clean",
              w.sh("status", "--porcelain").stdout)
        check(w.rows() == {} and (w.tree / CHANGES).exists(), "the list and the declaration file are as they were")
    finally:
        w.close()

    # --- a difference nothing declares is fail; one that is declared is approved ------------------------------------
    w = case("changed")
    try:
        g = w.gate()
        w.run(g, "001-AAA-1", A)
        w.approve(g)
        g = w.gate()
        w.run(g, "001-AAA-1", B)
        row = w.check(g)
        check(row["state"] == "fail" and not row["passed"] and kinds_of(row) == ["fail"]
              and "of 32 pixels" in row["records"][0]["message"], "an undeclared difference is fail", str(row))
        verdict, code, text = w.finish(g)
        check(verdict == "fail" and code == 1 and "new picture" not in text, "the run is VERDICT fail", text)

        # declared for arrive only: arrive is new, after still fails
        w.declare("tour/001-AAA-1 arrive pc d3d12 RMGE01 :: the sky changed")
        g = w.gate()
        w.run(g, "001-AAA-1", B)
        row = w.check(g)
        by = {r["test"]: r["kind"] for r in row["records"]}
        check(by == {"tour/001-AAA-1/d3d12/arrive": "new", "tour/001-AAA-1/d3d12/after": "fail"} and row["state"] == "fail",
              "a declared picture is new, an undeclared one beside it still fails", str(by))

        # declared for both: approved, its old row replaced, the declaration file removed in the same commit
        tested = w.declare("tour/* * * * * :: the sky changed", "# a comment")
        old = w.rows()
        g = w.gate()
        w.run(g, "001-AAA-1", B)
        row = w.check(g)
        check(row["state"] == "new" and kinds_of(row) == ["new"] and "declared: the sky changed" in row["records"][0]["message"],
              "both declared: new", str(row))
        w.finish(g)
        landed, fault = gate.approve_pictures(g, tested, say=lambda text: None)
        check(fault is None, "declared pictures are approved", fault)
        files = sorted(w.sh("diff", "--name-only", tested, landed).stdout.split())
        check(files == [CHANGES, MANIFEST] and not (w.tree / CHANGES).exists(), "the commit touches the list and removes the "
              "declaration file, nothing else", str(files))
        now = w.rows()
        key = ("pc", "tour", "001-AAA-1", "d3d12", "arrive")
        check(set(now) == set(old) and now[key][2] != old[key][2] and
              now[key][2] == pictures.pixel_hash(8, 4, Image.new("RGB", (8, 4), B).tobytes()),
              "the old row is replaced, not added to", str(now))
        check(gate.picture_recheck(g) == [("pictures/tour/d3d12", "pass")], "and the second check passes")
    finally:
        w.close()

    # --- a declaration that names no differing picture ----------------------------------------------------------------
    w = case("unused")
    try:
        g = w.gate()
        w.run(g, "001-AAA-1", A)
        w.approve(g)
        w.declare("tour/001-AAA-1 * pc d3d12 RMGE01 :: the sky changed", "tour/999-ZZZ-9 * * all * :: a stale line")
        g = w.gate()
        w.run(g, "001-AAA-1", B)
        w.check(g)
        with contextlib.redirect_stdout(io.StringIO()):
            ok = g.check("pictures/declared", gate.declared_check(g))
        row = g.results[-1]
        check(ok is False and row["state"] == "tool" and "line 2" in row["detail"] and "stale" in row["detail"]
              and "line 1" not in row["detail"], "a declaration that names no differing picture is tool", str(row))
        verdict, code, _ = w.finish(g)
        check(verdict == "not-judged" and code == 2, "the run is not judged: main does not move", verdict)
        g = w.gate()
        w.run(g, "001-AAA-1", B)
        w.check(g)
        with contextlib.redirect_stdout(io.StringIO()):
            g.owe("tablet/x", "the tablet is away")
            g.check("pictures/declared", gate.declared_check(g))
        check(g.results[-1]["state"] == "env", "with the tablet's part owed a stale line is env: it cannot be told",
              str(g.results[-1]))
        w.declare("this is not a declaration")
        g = w.gate()
        w.run(g, "001-AAA-1", B)
        row = w.check(g)
        check(row["state"] == "tool" and "line 1" in row["detail"] + str(row["records"]), "a line that is not one is tool",
              str(row))
    finally:
        w.close()

    # --- a step's landing carries the declarations to the section's full check (mend M-006) -------------------------------
    w = case("carried")
    try:
        g = w.gate()
        w.run(g, "001-AAA-1", A)
        w.run(g, "050-ZZZ-1", A)
        w.approve(g)
        w.declare("tour/001-AAA-1 * pc d3d12 RMGE01 :: the sky is another blue",
                  "tour/050-ZZZ-1 * pc d3d12 RMGE01 :: a stop the 12 quick stops do not play")
        g = w.gate()
        g.level = gate.LAND_LEVEL
        w.run(g, "001-AAA-1", B)
        w.check(g)
        with contextlib.redirect_stdout(io.StringIO()):
            ok = g.check("pictures/declared", gate.declared_check(g))
        row = g.results[-1]
        check(ok is not False and row["state"] == "pass" and "the section's full check judges it" in row["detail"],
              "at a step's landing a line no picture of the 12 stops used waits for the section's check", str(row))
        w.finish(g)
        tested = w.head()
        landed, fault = gate.approve_pictures(g, tested, say=lambda text: None)
        files = sorted(w.sh("diff", "--name-only", tested, landed).stdout.split())
        kept = (w.tree / CHANGES).read_text(encoding="utf-8").splitlines() if (w.tree / CHANGES).exists() else []
        check(fault is None and files == sorted([CHANGES, MANIFEST]), "the step's approval touches the list and the declarations",
              "%s %s" % (fault, files))
        check(kept == ["tour/001-AAA-1 * pc d3d12 RMGE01 :: the sky is another blue " + pictures.STEP_MARK,
                       "tour/050-ZZZ-1 * pc d3d12 RMGE01 :: a stop the 12 quick stops do not play"],
              "the file stays: the used line is marked, the other as it was", "\n".join(kept))
        # The section's check: the marked line is not stale; the other is used by the stop it names; the approval removes the file.
        g = w.gate()
        w.run(g, "001-AAA-1", B)
        w.run(g, "050-ZZZ-1", B)
        w.check(g)
        with contextlib.redirect_stdout(io.StringIO()):
            ok = g.check("pictures/declared", gate.declared_check(g))
        check(ok is not False and g.results[-1]["state"] == "pass", "the section's check takes both lines", str(g.results[-1]))
        w.finish(g)
        section_tested = w.head()
        landed, fault = gate.approve_pictures(g, section_tested, say=lambda text: None)
        check(fault is None and not (w.tree / CHANGES).exists(), "the section's approval removes the file", str(fault))
        # A line still unused at the section's check, never used by a step's landing, is stale there as before.
        w.sh("reset", "-q", "--hard", section_tested)
        g = w.gate()
        w.run(g, "001-AAA-1", B)
        w.check(g)
        with contextlib.redirect_stdout(io.StringIO()):
            ok = g.check("pictures/declared", gate.declared_check(g))
        row = g.results[-1]
        check(ok is False and row["state"] == "tool" and "line 2" in row["detail"] and "line 1" not in row["detail"],
              "at the section's check an unmarked line nothing used is stale", str(row))
    finally:
        w.close()

    # --- one list for every level and suite ---------------------------------------------------------------------------
    w = case("one-list")
    try:
        g = w.gate("quick")
        w.run(g, "001-AAA-1", A, suite_dir="tour")
        w.run(g, "boot", B, label="engine", suite_dir="engine", test_path="engine/boot", names=("title",))
        w.approve(g)
        rows = w.rows()
        check(("pc", "tour", "001-AAA-1", "d3d12", "arrive") in rows and ("pc", "engine", "boot", "d3d12", "title") in rows,
              "rows are filed under the tour and under the test's folder", str(sorted(rows)))
        g = w.gate("main")
        w.run(g, "001-AAA-1", A, suite_dir="the-nightlys-own-name")
        w.run(g, "boot", B, label="engine", suite_dir="another-name", test_path="engine/boot", names=("title",))
        r1, r2 = w.check(g), w.check(g, "engine")
        check(r1["state"] == "pass" and r2["state"] == "pass", "found from a run that gave its suite another name",
              "%s\n%s" % (r1, r2))
        # the same test name kept in another folder is another test
        w.run(g, "boot", B, label="engine", suite_dir="x", test_path="mods/boot", names=("title",))
        remove(g.work / "runs-engine-d3d12" / "runs" / "another-name")
        row = w.check(g, "engine", name="pictures/engine/other-folder")
        check(row["state"] == "new", "a test of the same name in another folder is another test", str(row))
        # rows written before the key was the test's place
        text = (w.tree / MANIFEST).read_text(encoding="utf-8")
        sha = pictures.pixel_hash(8, 4, Image.new("RGB", (8, 4), C).tobytes())
        pictures.put_in_store(w.store, w.run(w.gate(), "x", C, names=("c",)) / "pictures" / "c.png", sha)
        (w.tree / MANIFEST).write_text(text + "picture\ttablet\ttour-tablet\t001-AAA-1\tvulkan\tarrive\t8\t4\t%s\n" % sha,
                                       encoding="utf-8")
        g = w.gate()
        w.run(g, "001-AAA-1", C, label="tour-tablet", renderer="vulkan", machine=TABLET, platform="android-arm64",
              names=("arrive",))
        row = w.check(g, "tour-tablet", "vulkan", "tablet", "pictures/tablet")
        check(row["state"] == "pass", "a row filed as tour-tablet is found by the tablet's tour", str(row))
    finally:
        w.close()

    # --- the PC's and the tablet's rows for one renderer ------------------------------------------------------------------
    w = case("places")
    try:
        g = w.gate()
        w.run(g, "001-AAA-1", A, renderer="vulkan")
        w.run(g, "001-AAA-1", B, renderer="vulkan", label="tour-tablet", machine=TABLET, platform="android-arm64")
        code, out = w.approve(g)
        rows = w.rows()
        pc = rows.get(("pc", "tour", "001-AAA-1", "vulkan", "arrive"))
        tab = rows.get(("tablet", "tour", "001-AAA-1", "vulkan", "arrive"))
        check(code == 0 and pc and tab and pc[2] != tab[2], "one approval folder with both: two rows, one for each place", out)
        g = w.gate()
        w.run(g, "001-AAA-1", A, renderer="vulkan")
        w.run(g, "001-AAA-1", B, renderer="vulkan", label="tour-tablet", machine=TABLET, platform="android-arm64")
        r_pc = w.check(g, "tour", "vulkan", "pc")
        r_tab = w.check(g, "tour-tablet", "vulkan", "tablet", "pictures/tablet")
        check(r_pc["state"] == "pass" and r_tab["state"] == "pass", "each is checked against its own rows", "%s\n%s" % (r_pc, r_tab))
        # the tablet's picture changes: the PC's check does not notice, the tablet's does
        w.run(g, "001-AAA-1", C, renderer="vulkan", label="tour-tablet", machine=TABLET, platform="android-arm64")
        r_tab = w.check(g, "tour-tablet", "vulkan", "tablet", "pictures/tablet2")
        r_pc = w.check(g, "tour", "vulkan", "pc", "pictures/tour/pc2")
        check(r_tab["state"] == "fail" and r_pc["state"] == "pass", "the tablet's pictures really are compared with its rows",
              "%s\n%s" % (r_tab, r_pc))
        # the tablet's runs are never judged against the PC's card
        r_wrong = w.check(g, "tour-tablet", "vulkan", "pc", "pictures/tablet-as-pc")
        check(r_wrong["state"] == "env", "a tablet run checked as the PC is another machine: env", str(r_wrong))
        # place from the run: no --place given
        folder = g.work / "runs-tour-tablet-vulkan"
        code, out = tool("check", "--runs", folder, "--manifest", w.tree / MANIFEST, "--store", w.store)
        check(code == 1 and "changed  tour/001-AAA-1/vulkan/arrive" in out, "the place is the run's own when none is given", out)
    finally:
        w.close()

    # --- the tablet away or another tablet: env --------------------------------------------------------------------------
    w = case("tablet")
    try:
        g = w.gate()
        w.run(g, "001-AAA-1", A, renderer="vulkan", label="tour-tablet", machine=("Other GPU", "1"), platform="android-arm64")
        row = w.check(g, "tour-tablet", "vulkan", "tablet", "pictures/tablet")
        check(row["state"] == "env" and kinds_of(row) == ["env"], "a tablet on another card is env", str(row))
        verdict, code, text = w.finish(g)
        check(verdict == "not-judged" and code == 2, "the run is not judged", text)
        g = w.gate()
        with contextlib.redirect_stdout(io.StringIO()):
            g.owe("pictures/tablet", "R52 does not answer adb")
        verdict, code, text = w.finish(g)
        check(g.results[-1]["state"] == "owed" and verdict == "pass",
              "the tablet away is owed (env that does not count), as before", text)
    finally:
        w.close()

    # --- a driver change ------------------------------------------------------------------------------------------------------
    w = case("driver")
    try:
        g = w.gate()
        for test in ("001-AAA-1", "002-BBB-1", "003-CCC-1"):
            w.run(g, test, A)
        w.approve(g)
        g = w.gate()
        new = ("Test GPU", "2.0")
        w.run(g, "001-AAA-1", A, machine=new)
        w.run(g, "002-BBB-1", A, machine=new, pixels={(1, 1): (11, 20, 30)})          # off by 1: approved
        w.run(g, "003-CCC-1", A, machine=new, pixels={(2, 2): (10, 20, 40)}, names=("arrive", "after"))  # off by 10: listed
        args = ["--runs", g.work, "--manifest", w.tree / MANIFEST, "--store", w.store]
        row = w.check(g)
        check(row["state"] == "env", "before it: another driver is env", str(row))
        code, out = tool("driver-update", *args)
        check(code == 1 and "4 pictures approved for the new machine, 2 listed as FAIL" in out
              and "FAIL     tour/003-CCC-1/d3d12/arrive: largest difference 10" in out, "within 1 approved, beyond listed", out)
        machines, rows = pictures.read_manifest(w.tree / MANIFEST)
        check(machines[("pc", "d3d12")] == new, "the machine row names the new driver", str(machines))
        sha = pictures.pixel_hash(8, 4, Image.new("RGB", (8, 4), A).tobytes())
        moved = rows[("pc", "tour", "002-BBB-1", "d3d12", "arrive")][2]
        check(moved != sha and pictures.store_path(w.store, moved).exists(),
              "the picture within 1 has its new pixels as its row", moved)
        check(rows[("pc", "tour", "003-CCC-1", "d3d12", "arrive")][2] == sha, "the one beyond 1 keeps its old row")
        row = w.check(g, name="pictures/tour/d3d12-after")
        check(row["state"] == "fail" and sorted(r["test"] for r in row["records"]) ==
              ["tour/003-CCC-1/d3d12/after", "tour/003-CCC-1/d3d12/arrive"],
              "after it the listed pictures fail as changed, the others pass", str(row))
        code, out = tool("driver-update", *args)
        check(code == 0 and "nothing to do" in out, "run again: the rows already name the machine", out)
        # another card maker: nothing approved
        w2 = case("driver-maker")
        try:
            g2 = w2.gate()
            w2.run(g2, "001-AAA-1", A)
            w2.approve(g2)
            g2 = w2.gate()
            w2.run(g2, "001-AAA-1", A, machine=("Other Maker X", "5"))
            before = (w2.tree / MANIFEST).read_text(encoding="utf-8")
            code, out = tool("driver-update", "--runs", g2.work, "--manifest", w2.tree / MANIFEST, "--store", w2.store)
            check(code == 1 and "another card maker" in out and (w2.tree / MANIFEST).read_text(encoding="utf-8") == before,
                  "another card maker approves nothing", out)
        finally:
            w2.close()
    finally:
        w.close()

    # --- no picture at all ------------------------------------------------------------------------------------------------------
    w = case("zero")
    try:
        g = w.gate()
        w.run(g, "001-AAA-1", A, names=())
        row = w.check(g)
        check(row["state"] == "tool" and "no picture" in row["detail"] + str(row["records"]), "runs with no picture are tool", str(row))
    finally:
        w.close()

    # --- the runner names where a test is kept -------------------------------------------------------------------------------
    spec = importlib.util.spec_from_file_location("luma_test_paths", TREE / "tools" / "lumacore" / "luma_test.py")
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    check(runner.test_path_of(TREE / "tests" / "engine" / "boot") == "engine/boot", "test_path_of: the folder under tests/")
    check(runner.test_path_of(root / "x" / "001-AAA-1") is None, "test_path_of: None for a test written elsewhere")

    print("\n".join(failed) if failed else "pictures_new: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
