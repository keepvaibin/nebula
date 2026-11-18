"""code_checks_test.py: the gate runs the tree's code checks as repo/code (P-043).

python -X utf8 -I tools/gate/tests/code_checks_test.py

- This tree passes all three (the console census, cast_check, byte_order_check).
- In a scratch repository, game code that reads a number through a dereferenced pointer cast fails cast_check, and a
  colour read as a word fails byte_order_check; the same files without them pass. The failure names the file.
- Every level's build part runs repo/code.
Exit 0 when every case holds.
"""
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


code_checks = getattr(gate, "code_checks", None)
check(code_checks is not None, "gate.code_checks exists")
if code_checks is not None:
    ok, said = code_checks(gate.TREE)
    check(ok, "this tree passes the code checks: %s" % said)

    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        source = t / "native" / "petari" / "src" / "Game" / "Planted.cpp"
        source.parent.mkdir(parents=True)
        subprocess.run([gate.GIT, "init", "-q", str(t)], check=True)
        reviewed = "native/petari/include/Game/Camera/CameraAnim.hpp"  # cast_check's reviewed file must be there
        (t / reviewed).parent.mkdir(parents=True)
        (t / reviewed).write_bytes((gate.TREE / reviewed).read_bytes())

        def with_text(text):
            source.write_text(text, encoding="utf-8")
            subprocess.run([gate.GIT, "-C", str(t), "add", "-A"], check=True)
            return code_checks(t, scripts=("cast_check.py", "byte_order_check.py"))

        ok, said = with_text("u32 read(const u8* p) {\n    return readBytes< u32 >(p);\n}\n")
        check(ok, "plain code passes: %s" % said)
        ok, said = with_text("u32 read(const u8* p) {\n    return *reinterpret_cast< const u32* >(p);\n}\n")
        check(not ok and "cast_check FAILED" in said and "Planted.cpp" in said,
              "a number read through a dereferenced cast fails cast_check, naming the file: %s" % said)
        ok, said = with_text("u32 word(const Color8& color) {\n    return *(u32*)&color;\n}\n")
        check(not ok and "byte_order_check FAILED" in said and "Planted.cpp" in said,
              "a colour read as a word fails byte_order_check, naming the file: %s" % said)

    import inspect
    check('g.check("repo/code", repo_code(g))' in inspect.getsource(gate.build_part),
          "every level's build part runs repo/code")

if failures:
    print("code_checks_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("code_checks_test.py: pass")
