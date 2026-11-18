"""harness_bare_test.py: what a bare test may hold (the game runs no test code), without a game.

  1. a bare test may list a mod the game imports, and check a fact the game writes (import.<name>);
  2. a placed mod, or a check of a mod.* fact, in a bare test is a harness error.
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
    base = {"harness": 1, "name": name, "about": "bare", "proves": ["harness.self"], "bare": True,
            "steps": [{"at": 10, "end": True}], "expect": []}
    base.update(test)
    (folder / "test.json").write_text(json.dumps(base), encoding="utf-8")
    return folder


def main():
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        mod = root / "m"
        mod.mkdir()
        (mod / "luma.json").write_text(json.dumps({"format": "1.0", "id": "bare-mod", "name": "m", "version": "1.0.0",
                                                   "authors": ["a"], "game": {"id": "RMG"}}), encoding="utf-8")
        imported = {"path": "../m", "install": "import", "loads": False}
        fact = {"fact": "import.m", "has": {"text": "FAILED"}, "when": "any"}

        # 1: accepted.
        try:
            t = luma_test.read_test(test_folder(root, "good", {"mods": [imported], "expect": [fact]}))
            check(t["bare"] and len(t["mods"]) == 1 and t["mods"][0]["install"] == "import", "the bare test reads")
        except luma_test.HarnessError as e:
            check(False, "a bare test that imports a mod and checks its fact is refused: %s" % e)

        # 2: refused.
        for name, test in (("placed", {"mods": [{"path": "../m", "loads": False}]}),
                           ("mod-fact", {"mods": [imported], "expect": [{"fact": "mod.bare-mod.x", "is": 1}]})):
            try:
                luma_test.read_test(test_folder(root, "bad-" + name, test))
                check(False, "a bare test with %s is refused" % name)
            except luma_test.HarnessError:
                pass

    for f in failed:
        print("FAIL:", f)
    print("harness_bare_test: %s" % ("FAILED" if failed else "passed"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
