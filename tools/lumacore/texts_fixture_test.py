"""texts_fixture_test.py --commands COMPILE_COMMANDS [--out DIR]

Which `const char *` parameters the game reads during the call only (N-4002), on the fixture
(tools/lumacore/tests/texts_fixture), through the real tools with a game unit's flags (Game/System/LumaLoader.cpp's
command):
  1. instrument.py writes the fixture unit's copy, .rows and .functions;
  2. gen_types.py (--tree the fixture, --defined the copy's folder) writes luma_functions.json: each row's marks are
     the expected ones exactly (`in` on each text parameter proven read only, nothing on any other), and
     luma_texts.left names each text parameter left unmarked with the expected reason, and no other: a row's, and
     each of a function with no row that the proof followed (HELPERS_LEFT).
Exit 0 only when both hold.
"""
import json
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
FIXTURE = HERE / "tests" / "texts_fixture"
IN = {"way": "in", "count": None, "fixed": None, "nullable": False}

# (name, signature): (marks, {parameter number: what luma_texts.left says of it})
EXPECTED = {
    ("txLen", "int(const char *)"): ([IN], {}),
    ("txCompare", "int(const char *, const char *)"): ([IN, IN], {}),
    ("txFirst", "int(const char *)"): ([IN], {}),
    ("txDeref", "int(const char *)"): ([IN], {}),
    ("txTest", "int(const char *)"): ([IN], {}),
    ("txLoop", "int(const char *)"): ([IN], {}),
    ("txCopy", "void(char *, const char *)"): ([None, IN], {}),
    ("txFormat", "void(char *, const char *)"): ([None, IN], {}),
    ("txCalls", "int(const char *)"): ([IN], {}),
    ("txKeep", "void(const char *)"): ([None], {1: ": stored"}),
    ("txCallsKeeper", "void(const char *)"): ([None], {1: "given to txKeep void(const char *) argument 1"}),
    ("txStaticLocal", "bool(const char *)"): ([None], {1: "kept in sSeen"}),
    ("txReturn", "const char *(const char *)"): ([None], {1: "returned"}),
    ("txStrchr", "int(const char *)"): ([IN], {}),
    ("txStrchrKept", "void(const char *)"): ([None], {1: ": stored"}),
    ("txAddress", "int(const char *)"): ([None], {1: "its address taken"}),
    ("txLambda", "int(const char *)"): ([None], {1: "used in a lambda"}),
    ("txCast", "bool(const char *)"): ([None], {1: "converted to unsigned long long"}),
    ("txPointer", "int(const char *, int (*)(const char *))"): ([None, None], {1: "a call through a function pointer"}),
    ("txRecursive", "int(const char *, int)"): ([IN, None], {}),
    ("txMutualA", "int(const char *, int)"): ([IN, None], {}),
    ("txMutualB", "int(const char *, int)"): ([IN, None], {}),
    ("txKeepsA", "int(const char *, int)"): ([None, None], {1: "given to txKeepsB int(const char *, int) argument 1"}),
    ("txKeepsB", "int(const char *, int)"): ([None, None], {1: ": stored"}),
    ("TxName::TxName", "(const char *)"): ([None], {1: "kept by a member initializer"}),
    ("txMakesName", "bool(const char *)"): ([None], {1: "given to TxName::TxName (const char *) argument 1"}),
    ("TxBase::~TxBase", "()"): ([], {}),
    ("TxBase::look", "int(const char *)"): ([IN], {}),
    ("TxBase::keep", "int(const char *)"): ([None], {1: "overridden by TxKeeper::keep int(const char *)"}),
    ("TxKeeper::look", "int(const char *)"): ([IN], {}),
    ("TxKeeper::keep", "int(const char *)"): ([None], {1: ": stored"}),
    ("TxKeeper::pure", "int(const char *)"): ([None], {1: ": stored"}),
    ("txLook", "int(TxBase *, const char *)"): ([None, IN], {}),
    ("txKeepThrough", "int(TxBase *, const char *)"): ([None, None], {2: "given to TxBase::keep int(const char *) argument 1"}),
    # TxBase::pure and TxMid::pure have no row; TxKeeper::pure overrides TxMid's, which overrides TxBase's.
    ("txPure", "int(TxBase *, const char *)"): ([None, None], {2: "given to TxBase::pure int(const char *) argument 1"}),
    ("txOwnLook", "int(TxKeeper *, const char *)"): ([None, IN], {}),
    ("txSum", "int(int, ...)"): ([None], {}),
    ("txGivesToDots", "int(const char *)"): ([None], {1: "given to txSum's ..."}),
    ("txLog", "int(const char *, ...)"): ([None], {1: "a variadic function"}),
    ("txLateCopy", "void(const char *)"): ([None], {1: ": stored"}),
    ("txBranch", "void(const char *, bool)"): ([None, None], {1: ": stored"}),
    ("txList", "int(const char *)"): ([None], {1: "kept in an initializer list"}),
    ("txInsideKept", "void(const char *)"): ([None], {1: ": stored"}),
    ("txInsideRead", "int(const char *)"): ([IN], {}),
    ("txTwo", "int(const char *, const char *)"): ([IN, None], {2: ": stored"}),
    ("txUsesHeaderRead", "int(const char *)"): ([IN], {}),
    ("txUsesHeaderKeep", "void(const char *)"): ([None], {1: "given to txHeaderKeep void(const char *) argument 1"}),
    ("txSubstring", "int(const char *, const char *)"): ([IN, IN], {}),
    ("TxShown::~TxShown", "()"): ([], {}),
    ("TxShown::show", "int(const char *)"): ([None], {1: "overridden by TxShownKeeper::show int(const char *)"}),
    ("txShow", "int(TxShown *, const char *)"): ([None, None], {2: "given to TxShown::show int(const char *) argument 1"}),
}

# The functions with no row the proof follows (defined in the header) that keep a text: (name, signature, parameter
# number): what luma_texts.left says of it. txHeaderCompare is followed too and keeps nothing.
HELPERS_LEFT = {
    ("txHeaderKeep", "void(const char *)", 1): ": stored",
    ("TxShownKeeper::show", "int(const char *)", 1): ": stored",
}


def option(flag, default=None):
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv else default


def run(args):
    r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout, r.stderr


def main():
    commands = option("--commands") or sys.exit(__doc__)
    out = pathlib.Path(option("--out", str(HERE.parents[1] / "build" / "texts-fixture"))).resolve()
    if out.exists():
        shutil.rmtree(out)
    defined = out / "defined"
    defined.mkdir(parents=True)
    failed = []

    # 1. instrument.py, with LumaLoader.cpp's command given to the copy
    entries = json.loads(pathlib.Path(commands).read_text(encoding="utf-8"))
    entry = dict(next(e for e in entries if e["file"].replace("\\", "/").endswith("Game/System/LumaLoader.cpp")))
    copy = defined / "texts_fixture.cpp"
    entry["file"] = str(copy)
    (out / "commands.json").write_text(json.dumps([entry]), encoding="utf-8")
    code, so, se = run([sys.executable, "-X", "utf8", "-B", str(HERE / "instrument.py"), str(FIXTURE / "texts_fixture.cpp"),
                        str(copy), "--commands", str(out / "commands.json"), "--root", str(FIXTURE)])
    if code != 0:
        sys.exit("texts_fixture_test.py: instrument.py failed:\n" + (so + se)[-3000:])

    # 2. gen_types.py's proof
    code, so, se = run([sys.executable, "-X", "utf8", "-B", str(HERE / "gen_types.py"), "--commands", commands,
                        "--out", str(out / "gen"), "--tree", str(FIXTURE), "--defined", str(defined)])
    if code != 0:
        sys.exit("texts_fixture_test.py: the generator failed:\n" + (so + se)[-3000:])
    have = {(f["name"], f["signature"]): f for f in
            json.loads((out / "gen" / "luma_functions.json").read_text(encoding="utf-8"))["functions"]}
    left = (out / "gen" / "luma_texts.left").read_text(encoding="utf-8").splitlines()
    named = set()
    for (name, signature), (marks, why) in EXPECTED.items():
        f = have.get((name, signature))
        if f is None:
            failed.append("%s %s: no row" % (name, signature))
            continue
        if "texts" in f:
            failed.append("%s %s: its texts facts reach luma_functions.json" % (name, signature))
        if f["marks"] != marks:
            failed.append("%s %s: marks %s, not %s" % (name, signature, f["marks"], marks))
        for number, reason in why.items():
            head = "%s %s parameter %d: " % (name, signature, number)
            lines = [line for line in left if line.startswith(head)]
            named.update(lines)
            if len(lines) != 1 or reason not in lines[0]:
                failed.append("luma_texts.left on %s: %s, not one line with %r" % (head.strip(), lines, reason))
    for (name, signature, number), reason in HELPERS_LEFT.items():
        head = "%s %s parameter %d (no row): " % (name, signature, number)
        lines = [line for line in left if line.startswith(head)]
        named.update(lines)
        if len(lines) != 1 or reason not in lines[0]:
            failed.append("luma_texts.left on %s: %s, not one line with %r" % (head.strip(), lines, reason))
    extra = [line for line in left if line not in named]
    if extra:
        failed.append("luma_texts.left names more: %s" % extra)
    if set(have) != set(EXPECTED):
        failed.append("rows not in the expected list: %s; expected and not rows: %s" % (
            sorted(set(have) - set(EXPECTED)), sorted(set(EXPECTED) - set(have))))
    if failed:
        sys.exit("texts_fixture_test.py: FAILED:\n  " + "\n  ".join(failed))
    print("texts_fixture_test.py: %d rows, %d text parameters `in`, %d left with their reason" % (
        len(EXPECTED), sum(m == IN for marks, _ in EXPECTED.values() for m in marks), len(left)))


if __name__ == "__main__":
    main()
