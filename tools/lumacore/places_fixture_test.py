"""places_fixture_test.py --commands COMPILE_COMMANDS [--out DIR]

The placement facts (plan 1.8 places, contract self-description), on the fixture (tools/lumacore/tests/places_fixture),
through the real tools with a game unit's flags (Game/System/LumaLoader.cpp's command):
  1. instrument.py writes each fixture unit's copy and its .places part;
  2. gen_types.py (--tree the fixture, --defined the copies' folder) writes the types file the classes' bases come from;
  3. placement.py writes the placement file: the factory's rows and each class's reads are the expected ones exactly
     (a field, a local, this->, a column read by its own name, a base's reads inherited with "from", a read through a
     helper with "via" and no target, a read no method reaches left);
  4. placement.py stops, naming the unit, when an instrumented unit has no .places part.
Exit 0 only when all four hold.
"""
import json
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
FIXTURE = HERE / "tests" / "places_fixture"
UNITS = ["places_fixture.cpp", "NameObj/NameObjFactory.cpp"]


def option(flag, default=None):
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv else default


def run(args):
    r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout, r.stderr


def row(arg, field, type_, target, function, from_, with_init=False, local=None, via=()):
    return {"arg": arg, "field": field, "type": type_, "with_init": with_init, "target": target, "local": local,
            "function": function, "from": from_, "via": list(via)}


# What placement.py must give, per class (the keys compared; file, line, written and signature are checked apart).
EXPECTED_ARGS = {
    "PfArea": [row(0, "Obj_arg0", "s32", "mObjArg0", "PfArea::initArea", "PfArea")],
    "PfCube": [row(0, "Obj_arg0", "s32", "mObjArg0", "PfArea::initArea", "PfArea"),
               row(1, "Obj_arg1", "f32", "mSpeed", "PfCube::init", "PfCube", with_init=True),
               row(2, "Obj_arg2", "bool", None, "PfCube::init", "PfCube", local="loop"),
               row(None, "RotateSpeed", "f32", "mRotateSpeed", "PfCube::init", "PfCube")],
    "PfDome": [row(0, "Obj_arg0", "s32", None, "PfDome::init", "PfDome", via=["pfDomeFromArg0"]),
               row(3, "Obj_arg3", "s32", "mDome", "PfDome::init", "PfDome")],
}
EXPECTED_FACTORY = [("PfAlpha", "PfAlpha", "createNameObj<PfAlpha>"),
                    ("PfBravo", "PfBravo", "NameObj/NameObjFactory.cpp#createBravo"),
                    ("PfCharlie", "PfCharlie", "pfCreateCharlie"),
                    ("PfElsewhere", None, "NameObj/NameObjFactory.cpp#createElsewhere")]
KEYS = ("arg", "field", "type", "with_init", "target", "local", "function", "from", "via")


def main():
    commands = option("--commands") or sys.exit(__doc__)
    out = pathlib.Path(option("--out", str(HERE.parents[1] / "build" / "places-fixture"))).resolve()
    if out.exists():
        shutil.rmtree(out)
    defined = out / "defined"
    failed = []

    # 1. instrument.py on each unit, with LumaLoader.cpp's command given to its copy
    entries = json.loads(pathlib.Path(commands).read_text(encoding="utf-8"))
    base = next(e for e in entries if e["file"].replace("\\", "/").endswith("Game/System/LumaLoader.cpp"))
    listed = []
    for unit in UNITS:
        copy = defined / unit
        copy.parent.mkdir(parents=True, exist_ok=True)
        listed.append(dict(base, file=str(copy)))
    (out / "commands.json").write_text(json.dumps(listed), encoding="utf-8")
    for unit in UNITS:
        code, so, se = run([sys.executable, "-X", "utf8", "-B", str(HERE / "instrument.py"), str(FIXTURE / unit),
                            str(defined / unit), "--commands", str(out / "commands.json"), "--root", str(FIXTURE)])
        if code != 0:
            sys.exit("places_fixture_test.py: instrument.py failed on %s:\n%s" % (unit, (so + se)[-3000:]))
        if not pathlib.Path(str(defined / unit) + ".places").is_file():
            failed.append("%s: no .places part" % unit)

    # 2. the types file
    code, so, se = run([sys.executable, "-X", "utf8", "-B", str(HERE / "gen_types.py"), "--commands", commands,
                        "--out", str(out / "gen"), "--tree", str(FIXTURE), "--defined", str(defined)])
    print("gen_types:", (so.strip() or se.strip())[-600:])
    if code != 0:
        sys.exit("places_fixture_test.py: the generator failed:\n" + se[-3000:])

    # 3. placement.py
    place = [sys.executable, "-X", "utf8", "-B", str(HERE / "placement.py"), "--types", str(out / "gen" / "luma_types.json"),
             "--defined", str(defined), "--out", str(out / "placement.json")]
    code, so, se = run(place)
    print(so.strip() or se.strip())
    if code != 0:
        sys.exit("places_fixture_test.py: placement.py failed:\n" + se[-3000:])
    have = json.loads((out / "placement.json").read_text(encoding="utf-8"))
    got_factory = [(r["name"], r["class"], r["creator"]) for r in have["factory"]]
    if got_factory != EXPECTED_FACTORY:
        failed.append("factory: %s, not %s" % (got_factory, EXPECTED_FACTORY))
    args = have["placement_args"]
    for cls, want in EXPECTED_ARGS.items():
        got = [{k: r.get(k) for k in KEYS} for r in args.get(cls, [])]
        if got != want:
            failed.append("%s: %s, not %s" % (cls, json.dumps(got), json.dumps(want)))
        for r in args.get(cls, []):
            if not r.get("file") or not isinstance(r.get("line"), int) or r["line"] <= 0 or not r.get("signature"):
                failed.append("%s: a row without its file, line or signature: %s" % (cls, r))
    extra = sorted(c for c in args if c not in EXPECTED_ARGS)
    if extra:
        failed.append("classes not expected: %s" % extra)
    if [l["function"] for l in have["left"]] != ["pfUnused"]:
        failed.append("left: %s, not pfUnused alone" % have["left"])
    written = [r["written"] for r in args.get("PfDome", [])]
    if written != ["&dome", "&this->mDome"]:
        failed.append("PfDome's written arguments: %s" % written)

    # 4. an instrumented unit without its .places part
    pathlib.Path(str(defined / UNITS[0]) + ".places").unlink()
    code, so, se = run(place)
    named = "places_fixture.cpp: instrumented, but no .places part"
    if code == 0 or named not in se:
        failed.append("placement.py did not stop naming %r: %s" % (named, se.strip()[-300:]))

    for f in failed:
        print("FAIL", f)
    print("places fixture: %s" % ("PASS" if not failed else "%d failed" % len(failed)))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
