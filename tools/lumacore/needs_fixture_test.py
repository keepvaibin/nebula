"""needs_fixture_test.py --commands COMPILE_COMMANDS [--out DIR]

What functions need (M2), on the fixture (tools/lumacore/tests/needs_fixture), through the real tools with a game
unit's flags (Game/System/LumaLoader.cpp's command):
  1. instrument.py writes the fixture unit's copy, .rows and .needs;
  2. gen_types.py (--tree the fixture, --defined the copy's folder) writes luma_functions.json: each function's needs,
     null_when and makes are the expected ones exactly, and the module's needs rows (luma_types_table.cpp) name each
     function that needs something, with its needs in order, and no other;
  3. a hand table (--needs) adds its needs to its row, and a class row to each member function but the excepted
     ones, to their callers, and to the needs rows as the game system's id (luma::kNeedUserFile);
  4. gen_types.py stops, naming the fault, on planted ones: a hand row that is no function, a hand row with no why,
     a class with no functions, a class's except that is no function of it, a unit without .needs, a .needs that is
     not its .rows row for row;
  5. the sets a handler proves (--guarantees): none with --tree alone (the kernel's table holds its end row only); a
     table's sets reach luma_functions.json "guarantees" with the sets they name in "with" folded in, and the kernel's
     table as their ids; gen_types.py stops, naming the fault, on a set named twice, a "with" that is no earlier set, a
     need with no why, a need named twice, a set with no needs and a set with no "holds"; with the sets, a need on
     an object no set names stops at a member function's call (a free function's call passes it on).
Exit 0 only when all five hold.
"""
import json
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
FIXTURE = HERE / "tests" / "needs_fixture"


def option(flag, default=None):
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv else default


def run(args):
    r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout, r.stderr


# (name, signature): (needs, null_when, makes)
EXPECTED = {
    ("ndHolder", "NdHolder *()"): ([], ["Holder"], []),
    ("ndActor", "NdActor *()"): ([], ["Holder"], []),
    ("ndUseActor", "void()"): (["Holder"], [], []),
    ("ndDeep", "void()"): (["Holder"], [], []),
    ("ndHasActor", "bool()"): ([], [], []),
    ("ndRaw", "void()"): (["Counter", "scene"], [], []),
    ("ndGuarded", "void()"): ([], [], []),
    ("ndMakes", "void()"): (["scene"], [], ["Made"]),
    ("ndCallsMakes", "void()"): (["scene"], [], ["Made"]),
    ("ndCounter", "NdObj *()"): (["scene"], ["Counter"], []),
    ("ndPlain", "int(int)"): ([], [], []),
    ("NdFile::NdFile", "()"): ([], [], []),
    ("NdFile::init", "void()"): ([], [], []),
    ("NdFile::use", "int()"): ([], [], []),
    ("ndCallsFile", "int(NdFile *)"): ([], [], []),
    ("NdKeeper::peek", "int()"): (["Holder", "scene"], [], []),
    ("ndCallsPeek", "int(NdKeeper *)"): (["Holder", "scene"], [], []),
    ("ndGuardedCall", "void()"): ([], [], []),
    ("ndGuardedBranch", "void()"): ([], [], []),
    ("ndOtherGuard", "void()"): (["Counter"], [], []),
}
# With the sets of step 5 (Holder in none): a member's call passes on only the sets' needs and the game system's; a
# free function's call every need.
WITH_SETS = {
    ("ndCallsPeek", "int(NdKeeper *)"): ["scene"],
    ("NdKeeper::peek", "int()"): ["Holder", "scene"],
    ("ndDeep", "void()"): ["Holder"],
}
# With the hand table of step 3: (name, signature) -> needs
HAND = {
    ("ndPlain", "int(int)"): ["Counter"],
    ("NdFile::NdFile", "()"): [],
    ("NdFile::init", "void()"): [],
    ("NdFile::use", "int()"): ["UserFile"],
    ("ndCallsFile", "int(NdFile *)"): ["UserFile"],
}


def table_rows(text):
    """The needs rows luma_types_table.cpp gives: (name, signature) -> the names of its needs, in order."""
    rows = {}
    for line in text.splitlines():
        if line.startswith("LUMA_NEEDS_SECTION const luma::NeedsRow "):
            inside = line[line.index("{") + 1:line.rindex("}")]  # "name", "signature", kNdN, "A\0B\0"
            name, rest = inside.split('", "', 1)
            signature, rest = rest.split('", kNd', 1)
            names = rest.split(', "', 1)[1].rsplit('"', 1)[0]
            rows[(name.lstrip('"'), signature)] = [n for n in names.split("\\0") if n]
    return rows


def main():
    commands = option("--commands") or sys.exit(__doc__)
    out = pathlib.Path(option("--out", str(HERE.parents[1] / "build" / "needs-fixture"))).resolve()
    if out.exists():
        shutil.rmtree(out)
    defined = out / "defined"
    defined.mkdir(parents=True)
    failed = []

    # 1. instrument.py, with LumaLoader.cpp's command given to the copy
    entries = json.loads(pathlib.Path(commands).read_text(encoding="utf-8"))
    entry = dict(next(e for e in entries if e["file"].replace("\\", "/").endswith("Game/System/LumaLoader.cpp")))
    copy = defined / "needs_fixture.cpp"
    entry["file"] = str(copy)
    (out / "commands.json").write_text(json.dumps([entry]), encoding="utf-8")
    code, so, se = run([sys.executable, "-X", "utf8", "-B", str(HERE / "instrument.py"), str(FIXTURE / "needs_fixture.cpp"),
                        str(copy), "--commands", str(out / "commands.json"), "--root", str(FIXTURE)])
    if code != 0:
        sys.exit("needs_fixture_test.py: instrument.py failed:\n" + (so + se)[-3000:])
    needs_file = pathlib.Path(str(copy) + ".needs")
    if not needs_file.is_file():
        sys.exit("needs_fixture_test.py: instrument.py wrote no %s" % needs_file.name)

    # 2. gen_types.py's merge
    generate = [sys.executable, "-X", "utf8", "-B", str(HERE / "gen_types.py"), "--commands", commands,
                "--out", str(out / "gen"), "--tree", str(FIXTURE), "--defined", str(defined)]
    code, so, se = run(generate)
    print("gen_types:", (so.strip() or se.strip())[-1500:])
    if code != 0:
        sys.exit("needs_fixture_test.py: the generator failed:\n" + se[-3000:])
    have = {(f["name"], f["signature"]): f for f in
            json.loads((out / "gen" / "luma_functions.json").read_text(encoding="utf-8"))["functions"]}
    for key, (needs, null_when, makes) in EXPECTED.items():
        f = have.get(key)
        if f is None:
            failed.append("%s %s: no row" % key)
            continue
        got = (f.get("needs"), f.get("null_when"), f.get("makes", []))
        if got != (needs, null_when, makes):
            failed.append("%s %s: needs %s, null_when %s, makes %s; not %s, %s, %s" % (key + got + (needs, null_when, makes)))
        if needs and not set(f.get("needs_why", {})) == set(needs):
            failed.append("%s %s: needs_why %s does not name each need" % (key + (f.get("needs_why"),)))
    rows = table_rows((out / "gen" / "luma_types_table.cpp").read_text(encoding="utf-8"))
    want_rows = {k: v[0] for k, v in EXPECTED.items() if v[0]}
    if rows != want_rows:
        failed.append("the needs rows are %s, not %s" % (rows, want_rows))

    # 3. A hand table's row adds its needs
    table = out / "needs.json"
    table.write_text(json.dumps({"rows": [
        {"name": "ndPlain", "signature": "int(int)", "needs": ["Counter"], "why": "the fixture's hand row"},
        {"class": "NdFile", "except": ["NdFile", "init"], "needs": ["UserFile"], "why": "the fixture's class row"},
    ]}), encoding="utf-8")
    code, so, se = run(generate + ["--needs", str(table)])
    if code != 0:
        failed.append("gen_types.py failed with a good hand table: " + se[-400:])
    else:
        hand = {(f["name"], f["signature"]): f for f in
                json.loads((out / "gen" / "luma_functions.json").read_text(encoding="utf-8"))["functions"]}
        plain = hand[("ndPlain", "int(int)")]
        if "needs.json: the fixture's hand row" not in plain.get("needs_why", {}).get("Counter", ""):
            failed.append("the hand row's why is not ndPlain's: %s" % plain)
        for key, needs in HAND.items():
            if hand.get(key, {}).get("needs") != needs:
                failed.append("with the hand table %s %s needs %s, not %s" % (key + (hand.get(key, {}).get("needs"), needs)))
        table_text = (out / "gen" / "luma_types_table.cpp").read_text(encoding="utf-8")
        if "luma::kNeedUserFile" not in table_text or "SceneObj_UserFile" in table_text:
            failed.append("the needs rows do not give UserFile as the game system's id luma::kNeedUserFile")

    # 4. planted faults, each named
    good = needs_file.read_text(encoding="utf-8")
    entries = json.loads(good)
    planted = [
        ("a hand row that is no function", None,
         {"rows": [{"name": "ndNothing", "signature": "void()", "needs": ["Counter"], "why": "planted"}]},
         "needs.json: ndNothing void() is no function row"),
        ("a hand row with no why", None, {"rows": [{"name": "ndPlain", "signature": "int(int)", "needs": ["Counter"]}]},
         "needs.json: ndPlain int(int) has no why"),
        ("a class with no functions", None, {"rows": [{"class": "NdNothing", "needs": ["UserFile"], "why": "planted"}]},
         "needs.json: class NdNothing has no function rows"),
        ("a class's except that is no function of it", None,
         {"rows": [{"class": "NdFile", "except": ["make"], "needs": ["UserFile"], "why": "planted"}]},
         "needs.json: class NdFile has no function make"),
        ("a unit without .needs", "", None, "needs_fixture.cpp: %d rows and no .needs" % len(entries)),
        ("a .needs that is not its .rows", json.dumps(entries[1:] + entries[:1]), None,
         "needs_fixture.cpp: its .needs is not its .rows, row for row"),
    ]
    for what, text, hand, named in planted:
        args = list(generate)
        if hand is not None:
            table.write_text(json.dumps(hand), encoding="utf-8")
            args += ["--needs", str(table)]
        if text == "":
            needs_file.unlink()
        elif text is not None:
            needs_file.write_text(text + "\n", encoding="utf-8")
        code, so, se = run(args)
        print("planted, %s:" % what, se.strip()[-300:])
        if code == 0 or named not in se:
            failed.append("gen_types.py did not stop naming %r (%s)" % (named, what))
        needs_file.write_text(good, encoding="utf-8")
    code, so, se = run(generate)
    if code != 0:
        failed.append("gen_types.py fails on the restored needs")

    # 5. the sets a handler proves
    def generated():
        functions = json.loads((out / "gen" / "luma_functions.json").read_text(encoding="utf-8"))
        return functions.get("guarantees"), (out / "gen" / "luma_types_table.cpp").read_text(encoding="utf-8")

    sets, table_text = generated()
    if sets != [] or "kGuaranteeSets[] = {{nullptr, false, nullptr}};" not in table_text:
        failed.append("--tree alone gives the sets %s, or a kernel table with rows" % sets)
    guarantees = out / "guarantees.json"
    guarantees.write_text(json.dumps({"sets": [
        {"name": "system", "holds": "the fixture's files", "needs": {"UserFile": "the fixture"}},
        {"name": "stage", "holds": "the fixture's scene", "made": True, "with": ["system"],
         "needs": {"scene": "the fixture", "Counter": "the fixture"}},
        {"name": "player", "holds": "the fixture's player", "with": ["stage"], "needs": {"MarioActor": "the fixture"}},
    ]}), encoding="utf-8")
    code, so, se = run(generate + ["--guarantees", str(guarantees)])
    if code != 0:
        failed.append("gen_types.py failed with a good table of sets: " + se[-400:])
    else:
        sets, table_text = generated()
        want = [("system", False, ["UserFile"]), ("stage", True, ["UserFile", "scene", "Counter"]),
                ("player", True, ["UserFile", "scene", "Counter", "MarioActor"])]
        got = [(g["name"], g["made"], list(g["needs"])) for g in sets or []]
        if got != want:
            failed.append("the sets are %s, not %s" % (got, want))
        if any(not all(g["needs"].values()) for g in sets or []):
            failed.append("a set's need has no why: %s" % sets)
        rows = ('kGuaranteeSets[] = {{"system", false, kGs0}, {"stage", true, kGs1}, {"player", true, kGs2}, '
                '{nullptr, false, nullptr}};')
        with_sets = {(f["name"], f["signature"]): f["needs"] for f in
                     json.loads((out / "gen" / "luma_functions.json").read_text(encoding="utf-8"))["functions"]}
        for key, needs in WITH_SETS.items():
            if with_sets.get(key) != needs:
                failed.append("with the sets %s %s needs %s, not %s" % (key + (with_sets.get(key), needs)))
        ids = "const unsigned char kGs2[] = {luma::kNeedUserFile, luma::kNeedScene, SceneObj_Counter, luma::kNeedMarioActor, luma::kNeedsEnd};"
        if rows not in table_text or ids not in table_text:
            failed.append("the kernel's table of sets is not the table's:\n" +
                          "\n".join(l for l in table_text.splitlines() if "kGs" in l or "kGuarantee" in l))
    planted_sets = [
        ("a set named twice", [{"name": "a", "holds": "h", "needs": {"scene": "w"}}, {"name": "a", "holds": "h", "needs": {"scene": "w"}}],
         "sets[1]: the name 'a' is not a new one"),
        ("a with that is no earlier set", [{"name": "a", "holds": "h", "with": ["b"], "needs": {"scene": "w"}},
                                           {"name": "b", "holds": "h", "needs": {"Counter": "w"}}],
         "sets[0] (a): with 'b' is no earlier set"),
        ("a need with no why", [{"name": "a", "holds": "h", "needs": {"scene": ""}}], "sets[0] (a): scene has no why"),
        ("a need named twice", [{"name": "a", "holds": "h", "needs": {"scene": "w"}},
                                {"name": "b", "holds": "h", "with": ["a"], "needs": {"scene": "w"}}],
         "sets[1] (b): scene is named twice"),
        ("a set with no needs", [{"name": "a", "holds": "h", "needs": {}}], "sets[0] (a): no needs"),
        ("a set with no holds", [{"name": "a", "needs": {"scene": "w"}}], "sets[0] (a): no holds"),
    ]
    for what, rows, named in planted_sets:
        guarantees.write_text(json.dumps({"sets": rows}), encoding="utf-8")
        code, so, se = run(generate + ["--guarantees", str(guarantees)])
        print("planted, %s:" % what, se.strip()[-300:])
        if code == 0 or named not in se:
            failed.append("gen_types.py did not stop naming %r (%s)" % (named, what))

    if failed:
        print("needs_fixture_test.py: FAILED:\n  " + "\n  ".join(failed))
        sys.exit(1)
    print("needs_fixture_test.py: pass (%d functions)" % len(EXPECTED))


if __name__ == "__main__":
    main()
