"""harness_switch_test.py: the proof harness's mods switched off and removed, without a game.

  1. a mod entry "enabled": false is installed with enabled=0 in its mods.ini section (as the Mods screen keeps a mod
     switched off) and is not among the mods every run checks as loaded;
  2. a mod kept from the test before (continue_from) is switched off or on in place: its section's enabled line
     changes, the other lines and sections stay;
  3. "remove": true takes a kept mod's folder away (as a player deleting it); its mods.ini section stays;
  4. remove without continue_from, remove with other keys, and loads true on a mod switched off are harness errors.
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
    base = {"harness": 1, "name": name, "about": "switch", "proves": ["harness.self"],
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
        (mod / "luma.json").write_text(json.dumps({"format": "1.0", "id": "sw-mod", "name": "m", "version": "1.0.0",
                                                   "authors": ["a"], "game": {"id": "RMG"}}), encoding="utf-8")
        settings = root / "settings"
        (settings / "mods").mkdir(parents=True)

        # 1: installed switched off.
        t = luma_test.read_test(test_folder(root, "off", {"mods": [{"path": "../m", "enabled": False}]}))
        check(t["mods"][0]["enabled"] is False and t["mods"][0]["loads"] is False,
              "enabled false reads as switched off and not loading: %r" % t["mods"][0])
        luma_test.install(settings, mod, True, 1, enabled=False)
        ini = settings / "mods.ini"
        check(ini.read_text(encoding="utf-8") == "[sw-mod]\nenabled=0\norder=1\n",
              "a mod switched off is enabled=0: %r" % ini.read_text(encoding="utf-8"))

        # 2: a kept mod switched on, then off, in place.
        ini.write_text("; Nebula mods\n[other]\nenabled=1\n[sw-mod]\nenabled=0\norder=1\nchoice.hide=on\n[last]\n"
                       "enabled=0\n", encoding="utf-8")
        luma_test.install(settings, mod, True, 1, kept=True, enabled=True)
        want = "; Nebula mods\n[other]\nenabled=1\n[sw-mod]\nenabled=1\norder=1\nchoice.hide=on\n[last]\nenabled=0\n"
        check(ini.read_text(encoding="utf-8") == want, "a kept mod switched on in place: %r" % ini.read_text(encoding="utf-8"))
        luma_test.install(settings, mod, True, 1, kept=True, enabled=False)
        check(ini.read_text(encoding="utf-8") == want.replace("[sw-mod]\nenabled=1", "[sw-mod]\nenabled=0"),
              "a kept mod switched off in place: %r" % ini.read_text(encoding="utf-8"))
        ini.write_text("[sw-mod]\norder=1\n[other]\nenabled=1\n", encoding="utf-8")
        luma_test.keep_enabled(ini, "sw-mod", False)
        check(ini.read_text(encoding="utf-8") == "[sw-mod]\norder=1\nenabled=0\n[other]\nenabled=1\n",
              "a section with no enabled line gets one: %r" % ini.read_text(encoding="utf-8"))
        try:
            luma_test.keep_enabled(ini, "absent", False)
            failed.append("no harness error for a kept mod with no section")
        except luma_test.HarnessError:
            pass

        # 3: removed as a player deletes it.
        t = luma_test.read_test(test_folder(root, "gone", {"continue_from": "off",
                                                           "mods": [{"path": "../m", "remove": True}]}))
        check(t["mods"][0]["remove"] is True and t["mods"][0]["loads"] is False, "remove reads: %r" % t["mods"][0])
        before = ini.read_text(encoding="utf-8")
        check(luma_test.remove_kept(settings, mod) == "sw-mod", "remove_kept gives the id")
        check(not (settings / "mods" / "sw-mod").exists(), "the removed mod's folder is gone")
        check(ini.read_text(encoding="utf-8") == before, "mods.ini is left as a player's deletion leaves it")
        try:
            luma_test.remove_kept(settings, mod)
            failed.append("no harness error for removing a mod that is not installed")
        except luma_test.HarnessError as error:
            check("not installed" in str(error), "the error names it: %s" % error)

        # 4: malformed entries.
        for name, test, named in (
                ("r1", {"mods": [{"path": "../m", "remove": True}]}, "continue_from"),
                ("r2", {"continue_from": "off", "mods": [{"path": "../m", "remove": True, "options": {}}]}, "nothing else"),
                ("r3", {"mods": [{"path": "../m", "enabled": False, "loads": True}]}, "does not load"),
                ("r4", {"mods": [{"path": "../m", "enabled": "no"}]}, "enabled")):
            try:
                luma_test.read_test(test_folder(root, name, test))
                failed.append("no harness error for %s" % name)
            except luma_test.HarnessError as error:
                check(named in str(error), "%s names %s: %s" % (name, named, error))

    if failed:
        print("harness_switch_test.py: FAILED:\n  " + "\n  ".join(failed))
        sys.exit(1)
    print("harness_switch_test.py: pass")


if __name__ == "__main__":
    main()
