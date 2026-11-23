"""agent_tour_test.py: tools/gate/agent_tour.py plays the tour and judges the pictures the way the gate does (brief AF), without the game, a
graphics card or the real picture store: the runner is a fake that writes a run folder the way luma_test.py does.

    python -X utf8 -I tools/gate/tests/agent_tour_test.py

Each test was seen to fail with the thing it guards broken (named in a phrase after the test).
  the_parts_of_the_set                the set `landing` without the other discs' Direct3D 11 and Vulkan when no line names a stop, with them
                                      for the named stops only; `whole` every renderer of every disc; the tablet's unit last
  the_right_case_passes               every picture the approved one: pass, exit 0
  an_undeclared_difference_is_fail    one picture differing, nothing declaring it: `fail` with its name and the line to write, exit 1,
                                      the sheet written; the same difference on the Japanese disc and on Vulkan
  a_declared_difference_passes        the line naming suite/stop, picture, place, renderer and disc: pass, counted as declared; a line for
                                      the same picture on another renderer or disc does not declare it
  a_line_nothing_uses_is_fail_in_a_set  a line that no differing picture uses: fail with --set, and no word of it for a part of the set
  no_picture_is_fail                  a run that took no picture compared nothing: fail
  a_stop_that_did_not_pass            the runner's exit 1 is exit 1; its exit 3 (the machine was not fit) is exit 3
  a_machine_that_is_not_the_approved_one  pictures of another card are not judged: exit 3
  the_tablets_unit                    the runner's command names the device and the test build, and the lock is taken and freed
Exit 0 when every case holds.
"""
import contextlib
import io
import json
import pathlib
import shutil
import sys
import tempfile
import unittest
from unittest import mock

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import agent_tour  # noqa: E402
import gate_tour  # noqa: E402
import pictures  # noqa: E402

A, B = (10, 20, 30), (200, 0, 0)
MACHINE = ("Test GPU", "1.0")
QUICK = agent_tour.quick_stops()


def stop_test(stop, disc=None):
    index = next(i for i, stage, scenario in gate_tour.stops() if "%s %d" % (stage, scenario) == stop)
    return gate_tour.test_for(index, *stop.split()[:1], int(stop.split()[1]), disc)[0]


class World:
    def __init__(self):
        self.t = pathlib.Path(tempfile.mkdtemp(prefix="agent-tour-"))
        self.store = self.t / "store"
        self.manifest = self.t / "approved.tsv"
        self.changes = self.t / "PICTURES-CHANGE.txt"
        self.work = self.t / "work"
        self.store.mkdir()
        self.colour = {}          # (renderer, disc) -> the colour a fake run draws, by default A
        self.differ = {}          # (renderer, disc, test) -> a colour
        self.codes = {}           # (renderer, disc) -> the runner's exit code
        self.machine = MACHINE
        self.calls = []

    def close(self):
        shutil.rmtree(self.t, ignore_errors=True)

    def approve(self, units):
        """Approved rows of every test of the units, colour A, each renderer and disc, and the machines."""
        machines, rows = {}, {}
        for renderer, disc, tests in units:
            machines[("pc", renderer)] = MACHINE
            for test in tests:
                for picture in ("arrive", "after"):
                    image = Image.new("RGB", (8, 4), A)
                    sha = pictures.pixel_hash(8, 4, image.tobytes())
                    target = pictures.store_path(self.store, sha)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    image.save(target)
                    suite = "tour" if disc == agent_tour.US_DISC else "tour-" + disc
                    rows[("pc", suite, test, renderer, picture)] = (8, 4, sha)
        pictures.write_manifest(self.manifest, machines, rows)

    def runner(self, cmd, **kwargs):
        """subprocess.run as the fake: the runner writes run folders, git answers."""
        self.calls.append(cmd)
        if "luma_test.py" not in " ".join(str(c) for c in cmd):
            return mock.Mock(returncode=0, stdout="abc123def\n", stderr="")
        suite = pathlib.Path(cmd[cmd.index("run") + 1])
        work = pathlib.Path(cmd[cmd.index("--work") + 1])
        renderer = cmd[cmd.index("--renderer") + 1]
        tests = json.loads(suite.read_text(encoding="utf-8"))["tests"]
        disc = next((d for d in agent_tour.OTHER_DISCS if tests and tests[0].startswith(d + "-")), agent_tour.US_DISC)
        for test in tests:
            folder = work / "runs" / suite.parent.name / test / renderer
            (folder / "pictures").mkdir(parents=True, exist_ok=True)
            (folder / "result.json").write_text(json.dumps({"test": test, "renderer": renderer, "verdict": "pass", "kind": "pass",
                                                            "gpu": '"%s" api=x' % self.machine[0], "driver": self.machine[1],
                                                            "platform": "windows-x64"}), encoding="utf-8")
            colour = self.differ.get((renderer, disc, test), self.colour.get((renderer, disc), A))
            for picture in ("arrive", "after"):
                Image.new("RGB", (8, 4), colour).save(folder / "pictures" / (picture + ".png"))
        return mock.Mock(returncode=self.codes.get((renderer, disc), 0), stdout="", stderr="")

    def go(self, *argv):
        args = ["--bin", "B/bins", "--pak", "US.pak", "--work", str(self.work), "--manifest", str(self.manifest), "--store", str(self.store),
                "--changes", str(self.changes)] + list(argv)
        out = io.StringIO()
        with mock.patch.object(agent_tour.subprocess, "run", self.runner), contextlib.redirect_stdout(out):
            code = agent_tour.main(args)
        return code, out.getvalue()


class Tour(unittest.TestCase):
    def setUp(self):
        self.w = World()
        self.addCleanup(self.w.close)
        self.quick = [stop_test(s) for s in QUICK]
        self.w.approve([("d3d12", agent_tour.US_DISC, self.quick)])

    def test_the_right_case_passes(self):
        code, out = self.w.go()
        self.assertEqual(code, 0, out)
        self.assertIn("agent_tour: pass: 24 pictures the same", out)
        self.assertNotIn("fail", out)

    def test_an_undeclared_difference_is_fail(self):
        w = self.w
        w.differ[("d3d12", agent_tour.US_DISC, self.quick[2])] = B
        code, out = w.go()
        self.assertEqual(code, 1, out)
        name = "tour/" + self.quick[2]
        self.assertIn("agent_tour: fail: 2 pictures differ from the approved picture and no line of PICTURES-CHANGE.txt declares them", out)
        self.assertIn("  %s arrive pc d3d12  " % name, out)
        self.assertIn("  %s after pc d3d12  " % name, out)
        self.assertIn("%s arrive pc d3d12 RMGE01 :: <why it changed>" % name, out, "the line to write")
        self.assertTrue((w.work / "pictures-review" / "review.html").exists(), "the sheet")
        self.assertNotIn("agent_tour: pass", out)

    def test_a_declared_difference_passes(self):
        w = self.w
        w.differ[("d3d12", agent_tour.US_DISC, self.quick[2])] = B
        name = "tour/" + self.quick[2]
        w.changes.write_text("%s arrive pc vulkan RMGE01 :: the wrong renderer\n%s arrive pc d3d12 RMGJ01 :: the wrong disc\n"
                             "%s arrive tablet d3d12 RMGE01 :: the wrong place\n" % (name, name, name), encoding="utf-8")
        code, out = w.go()
        self.assertEqual(code, 1, "a line for another renderer, disc or place declares nothing here: " + out)
        w.changes.write_text("%s * pc d3d12 RMGE01 :: the sky is another blue\n" % name, encoding="utf-8")
        code, out = w.go()
        self.assertEqual(code, 0, out)
        self.assertIn("agent_tour: pass: 22 pictures the same, 2 declared", out)

    def test_a_line_nothing_uses_is_fail_in_a_set(self):
        w = self.w
        w.changes.write_text("tour/001-BattleShipGalaxy-1 * * * * :: names a stop that does not differ\n", encoding="utf-8")
        code, out = w.go()
        self.assertEqual(code, 0, "a part of the set cannot tell a line stale: " + out)
        w.approve([(r, d, [stop_test(s, None if d == agent_tour.US_DISC else d) for s in gate_tour_stops()])
                   for r in agent_tour.RENDERERS for d in agent_tour.DISCS])
        code, out = w.go("--set", "whole", "--jobs", "1", "--disc-pak", "RMGP01=P.pak", "--disc-pak", "RMGJ01=J.pak")
        self.assertEqual(code, 1, out)
        self.assertIn("PICTURES-CHANGE.txt line 1 names no differing picture", out)

    def test_no_picture_is_fail(self):
        w = self.w

        def nothing(cmd, **kwargs):
            w.calls.append(cmd)
            return mock.Mock(returncode=0, stdout="abc123def\n", stderr="")
        out = io.StringIO()
        with mock.patch.object(agent_tour.subprocess, "run", nothing), contextlib.redirect_stdout(out):
            code = agent_tour.main(["--bin", "B/bins", "--pak", "US.pak", "--work", str(w.work), "--manifest", str(w.manifest),
                                    "--store", str(w.store), "--changes", str(w.changes)])
        self.assertEqual(code, 1)
        self.assertIn("the runs took no picture: nothing was compared", out.getvalue())

    def test_a_stop_that_did_not_pass(self):
        w = self.w
        w.codes[("d3d12", agent_tour.US_DISC)] = 1
        code, out = w.go()
        self.assertEqual(code, 1, out)
        self.assertIn("a stop did not pass", out)
        w.codes[("d3d12", agent_tour.US_DISC)] = 3
        self.assertEqual(w.go()[0], 3)

    def test_a_machine_that_is_not_the_approved_one(self):
        w = self.w
        w.machine = ("Another GPU", "9")
        code, out = w.go()
        self.assertEqual(code, 3, out)
        self.assertIn("not judged", out)
        self.assertNotIn("agent_tour: pass", out)


def gate_tour_stops():
    return ["%s %d" % (stage, scenario) for _, stage, scenario in gate_tour.stops()]


class Parts(unittest.TestCase):
    def setUp(self):
        self.t = pathlib.Path(tempfile.mkdtemp(prefix="agent-tour-parts-"))
        self.addCleanup(shutil.rmtree, self.t, True)
        self.changes = self.t / "PICTURES-CHANGE.txt"

    def units(self, *argv):
        a = agent_tour.parser().parse_args(["--bin", "B", "--pak", "P", "--work", str(self.t / "w"), "--changes", str(self.changes)] + list(argv))
        return [(u.label, u.renderer, None if u.stops is None else len(u.stops)) for u in agent_tour.units_of(a)]

    def test_the_parts_of_the_set(self):
        self.assertEqual(self.units(), [("tour", "d3d12", 12)])
        every = [("tour", r, None) for r in agent_tour.RENDERERS] + [("tour-RMGP01", "d3d12", None), ("tour-RMGJ01", "d3d12", None)]
        self.assertEqual(self.units("--set", "landing"), every, "no line names a stop: no sibling play")
        self.changes.write_text("tour/010-* * * * * :: x\n", encoding="utf-8")
        named = len([s for s in gate_tour.test_names() if s.startswith("010-")])
        self.assertEqual(named, 1)
        self.assertEqual(self.units("--set", "landing"),
                         every + [("tour-RMGP01", "vulkan", 1), ("tour-RMGP01", "d3d11", 1), ("tour-RMGJ01", "vulkan", 1), ("tour-RMGJ01", "d3d11", 1)])
        self.assertEqual(self.units("--set", "whole"), [(l, r, None) for l in ("tour", "tour-RMGP01", "tour-RMGJ01")
                                                         for r in agent_tour.RENDERERS])
        self.assertEqual(self.units("--renderer", "all", "--disc", "RMGJ01", "--stops", "declared"),
                         [("tour-RMGJ01", r, 1) for r in agent_tour.RENDERERS])
        with_tablet = agent_tour.units_of(agent_tour.parser().parse_args(
            ["--bin", "B", "--pak", "P", "--work", "w", "--set", "landing", "--tablet", "T", "--apk", "a", "--android-bin", "b"]))
        self.assertEqual((with_tablet[-1].label, with_tablet[-1].place, with_tablet[-1].renderer, len(with_tablet[-1].stops)),
                         ("tour-tablet", "tablet", "vulkan", 12))


class Tablet(unittest.TestCase):
    def test_the_tablets_unit(self):
        lock = pathlib.Path(tempfile.mkdtemp(prefix="agent-tour-lock-")) / "tablet.lock"
        self.addCleanup(shutil.rmtree, lock.parent, True)
        a = agent_tour.parser().parse_args(["--bin", "B", "--pak", "P", "--work", str(lock.parent / "w"), "--tablet", "T", "--apk", "A.apk",
                                            "--android-bin", "ABIN", "--tablet-lock", str(lock)])
        unit = agent_tour.Unit(agent_tour.US_DISC, "vulkan", ["HoneyBeeKingdomGalaxy 1"], place="tablet")
        calls, held = [], []

        def run(cmd, **kwargs):
            calls.append([str(c) for c in cmd])
            held.append(lock.exists())
            return mock.Mock(returncode=0)
        with mock.patch.object(agent_tour.subprocess, "run", run), mock.patch.object(agent_tour, "play", lambda *x: (0, [])):
            agent_tour.play_tablet_unit(a, unit, False, {agent_tour.US_DISC: "US.pak"}, "abc", print)
        self.assertEqual([c[5] for c in calls], ["prepare", "remove"])
        self.assertEqual(calls[0][calls[0].index("--device") + 1], "T")
        self.assertEqual(calls[0][calls[0].index("--pak") + 1], "US.pak")
        self.assertEqual(held, [True, True], "the lock is held while the app is installed and removed")
        self.assertFalse(lock.exists(), "and freed at the end")
        cmd = agent_tour.command("S/suite.json", "ABIN", None, "W", "vulkan", "abc", device="T")
        self.assertEqual(cmd[cmd.index("--device") + 1], "T")
        self.assertEqual(cmd[cmd.index("--bin") + 1], "ABIN")
        self.assertNotIn("--pak", cmd)


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("agent_tour_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
