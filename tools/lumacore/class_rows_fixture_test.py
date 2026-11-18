"""class_rows_fixture_test.py --commands COMPILE_COMMANDS [--out DIR]

LumaCore's class rows (gen_lumacore.py, gen_classes.py) against the game's headers and the compiler:
  1. the committed lumacore_class_ids.hpp and lumacore_classes.cpp are what the generator makes from this tree's
     headers now (a class a header gained without a new run of gen_lumacore.py fails here, by name);
  2. no class name is in two headers;
  3. with a game unit's flags, kClassId of a class that has a row compiles, and kClassId of one that has none stops the
     compile (M-110: it was 0, NameObj's id, with no error).
Exit 0 only when all three hold.
"""
import json
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[1]
sys.path.insert(0, str(HERE))
import gen_classes  # noqa: E402
import instrument as I  # noqa: E402

INC = TREE / "native" / "petari" / "include"
IDS = INC / "Game" / "System" / "generated" / "lumacore_class_ids.hpp"
ROWS = TREE / "native" / "petari" / "src" / "Game" / "System" / "generated" / "lumacore_classes.cpp"


def option(flag, default=None):
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv[:-1] else default


def text(path):
    return path.read_bytes().replace(b"\r\n", b"\n").decode("utf-8")


def first_difference(made, kept):
    a, b = made.split("\n"), kept.split("\n")
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return "line %d: the generator makes %r, the file has %r" % (i + 1, x, y)
    return "the generator makes %d lines, the file has %d" % (len(a), len(b))


def main():
    commands = option("--commands") or sys.exit(__doc__)
    out = pathlib.Path(option("--out", str(TREE / "build" / "class-rows-fixture"))).resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    failed = []

    # 1 and 2: the generator, in memory
    rows, dupes = gen_classes.classes(INC)
    for made, path in ((gen_classes.emit_class_ids(rows), IDS), (gen_classes.emit_class_rows(rows), ROWS)):
        kept = text(path)
        if made != kept:
            failed.append("%s is not what gen_lumacore.py makes from the headers now (%s): run tools/lumacore/gen_lumacore.py"
                          % (path.name, first_difference(made, kept)))
    if dupes:
        failed.append("classes named in two headers: " + ", ".join(sorted(dupes)))
    print("class rows: %d, %d named twice" % (len(rows), len(dupes)))

    # 3: the compiler, with a game unit's flags (the commands may come from another tree's build: this tree's headers)
    entries = json.loads(pathlib.Path(commands).read_text(encoding="utf-8"))
    entry = next(e for e in entries if e["file"].replace("\\", "/").endswith("native/petari/src/Game/System/LumaClasses.cpp"))
    raw = entry.get("arguments") or I.split_command(entry["command"])
    built = entry["file"].replace("\\", "/")
    built = built[:built.rindex("/native/petari/")]
    raw = [a.replace(built, TREE.as_posix()) for a in raw]
    probes = {
        "has_row": ("::NameObj", True),
        "no_row": ("LumaNoRowProbe", False),
    }
    for name, (cls, compiles) in probes.items():
        source = out / ("%s.cpp" % name)
        source.write_text('#include "Game/System/generated/lumacore_class_ids.hpp"\n'
                          "class LumaNoRowProbe;\n"
                          "u16 probe() { return lumacore::kClassId< %s >; }\n" % cls, encoding="utf-8")
        args = [a for a in I.parse_arguments(raw, source) if a != str(source)] + ["-fsyntax-only", str(source)]
        p = subprocess.run(args, cwd=entry["directory"], capture_output=True, text=True, encoding="utf-8", errors="replace")
        said = p.stdout + p.stderr
        if compiles and p.returncode != 0:
            failed.append("kClassId< %s > does not compile:\n%s" % (cls, said[-2000:]))
        elif not compiles and p.returncode == 0:
            failed.append("kClassId< %s > (a class with no row) compiles: the build would name its objects NameObj" % cls)
        elif not compiles and "has no row" not in said:
            failed.append("kClassId< %s > stops the compile, but not with the generator's words:\n%s" % (cls, said[-2000:]))
        else:
            print("kClassId< %s >: %s" % (cls, "compiles" if compiles else "stops the compile"))

    if failed:
        print("class_rows_fixture_test.py: FAIL")
        for f in failed:
            print("  " + f)
        sys.exit(1)
    print("class_rows_fixture_test.py: pass")


if __name__ == "__main__":
    main()
