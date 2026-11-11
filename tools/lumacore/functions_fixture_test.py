"""functions_fixture_test.py --commands COMPILE_COMMANDS [--out DIR]

The function rows' facts (the checker design's C-1), on the fixture (tools/lumacore/tests/functions_fixture), through
the real tools with a game unit's flags (Game/System/LumaLoader.cpp's command):
  1. instrument.py writes the fixture unit's copy, .rows and .functions; its identity() check holds on every row;
  2. gen_types.py (--tree the fixture, --defined the copy's folder) writes luma_functions.json: each row's facts are
     the expected ones exactly (kind, owner as the types file names the record, virtual, pure, const, what it
     overrides and whether that is a row);
  3. gen_types.py stops, naming the fault, on planted ones: a unit without .functions, a row without facts, an owner
     that is no record, an override with no name.
Exit 0 only when all three hold.
"""
import json
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
FIXTURE = HERE / "tests" / "functions_fixture"


def option(flag, default=None):
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv else default


def run(args):
    r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout, r.stderr


def fact(name, signature, kind, owner, virtual=False, pure=False, const=False, overrides=()):
    return {"name": name, "signature": signature, "kind": kind, "owner": owner, "virtual": virtual, "pure": pure,
            "const": const, "overrides": [{"name": n, "signature": s, "row": r} for n, s, r in overrides]}


EXPECTED = [
    fact("FnBase::~FnBase", "()", "destructor", "FnBase", virtual=True),
    fact("FnBase::f", "void(int)", "method", "FnBase", virtual=True),
    fact("FnBase::g", "int() const", "method", "FnBase", virtual=True, pure=True, const=True),
    fact("FnBase::s", "int()", "static", "FnBase"),
    fact("FnBase::plain", "int() const", "method", "FnBase", const=True),
    fact("FnMid::f", "void(int)", "method", "FnMid", virtual=True, overrides=[("FnBase::f", "void(int)", True)]),
    fact("FnDerived::FnDerived", "()", "constructor", "FnDerived"),
    # The direct base's destructor: FnMid's, which clang declares implicitly (no row).
    fact("FnDerived::~FnDerived", "()", "destructor", "FnDerived", virtual=True, overrides=[("FnMid::~FnMid", "()", False)]),
    fact("FnDerived::f", "void(int)", "method", "FnDerived", virtual=True, overrides=[("FnMid::f", "void(int)", True)]),
    fact("FnDerived::g", "int() const", "method", "FnDerived", virtual=True, const=True,
         overrides=[("FnBase::g", "int() const", True)]),
    fact("FnDerived::h", "void()", "method", "FnDerived", virtual=True, overrides=[("FnOther::h", "void()", False)]),
    fact("FnFromT::t", "void(int)", "method", "FnFromT", virtual=True, overrides=[("FnT<s32>::t", "void(int)", False)]),
    fact("FnNs::Inner::m", "void()", "method", "FnNs::Inner"),
    fact("fnFree", "int(int)", "free", None),
    fact("functions_fixture.cpp#Local::g", "int() const", "method", None, virtual=True, const=True,
         overrides=[("FnBase::g", "int() const", True)]),
    fact("fnMake", "FnBase *()", "free", None),
]


def main():
    commands = option("--commands") or sys.exit(__doc__)
    out = pathlib.Path(option("--out", str(HERE.parents[1] / "build" / "functions-fixture"))).resolve()
    if out.exists():
        shutil.rmtree(out)
    defined = out / "defined"
    defined.mkdir(parents=True)
    failed = []

    # 1. instrument.py, with LumaLoader.cpp's command given to the copy
    entries = json.loads(pathlib.Path(commands).read_text(encoding="utf-8"))
    entry = dict(next(e for e in entries if e["file"].replace("\\", "/").endswith("Game/System/LumaLoader.cpp")))
    copy = defined / "functions_fixture.cpp"
    entry["file"] = str(copy)
    (out / "commands.json").write_text(json.dumps([entry]), encoding="utf-8")
    code, so, se = run([sys.executable, "-X", "utf8", "-B", str(HERE / "instrument.py"), str(FIXTURE / "functions_fixture.cpp"),
                        str(copy), "--commands", str(out / "commands.json"), "--root", str(FIXTURE)])
    if code != 0:
        sys.exit("functions_fixture_test.py: instrument.py failed:\n" + (so + se)[-3000:])
    facts_file = pathlib.Path(str(copy) + ".functions")

    # 2. gen_types.py's merge
    generate = [sys.executable, "-X", "utf8", "-B", str(HERE / "gen_types.py"), "--commands", commands,
                "--out", str(out / "gen"), "--tree", str(FIXTURE), "--defined", str(defined)]
    code, so, se = run(generate)
    print("gen_types:", (so.strip() or se.strip())[-1500:])
    if code != 0:
        sys.exit("functions_fixture_test.py: the generator failed")
    have = json.loads((out / "gen" / "luma_functions.json").read_text(encoding="utf-8"))["functions"]
    for want in EXPECTED:
        got = next((f for f in have if f["name"] == want["name"] and f["signature"] == want["signature"]), None)
        if got != want:
            failed.append("%s %s: %s, not %s" % (want["name"], want["signature"], got, want))
    extra = [(f["name"], f["signature"]) for f in have if f not in EXPECTED]
    if extra or len(have) != len(EXPECTED):
        failed.append("rows not in the expected list, or counted twice: %s (%d rows)" % (extra, len(have)))

    # 3. planted faults, each named
    good = facts_file.read_text(encoding="utf-8")
    facts = json.loads(good)
    planted = [
        ("a row without facts", json.dumps(facts[:-1]), "row %d is fnMake" % len(facts)),
        ("an owner that is no record", json.dumps([dict(f, owner="NoSuchRecord") if f["name"] == "FnNs::Inner::m" else f
                                                  for f in facts]), "its owner NoSuchRecord is no record"),
        ("an override with no name", json.dumps([dict(f, overrides=[{"name": None, "signature": None}])
                                                 if f["name"] == "FnMid::f" else f for f in facts]),
         "FnMid::f void(int): it overrides a function with no name"),
        ("a unit without .functions", None, "functions_fixture.cpp: %d rows and no .functions" % len(facts)),
    ]
    for what, text, named in planted:
        if text is None:
            facts_file.unlink()
        else:
            facts_file.write_text(text + "\n", encoding="utf-8")
        code, so, se = run(generate)
        print("planted, %s:" % what, se.strip()[-400:])
        if code == 0 or named not in se:
            failed.append("gen_types.py did not stop naming %r (%s)" % (named, what))
        facts_file.write_text(good, encoding="utf-8")
    code, so, se = run(generate)
    if code != 0:
        failed.append("gen_types.py fails on the restored facts")

    if failed:
        print("functions_fixture_test.py: FAILED:\n  " + "\n  ".join(failed))
        sys.exit(1)
    print("functions_fixture_test.py: pass (%d rows)" % len(have))


if __name__ == "__main__":
    main()
