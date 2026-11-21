"""gate_data_test.py: how gate.py asks for game data by key (tools/gate/gate.py data_check, package T4), without the game, an
importer, a disc or a build. tools/gate/tests/gamedata_test.py tests the key and the folder; here the gate's use of it.

    python -X utf8 -I tools/gate/tests/gate_data_test.py [--work FOLDER]

  data_found_names_the_pak        the check finds the tree's key: the US pak is the check's data, bin/pak.path names it, and
                                  the run's LUMA_TEST_PAKS carries the other discs' data
  data_made_on_demand             not found, it is made with the build's importer into the key's folder (a stand-in here)
  data_kinds                      no disc image is env (the run is not judged), no importer tool, a wrong result fail
  build_refuses_no_data           the build is started without -Pak (it asks for none and refuses none); --pak still names one;
                                  build_windows.ps1 has no mandatory -Pak and no exit on another store version
  checks_that_run_the_game_need_it   the checks that play the game need data/RMGE01 (and the discs' own for their tours); the unit
                                  tests, the start checks and the repository checks do not
  data_command                    gate.py data list|adopt|path
Exit 0 when every case holds.
"""
import contextlib
import io
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gamedata  # noqa: E402
import gate  # noqa: E402

GIT = gate.GIT


def header(store, disc):
    head = bytearray(64)
    head[:8] = b"NBSTORE\0"
    head[8:12] = int(store).to_bytes(4, "little")
    head[48:54] = disc.encode()
    return bytes(head)


class Cases(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.t = pathlib.Path(self.tmp.name)
        self.saved = (gamedata.ROOT, dict(gamedata.DISC_IMAGES), gamedata.GameData.run_program)
        gamedata.ROOT = self.t / "game-data"
        for disc in gamedata.DISC_IMAGES:
            (self.t / (disc + ".img")).write_bytes(b"IMG")
            gamedata.DISC_IMAGES[disc] = self.t / (disc + ".img")
        self.runs = []
        self.store = gamedata.tree_store_version(gate.TREE)

        def importer(cmd, log):
            self.runs.append([str(c) for c in cmd])
            out = pathlib.Path(cmd[4])
            out.mkdir(parents=True)
            disc = next(d for d, p in gamedata.DISC_IMAGES.items() if str(p) == str(cmd[2]))
            (out / "game.pak").write_bytes(header(self.wrong or self.store, disc))
            return self.exit
        self.wrong, self.exit = None, 0
        gamedata.GameData.run_program = staticmethod(importer)
        self.addCleanup(self.restore)
        (self.t / "bins").mkdir()
        (self.t / "bins" / "nebula-import.exe").write_bytes(b"stand-in")

    def restore(self):
        gamedata.ROOT, images, gamedata.GameData.run_program = self.saved
        gamedata.DISC_IMAGES.clear()
        gamedata.DISC_IMAGES.update(images)

    def gate(self, *extra):
        args = gate.parser().parse_args(["--main", "--tablet", "T", "--work", str(self.t / "work"), "--build-root", str(self.t),
                                         "--status", "", "--board", "", *extra])
        return gate.Gate(args)

    def run_check(self, g, disc):
        with contextlib.redirect_stdout(io.StringIO()):
            return g.check("data/%s" % disc, gate.data_check(g, disc), ("build/windows",))

    def test_data_made_on_demand_then_found(self):
        g = self.gate()
        self.assertTrue(self.run_check(g, "RMGE01"))
        row = g.results[-1]
        self.assertEqual((row["check"], row["state"]), ("data/RMGE01", "pass"))
        self.assertIn("made", row["detail"])
        key = row["detail"].split()[0]
        self.assertTrue(key.startswith("RMGE01-%d-" % self.store), key)
        pak = gamedata.ROOT / key / "game.pak"
        self.assertTrue(pak.is_file())
        self.assertEqual(g.pak, str(pak))
        self.assertEqual((self.t / "bins" / "pak.path").read_text(encoding="ascii").strip(), str(pak), "bin/pak.path names it")
        self.assertEqual(len(self.runs), 1)
        self.assertEqual(self.runs[0][1], "import")
        self.assertEqual(self.runs[0][2], str(self.t / "RMGE01.img"), "from the disc's image")
        g2 = self.gate()
        self.run_check(g2, "RMGE01")
        self.assertIn("found", g2.results[-1]["detail"])
        self.assertEqual(len(self.runs), 1, "found: the importer did not run")

    def test_the_other_discs_data_is_in_the_runs_paks(self):
        g = self.gate()
        for disc in ("RMGE01", "RMGP01"):
            self.run_check(g, disc)
        text = g.paks_text()
        self.assertEqual(text.split(os.pathsep).__len__(), 1)
        self.assertTrue(text.startswith("RMGP01="), text)
        self.assertNotIn("RMGE01", text, "the US data is the run's own pak")
        explicit = self.gate("--paks", "RMGP01=X/game.pak", "--pak", "Y/game.pak")
        self.assertEqual((explicit.paks_text(), explicit.pak), ("RMGP01=X/game.pak", "Y/game.pak"), "an explicit pak is used as it is")

    def test_data_kinds(self):
        g = self.gate()
        gamedata.DISC_IMAGES["RMGE01"] = self.t / "nothere.iso"
        self.assertFalse(self.run_check(g, "RMGE01"))
        self.assertEqual(g.results[-1]["state"], "env", "no disc image on this machine: not judged")
        self.assertEqual(self.runs, [])
        gamedata.DISC_IMAGES["RMGE01"] = self.t / "RMGE01.img"
        (self.t / "bins" / "nebula-import.exe").unlink()
        g = self.gate()
        self.run_check(g, "RMGE01")
        self.assertEqual(g.results[-1]["state"], "tool")
        (self.t / "bins" / "nebula-import.exe").write_bytes(b"stand-in")
        self.wrong = self.store + 1
        g = self.gate()
        self.run_check(g, "RMGE01")
        self.assertEqual(g.results[-1]["state"], "fail", "the importer made data that is not the key's")
        self.assertIn("store %d" % (self.store + 1), g.results[-1]["detail"])
        self.wrong, self.exit = None, 3
        g = self.gate()
        self.run_check(g, "RMGE01")
        self.assertEqual(g.results[-1]["state"], "fail")
        self.assertEqual(g.verdict, None)

    def test_build_refuses_no_data(self):
        seen = []
        saved = gate.run
        gate.run = lambda cmd, log, **k: (seen.append([str(c) for c in cmd]), 0)[1]
        try:
            for extra, want in (((), False), (("--pak", "D:/x/game.pak"), True)):
                g = self.gate(*extra)
                log = self.t / "build.log"
                log.write_text("", encoding="utf-8")
                ok, detail = gate.build_windows(g)(log)
                self.assertTrue(ok, detail)
                cmd = seen[-1]
                self.assertEqual("-Pak" in cmd, want, extra)
                if want:
                    self.assertEqual(cmd[cmd.index("-Pak") + 1], "D:/x/game.pak")
        finally:
            gate.run = saved
        script = (gate.HERE / "build_windows.ps1").read_text(encoding="utf-8")
        self.assertNotIn("Mandatory = $true)][string]$Pak", script, "-Pak is optional")
        self.assertNotIn("is store version $pv, the tree's is $tv\"; exit 1", script, "another store version is not refused")
        self.assertIn("pak.path", script)

    def test_checks_that_run_the_game_need_it(self):
        known, planned, _ = gate.read_lists()
        for level in ("quick", "main", "long"):
            args = gate.parser().parse_args(["--main", "--tablet", "T"])
            args.mode = level
            needs = gate.run_checks(args, known, planned, dry=True).needs
            self.assertIn("data/RMGE01", needs, level)
            self.assertEqual(tuple(needs["data/RMGE01"]), ("build/windows",))
            for name in ("tour/d3d12", "engine/vulkan", "tablet/prepare", "tour/tablet-pc"):
                if name in needs:
                    self.assertIn("data/RMGE01", needs[name], "%s %s" % (level, name))
            for name in ("unit/windows", "unit/game", "start/d3d12", "repo/code", "rust"):
                self.assertNotIn("data/RMGE01", needs.get(name, ()), name)
            if level != "long":
                self.assertIn("data/RMGP01", needs["tour-RMGP01/d3d12"])
        args = gate.parser().parse_args(["--main", "--tablet", "T", "--pak", "X"])
        args.mode = "main"
        self.assertNotIn("data/RMGE01", gate.run_checks(args, known, planned, dry=True).needs, "--pak names the data: no check")

    def test_data_command(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(gate.main(["data", "list", "--root", str(self.t / "game-data")]), 0)
            self.assertEqual(out.getvalue(), "")
        g = self.gate()
        self.run_check(g, "RMGE01")
        with contextlib.redirect_stdout(out):
            self.assertEqual(gate.main(["data", "list", "--root", str(self.t / "game-data")]), 0)
        self.assertIn("RMGE01-", out.getvalue())


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("gate_data_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
