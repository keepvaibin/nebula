"""console_census_test.py: tools/verify/console_census.py on small trees whose counts are known (T11-B, the model file's four section names).

    python -X utf8 -I tools/gate/tests/console_census_test.py [--work FOLDER]

The four section names GXMT, GXTG, GXTM and GXWP are counted by the open group `gx_sections`, which has a ceiling (31, the uses at the tip of
S0.1.5); `fifo` stays closed and refuses every other GX name:
  the four names alone, 31 uses     exit 0, fifo 0, gx_sections 31
  a 32nd use                        exit 1, "OVER THE CEILING", the file named
  GXBegin( / GXSetTevColor / GXZZ   exit 1, fifo, the file named
  GXMTZ, GXMT2, GXTGX               refused by fifo: only the four whole words are excluded
  GP and FIFO names                 refused by fifo as before
  no file of the model's is in SKIP (the names are counted, not hidden)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "verify" / "console_census.py"
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def census(text, name="native/a.h"):
    """(exit code, output, {group: count}) of the census on a tree with one file."""
    with tempfile.TemporaryDirectory() as tmp:
        tree = pathlib.Path(tmp)
        for root in ("native", "runtime", "crates", "tools/dev", "docs"):
            (tree / root).mkdir(parents=True)
        (tree / name).write_text(text, encoding="utf-8")
        r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(SCRIPT), str(tree)], capture_output=True, text=True)
    counts = {m.group(1): int(m.group(2)) for m in re.finditer(r"^(\w+)\s+(\d+) (?:closed|open)", r.stdout, re.M)}
    return r.returncode, r.stdout, counts


def main():
    names = ["GXMT", "GXTG", "GXTM", "GXWP"]

    code, out, counts = census("")
    check(code == 0 and counts.get("gx_sections") == 0 and counts.get("fifo") == 0, "an empty tree: exit 0, both groups 0: %s" % out)

    code, out, counts = census("\n".join("/* %s */" % names[i % 4] for i in range(31)))
    check(code == 0 and counts["fifo"] == 0 and counts["gx_sections"] == 31 and "(ceiling 31)" in out,
          "the four names 31 times: exit 0, fifo 0, gx_sections 31: %s" % out)

    code, out, counts = census("\n".join("/* %s */" % names[i % 4] for i in range(32)), "native/model.h")
    check(code == 1 and counts["gx_sections"] == 32 and "OVER THE CEILING" in out and "native/model.h" in out,
          "a 32nd use: exit 1, over the ceiling, the file named: %s" % out)

    for text in ("GXBegin(GX_QUADS, 0, 4);", "GXSetTevColor(0, c);", "int GXZZ;", "GXMTZ x;", "b = GXMT2;", "GXTGX", "GXWPP"):
        code, out, counts = census(text, "native/b.cpp")
        check(code == 1 and counts["fifo"] >= 1 and counts["gx_sections"] == 0 and "native/b.cpp" in out,
              "%r is refused by fifo, not counted as a section name: exit %d %s" % (text, code, counts))

    for text in ("GPSetVtxDesc(1);", "u8 cmd_fifo[8];", "DisplayList d;", "Fifo f;"):
        code, out, counts = census(text, "native/c.cpp")
        check(code == 1 and counts["fifo"] >= 1, "%r is still refused by fifo: exit %d %s" % (text, code, counts))

    # The four names and another GX name in one file: the other is refused, the names counted.
    code, out, counts = census("GXMT GXTG GXTM GXWP GXBegin", "native/d.cpp")
    check(code == 1 and counts["fifo"] == 1 and counts["gx_sections"] == 4, "mixed: fifo 1, gx_sections 4: %s" % counts)

    # Nothing of the model's files is hidden.
    skip = re.search(r"^SKIP = \((.*?)^\)", SCRIPT.read_text(encoding="utf-8"), re.M | re.S).group(1)
    check(not any(w in skip for w in ("nebula_model", "model_write", "model_sections")), "no model file is in SKIP")

    if failures:
        print("console_census_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("console_census_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
