"""harness_options_test.py: the proof harness's stored options (a test's "options" on a mod entry), without a game.

  1. option_lines writes each kind as the Mods screen keeps it: a toggle on or off, a choice's value id, a number in
     plain digits (20000, 80.5), a text escaped as mods.ini escapes it; a missing option, a key option and a value of
     another kind are harness errors naming the option;
  2. a mod kept from the test before (continue_from) gets its new values in its own section: the option's old lines
     give way, the other mods' sections and the mod's other lines stay, and no second section is written.
Exit 0 only when every check holds.
"""
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import luma_test  # noqa: E402

MANIFEST = {"options": [{"key": "port", "kind": "number"}, {"key": "hide", "kind": "toggle"},
                        {"key": "lives", "kind": "choice"}, {"key": "label", "kind": "text"},
                        {"key": "jump", "kind": "key"}]}


def main():
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    lines = luma_test.option_lines("m", MANIFEST, {"hide": False, "port": 20000.0, "lives": "one", "label": "a=b%[c]"})
    check(lines == "choice.hide=off\nchoice.port=20000\nchoice.lives=one\ntext.label=a%3Db%25%5Bc%5D\n",
          "each kind as the Mods screen keeps it: %r" % lines)
    check(luma_test.option_lines("m", MANIFEST, {"port": 80.5, "hide": True}) == "choice.port=80.5\nchoice.hide=on\n",
          "a number off its steps is stored as given; a toggle true is on")
    for bad, named in (({"port": "1"}, "options.port"), ({"port": True}, "options.port"), ({"hide": 1}, "options.hide"),
                       ({"lives": 2}, "options.lives"), ({"jump": "A"}, "options.jump"), ({"nope": 1}, "nope"),
                       ({"port": float("inf")}, "options.port")):
        try:
            luma_test.option_lines("m", MANIFEST, bad)
            failed.append("no harness error for %r" % bad)
        except luma_test.HarnessError as error:
            check(named in str(error), "the error for %r names %s: %s" % (bad, named, error))

    with tempfile.TemporaryDirectory() as folder:
        ini = pathlib.Path(folder) / "mods.ini"
        ini.write_text("; Nebula mods\n[other]\nenabled=1\nchoice.port=1\n[m]\nenabled=1\norder=1\noption.port=0\n"
                       "choice.port=16834\nchoice.hide=on\n[last]\nenabled=0\n", encoding="utf-8")
        luma_test.keep_options(ini, "m", luma_test.option_lines("m", MANIFEST, {"port": 20000}))
        text = ini.read_text(encoding="utf-8")
        want = ("; Nebula mods\n[other]\nenabled=1\nchoice.port=1\n[m]\nenabled=1\norder=1\nchoice.hide=on\n"
                "choice.port=20000\n[last]\nenabled=0\n")
        check(text == want, "a kept mod's option changed in its own section: %r" % text)
        check(text.count("[m]") == 1, "one section for the kept mod")
        try:
            luma_test.keep_options(ini, "absent", "choice.port=1\n")
            failed.append("no harness error for a kept mod with no section")
        except luma_test.HarnessError:
            pass

    if failed:
        print("harness_options_test.py: FAILED:\n  " + "\n  ".join(failed))
        sys.exit(1)
    print("harness_options_test.py: pass")


if __name__ == "__main__":
    main()
