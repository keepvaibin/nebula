"""functions_fixture_test.py --commands COMPILE_COMMANDS [--out DIR]

The function rows' facts (the checker design's C-1), on the fixture (tools/lumacore/tests/functions_fixture), through
the real tools with a game unit's flags (Game/System/LumaLoader.cpp's command):
  1. instrument.py writes the fixture unit's copy, .rows and .functions; its identity() check holds on every row;
  2. gen_types.py (--tree the fixture, --defined the copy's folder) writes luma_functions.json: each row's facts are
     the expected ones exactly (kind, owner as the types file names the record, virtual, pure, const, what it
     overrides and whether that is a row);
  3. gen_types.py stops, naming the fault, on planted ones: a unit without .functions, a row without facts, an owner
     that is no record, an override with no name, facts without "marks";
  4. C-6's marks: each row's "marks" are the expected ones (step 2), and instrument.py stops, naming the file, line and
     fault, on each planted wrong mark.
Exit 0 only when all four hold.
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


def fact(name, signature, kind, owner, virtual=False, pure=False, const=False, overrides=(), marks=None):
    if marks is None:
        inside = signature[signature.index("(") + 1:signature.rindex(")")]
        marks = [None] * (inside.count(",") + 1 if inside.strip() else 0)
    return {"name": name, "signature": signature, "kind": kind, "owner": owner, "virtual": virtual, "pure": pure,
            "const": const, "overrides": [{"name": n, "signature": s, "row": r} for n, s, r in overrides], "marks": marks}


def mark(way, count=None, fixed=None, nullable=False):
    return {"way": way, "count": count, "fixed": fixed, "nullable": nullable}


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
    fact("fnOut", "void(FnVec *)", "free", None, marks=[mark("out")]),
    fact("fnIn", "void(const float *)", "free", None, marks=[mark("in")]),
    fact("fnInout", "void(FnColor *)", "free", None, marks=[mark("inout")]),
    fact("fnKept", "void(int *)", "free", None, marks=[mark("kept")]),
    fact("fnCount", "void(const float *, int, FnVec *)", "free", None,
         marks=[mark("in", count=1), None, mark("out", nullable=True)]),
    fact("fnFixed", "void(float *)", "free", None, marks=[mark("out", fixed=12)]),
    fact("fnInside", "void(const FnV2<float> *, FnVec4 *)", "free", None, marks=[mark("in"), mark("inout")]),
    fact("fnDefined", "void(FnVec *)", "free", None, marks=[mark("out")]),
    fact("FnMarked::get", "void(FnVec *) const", "method", "FnMarked", const=True, marks=[mark("out")]),
    fact("fnByMacro", "void(float *)", "free", None, marks=[mark("out")]),
]

# C-6's refusals: (what, the unit's text after its first line `#include "Game/LumaMarks.hpp"`, what the stop names
# after the unit's path; {f} is that path)
REFUSED = [
    ("a mark on an int", "void f(LUMA_OUT int x);",
     "{f}:2: f parameter 1 (x): LUMA_OUT on a parameter of type int: a mark takes a pointer to a number"),
    ("a mark on a record pointer", "struct R { int a; };\nvoid f(LUMA_OUT R* r);",
     "{f}:3: f parameter 1 (r): LUMA_OUT on a parameter of type R *"),
    ("a mark on a pointer to a pointer", "void f(LUMA_OUT float** p);",
     "{f}:2: f parameter 1 (p): LUMA_OUT on a parameter of type float **"),
    ("a COUNT naming a float", "void f(LUMA_IN LUMA_COUNT(n) const float* p, float n);",
     "{f}:2: f parameter 1 (p): LUMA_COUNT(n) names a parameter of type float, not an integer of one width"),
    ("a COUNT naming no parameter", "void f(LUMA_IN LUMA_COUNT(m) const float* p, int n);",
     "{f}:2: f parameter 1 (p): LUMA_COUNT(m) names no parameter of this declaration"),
    ("a COUNT naming its own parameter", "void f(LUMA_IN LUMA_COUNT(p) const float* p);",
     "{f}:2: f parameter 1 (p): LUMA_COUNT(p) names its own parameter"),
    ("two ways", "void f(LUMA_IN LUMA_OUT float* p);", "{f}:2: f parameter 1 (p): two ways (LUMA_IN and LUMA_OUT)"),
    ("a mark twice", "void f(LUMA_OUT LUMA_OUT float* p);", "{f}:2: f parameter 1 (p): LUMA_OUT twice"),
    ("a COUNT with no way", "void f(LUMA_COUNT(n) const float* p, int n);",
     "{f}:2: f parameter 1 (p): LUMA_COUNT with no way (LUMA_IN, LUMA_OUT or LUMA_INOUT)"),
    ("a COUNT with KEPT", "void f(LUMA_KEPT LUMA_COUNT(n) float* p, int n);",
     "{f}:2: f parameter 1 (p): LUMA_COUNT with LUMA_KEPT"),
    ("a COUNT_FIXED with no way", "void f(LUMA_COUNT_FIXED(3) float* p);",
     "{f}:2: f parameter 1 (p): LUMA_COUNT_FIXED with no way"),
    ("COUNT and COUNT_FIXED", "void f(LUMA_IN LUMA_COUNT(n) LUMA_COUNT_FIXED(3) const float* p, int n);",
     "{f}:2: f parameter 1 (p): LUMA_COUNT and LUMA_COUNT_FIXED on one parameter"),
    ("a COUNT_FIXED of 0", "void f(LUMA_OUT LUMA_COUNT_FIXED(0) float* p);",
     "{f}:2: f parameter 1 (p): LUMA_COUNT_FIXED(0): the count is a number from 1 to 4096"),
    ("a COUNT_FIXED of 4097", "void f(LUMA_OUT LUMA_COUNT_FIXED(4097) float* p);",
     "{f}:2: f parameter 1 (p): LUMA_COUNT_FIXED(4097): the count is a number from 1 to 4096"),
    ("a NULLABLE with no way", "void f(LUMA_NULLABLE float* p);",
     "{f}:2: f parameter 1 (p): LUMA_NULLABLE with no way"),
    ("a NULLABLE with KEPT", "void f(LUMA_KEPT LUMA_NULLABLE float* p);",
     "{f}:2: f parameter 1 (p): LUMA_NULLABLE with LUMA_KEPT"),
    ("two declarations marking one parameter differently", "void f(LUMA_IN float* p);\nvoid f(LUMA_OUT float* p);",
     "{f}:2 and {f}:3: f parameter 1 is marked LUMA_IN in one declaration and LUMA_OUT in the other"),
    ("a mark before the result type", "LUMA_OUT void f(float* p);", "{f}:2: LUMA_OUT marks no parameter"),
    ("a mark after the last parameter", "void f(float* p LUMA_OUT);", "{f}:2: LUMA_OUT marks no parameter"),
    ("a mark on a function template", "template <class T> void f(LUMA_OUT float* p);",
     "{f}:2: LUMA_OUT is on a function template"),
    ("a mark in a class template", "template <class T> struct S { void f(LUMA_OUT float* p); };",
     "{f}:2: LUMA_OUT is on a function in a template or an unnamed class"),
    ("a mark defined elsewhere", "#undef LUMA_OUT\n#define LUMA_OUT\nvoid f(LUMA_OUT float* p);",
     "{f}:4: LUMA_OUT is not the mark native/petari/include/Game/LumaMarks.hpp defines"),
    # A18M-1: numbers LumaScript does not cross (a buffer, a width that differs per system)
    ("a mark on a char pointer", "void f(LUMA_OUT char* p);", "{f}:2: f parameter 1 (p): LUMA_OUT on a parameter of type char *"),
    ("a mark on a long pointer", "void f(LUMA_OUT long* p);", "{f}:2: f parameter 1 (p): LUMA_OUT on a parameter of type long *"),
    ("a COUNT naming a long", "void f(LUMA_IN LUMA_COUNT(n) const float* p, long n);",
     "{f}:2: f parameter 1 (p): LUMA_COUNT(n) names a parameter of type long, not an integer of one width"),
    # A18M-4: a mark inside a macro's own text
    ("a mark inside a macro's text", "#define OUT_VEC LUMA_OUT float*\nvoid f(OUT_VEC p);",
     "{f}:2: LUMA_OUT is written inside a macro (OUT_VEC)"),
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
        ("a row without facts", json.dumps(facts[:-1]), "row %d is %s %s" % (len(facts), facts[-1]["name"], facts[-1]["signature"])),
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
    no_marks = json.dumps([{k: v for k, v in f.items() if k != "marks"} if f["name"] == "fnFree" else f for f in facts])
    facts_file.write_text(no_marks + "\n", encoding="utf-8")
    code, so, se = run(generate)
    print("planted, facts without marks:", se.strip()[-300:])
    if code == 0 or "fnFree int(int) has no marks (C-6)" not in se:
        failed.append("gen_types.py did not stop naming facts without marks")
    facts_file.write_text(good, encoding="utf-8")

    # 4. C-6's refusals, each a unit of its own with LumaLoader.cpp's command
    planted_dir = out / "planted"
    planted_dir.mkdir()
    for n, (what, text, named) in enumerate(REFUSED, 1):
        source = planted_dir / ("p%02d.cpp" % n)
        source.write_text('#include "Game/LumaMarks.hpp"\n' + text + "\n", encoding="utf-8")
        target = planted_dir / ("p%02d.out.cpp" % n)
        commands_file = planted_dir / ("p%02d.json" % n)
        commands_file.write_text(json.dumps([dict(entry, file=str(target))]), encoding="utf-8")
        code, so, se = run([sys.executable, "-X", "utf8", "-B", str(HERE / "instrument.py"), str(source), str(target),
                            "--commands", str(commands_file), "--root", str(planted_dir)])
        wanted = "instrument.py: " + named.replace("{f}", source.resolve().as_posix())
        print("refused, %s:" % what, (so + se).strip()[-300:])
        if code == 0 or wanted not in so + se:
            failed.append("instrument.py did not stop with %r (%s)" % (wanted, what))

    if failed:
        print("functions_fixture_test.py: FAILED:\n  " + "\n  ".join(failed))
        sys.exit(1)
    print("functions_fixture_test.py: pass (%d rows)" % len(have))


if __name__ == "__main__":
    main()
