"""harness_options_test.py: the proof harness's stored options (a test's "options" on a mod entry), without a game.

  1. option_lines writes each kind as the Mods screen keeps it: a toggle on or off, a choice's value id, a number in
     plain digits (20000, 80.5), a text escaped as mods.ini escapes it; a missing option, a key option and a value of
     another kind are harness errors naming the option;
  2. a mod kept from the test before (continue_from) gets its new values in its own section: the option's old lines
     give way, the other mods' sections and the mod's other lines stay, and no second section is written;
  3. an option given {"free_port": "<name>"} gets a port free on this computer, one per name for the run; a malformed
     one is a harness error naming the option.
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

    # 3. {"free_port": "<name>"}: one free port per name for the run, the same in every option naming it, and two names
    #    two ports; the port is free (a listener can bind it) and is stored as a number.
    test = {"mods": [{"options": {"port": {"free_port": "split"}, "hide": True}},
                     {"options": {"port": {"free_port": "split"}}}, {"options": {"port": {"free_port": "other"}}}]}
    ports = luma_test.free_ports(test)
    check(sorted(ports) == ["other", "split"] and ports["split"] != ports["other"], "one port a name: %r" % ports)
    given = [luma_test.with_ports(m["options"], ports) for m in test["mods"]]
    check(given[0] == {"port": ports["split"], "hide": True} and given[1]["port"] == ports["split"]
          and given[2]["port"] == ports["other"], "every option naming a port gets it: %r" % given)
    check(luma_test.option_lines("m", MANIFEST, given[0]) == "choice.port=%d\nchoice.hide=on\n" % ports["split"],
          "a free port is stored as a number")
    import socket
    with socket.socket() as s:
        try:
            s.bind(("127.0.0.1", ports["split"]))
        except OSError as error:
            failed.append("the free port %d does not bind: %s" % (ports["split"], error))
    check(luma_test.free_ports({"mods": [{"options": {"port": 16834}}]}) == {}, "no port asked, none picked")
    with tempfile.TemporaryDirectory() as folder:
        for bad in ({"free_port": "Split"}, {"free_port": 1}, {"free_port": "a", "more": 1}, {"port": "a"}):
            (pathlib.Path(folder) / "test.json").write_text(
                '{"harness": 1, "name": "t", "about": "options", "proves": ["services.net.local"], "mods": [{"path": "m", "options": {"port": %s}}], "steps": [{"at": 1, '
                '"end": true}], "expect": [{"fact": "f", "is": true}]}' % luma_test.json.dumps(bad), encoding="utf-8")
            try:
                luma_test.read_test(pathlib.Path(folder))
                failed.append("no harness error for the option %r" % bad)
            except luma_test.HarnessError as error:
                check("options.port" in str(error), "the error for %r names options.port: %s" % (bad, error))
        (pathlib.Path(folder) / "test.json").write_text((pathlib.Path(folder) / "test.json").read_text(encoding="utf-8")
                                                        .replace(luma_test.json.dumps(bad), '{"free_port": "split"}'),
                                                        encoding="utf-8")
        check(luma_test.read_test(pathlib.Path(folder))["mods"][0]["options"] == {"port": {"free_port": "split"}},
              "a well-formed free_port option is read as given")

    if failed:
        print("harness_options_test.py: FAILED:\n  " + "\n  ".join(failed))
        sys.exit(1)
    print("harness_options_test.py: pass")


if __name__ == "__main__":
    main()
