"""pictures_where_test.py: a declared picture is named by suite, stop, picture, place, renderer and disc (tools/gate/pictures.py),
without the game, a graphics card or the real picture store.

    python -X utf8 -I tools/gate/tests/pictures_where_test.py [--work FOLDER]

Each test was seen to fail with the thing it guards broken (named in a phrase after the test).
  the_form_has_five_words                 a three-word line of the old form (read as a declaration); a line with no reason
  names_come_from_the_suite_and_the_test  the disc left in the stop's name (tour-RMGJ01/RMGJ01-010-... not tour/010-...)
  a_line_names_one_place_renderer_disc_and_stop   the place left out of the key (the tablet's declaration declared the PC's picture);
                                          the disc left out (a Japanese-disc line declared the US picture)
  a_wild_card_names_every_one             `*` and `all` taken as a name
  declared_stops_are_the_tours            the stops of a line `tour/*` or `tour/010-*` taken from the list of the tour; an engine line
                                          counted as a stop
  a_difference_is_declared_only_where_a_line_names_it   five real runs (PC d3d12, PC Vulkan, tablet, Japanese disc, engine) each
                                          differing: with one line, that picture is new and the four others fail; the check exits 1
  a_line_nothing_uses_is_unused           a line for the tablet's picture when only the PC's differed counted as used
Exit 0 when every case holds.
"""
import argparse
import pathlib
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import pictures  # noqa: E402
import pictures_new_test as base  # noqa: E402


def declaration(text, tmp):
    path = pathlib.Path(tmp) / "PICTURES-CHANGE.txt"
    path.write_text(text, encoding="utf-8")
    return pictures.read_declarations(path)


class Form(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(base.remove, self.tmp)

    def test_the_form_has_five_words(self):
        lines = declaration("# a comment\n\ntour/010-CannonFleetGalaxy-1 arrive pc vulkan RMGE01 :: the sky is another blue\n"
                            "engine/boot * * * * :: the title fades\n", self.tmp)
        self.assertEqual([(l[0], l[1], l[2], l[3], l[4], l[5], l[6]) for l in lines],
                         [(3, "tour/010-CannonFleetGalaxy-1", "arrive", "pc", "vulkan", "RMGE01", "the sky is another blue"),
                          (4, "engine/boot", "*", "*", "*", "*", "the title fades")])
        for text, said in (("tour/010-CannonFleetGalaxy-1 * all :: the old form\n", "three words names no place and no disc"),
                           ("tour/010-CannonFleetGalaxy-1 * pc vulkan :: four words\n", "expected"),
                           ("tour/010-CannonFleetGalaxy-1 * pc vulkan RMGE01 ::\n", "expected"),
                           ("tour/010-CannonFleetGalaxy-1 * pc vulkan RMGE01 no separator\n", "expected")):
            with self.assertRaises(pictures.DeclarationError) as raised:
                declaration(text, self.tmp)
            self.assertIn(said, str(raised.exception))
            self.assertIn("<suite>/<stop> <picture> <place> <renderer> <disc>", str(raised.exception))

    def test_names_come_from_the_suite_and_the_test(self):
        self.assertEqual(pictures.stop_name("tour", "010-CannonFleetGalaxy-1"), "tour/010-CannonFleetGalaxy-1")
        self.assertEqual(pictures.stop_name("tour-RMGJ01", "RMGJ01-010-CannonFleetGalaxy-1"), "tour/010-CannonFleetGalaxy-1")
        self.assertEqual(pictures.stop_name("tour-RMGP01", "RMGP01-010-CannonFleetGalaxy-1"), "tour/010-CannonFleetGalaxy-1")
        self.assertEqual(pictures.stop_name("tour@1280x720", "010-CannonFleetGalaxy-1"), "tour@1280x720/010-CannonFleetGalaxy-1")
        self.assertEqual(pictures.stop_name("engine", "boot"), "engine/boot")
        self.assertEqual([pictures.disc_of(s) for s in ("tour", "tour-RMGP01", "tour-RMGJ01", "engine", "tour@1280x720")],
                         ["RMGE01", "RMGP01", "RMGJ01", "RMGE01", "RMGE01"])

    def test_a_line_names_one_place_renderer_disc_and_stop(self):
        one = declaration("tour/010-CannonFleetGalaxy-1 arrive pc vulkan RMGE01 :: x\n", self.tmp)
        named = lambda place="pc", suite="tour", test="010-CannonFleetGalaxy-1", picture="arrive", renderer="vulkan": bool(  # noqa: E731
            pictures.declared_by(one, place, suite, test, picture, renderer))
        self.assertTrue(named())
        self.assertFalse(named(place="tablet"), "the tablet's picture is not the PC's")
        self.assertFalse(named(renderer="d3d12"))
        self.assertFalse(named(renderer="d3d11"))
        self.assertFalse(named(picture="after"))
        self.assertFalse(named(test="011-CannonFleetGalaxy-2"))
        self.assertFalse(named(suite="tour-RMGJ01", test="RMGJ01-010-CannonFleetGalaxy-1"), "the Japanese disc's picture is not the US disc's")
        self.assertFalse(named(suite="tour-RMGP01", test="RMGP01-010-CannonFleetGalaxy-1"))
        self.assertFalse(named(suite="engine", test="010-CannonFleetGalaxy-1"))
        jp = declaration("tour/010-CannonFleetGalaxy-1 * pc * RMGJ01 :: x\n", self.tmp)
        self.assertTrue(pictures.declared_by(jp, "pc", "tour-RMGJ01", "RMGJ01-010-CannonFleetGalaxy-1", "after", "d3d11"))
        self.assertFalse(pictures.declared_by(jp, "pc", "tour", "010-CannonFleetGalaxy-1", "after", "d3d11"))
        tablet = declaration("tour/010-CannonFleetGalaxy-1 * tablet vulkan RMGE01 :: x\n", self.tmp)
        self.assertTrue(pictures.declared_by(tablet, "tablet", "tour", "010-CannonFleetGalaxy-1", "arrive", "vulkan"))
        self.assertFalse(pictures.declared_by(tablet, "pc", "tour", "010-CannonFleetGalaxy-1", "arrive", "vulkan"))

    def test_a_wild_card_names_every_one(self):
        every = declaration("tour/010-CannonFleetGalaxy-1 * * * * :: x\ntour/011-* * all all all :: y\n", self.tmp)
        for place in ("pc", "tablet"):
            for renderer in ("d3d12", "vulkan", "d3d11"):
                for suite, test in (("tour", "010-CannonFleetGalaxy-1"), ("tour-RMGP01", "RMGP01-010-CannonFleetGalaxy-1"),
                                    ("tour-RMGJ01", "RMGJ01-011-CannonFleetGalaxy-2")):
                    self.assertTrue(pictures.declared_by(every, place, suite, test, "after", renderer), (place, renderer, suite))
        self.assertFalse(pictures.declared_by(every, "pc", "tour", "012-CannonFleetGalaxy-3", "after", "d3d12"))

    def test_declared_stops_are_the_tours(self):
        tour = ["009-A-1", "010-CannonFleetGalaxy-1", "011-B-2", "012-C-3"]
        self.assertEqual(pictures.declared_stops(declaration("tour/010-* * * * * :: x\n", self.tmp), tour), ["010-CannonFleetGalaxy-1"])
        self.assertEqual(pictures.declared_stops(declaration("tour/* * * * * :: x\n", self.tmp), tour), tour)
        self.assertEqual(pictures.declared_stops(declaration("tour/012-C-3 * * * * :: x\ntour/009-A-1 * * * * :: y\n", self.tmp), tour),
                         ["009-A-1", "012-C-3"], "in the tour's order, each once")
        self.assertEqual(pictures.declared_stops(declaration("engine/* * * * * :: x\n", self.tmp), tour), [])
        self.assertEqual(pictures.declared_stops([], tour), [])


class Judging(unittest.TestCase):
    """Real runs, drawn the way luma_test.py leaves them, judged by pictures.judge against approved rows."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(base.remove, self.tmp)
        self.w = base.World(self.tmp)
        self.addCleanup(self.w.close)
        self.g = self.w.gate()

    def draw(self, colour):
        """Each place, renderer and disc one run: the PC's Direct3D 12 and Vulkan of the US tour, the Japanese disc's Direct3D 12,
        the tablet's Vulkan, and an engine test on Direct3D 12."""
        w, g = self.w, self.g
        w.run(g, "001-AAA-1", colour)
        w.run(g, "001-AAA-1", colour, renderer="vulkan")
        w.run(g, "RMGJ01-001-AAA-1", colour, label="tour-RMGJ01")
        w.run(g, "001-AAA-1", colour, label="tour-tablet", renderer="vulkan", platform="android-arm64", suite_dir="tour-tablet", machine=base.TABLET)
        w.run(g, "boot", colour, label="engine", test_path="engine/boot")

    def judged(self, work_dir):
        args = argparse.Namespace(manifest=self.w.tree / base.MANIFEST, store=self.w.store, changes=self.w.tree / base.CHANGES,
                                  runs=work_dir, renderer=None, place="auto", complete=False, out=None, new_out=None)
        return {i["key"] + ("" if i.get("place", "pc") == "pc" else " (%s)" % i["place"]): i["kind"]
                for i in pictures.judge(args) if "place" in i}

    def test_a_difference_is_declared_only_where_a_line_names_it(self):
        w, g = self.w, self.g
        self.draw(base.A)
        # All the machines' pictures approved from the first runs: the tablet's runs are filed by place, the PC's by renderer.
        code, out = base.tool("approve", "--runs", g.work, "--manifest", w.tree / base.MANIFEST, "--store", w.store)
        self.assertEqual(code, 0, out)
        w.commit("approved")
        again = w.gate()
        self.g = again
        self.draw(base.B)
        # No declaration: five differing runs, ten pictures, each changed.
        kinds = self.judged(again.work)
        self.assertEqual(sorted(set(kinds.values())), ["changed"], kinds)
        self.assertEqual(len(kinds), 10, kinds)
        w.declare("tour/001-AAA-1 arrive pc vulkan RMGE01 :: only the US disc's Vulkan picture")
        kinds = self.judged(again.work)
        declared = sorted(k for k, v in kinds.items() if v == "declared")
        self.assertEqual(declared, ["tour/001-AAA-1/vulkan/arrive"], "only the PC's Vulkan picture of the US disc, one picture")
        self.assertEqual(sum(1 for v in kinds.values() if v == "changed"), 9)
        # A line for the tablet declares the tablet's picture and not the PC's Vulkan picture of the same stop.
        w.declare("tour/001-AAA-1 arrive tablet vulkan RMGE01 :: only the tablet")
        kinds = self.judged(again.work)
        self.assertEqual(sorted(k for k, v in kinds.items() if v == "declared"), ["tour/001-AAA-1/vulkan/arrive (tablet)"])
        # A line for the Japanese disc declares its picture and no US picture.
        w.declare("tour/001-AAA-1 * pc d3d12 RMGJ01 :: only the Japanese disc")
        kinds = self.judged(again.work)
        self.assertEqual(sorted(k for k, v in kinds.items() if v == "declared"),
                         ["tour-RMGJ01/RMGJ01-001-AAA-1/d3d12/after", "tour-RMGJ01/RMGJ01-001-AAA-1/d3d12/arrive"])
        # One line with a wild card in each place names all ten.
        w.declare("* * * * * :: everything")
        self.assertEqual(sorted(set(self.judged(again.work).values())), ["declared"])

    def test_a_line_nothing_uses_is_unused(self):
        w = self.w
        entries = [{"kind": "declared", "declaration": "1"}, {"kind": "new", "declaration": "-"}]
        path = w.declare("tour/001-AAA-1 arrive pc vulkan RMGE01 :: used", "tour/001-AAA-1 arrive tablet vulkan RMGE01 :: nothing used it")
        self.assertEqual(pictures.unused_declarations(w.tree / base.CHANGES, entries),
                         [(2, "tour/001-AAA-1 arrive tablet vulkan RMGE01 :: nothing used it")])
        self.assertEqual(pictures.unused_declarations(w.tree / base.CHANGES, [{"kind": "declared", "declaration": "1,2"}]), [])
        del path


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("pictures_where_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
