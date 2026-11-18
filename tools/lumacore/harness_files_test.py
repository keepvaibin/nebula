"""harness_files_test.py: the proof harness's "unreadable" and "cut_files" (M-150's tests), without a game.

  1. a mod entry "unreadable": ["source/a.c"] names that file in the run's placed copy of the mod;
  2. while held_unreadable runs, another reader cannot open the file; after it, the file reads as before;
  3. unreadable with import, copies or remove, a path with .. or a drive, and an empty list are harness errors; a file
     the placed mod does not have is a setup failure;
  4. "cut_files" cuts each named settings file to half its bytes; without continue_from it is a harness error, and a
     file the test before did not leave is a setup failure.
Exit 0 only when every check holds.
"""
import json
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import luma_test  # noqa: E402


def test_folder(root, name, test):
    folder = root / name
    folder.mkdir(parents=True)
    base = {"harness": 1, "name": name, "about": "unreadable", "proves": ["harness.self"],
            "steps": [{"at": 10, "end": True}], "expect": []}
    base.update(test)
    (folder / "test.json").write_text(json.dumps(base), encoding="utf-8")
    return folder


def readable(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def main():
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        mod = root / "m"
        (mod / "source").mkdir(parents=True)
        (mod / "luma.json").write_text(json.dumps({"format": "1.0", "id": "un-mod", "name": "m", "version": "1.0.0",
                                                   "authors": ["a"], "game": {"id": "RMG"}}), encoding="utf-8")
        (mod / "source" / "a.c").write_bytes(b"int a;\n")
        settings = root / "settings"
        (settings / "mods").mkdir(parents=True)

        # 1: the placed copy's file.
        t = luma_test.read_test(test_folder(root, "one", {"mods": [{"path": "../m", "unreadable": ["source/a.c"]}]}))
        placed, _ = luma_test.place_mods(t, settings, None, {})
        files = luma_test.unreadable_files(t, settings, placed)
        check(files == [settings / "mods" / "un-mod" / "source" / "a.c"], "the placed copy's file is named: %r" % files)

        # 2: held, then given back.
        with luma_test.held_unreadable(files):
            check(readable(files[0]) is None, "a held file cannot be read")
        check(readable(files[0]) == b"int a;\n", "the file reads again after the run")
        try:
            with luma_test.held_unreadable([settings / "mods" / "un-mod" / "source" / "none.c"]):
                check(False, "a missing file is held")
        except luma_test.SetupFailed:
            pass

        # 3: refusals.
        for name, entry in (("import", {"path": "../m", "unreadable": ["luma.json"], "install": "import", "loads": False}),
                            ("copies", {"path": "../m", "unreadable": ["luma.json"], "copies": 2}),
                            ("up", {"path": "../m", "unreadable": ["../x"]}),
                            ("drive", {"path": "../m", "unreadable": ["C:/x"]}),
                            ("empty", {"path": "../m", "unreadable": []})):
            try:
                luma_test.read_test(test_folder(root, "bad-" + name, {"mods": [entry]}))
                check(False, "unreadable with %s is refused" % name)
            except luma_test.HarnessError:
                pass

        # 4: cut_files.
        kept = settings / "mods" / ".cooked" / "validate" / "un-mod.kept"
        kept.parent.mkdir(parents=True)
        kept.write_bytes(b"0123456789")
        (root / "first").mkdir()
        t = luma_test.read_test(test_folder(root, "cut", {"continue_from": "first",
                                                          "cut_files": ["mods/.cooked/validate/un-mod.kept"]}))
        luma_test.cut_files(t, settings)
        check(kept.read_bytes() == b"01234", "a cut file keeps its first half: %r" % kept.read_bytes())
        try:
            luma_test.cut_files(luma_test.read_test(test_folder(root, "cut-none", {
                "continue_from": "first", "cut_files": ["mods/none.kept"]})), settings)
            check(False, "a file the test before did not leave is cut")
        except luma_test.SetupFailed:
            pass
        try:
            luma_test.read_test(test_folder(root, "cut-alone", {"cut_files": ["mods/x"]}))
            check(False, "cut_files without continue_from is refused")
        except luma_test.HarnessError:
            pass

    if failed:
        for f in failed:
            print("FAIL:", f)
        return 1
    print("harness_files_test: all checks pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
