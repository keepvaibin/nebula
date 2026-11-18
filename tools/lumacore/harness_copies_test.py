"""harness_copies_test.py: the proof harness's "copies" (N-3008), without a game.

  1. a mod entry "copies": 3 places the mod three times, copy k in the folder <id>-<k> with luma.json's id and name
     <id>-<k>, each in mods.ini in turn (orders 1 to 3), and a mod after the entry takes the next place (4);
  2. each copy is among the mods every run checks as loaded, under its own id;
  3. copies below 2, and copies with folder, remove or import, are harness errors.
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
    base = {"harness": 1, "name": name, "about": "copies", "proves": ["harness.self"],
            "steps": [{"at": 10, "end": True}], "expect": []}
    base.update(test)
    (folder / "test.json").write_text(json.dumps(base), encoding="utf-8")
    return folder


def manifest(folder, mod_id):
    folder.mkdir()
    (folder / "luma.json").write_text(json.dumps({"format": "1.0", "id": mod_id, "name": "m", "version": "1.0.0",
                                                  "authors": ["a"], "game": {"id": "RMG"}}), encoding="utf-8")


def main():
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        manifest(root / "m", "cp-mod")
        manifest(root / "n", "cp-next")
        settings = root / "settings"
        (settings / "mods").mkdir(parents=True)

        # 1 and 2: three copies, then one more mod.
        t = luma_test.read_test(test_folder(root, "three", {"mods": [{"path": "../m", "copies": 3}, "../n"]}))
        placed, _ = luma_test.place_mods(t, settings, None, {})
        ids = [mod_id for mod_id, _ in placed]
        check(ids == ["cp-mod-1", "cp-mod-2", "cp-mod-3", "cp-next"], "the copies and the next mod are placed: %r" % ids)
        for k in (1, 2, 3):
            folder = settings / "mods" / ("cp-mod-%d" % k)
            m = json.loads((folder / "luma.json").read_text(encoding="utf-8")) if folder.is_dir() else {}
            check(m.get("id") == "cp-mod-%d" % k and m.get("name") == "cp-mod-%d" % k,
                  "copy %d's luma.json names it: %r" % (k, m))
        ini = (settings / "mods.ini").read_text(encoding="utf-8")
        orders = {}
        section = None
        for line in ini.splitlines():
            if line.startswith("["):
                section = line[1:-1]
            elif line.startswith("order="):
                orders[section] = int(line[6:])
        check(orders == {"cp-mod-1": 1, "cp-mod-2": 2, "cp-mod-3": 3, "cp-next": 4}, "each in turn in mods.ini: %r" % orders)
        check(all(m["loads"] for _, m in placed), "every copy is checked as loaded")

        # 3: refusals.
        for name, entry in (("one", {"path": "../m", "copies": 1}), ("folder", {"path": "../m", "copies": 2, "folder": "x"}),
                            ("remove", {"path": "../m", "copies": 2, "remove": True}),
                            ("import", {"path": "../m", "copies": 2, "install": "import", "loads": False})):
            try:
                luma_test.read_test(test_folder(root, "bad-" + name, {"mods": [entry]}))
                check(False, "copies with %s is refused" % name)
            except luma_test.HarnessError:
                pass

    if failed:
        for f in failed:
            print("FAIL", f)
        return 1
    print("harness copies: every check holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
