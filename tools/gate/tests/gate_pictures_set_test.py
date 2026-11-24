"""gate_pictures_set_test.py: a landing that can move a picture takes every picture the main level compares, and plays each stop its
PICTURES-CHANGE.txt names on the other discs' Direct3D 11 and Vulkan too (brief AF), without the game, a graphics card or a tablet.

    python -X utf8 -I tools/gate/tests/gate_pictures_set_test.py

Each test was seen to fail with the thing it guards broken (named in a phrase after the test).
  the_level_of_a_landing          a step's game-code plan landed at the main level, or its section's check at the 12-stop level; a
                                  documents plan at the main level
  a_steps_landing_plays_the_quick_stops_everywhere_and_leaves_the_rest_to_the_section   a step's landing took the debug layers, a
                                  speed measure or another disc; or left out a renderer or the tablet; a section's check of documents
                                  ran less than the whole set
  stops_a_declaration_names       the stops of the tree's PICTURES-CHANGE.txt for the sibling plays: a line `tour/010-*` named one stop, an
                                  engine line none, a file that is not a declaration none
  no_sibling_without_a_line       the sibling checks registered with no declaration
  a_declared_stop_is_played_on_every_disc_and_renderer   the other discs' Vulkan and Direct3D 11 checks missing from the registered names
                                  of a main level with a line; or registered at the 12-stop level; or not taken by the landing's selection
  sibling_tours_hold_only_the_stops  a sibling tour written with every stop of the tour
  a_part_of_a_tour_is_not_missing_pictures  --complete on a sibling play (every approved picture of its tour it did not play failed)
  a_whole_tour_still_needs_every_picture    --complete taken off for a whole tour
Exit 0 when every case holds.
"""
import argparse
import json
import pathlib
import shutil
import sys
import tempfile
import types
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402
import gate_tour  # noqa: E402
import pictures  # noqa: E402
import scope  # noqa: E402

TABLE = scope.read_table()
NEW = ["tour-RMGP01/vulkan", "tour-RMGP01/d3d11", "tour-RMGJ01/vulkan", "tour-RMGJ01/d3d11"]


class Case(unittest.TestCase):
    def setUp(self):
        self.t = pathlib.Path(tempfile.mkdtemp(prefix="pictures-set-"))
        self.addCleanup(shutil.rmtree, self.t, True)
        self.changes = self.t / "PICTURES-CHANGE.txt"
        patch = mock.patch.object(pictures, "CHANGES_NAME", str(self.changes))   # an absolute name: the tree's own file is not touched
        patch.start()
        self.addCleanup(patch.stop)

    def declare(self, *lines):
        self.changes.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def dry(self, level):
        known, planned, _ = gate.read_lists()
        args = gate.parser().parse_args(["--main", "--tablet", "T"])
        args.mode = level
        d = gate.run_checks(args, known, planned, dry=True)
        shutil.rmtree(d.work, ignore_errors=True)
        return args, d


class Level(Case):
    def test_the_level_of_a_landing(self):
        plan = lambda *paths: scope.plan_for(list(paths), TABLE)  # noqa: E731
        self.assertEqual(gate.landing_level(plan("docs/plan/04-GATE-AND-MAIN.md"), []), gate.LAND_LEVEL)
        self.assertEqual(gate.landing_level(plan("tools/gate/gate.py", "tools/plan/plan.py"), []), gate.LAND_LEVEL)
        for paths in (("native/petari/src/Game/Foo.cpp",), ("runtime/src/render/a.cpp",), ("platform/sdl/a.cpp",),
                      ("crates/nebula-import/src/a.rs",), ("PICTURES-CHANGE.txt",), ("tests/replays/a.tsv",)):
            # A step's landing of a change that can move a picture plays the 12 quick stops (mend M-006); its section's check the main level.
            self.assertEqual(gate.landing_level(scope.step_plan(plan(*paths)), []), gate.LAND_LEVEL, paths)
            self.assertEqual(gate.landing_level(scope.section_plan(plan(*paths)), []), "main", paths)
        self.assertEqual(gate.landing_level(scope.section_plan(plan("docs/a.md")), []), "main", "a section's check of documents too")
        self.assertEqual(gate.landing_level(plan("docs/a.md"), ["stress"]), "main", "a check asked for by name keeps the main level")

    def test_a_steps_landing_plays_the_quick_stops_everywhere_and_leaves_the_rest_to_the_section(self):
        known, planned, _ = gate.read_lists()
        args = gate.parser().parse_args(["--main", "--tablet", "T"])

        def chosen(p):
            args.mode = gate.landing_level(p, [])
            return gate.landing_selection(args, p, [], known, planned)
        step = scope.step_plan(scope.plan_for(["runtime/src/render/a.cpp"], TABLE))
        got = chosen(step)
        must = {"engine/d3d12", "engine/vulkan", "engine/d3d11", "tour/d3d12", "tour/vulkan", "tour/d3d11", "replays/vulkan",
                "pictures/tour/d3d11", "tour/tablet", "pictures/tablet", "build/android", "pictures/declared", "unit/windows"}
        self.assertTrue(must <= got, sorted(must - got))
        section_only = {"validate/d3d12", "validate/vulkan", "measure/step/pc", "measure/step/tablet", "tour-RMGP01/d3d12",
                        "tour-RMGJ01/d3d12"}
        self.assertFalse(section_only & got, sorted(section_only & got))
        got = chosen(scope.section_plan(step))
        self.assertTrue((must | section_only) <= got, sorted((must | section_only) - got))
        got = chosen(scope.section_plan(scope.plan_for(["docs/a.md"], TABLE)))
        self.assertTrue((must | section_only) - {"unit/windows"} <= got,
                        "a section's check runs the whole set whatever its files: %s" % sorted((must | section_only) - got))


class Siblings(Case):
    def stops(self):
        return gate.sibling_stops(types.SimpleNamespace(tree=self.t))

    def test_stops_a_declaration_names(self):
        self.assertEqual(self.stops(), [], "no file")
        self.declare("tour/010-* * * * * :: x")
        self.assertEqual(self.stops(), ["CannonFleetGalaxy 1"])
        self.declare("tour/010-* * * * * :: x", "tour/001-BattleShipGalaxy-1 arrive pc vulkan RMGE01 :: y", "engine/boot * * * * :: z")
        self.assertEqual(self.stops(), ["BattleShipGalaxy 1", "CannonFleetGalaxy 1"], "in the tour's order, the engine line not a stop")
        self.declare("engine/boot * * * * :: z")
        self.assertEqual(self.stops(), [])
        self.declare("tour/010-CannonFleetGalaxy-1 * all :: the old form")
        self.assertEqual(self.stops(), [], "a line that is not a declaration is the picture checks' to say, not this one's")
        self.declare("tour/* * * * * :: everything")
        self.assertEqual(len(self.stops()), len(gate_tour.stops()))

    def test_no_sibling_without_a_line(self):
        _, d = self.dry("main")
        self.assertFalse([n for n in NEW if n in d.needs], sorted(d.needs))
        self.assertFalse([n for n in d.needs if n.startswith("pictures/tour-") and not n.endswith("/d3d12")])

    def test_a_declared_stop_is_played_on_every_disc_and_renderer(self):
        self.declare("tour/010-* * * * * :: x")
        args, d = self.dry("main")
        for name in NEW:
            self.assertIn(name, d.needs)
            self.assertIn("pictures/" + name, d.needs)
            self.assertIn("pictures/" + name, d.needs["pictures/declared"], "a line can name a sibling's picture")
            self.assertIn("data/" + name.split("/")[0].split("-")[1], d.needs[name], "its game data")
        for name in ("tour-RMGP01/d3d12", "tour-RMGJ01/d3d12", "tour/d3d11", "tour/vulkan", "tour/tablet", "pictures/tablet"):
            self.assertIn(name, d.needs, "the main level's own")
        # taken by a landing of a game-code plan, left out of one that takes no picture
        plan = scope.plan_for(["native/petari/src/Game/Foo.cpp"], TABLE)
        chosen = gate.landing_selection(args, plan, [], {}, {})
        for name in NEW + ["pictures/" + n for n in NEW]:
            self.assertIn(name, chosen)
        self.assertNotIn("validate/d3d12", chosen)
        docs = scope.plan_for(["docs/a.md"], TABLE)
        args.mode = gate.LAND_LEVEL
        self.assertFalse(set(NEW) & gate.landing_selection(args, docs, [], {}, {}))
        # the 12-stop level has no other disc and plays no sibling
        _, land = self.dry(gate.LAND_LEVEL)
        self.assertFalse([n for n in NEW if n in land.needs])

    def test_sibling_tours_hold_only_the_stops(self):
        self.declare("tour/010-* * * * * :: x", "tour/022-* * * * * :: y")
        g = types.SimpleNamespace(work=self.t / "work", tree=self.t)
        stops = gate.sibling_stops(g)
        tours = gate.write_tours(g, gate.QUICK_STOPS, gate.QUICK_REGION_STOPS, stops)
        for disc in gate.REGION_DISCS:
            names = json.loads(tours["sibling-" + disc].read_text(encoding="utf-8"))["tests"]
            self.assertEqual(names, ["%s-010-CannonFleetGalaxy-1" % disc, "%s-022-EggStarGalaxy-1" % disc])
            test = json.loads((tours["sibling-" + disc].parent / names[0] / "test.json").read_text(encoding="utf-8"))
            self.assertEqual(test["disc"], disc)
        self.assertNotIn("sibling-RMGE01", tours)
        self.assertEqual(len(json.loads(tours["tour-RMGP01"].read_text(encoding="utf-8"))["tests"]), 2, "the disc's own tour as it was")


class Complete(Case):
    def command(self, level, label, renderer, partial):
        seen = []

        def fake_run(cmd, log, **kwargs):
            seen.append([str(c) for c in cmd])
            pathlib.Path(log).write_text("pictures: 1 of 1 the same\n", encoding="utf-8")
            return 0
        g = types.SimpleNamespace(work=self.t, tree=self.t, level=level, partial={(label, renderer)} if partial else set())
        with mock.patch.object(gate, "run", fake_run):
            gate.picture_result(g, label, renderer, "pc", self.t / "log.txt")
        return seen[0]

    def test_a_part_of_a_tour_is_not_missing_pictures(self):
        self.assertNotIn("--complete", self.command("main", "tour-RMGP01", "vulkan", True))

    def test_a_whole_tour_still_needs_every_picture(self):
        self.assertIn("--complete", self.command("main", "tour-RMGP01", "d3d12", False))
        self.assertIn("--complete", self.command("main", "tour-RMGP01", "vulkan", False), "the same name, played whole")
        self.assertNotIn("--complete", self.command(gate.LAND_LEVEL, "tour", "d3d12", False), "the quick stops are never complete")


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("gate_pictures_set_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
