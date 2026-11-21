"""gamedata.py: the game data the tests run on, found by key (package T4), without the game.

    python -X utf8 -I tools/gate/gamedata.py list | adopt [--folders F ...] | path DISC | make DISC | clean [--min-free-gb N]

One folder, D:/NebulaWork/build/game-data/, with one sub-folder per disc, store version and importer:

    <disc>-<store version>-<first 12 characters of the git tree hash of crates/nebula-import>/game.pak

for example RMGE01-37-0f3a9c1d2b4e. The gate asks for a disc's data by that key (gate.py: the check data/<disc>): found, it is
used; not found, it is made there with the tree's own importer (nebula-import import IMAGE --output FOLDER) from the disc
image of that disc, never from a Downloads folder. The build refuses nothing for a wrong data version any more: the gate asks
for the right data and writes the bin folder's pak.path itself.

registry.tsv (key, path, how, made, last used) lists every key the gate knows and where its game.pak is. The older folders of
the agents' work (us-pak-37, a1-pak-pal-37, a1-pak-jp-37 and the 39 and 40 ones) are adopted once, by reading and registering,
never moved or deleted here: each one's store version is read from its header (the 64-byte header of cook/store.rs: the magic
NBSTORE, the version at byte 8, the disc's id at byte 48) and is registered under the importer tree hash of the newest commit,
on origin/main or on the branches f4/e3-1c and f4/e3-2a, whose runtime/include/galaxy/store_version.inc says that version. A
folder that matches none is left where it is and reported.

Folders the gate made here that no run has used for 14 days are removed when D: has under 300 GB free; a folder registered
from outside (adopted) is only unregistered, never deleted.
"""
import datetime
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import landing  # noqa: E402 - the lock folder both use

ROOT = pathlib.Path("D:/NebulaWork/build/game-data")
BUILD = pathlib.Path("D:/NebulaWork/build")
# The disc image each disc's data is made from: the US one from the image the E3 scripts use, the others from the
# agents' region discs. Never from a Downloads folder.
DISC_IMAGES = {"RMGE01": pathlib.Path("D:/NebulaWork/testimg/SMG-rebuilt.iso"),
               "RMGP01": pathlib.Path("D:/NebulaWork/discs/RMGP01.rvz"),
               "RMGJ01": pathlib.Path("D:/NebulaWork/discs/RMGJ01.rvz")}
# The older folders: name prefix, then the store version.
LEGACY_PREFIX = {"us-pak-": "RMGE01", "a1-pak-pal-": "RMGP01", "a1-pak-jp-": "RMGJ01"}
# origin/main, then the two branches of the E3 steps (on origin when pushed, else the repository's own branches)
ADOPT_REFS = ("origin/main", "origin/f4/e3-1c", "f4/e3-1c", "origin/f4/e3-2a", "f4/e3-2a")
STORE_FILE = "runtime/include/galaxy/store_version.inc"
IMPORTER_DIR = "crates/nebula-import"
UNUSED_DAYS = 14
FREE_FLOOR_BYTES = 300 * 10 ** 9
GIT = next((p for p in ("C:/Program Files/Git/cmd/git.exe",) if os.path.exists(p)), "git")
REGISTRY_COLUMNS = ("key", "path", "how", "made", "last used")
MAGIC = b"NBSTORE"
HEADER_BYTES = 64


class DataError(Exception):
    """The data could not be had. `kind` is what a check of it is: fail (the importer ran and its result is wrong), tool (the
    importer is not there or could not start) or env (the disc image is not on this machine, the drive is full)."""

    def __init__(self, text, kind="fail"):
        Exception.__init__(self, text)
        self.kind = kind


def stamp(now=None):
    return (now or datetime.datetime.now()).isoformat(timespec="seconds")


# The key ---------------------------------------------------------------------------------------------------------------------


def key_of(disc, store, importer_tree):
    """<disc>-<store version>-<first 12 characters of the importer's tree hash>."""
    return "%s-%d-%s" % (disc, int(store), importer_tree[:12])


def store_version_text(text):
    m = re.search(r"\d+", text)
    if m is None:
        raise ValueError("no store version in %r" % text[:40])
    return int(m.group(0))


def tree_store_version(tree):
    return store_version_text((pathlib.Path(tree) / STORE_FILE).read_text(encoding="utf-8"))


def git_out(git, tree, *args):
    r = subprocess.run([git, "-C", str(tree), *args], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def importer_tree_hash(git, tree, rev="HEAD"):
    """The git tree hash of crates/nebula-import at `rev` ('' when there is none)."""
    return git_out(git, tree, "rev-parse", "%s:%s" % (rev, IMPORTER_DIR))


def read_header(pak):
    """(store version, disc id) from a game.pak's 64-byte header. ValueError when it is not a store."""
    try:
        with open(pak, "rb") as f:
            head = f.read(HEADER_BYTES)
    except OSError as error:
        raise ValueError("%s does not open: %s" % (pak, error))
    if len(head) < HEADER_BYTES or head[:7] != MAGIC:
        raise ValueError("%s is not a game store (no NBSTORE header)" % pak)
    return int.from_bytes(head[8:12], "little"), head[48:54].decode("ascii", "replace")


# The registry ----------------------------------------------------------------------------------------------------------------------


class Registry:
    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.lock = landing.FolderLock(str(self.path) + ".lock")

    def read(self):
        try:
            text = self.path.read_text(encoding="utf-8")
        except FileNotFoundError:
            return []
        rows = []
        for line in text.splitlines()[1:]:
            cells = line.split("\t")
            if len(cells) == len(REGISTRY_COLUMNS):
                rows.append(dict(zip(REGISTRY_COLUMNS, cells)))
        return rows

    def change(self, fn):
        if not self.lock.take("game-data registry", timeout=60.0):
            raise RuntimeError("the registry's lock %s is held" % self.lock.path)
        try:
            rows = fn(self.read())
            self.path.parent.mkdir(parents=True, exist_ok=True)
            text = "\t".join(REGISTRY_COLUMNS) + "\n" + "".join("\t".join(r[c] for c in REGISTRY_COLUMNS) + "\n" for r in rows)
            tmp = self.path.with_name(self.path.name + ".tmp%d" % os.getpid())
            tmp.write_text(text, encoding="utf-8", newline="\n")
            os.replace(tmp, self.path)
        finally:
            self.lock.free()

    def find(self, key):
        return next((r for r in self.read() if r["key"] == key), None)

    def put(self, key, path, how, now=None):
        def fn(rows):
            rows = [r for r in rows if r["key"] != key]
            rows.append({"key": key, "path": str(path), "how": how, "made": stamp(now), "last used": stamp(now)})
            return rows
        self.change(fn)

    def touch(self, key, now=None):
        def fn(rows):
            for r in rows:
                if r["key"] == key:
                    r["last used"] = stamp(now)
            return rows
        self.change(fn)

    def drop(self, keys):
        self.change(lambda rows: [r for r in rows if r["key"] not in keys])


# The data ---------------------------------------------------------------------------------------------------------------------------


class GameData:
    """The folder of game data by key. `run(cmd, log)` starts the importer and returns its exit code (tests give their own)."""

    def __init__(self, root=None, images=None, git=GIT, run=None, clock=time.time):
        self.root = pathlib.Path(root or ROOT)
        self.images = dict(DISC_IMAGES if images is None else images)
        self.git, self.clock = git, clock
        self.registry = Registry(self.root / "registry.tsv")
        self.run = run or self.run_program

    @staticmethod
    def run_program(cmd, log):
        with open(log, "ab") as f:
            f.write(("$ %s\n" % " ".join(str(c) for c in cmd)).encode())
            f.flush()
            return subprocess.run([str(c) for c in cmd], stdout=f, stderr=subprocess.STDOUT).returncode

    def lookup(self, key, now=None):
        """The game.pak of a key, or None: the registry's path (adopted or made), else the key's own folder."""
        row = self.registry.find(key)
        candidates = ([pathlib.Path(row["path"])] if row else []) + [self.root / key / "game.pak"]
        for pak in candidates:
            if pak.is_file():
                if row:
                    self.registry.touch(key, now)
                return pak
        return None

    def ensure(self, disc, tree, importer, log, now=None):
        """(game.pak, how, key): `how` is `found` or `made`. Not found, the data is made with `importer` from the disc's image
        into the key's folder (under a name of its own until it is whole), checked against its key, and registered. DataError
        says why not."""
        tree = pathlib.Path(tree)
        store = tree_store_version(tree)
        importer_tree = importer_tree_hash(self.git, tree)
        if not importer_tree:
            raise DataError("the tree has no git tree for %s: the key cannot be made" % IMPORTER_DIR, "tool")
        key = key_of(disc, store, importer_tree)
        pak = self.lookup(key, now)
        if pak is not None:
            return pak, "found", key
        image = self.images.get(disc)
        if image is None or not pathlib.Path(image).is_file():
            raise DataError("no disc image for %s (%s): its game data %s is not made" % (disc, image, key), "env")
        if not pathlib.Path(importer).is_file():
            raise DataError("the importer %s is not there" % importer, "tool")
        self.root.mkdir(parents=True, exist_ok=True)
        part = self.root / (key + ".part")
        shutil.rmtree(part, ignore_errors=True)
        started = self.clock()
        try:
            code = self.run([importer, "import", image, "--output", part], log)
        except OSError as error:
            if getattr(error, "errno", None) == 28 or getattr(error, "winerror", None) in (39, 112):
                shutil.rmtree(part, ignore_errors=True)
                raise DataError("the drive is full while %s is made: %s" % (key, error), "env")
            raise DataError("the importer could not start: %s" % error, "tool")
        made = part / "game.pak"
        if code != 0 or not made.is_file():
            shutil.rmtree(part, ignore_errors=True)
            raise DataError("the importer exited %s for %s and left no game.pak (log %s)" % (code, key, log), "fail")
        try:
            got_store, got_disc = read_header(made)
        except ValueError as error:
            shutil.rmtree(part, ignore_errors=True)
            raise DataError("the data made for %s is no store: %s" % (key, error), "fail")
        if (got_store, got_disc) != (store, disc):
            shutil.rmtree(part, ignore_errors=True)
            raise DataError("the data made for %s says store %d of %s, not store %d of %s" % (key, got_store, got_disc, store, disc),
                            "fail")
        final = self.root / key
        shutil.rmtree(final, ignore_errors=True)
        os.replace(part, final)
        self.registry.put(key, final / "game.pak", "made", now)
        with open(log, "ab") as f:
            f.write(("made %s in %.0f s\n" % (key, self.clock() - started)).encode())
        return final / "game.pak", "made", key

    # --- the older folders -------------------------------------------------------------------------------------------------

    def importer_hash_for(self, tree, version, refs=ADOPT_REFS):
        """(ref, commit, importer tree hash) of the newest commit on the first ref whose store_version.inc says `version`, or
        None. The run of commits with one version ends before the next bump, or at the ref's tip."""
        for ref in refs:
            if not git_out(self.git, tree, "rev-parse", "--verify", "-q", ref):
                continue
            bumps = git_out(self.git, tree, "log", "--format=%H", ref, "--", STORE_FILE).split()
            for i, commit in enumerate(bumps):
                try:
                    v = store_version_text(git_out(self.git, tree, "show", "%s:%s" % (commit, STORE_FILE)))
                except ValueError:
                    continue
                if v != version:
                    continue
                end = ref if i == 0 else git_out(self.git, tree, "rev-parse", bumps[i - 1] + "^")
                if not end:
                    continue
                h = importer_tree_hash(self.git, tree, end)
                if h:
                    return ref, git_out(self.git, tree, "rev-parse", end), h
        return None

    def adopt(self, tree, folders=None, refs=ADOPT_REFS, now=None):
        """Registers the older data folders under their keys, by reading only. Returns [(folder, key or None, text)]: a folder
        whose store version no commit of `refs` has is left where it is and reported (key None)."""
        folders = [pathlib.Path(f) for f in (folders if folders is not None else self.legacy_folders())]
        report = []
        for folder in folders:
            pak = folder / "game.pak"
            try:
                store, disc = read_header(pak)
            except ValueError as error:
                report.append((folder, None, "left where it is: %s" % error))
                continue
            found = self.importer_hash_for(tree, store, refs)
            if found is None:
                report.append((folder, None, "left where it is: no commit of %s has store version %d (%s)" % (
                    ", ".join(refs), store, disc)))
                continue
            ref, commit, importer_tree = found
            key = key_of(disc, store, importer_tree)
            row = self.registry.find(key)
            if row is not None and pathlib.Path(row["path"]).is_file():
                report.append((folder, key, "already registered as %s" % key))
                continue
            self.registry.put(key, pak, "adopted", now)
            report.append((folder, key, "registered as %s (store %d of %s, importer tree of %s on %s)" % (
                key, store, disc, commit[:9], ref)))
        return report

    def legacy_folders(self, build=None):
        build = pathlib.Path(build or BUILD)
        out = []
        for folder in sorted(build.iterdir()) if build.is_dir() else []:
            if folder.is_dir() and any(folder.name.startswith(p) and folder.name[len(p):].isdigit() for p in LEGACY_PREFIX):
                out.append(folder)
        return out

    # --- the old ones are removed ----------------------------------------------------------------------------------------------

    def clean(self, free_bytes, now=None, days=UNUSED_DAYS, floor=FREE_FLOOR_BYTES):
        """When D: has under `floor` bytes free: the folders made here that no run used for `days` days are removed (a folder
        registered from outside is unregistered only, never deleted). Returns [(key, what was done)]."""
        if free_bytes >= floor:
            return []
        now = now or datetime.datetime.now()
        done, drop = [], []
        for r in self.registry.read():
            try:
                used = datetime.datetime.fromisoformat(r["last used"])
            except ValueError:
                continue
            if (now - used).days < days:
                continue
            pak = pathlib.Path(r["path"])
            if r["how"] == "made" and pak.parent.parent == self.root and pak.parent.is_dir():
                shutil.rmtree(pak.parent, ignore_errors=True)
                done.append((r["key"], "removed %s (unused %d days)" % (pak.parent, (now - used).days)))
            else:
                done.append((r["key"], "unregistered; %s is left where it is (unused %d days)" % (pak.parent, (now - used).days)))
            drop.append(r["key"])
        if drop:
            self.registry.drop(drop)
        return done


# The command -----------------------------------------------------------------------------------------------------------------


def main(argv=None):
    import argparse
    p = argparse.ArgumentParser(prog="gamedata.py", description=__doc__.split("\n")[0])
    p.add_argument("command", choices=("list", "adopt", "path", "make", "clean"))
    p.add_argument("disc", nargs="?")
    p.add_argument("--root", default=str(ROOT))
    p.add_argument("--tree", default=str(HERE.parent.parent))
    p.add_argument("--folders", nargs="*", default=None, help="adopt: these folders instead of the build folder's older ones")
    p.add_argument("--importer", default="", help="make: the nebula-import program (default: the tree's gate build)")
    p.add_argument("--min-free-gb", type=float, default=FREE_FLOOR_BYTES / 1e9)
    a = p.parse_args(argv)
    data = GameData(a.root)
    if a.command == "list":
        for r in data.registry.read():
            print("%s\t%s\t%s\tlast used %s" % (r["key"], r["path"], r["how"], r["last used"]))
        return 0
    if a.command == "adopt":
        for folder, key, text in data.adopt(a.tree, a.folders):
            print("%s: %s" % (folder, text))
        return 0
    if a.command == "clean":
        for key, text in data.clean(shutil.disk_usage(a.root if pathlib.Path(a.root).exists() else ".").free, floor=a.min_free_gb * 1e9):
            print("%s: %s" % (key, text))
        return 0
    if a.disc not in DISC_IMAGES:
        print("gamedata.py: %s needs a disc: %s" % (a.command, ", ".join(DISC_IMAGES)), file=sys.stderr)
        return 2
    if a.command == "path":
        tree = pathlib.Path(a.tree)
        pak = data.lookup(key_of(a.disc, tree_store_version(tree), importer_tree_hash(GIT, tree)))
        print(pak or "(not made)")
        return 0 if pak else 1
    importer = a.importer or str(pathlib.Path("D:/NebulaWork/build") / ("gate-" + pathlib.Path(a.tree).name) / "bins" / "nebula-import.exe")
    log = pathlib.Path(a.root) / ("make-%s.log" % a.disc)
    log.parent.mkdir(parents=True, exist_ok=True)
    try:
        started = time.time()
        pak, how, key = data.ensure(a.disc, a.tree, importer, log)
    except DataError as error:
        print("gamedata.py: %s: %s" % (error.kind, error), file=sys.stderr)
        return 1
    print("%s: %s %s (%.0f s)" % (key, how, pak, time.time() - started))
    return 0


if __name__ == "__main__":
    sys.exit(main())
