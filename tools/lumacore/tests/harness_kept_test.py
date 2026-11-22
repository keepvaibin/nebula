"""harness_kept_test.py: the game data kept on the tablet between runs (luma_test_android.py, B14 part 2) against a made-up device: a dictionary of
files with the device's own answers, no adb, no tablet.

python -X utf8 -I tools/lumacore/tests/harness_kept_test.py [--work <folder>]

Every branch of the brief:
- kept_and_right       the kept file of this key, the recorded size, the info file: nothing crosses the cable (no push, no hash), the app's
                       file is filled by a copy on the device
- none                 no kept copy: pushed once, its hash made on the device and equal to the PC's, the info file written (the size and the
                       hash), then copied in; the key and size are said in a `machine:` line
- kept_and_wrong       the kept file cut short (its size is not the recorded one): said, removed, pushed again; an info file with no kept file
                       and a kept file with no info file (the hash file missing) the same
- another_key          a kept file of another key is removed first (said), this key's pushed: one kept copy at a time
- hash_unequal         a push that arrives wrong (the device's hash is not the PC's): pushed once more; wrong twice: exit 3 with a `tool:` line
                       holding both values and nothing kept
- no_hash_tool         neither sha256sum nor md5sum: md5sum when sha256sum is missing; none: size only, and a `machine:` line says so
- no_room              less free room than the kept copy, the app's copy and 1 GB: the old way for this run (a temporary file pushed, copied in,
                       removed), nothing kept, a `machine:` line says so; room that is enough keeps it
- copy_into_the_app    run-as cp refused: the pipe through the shell is tried; both refused: a plain line; the app's file of another size: a plain line
- remove_data          `remove --data` removes the kept copy and its info file, nothing else
- key                  the key from the registry, else the folder's name, only safe characters; the names of the kept files
Exit 0 when every case holds.
"""
import hashlib
import importlib.util
import io
import pathlib
import shutil
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("luma_test_android", HERE.parent / "luma_test_android.py")
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)

GB = 1 << 30


class Tablet(android.Device):
    """A device that is a dictionary of files: path -> bytes. Its shell is not used; the primitives are answered here."""

    def __init__(self, free=100 * GB, tools=("sha256sum", "md5sum"), cp_refused=False, pipe_refused=False, corrupt_pushes=0, app_extra=0):
        super().__init__("fake")
        self.files, self.free, self.tools = {}, free, tools
        self.cp_refused, self.pipe_refused, self.corrupt_pushes, self.app_extra = cp_refused, pipe_refused, corrupt_pushes, app_extra
        self.pushes, self.copies, self.hashes, self.removed, self.shell_lines = [], [], [], [], []
        self.app = None

    def free_bytes(self, path="/data"):
        return self.free

    def size_of(self, path):
        return len(self.files[path]) if path in self.files else None

    def read_text(self, path):
        return self.files[path].decode() if path in self.files else None

    def write_text(self, path, text):
        self.files[path] = text.encode()
        return True

    def list_names(self, folder):
        return sorted(p.rsplit("/", 1)[1] for p in self.files if p.rsplit("/", 1)[0] == folder)

    def remove_files(self, *paths):
        for p in paths:
            self.removed.append(p)
            self.files.pop(p, None)

    def make_folder(self, folder):
        return True

    def move_file(self, source, target):
        self.files[target] = self.files.pop(source)
        return True

    def push_file(self, local, remote):
        data = pathlib.Path(local).read_bytes()
        self.pushes.append(remote)
        if self.corrupt_pushes > 0:
            self.corrupt_pushes -= 1
            data = b"X" + data[1:]
        self.files[remote] = data
        return True, ""

    def hash_of(self, path):
        self.hashes.append(path)
        for algorithm, tool, width in android.HASH_TOOLS:
            if tool in self.tools:
                return algorithm, hashlib.new(algorithm, self.files[path]).hexdigest()
        return None, None

    def copy_into_app(self, source):
        self.copies.append(source)
        if self.cp_refused and self.pipe_refused:
            return False, "cp: Permission denied; pipe: refused"
        self.app = self.files[source] + b"\0" * self.app_extra
        return True, ""

    def app_pak_size(self):
        return None if self.app is None else len(self.app)

    def shell(self, command, timeout=600):
        self.shell_lines.append(command)
        return 0, ""


class Kept(unittest.TestCase):
    def setUp(self):
        self.t = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(lambda: shutil.rmtree(self.t, ignore_errors=True))
        self.pak = self.t / "game.pak"
        self.pak.write_bytes(b"NBSTORE\0" + bytes(range(256)) * 40)
        self.size = self.pak.stat().st_size
        self.key = "RMGE01-37-3aa6533b1f08"
        self.kept, self.info = android.kept_paths(self.key)
        self.said = []

    def say(self, text):
        self.said.append(text)

    def prepare(self, device, key=None, **kw):
        android.push_pak(device, self.pak, key or self.key, self.say, **kw)
        return device

    def kept_ok(self, device):
        device.files[self.kept] = self.pak.read_bytes()
        device.files[self.info] = ("size %d\nhash sha256 %s\n" % (self.size, hashlib.sha256(self.pak.read_bytes()).hexdigest())).encode()

    def lines(self):
        return "\n".join(self.said)

    def test_none_pushes_hashes_and_copies(self):
        d = self.prepare(Tablet())
        self.assertEqual(d.pushes, [self.kept + ".part"], "pushed once, to the kept place")
        self.assertEqual(d.hashes, [self.kept + ".part"], "its hash made on the device")
        self.assertEqual(d.copies, [self.kept], "the app's file is a copy of the kept one, on the device")
        self.assertEqual(d.app, self.pak.read_bytes())
        digest = hashlib.sha256(self.pak.read_bytes()).hexdigest()
        self.assertEqual(d.files[self.info].decode(), "size %d\nhash sha256 %s\n" % (self.size, digest))
        self.assertEqual(sorted(d.files), sorted([self.kept, self.info]), "no .part left, no temporary file")
        self.assertIn("machine: game data kept on the tablet: %s, %s" % (self.key, android.gb(self.size)), self.lines())
        self.assertIn("pushed, then copied on the tablet", self.lines())

    def test_kept_and_right_crosses_no_cable(self):
        d = Tablet()
        self.kept_ok(d)
        self.prepare(d)
        self.assertEqual((d.pushes, d.hashes), ([], []), "nothing pushed, nothing hashed")
        self.assertEqual(d.copies, [self.kept])
        self.assertEqual(d.app, self.pak.read_bytes())
        self.assertIn("copied on the tablet", self.lines())
        self.assertIn("game data kept on the tablet: %s" % self.key, self.lines())
        # the second and third run are the same
        self.prepare(d)
        self.assertEqual(d.pushes, [])

    def test_kept_and_wrong_is_pushed_again(self):
        d = Tablet()
        self.kept_ok(d)
        d.files[self.kept] = d.files[self.kept][:-100]            # cut short
        self.prepare(d)
        self.assertEqual(d.pushes, [self.kept + ".part"])
        self.assertIn("is wrong (its size is %d, the recorded one %d, the pak's %d)" % (self.size - 100, self.size, self.size), self.lines())
        self.assertEqual(d.files[self.kept], self.pak.read_bytes())
        # the hash (info) file missing
        d = Tablet()
        self.kept_ok(d)
        del d.files[self.info]
        self.said.clear()
        self.prepare(d)
        self.assertEqual(d.pushes, [self.kept + ".part"])
        self.assertIn("it has no info file", self.lines())
        self.assertIn(self.info, d.files)
        # an info file that records another size than the pak's (the data changed under the same key)
        d = Tablet()
        self.kept_ok(d)
        d.files[self.info] = b"size 5\nhash none\n"
        self.prepare(d)
        self.assertEqual(len(d.pushes), 1)
        # an info file and no kept file
        d = Tablet()
        self.kept_ok(d)
        del d.files[self.kept]
        self.prepare(d)
        self.assertEqual(len(d.pushes), 1)

    def test_another_key_is_removed_first(self):
        d = Tablet()
        other = android.kept_paths("RMGE01-36-0000")
        d.files[other[0]], d.files[other[1]] = b"old data", b"size 8\nhash none\n"
        self.prepare(d)
        self.assertNotIn(other[0], d.files)
        self.assertNotIn(other[1], d.files)
        self.assertEqual(sorted(d.files), sorted([self.kept, self.info]), "one kept copy at a time")
        self.assertIn("removed the kept game data of another key: game-RMGE01-36-0000.pak", self.lines())
        self.assertLess(d.removed.index(other[0]), len(d.removed))
        self.assertEqual(len(d.pushes), 1)

    def test_a_push_that_arrives_wrong(self):
        d = Tablet(corrupt_pushes=1)
        self.prepare(d)
        self.assertEqual(len(d.pushes), 2, "pushed once more")
        self.assertIn("is not the PC's", self.lines())
        self.assertEqual(d.files[self.kept], self.pak.read_bytes())
        # wrong twice: exit 3 and a tool line with both values, nothing kept
        d = Tablet(corrupt_pushes=2)
        out = io.StringIO()
        saved, sys.stdout = sys.stdout, out
        try:
            with self.assertRaises(SystemExit) as caught:
                self.prepare(d)
        finally:
            sys.stdout = saved
        self.assertEqual(caught.exception.code, android.EXIT_TOOL)
        line = out.getvalue()
        self.assertTrue(line.startswith("tool: the game data on the tablet is not the PC's after two pushes"), line)
        self.assertIn(hashlib.sha256(self.pak.read_bytes()).hexdigest(), line, "the PC's value")
        self.assertIn(hashlib.sha256(b"X" + self.pak.read_bytes()[1:]).hexdigest(), line, "the tablet's value")
        self.assertEqual(len(d.pushes), 2)
        self.assertEqual(d.files, {}, "nothing kept, no half file left")
        self.assertIsNone(d.app)

    def test_the_hash_tool(self):
        d = self.prepare(Tablet(tools=("md5sum",)))
        self.assertTrue(d.files[self.info].decode().startswith("size %d\nhash md5 " % self.size))
        self.assertEqual(d.app, self.pak.read_bytes())
        self.said.clear()
        d = self.prepare(Tablet(tools=()))
        self.assertEqual(d.files[self.info].decode(), "size %d\nhash none\n" % self.size)
        self.assertIn("machine: the tablet has no sha256sum and no md5sum: the kept game data %s is checked by its size only" % self.key, self.lines())
        self.assertEqual(d.app, self.pak.read_bytes())
        # a size-only copy that arrives short is still caught
        d = Tablet(tools=())
        real = d.push_file

        def short(local, remote):
            real(local, remote)
            d.files[remote] = d.files[remote][:-1]
            return True, ""
        d.push_file = short
        out, saved = io.StringIO(), sys.stdout
        sys.stdout = out
        try:
            with self.assertRaises(SystemExit):
                self.prepare(d)
        finally:
            sys.stdout = saved
        self.assertIn("tool:", out.getvalue())

    def test_no_room_is_the_old_way(self):
        d = Tablet(free=2 * self.size + android.KEPT_SPARE_BYTES - 1)
        self.prepare(d)
        self.assertEqual(d.pushes, ["/data/local/tmp/luma-game.pak"], "pushed to a temporary file")
        self.assertEqual(d.copies, ["/data/local/tmp/luma-game.pak"])
        self.assertEqual(d.files, {}, "nothing kept, the temporary file removed")
        self.assertEqual(d.app, self.pak.read_bytes())
        self.assertIn("not enough room to keep the game data on the tablet", self.lines())
        self.assertIn("nothing is kept", self.lines())
        self.assertNotIn("game data kept on the tablet", self.lines())
        # exactly enough room keeps it
        d = Tablet(free=2 * self.size + android.KEPT_SPARE_BYTES)
        self.prepare(d)
        self.assertEqual(d.pushes, [self.kept + ".part"])
        # a tablet that does not say its room: it is kept
        d = Tablet(free=None)
        self.prepare(d)
        self.assertIn(self.kept, d.files)

    def test_copy_into_the_app(self):
        d = Tablet(cp_refused=True, pipe_refused=True)
        with self.assertRaises(SystemExit) as caught:
            self.prepare(d)
        self.assertIn("the pak could not be copied into the app's files: cp: Permission denied; pipe: refused", str(caught.exception))
        d = Tablet(app_extra=3)
        with self.assertRaises(SystemExit) as caught:
            self.prepare(d)
        self.assertEqual(str(caught.exception), "the device's game.pak is %d bytes, the pak %d" % (self.size + 3, self.size))

    def test_the_real_copy_tries_the_pipe_when_cp_is_refused(self):
        class Shell(android.Device):
            def __init__(self):
                super().__init__("fake")
                self.commands = []

            def run_as(self, command, timeout=600):
                self.commands.append("run-as: " + command)
                return 1, "cp: refused"

            def shell(self, command, timeout=600):
                self.commands.append("shell: " + command)
                return 0, ""
        d = Shell()
        self.assertEqual(d.copy_into_app("/data/local/tmp/nebula-test/game-x.pak"), (True, ""))
        self.assertEqual(len(d.commands), 2)
        self.assertTrue(d.commands[0].startswith("run-as: mkdir -p files/game && cp /data/local/tmp/nebula-test/game-x.pak files/game/game.pak.part"))
        self.assertIn("cat /data/local/tmp/nebula-test/game-x.pak | run-as dev.nebula.galaxy.test sh -c", d.commands[1])
        self.assertIn("cat > files/game/game.pak.part", d.commands[1])

    def test_remove_data_removes_the_kept_copy_only(self):
        d = Tablet()
        self.kept_ok(d)
        d.files["/data/local/tmp/other-file"] = b"not ours"
        d.files[KEPT_OTHER] = b"another thing in the folder"

        class Args:
            device = "fake"
            data = True
        real = android.Device
        android.Device = lambda serial: d
        d.answers = lambda: True
        out, saved = io.StringIO(), sys.stdout
        sys.stdout = out
        try:
            code = android.remove(Args())
        finally:
            sys.stdout = saved
            android.Device = real
        self.assertEqual(code, 0)
        self.assertEqual(sorted(d.files), sorted(["/data/local/tmp/other-file", KEPT_OTHER]), "the kept copy and its info file are gone, nothing else")
        self.assertIn("kept game data removed: game-%s.pak, game-%s.pak.info" % (self.key, self.key), out.getvalue())
        self.assertNotIn("uninstall", "\n".join(d.shell_lines), "the app is not touched")

    def test_the_key_and_the_names(self):
        registry = self.t / "registry.tsv"
        registry.write_text("key\tpath\thow\tmade\tlast used\nRMGE01-37-aaaaaaaaaaaa\t%s\tmade\tx\ty\n" % self.pak, encoding="utf-8")
        android.DATA_REGISTRY, saved = str(registry), android.DATA_REGISTRY
        try:
            self.assertEqual(android.kept_key(self.pak), "RMGE01-37-aaaaaaaaaaaa")
            other = self.t / "us pak 37" / "game.pak"
            other.parent.mkdir()
            other.write_bytes(b"x")
            self.assertEqual(android.kept_key(other), "us_pak_37", "not in the registry: the folder's name, safe characters only")
            self.assertEqual(android.kept_key(other, "A/B:c"), "A_B_c")
        finally:
            android.DATA_REGISTRY = saved
        self.assertEqual(android.kept_paths("K"), ("/data/local/tmp/nebula-test/game-K.pak", "/data/local/tmp/nebula-test/game-K.pak.info"))

    def test_the_report_of_what_is_kept(self):
        d = Tablet()
        self.assertIsNone(android.kept_report(d))
        self.kept_ok(d)
        self.assertEqual(android.kept_report(d), (self.key, self.size))
        d.files[self.kept] = d.files[self.kept][:-1]
        self.assertIsNone(android.kept_report(d), "a cut copy is not reported as kept")

    def test_the_info_file(self):
        self.assertEqual(android.read_info("size 12\nhash sha256 abc\n"), {"size": 12, "hash": ("sha256", "abc")})
        self.assertEqual(android.read_info("size 12\nhash none\n"), {"size": 12, "hash": None})
        self.assertIsNone(android.read_info(""))
        self.assertIsNone(android.read_info("hash sha256 abc\n"))
        self.assertIsNone(android.read_info("size twelve\n"))


KEPT_OTHER = "/data/local/tmp/nebula-test/notes.txt"


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("harness_kept_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    sys.exit(0 if result.wasSuccessful() else 1)
