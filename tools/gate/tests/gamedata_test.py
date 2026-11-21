"""gamedata_test.py: game data by key (tools/gate/gamedata.py, package T4), without the game, an importer or a disc.

    python -X utf8 -I tools/gate/tests/gamedata_test.py [--work FOLDER]

Everything is temporary: a git repository as the tree (crates/nebula-import and runtime/include/galaxy/store_version.inc), a
folder as game-data, a stand-in importer that writes a tiny game.pak with the real header (the 64-byte header of cook/store.rs).

  key_found_then_made       a key not in the folder is made by the importer from the disc's image into <disc>-<store>-<12
                            characters of the importer's tree hash>/game.pak and registered; asked again it is found and the
                            importer does not run
  key_follows_the_importer  a changed crates/nebula-import or store version is another key: made again
  made_data_is_checked      the data's header must say the key's store version and disc; a stand-in that writes another
                            is a fail and leaves no folder
  kinds_of_not_had          no disc image is env, no importer tool, an importer that exits 3 fail (and leaves no part folder)
  adopted_by_reading        older folders are registered under the importer tree hash of the newest commit whose store
                            version they have (main, then the two branches); none is moved, changed or deleted; one that
                            matches none is left where it is and reported; again changes nothing
  removed_when_old          below the free-space floor, a folder made here that no run used for 14 days is removed; an adopted
                            folder is only unregistered; above the floor nothing is touched
  header_read               the store version and the disc from the header; a file that is no store is refused
Exit 0 when every case holds.
"""
import datetime
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gamedata  # noqa: E402

GIT = gamedata.GIT
IMPORTER = '''import pathlib, sys
args = sys.argv[1:]
assert args[0] == "import" and args[2] == "--output", args
image, out = pathlib.Path(args[1]), pathlib.Path(args[3])
store, disc, mode = [l.strip() for l in pathlib.Path(__file__).with_name("plan.txt").read_text().splitlines()[:3]]
if mode == "fail":
    sys.exit(3)
out.mkdir(parents=True)
head = bytearray(64)
head[:8] = b"NBSTORE\\0"
head[8:12] = int(store).to_bytes(4, "little")
head[48:54] = disc.encode()
(out / "game.pak").write_bytes(bytes(head) + image.read_bytes())
'''


def header(store, disc):
    head = bytearray(64)
    head[:8] = b"NBSTORE\0"
    head[8:12] = int(store).to_bytes(4, "little")
    head[48:54] = disc.encode()
    return bytes(head)


class World:
    def __init__(self, tmp):
        self.t = pathlib.Path(tmp)
        self.tree = self.t / "tree"
        self.root = self.t / "game-data"
        self.runs = []
        self.script = self.t / "importer.py"
        self.script.write_text(IMPORTER, encoding="utf-8")
        self.plan = self.t / "plan.txt"
        self.set_plan("37", "RMGE01", "ok")
        self.image = self.t / "disc.iso"
        self.image.write_bytes(b"DISC")
        self.tree.mkdir()
        self.sh("init", "-q", "-b", "main")
        for k, v in (("user.name", "keepvaibin"), ("user.email", "122556909+keepvaibin@users.noreply.github.com"),
                     ("commit.gpgsign", "false")):
            self.sh("config", k, v)
        self.commit("c1", store="37", importer="A")

    def set_plan(self, store, disc, mode):
        self.plan.write_text("%s\n%s\n%s\n" % (store, disc, mode), encoding="utf-8")

    def sh(self, *a):
        r = subprocess.run([GIT, "-C", str(self.tree), *a], capture_output=True, text=True)
        assert r.returncode == 0, "git %s: %s" % (" ".join(a), r.stderr)
        return r.stdout.strip()

    def commit(self, message, store=None, importer=None):
        if store is not None:
            p = self.tree / "runtime" / "include" / "galaxy" / "store_version.inc"
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(store + "\n", encoding="utf-8")
        if importer is not None:
            p = self.tree / "crates" / "nebula-import" / "src" / "lib.rs"
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text("// importer %s\n" % importer, encoding="utf-8")
        (self.tree / "notes.txt").write_text(message + "\n", encoding="utf-8")
        self.sh("add", "-A")
        self.sh("commit", "-q", "-m", message)

    def data(self, images=None):
        def run(cmd, log):
            self.runs.append([str(c) for c in cmd])
            r = subprocess.run([sys.executable, str(self.script), *[str(c) for c in cmd[1:]]], capture_output=True, text=True)
            with open(log, "ab") as f:
                f.write((r.stdout + r.stderr).encode())
            return r.returncode
        return gamedata.GameData(self.root, images if images is not None else {"RMGE01": self.image, "RMGP01": self.image}, GIT, run)

    def ensure(self, data, disc="RMGE01", importer=None, now=None):
        return data.ensure(disc, self.tree, importer or self.script, self.t / "make.log", now)

    def tree_hash(self):
        return self.sh("rev-parse", "HEAD:crates/nebula-import")


class Cases(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.w = World(self.tmp.name)

    def test_key_found_then_made(self):
        w = self.w
        data = w.data()
        pak, how, key = w.ensure(data)
        self.assertEqual((how, key), ("made", "RMGE01-37-" + w.tree_hash()[:12]))
        self.assertEqual(pak, w.root / key / "game.pak")
        self.assertEqual(gamedata.read_header(pak), (37, "RMGE01"))
        self.assertEqual(len(w.runs), 1)
        self.assertEqual(w.runs[0][1:3], ["import", str(w.image)], "from the disc's image")
        self.assertTrue(w.runs[0][4].endswith(key + ".part"), "into a folder of its own until whole")
        self.assertFalse((w.root / (key + ".part")).exists())
        row = data.registry.find(key)
        self.assertEqual((row["how"], pathlib.Path(row["path"])), ("made", pak))
        pak2, how2, key2 = w.ensure(w.data())
        self.assertEqual((pak2, how2, key2), (pak, "found", key))
        self.assertEqual(len(w.runs), 1, "found: the importer did not run")

    def test_the_key_is_twelve_characters_of_the_tree_hash(self):
        self.assertEqual(gamedata.key_of("RMGP01", 40, "0123456789abcdef0123"), "RMGP01-40-0123456789ab")

    def test_key_follows_the_importer_and_the_store_version(self):
        w = self.w
        data = w.data()
        _, _, key1 = w.ensure(data)
        w.commit("importer changes", importer="B")
        _, how, key2 = w.ensure(data)
        self.assertEqual(how, "made")
        self.assertNotEqual(key1, key2)
        self.assertTrue((w.root / key1 / "game.pak").is_file() and (w.root / key2 / "game.pak").is_file(), "the old one stays")
        w.set_plan("38", "RMGE01", "ok")
        w.commit("store 38", store="38")
        _, how, key3 = w.ensure(data)
        self.assertEqual((how, key3[:10]), ("made", "RMGE01-38-"))
        w.commit("docs only")  # nothing under the importer changed: the same key
        self.assertEqual(w.ensure(data)[1], "found")

    def test_made_data_is_checked(self):
        w = self.w
        data = w.data()
        w.set_plan("36", "RMGE01", "ok")
        with self.assertRaises(gamedata.DataError) as caught:
            w.ensure(data)
        self.assertEqual(caught.exception.kind, "fail")
        self.assertIn("store 36", str(caught.exception))
        w.set_plan("37", "RMGP01", "ok")
        with self.assertRaises(gamedata.DataError) as caught:
            w.ensure(data)
        self.assertIn("RMGP01", str(caught.exception))
        self.assertEqual([p.name for p in w.root.iterdir() if p.name != "registry.tsv" and not p.name.endswith(".lock")], [],
                         "no folder is left for data that is not the key's")

    def test_kinds_of_not_had(self):
        w = self.w
        with self.assertRaises(gamedata.DataError) as caught:
            w.ensure(w.data(images={}))
        self.assertEqual(caught.exception.kind, "env", "no disc image on this machine")
        with self.assertRaises(gamedata.DataError) as caught:
            w.ensure(w.data(), importer=w.t / "missing.exe")
        self.assertEqual(caught.exception.kind, "tool", "no importer")
        w.set_plan("37", "RMGE01", "fail")
        with self.assertRaises(gamedata.DataError) as caught:
            w.ensure(w.data())
        self.assertEqual(caught.exception.kind, "fail", "the importer exited 3")
        self.assertIn("exited 3", str(caught.exception))
        self.assertEqual([p.name for p in w.root.iterdir() if p.suffix == ".part"], [])

    # --- the older folders -----------------------------------------------------------------------------------------------

    def old_world(self):
        """main: c1 (store 37, importer A), c2 (importer B); b39 = main + c3 (39, C); b40 = b39 + c4 (40, D)."""
        w = self.w
        w.commit("c2", importer="B")
        self.hash37 = w.tree_hash()
        w.sh("checkout", "-q", "-b", "b39")
        w.commit("c3", store="39", importer="C")
        self.hash39 = w.tree_hash()
        w.sh("checkout", "-q", "-b", "b40")
        w.commit("c4", store="40", importer="D")
        self.hash40 = w.tree_hash()
        w.sh("checkout", "-q", "main")
        build = w.t / "build"
        folders = {}
        for name, store, disc in (("us-pak-37", 37, "RMGE01"), ("us-pak-39", 39, "RMGE01"), ("us-pak-40", 40, "RMGE01"),
                                  ("a1-pak-pal-37", 37, "RMGP01"), ("a1-pak-jp-40", 40, "RMGJ01"), ("us-pak-36", 36, "RMGE01")):
            (build / name).mkdir(parents=True)
            (build / name / "game.pak").write_bytes(header(store, disc) + name.encode())
            folders[name] = build / name
        (build / "us-pak-notstore").mkdir()
        (build / "a1-pak-pal-37-other").mkdir()
        (build / "us-pak-41").mkdir()
        (build / "us-pak-41" / "game.pak").write_bytes(b"not a store")
        return build, folders

    def test_adopted_by_reading(self):
        w = self.w
        build, folders = self.old_world()
        before = {n: (f / "game.pak").read_bytes() for n, f in folders.items()}
        stamps = {n: (f / "game.pak").stat().st_mtime_ns for n, f in folders.items()}
        data = w.data()
        report = data.adopt(w.tree, data.legacy_folders(build), refs=("main", "b39", "b40"))
        by = {pathlib.Path(f).name: (key, text) for f, key, text in report}
        self.assertEqual(sorted(by), ["a1-pak-jp-40", "a1-pak-pal-37", "us-pak-36", "us-pak-37", "us-pak-39", "us-pak-40", "us-pak-41"],
                         "the folders named like data folders (the ones with another name or no digits are not touched)")
        self.assertEqual(by["us-pak-37"][0], "RMGE01-37-" + self.hash37[:12], "the newest commit of main with store 37")
        self.assertEqual(by["a1-pak-pal-37"][0], "RMGP01-37-" + self.hash37[:12])
        self.assertEqual(by["us-pak-39"][0], "RMGE01-39-" + self.hash39[:12], "found on the branch")
        self.assertEqual(by["us-pak-40"][0], "RMGE01-40-" + self.hash40[:12])
        self.assertEqual(by["a1-pak-jp-40"][0], "RMGJ01-40-" + self.hash40[:12])
        self.assertIsNone(by["us-pak-36"][0])
        self.assertIn("left where it is", by["us-pak-36"][1])
        self.assertIn("no commit", by["us-pak-36"][1])
        self.assertIsNone(by["us-pak-41"][0])
        self.assertIn("not a game store", by["us-pak-41"][1])
        for n, f in folders.items():
            self.assertEqual((f / "game.pak").read_bytes(), before[n], n)
            self.assertEqual((f / "game.pak").stat().st_mtime_ns, stamps[n], "%s is only read" % n)
        rows = {r["key"]: r for r in data.registry.read()}
        self.assertEqual(len(rows), 5)
        self.assertEqual(pathlib.Path(rows[by["us-pak-37"][0]]["path"]), folders["us-pak-37"] / "game.pak")
        self.assertEqual({r["how"] for r in rows.values()}, {"adopted"})
        # the importer of main at that tree finds its data: no cook
        pak, how, key = w.ensure(data)
        self.assertEqual((how, key), ("found", "RMGE01-37-" + self.hash37[:12]))
        self.assertEqual(pak, folders["us-pak-37"] / "game.pak")
        self.assertEqual(w.runs, [])
        again = data.adopt(w.tree, data.legacy_folders(build), refs=("main", "b39", "b40"))
        self.assertTrue(all("already registered" in t for _, k, t in again if k), "adopting again changes nothing")
        self.assertEqual(len(data.registry.read()), 5)

    def test_a_store_version_that_is_not_the_newest_run_of_its_branch(self):
        w = self.w
        build, folders = self.old_world()
        data = w.data()
        report = data.adopt(w.tree, [folders["us-pak-37"]], refs=("b39",))
        # on b39 the store 37 run ends before c3's bump: its last commit is main's c2
        self.assertEqual(report[0][1], "RMGE01-37-" + self.hash37[:12])

    def test_the_default_folders_are_the_builds_with_a_store_version_in_the_name(self):
        w = self.w
        build, folders = self.old_world()
        names = [f.name for f in w.data().legacy_folders(build)]
        self.assertEqual(names, ["a1-pak-jp-40", "a1-pak-pal-37", "us-pak-36", "us-pak-37", "us-pak-39", "us-pak-40", "us-pak-41"])

    # --- removal ---------------------------------------------------------------------------------------------------------------

    def test_removed_when_old(self):
        w = self.w
        build, folders = self.old_world()
        data = w.data()
        _, _, made_key = w.ensure(data)
        data.adopt(w.tree, [folders["us-pak-39"]], refs=("main", "b39", "b40"))
        old = datetime.datetime.now() - datetime.timedelta(days=20)
        data.registry.change(lambda rows: [dict(r, **{"last used": gamedata.stamp(old)}) for r in rows])
        enough = 400 * 10 ** 9
        self.assertEqual(data.clean(enough), [], "with room to spare nothing is touched")
        self.assertTrue((w.root / made_key / "game.pak").is_file())
        done = dict(data.clean(100 * 10 ** 9))
        self.assertIn("removed", done[made_key])
        self.assertFalse((w.root / made_key).exists(), "the folder the gate made is removed")
        adopted_key = [k for k in done if k != made_key][0]
        self.assertIn("unregistered", done[adopted_key])
        self.assertTrue((folders["us-pak-39"] / "game.pak").is_file(), "an adopted folder is never deleted")
        self.assertEqual(data.registry.read(), [])

    def test_a_folder_used_lately_stays(self):
        w = self.w
        data = w.data()
        _, _, key = w.ensure(data)
        data.registry.change(lambda rows: [dict(r, **{"last used": gamedata.stamp(datetime.datetime.now() - datetime.timedelta(days=13))})
                                           for r in rows])
        self.assertEqual(data.clean(0), [], "13 days is not 14")
        w.ensure(data)  # a find touches it
        self.assertLess((datetime.datetime.now() - datetime.datetime.fromisoformat(data.registry.find(key)["last used"])).total_seconds(), 60)

    def test_header_read(self):
        w = self.w
        p = w.t / "x.pak"
        p.write_bytes(header(37, "RMGJ01") + b"data")
        self.assertEqual(gamedata.read_header(p), (37, "RMGJ01"))
        p.write_bytes(b"short")
        with self.assertRaises(ValueError):
            gamedata.read_header(p)
        with self.assertRaises(ValueError):
            gamedata.read_header(w.t / "missing.pak")

    def test_the_command_lists_and_adopts(self):
        w = self.w
        build, folders = self.old_world()
        import contextlib
        import io
        out = io.StringIO()
        data = w.data()
        for f, k, t in data.adopt(w.tree, [folders["us-pak-37"]], refs=("main",)):
            pass
        with contextlib.redirect_stdout(out):
            self.assertEqual(gamedata.main(["list", "--root", str(w.root)]), 0)
        self.assertIn("RMGE01-37-", out.getvalue())
        self.assertIn("adopted", out.getvalue())


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("gamedata_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
