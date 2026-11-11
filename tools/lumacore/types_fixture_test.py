"""types_fixture_test.py --commands COMPILE_COMMANDS [--out DIR]

Contract types TY-8, on the fixture (tools/lumacore/tests/types_fixture): what the game's headers have too little of
(a name hidden by a derived class's, one two bases up, ambiguous ones, one through a virtual base, bit-fields through a
base, an instance only an alias names, anonymous members, globals) through the real generator (gen_types.py --tree),
the real kernel (LumaTypes.cpp) and the real comparison (compare_types.py):
  1. gen_types.py writes the fixture's table and JSON;
  2. the table, the kernel and the driver are compiled with a game unit's flags and run: each answer against the
     compiler's (offsetof, the bit-field's own access, a guard page for TY-4), then the kernel's description;
  3. compare_types.py passes on that description, and fails, naming the row, on a copy of the JSON with one offset
     planted wrong.
Exit 0 only when all three hold.
"""
import json
import os
import re
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[1]
FIXTURE = HERE / "tests" / "types_fixture"
sys.path.insert(0, str(HERE))
import instrument as I  # noqa: E402


def option(flag, default=None):
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv else default


def names_in(text):
    """The names a type text holds outside template arguments; an instance is one name with its arguments
    (`FxBox<u8> *` holds FxBox<u8>)."""
    out, i = [], 0
    while i < len(text):
        m = re.compile(r"[A-Za-z_][\w:]*").match(text, i)
        if not m:
            i += 1
            continue
        name, i = m.group(0), m.end()
        if i < len(text) and text[i] == "<":
            depth, j = 0, i
            while j < len(text):
                depth += {"<": 1, ">": -1}.get(text[j], 0)
                j += 1
                if depth == 0:
                    break
            name, i = name + text[i:j], j
        out.append(name)
    return out


def run(args, **kw):
    r = subprocess.run(args, capture_output=True, text=True, **kw)
    return r.returncode, r.stdout, r.stderr


def main():
    commands = option("--commands") or sys.exit(__doc__)
    out = pathlib.Path(option("--out", str(TREE / "build" / "types-fixture"))).resolve()
    if out.exists():
        shutil.rmtree(out)
    (out / "defined").mkdir(parents=True)  # no unit defines anything: a declared-only global is left out
    failed = []

    # 1. the generator
    code, so, se = run([sys.executable, "-X", "utf8", "-B", str(HERE / "gen_types.py"), "--commands", commands,
                        "--out", str(out), "--tree", str(FIXTURE), "--defined", str(out / "defined")])
    print("gen_types:", so.strip() or se.strip()[-2000:])
    if code != 0:
        sys.exit("types_fixture_test.py: the generator failed")
    data = json.loads((out / "luma_types.json").read_text(encoding="utf-8"))
    left = {(x["name"], x["reason"]) for x in data["left_out"]}
    for want in (("FxBoth.dup", "ambiguous: FxA::dup, FxB::dup"), ("FxDiamond.v", "through the virtual base FxV"),
                 ("gFxDeclaredOnly", "declared, defined nowhere in the game"), ("FxBox", "a template not used with these arguments")):
        if want not in left:
            failed.append("left_out lacks %s: %s" % want)
    rows = {r["name"]: r for r in data["types"]}
    # the generator's own reader of the names a text holds (its build check), on texts the fixture has none of
    import gen_types  # noqa: E402
    for text, want in (("FxMid &(*)(FxBase *, FxBox<u16> &)", ["FxMid", "FxBase", "FxBox<u16>"]),
                       ("s32 FxBase::*", ["s32", "FxBase"]),
                       ("const NebulaList<Foo>::Link *", ["NebulaList<Foo>::Link"]),
                       ("A<x>::B<y>::C &", ["A<x>::B<y>::C"]),
                       ("unsigned long long : 3", []),
                       ("Ui::NameText<17> [2]", ["Ui::NameText<17>"])):
        if gen_types.type_names(text) != want:
            failed.append("type_names(%r) is %s, not %s" % (text, gen_types.type_names(text), want))
    # TY-9: every name a text holds is a row (record, enum, alias), a built-in word or left out.
    known = set(rows) | {x["name"] for x in data["left_out"]}
    words = {"const", "volatile", "signed", "unsigned", "char", "short", "int", "long", "float", "double", "bool", "void"}
    texts = [(r["name"] + "." + f["name"], f["type"]) for r in data["types"] for f in r.get("fields", [])]
    texts += [(g["name"], g["type"]) for g in data["globals"]]
    texts += [(r["name"], r["of"]) for r in data["types"] if r.get("of")]
    for where, text in texts:
        for n in names_in(text):
            if n not in words and n not in known:
                failed.append("TY-9: %s's text %r names %s, which is no row" % (where, text, n))
    for n in ("uintptr_t", "FxBox<u8>", "FxBox<u16>"):
        if n not in rows:
            failed.append("TY-9: %s has no row" % n)
    if ("FxAnon.named", "a type with no name a mod can write") not in left:
        failed.append("TY-9: FxAnon.named is not left out")
    # TY-10: each record says both, as the compiler's traits (the table's static_asserts) say.
    keys = ("trivial_copy", "trivial_destroy", "no_copy")
    for n, want in (("FxBase", (True, True, False)), ("FxL", (False, True, False)), ("FxOwns", (False, False, False)),
                    ("FxCopies", (False, True, False)), ("FxDefaulted", (True, True, False)),
                    ("FxNoCopy", (True, True, True)), ("FxHoldsNoCopy", (True, True, True)),
                    ("FxMoveOnly", (True, True, False)), ("FxHoldsOwner", (False, False, False)),
                    ("FxPoly", (False, True, False)), ("FxConstField", (True, True, False))):
        r = rows.get(n)
        have = None if r is None else tuple(r.get(k) for k in keys)
        if have != want:
            failed.append("TY-10: %s should be %s %s; it is %s" % (n, keys, want, have))
    missing = [r["name"] for r in data["types"] if r.get("kind") in ("struct", "class", "union")
               and any(k not in r for k in keys)]
    if missing:
        failed.append("TY-10: records without the three keys: " + ", ".join(missing))

    # 2. table + kernel + driver, a game unit's flags
    entries = json.loads(pathlib.Path(commands).read_text(encoding="utf-8"))
    entry = next(e for e in entries if e["file"].replace("\\", "/").endswith("Game/System/LumaLoader.cpp"))
    raw = entry.get("arguments") or I.split_command(entry["command"])
    objects = []
    sources = [(out / "luma_types_table.cpp", ["-Xclang", "-fno-access-control"]),
               (TREE / "native/petari/src/Game/System/LumaTypes.cpp", []),
               (FIXTURE / "types_fixture_driver.cpp", [])]
    compiler = raw[0]
    for source, extra in sources:
        args = [a for a in I.parse_arguments(raw, source) if a not in ("-fsyntax-only", "-Wno-everything", str(source))]
        obj = out / (source.stem + ".o")
        args[1:1] = extra + ["-I", str(FIXTURE)]
        code, so, se = run(args + ["-c", str(source), "-o", str(obj)], cwd=entry["directory"])
        if code != 0:
            sys.exit("types_fixture_test.py: %s does not compile:\n%s" % (source.name, se[-3000:]))
        objects.append(str(obj))
    exe = out / ("types_fixture_driver" + (".exe" if os.name == "nt" else ""))
    link = [compiler] + objects + ["-o", str(exe)] + (["-fuse-ld=lld"] if os.name == "nt" else [])
    code, so, se = run(link)
    if code != 0:
        sys.exit("types_fixture_test.py: the driver does not link:\n" + se[-3000:])
    described = out / "luma_types_described.json"
    code, so, se = run([str(exe), str(described)])
    print("driver:", so.strip()[-3000:], se.strip()[-1000:])
    if code != 0:
        failed.append("the driver's checks (exit %d)" % code)

    # 3. the comparison: passes as built, fails naming a planted difference
    compare = [sys.executable, "-X", "utf8", "-B", str(HERE / "compare_types.py")]
    code, so, se = run(compare + [str(out / "luma_types.json"), str(described)])
    print("compare:", (so + se).strip()[-2000:])
    if code != 0:
        failed.append("compare_types.py on the fixture as built")
    planted = json.loads((out / "luma_types.json").read_text(encoding="utf-8"))
    mid = next(r for r in planted["types"] if r.get("name") == "FxMid")
    mid["fields"][0]["offset"] += 4
    (out / "planted.json").write_text(json.dumps(planted), encoding="utf-8")
    code, so, se = run(compare + [str(out / "planted.json"), str(described)])
    print("compare, planted:", se.strip()[-600:])
    if code == 0 or "field FxMid.y" not in se:
        failed.append("compare_types.py did not name the planted difference (field FxMid.y)")

    if failed:
        print("types_fixture_test.py: FAILED:\n  " + "\n  ".join(failed))
        sys.exit(1)
    print("types_fixture_test.py: pass")


if __name__ == "__main__":
    main()
