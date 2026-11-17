"""parse_scripts_test.py: tools/gate/parse_scripts.py on scripts written here.

python -X utf8 -I tools/gate/tests/parse_scripts_test.py [--work <folder>]

- a PowerShell file and a Python file that parse: exit 0, "all parse";
- a PowerShell argument written -Wl,--no-undefined (a comma is PowerShell's array operator, so the parser stops with
  "Missing argument in parameter list"): exit 1 and the file's line named;
- a Python file with a syntax fault: exit 1 and its line named.
Exit 0 when every case holds.
"""
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TOOL = HERE.parent / "parse_scripts.py"

GOOD_PS1 = '& clang++ -O2 "-Wl,--no-undefined" -o out.so\n'
BAD_PS1 = "$x = 1\n& clang++ -O2 -fPIC -shared -Wl,--no-undefined `\n    -Wall -o out.so\n"
GOOD_PY = "print('fine')\n"
BAD_PY = "x = 1\nif x\n    pass\n"


def run(files):
    r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(TOOL)] + [str(f) for f in files],
                       capture_output=True, text=True)
    return r.returncode, r.stdout


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else None
    with tempfile.TemporaryDirectory() as tmp:
        d = (work / "parse-scripts-case") if work else pathlib.Path(tmp)
        d.mkdir(parents=True, exist_ok=True)
        (d / "good.ps1").write_text(GOOD_PS1, encoding="utf-8")
        (d / "bad.ps1").write_text(BAD_PS1, encoding="utf-8")
        (d / "good.py").write_text(GOOD_PY, encoding="utf-8")
        (d / "bad.py").write_text(BAD_PY, encoding="utf-8")
        failures = []

        code, out = run([d / "good.ps1", d / "good.py"])
        if code != 0 or "all parse" not in out:
            failures.append("good files: exit %d, %r" % (code, out))
        code, out = run([d / "bad.ps1", d / "good.py"])
        if code != 1 or ("bad.ps1:2:" not in out):
            failures.append("PowerShell comma argument: exit %d, %r" % (code, out))
        code, out = run([d / "good.ps1", d / "bad.py"])
        if code != 1 or ("bad.py:2:" not in out):
            failures.append("Python syntax fault: exit %d, %r" % (code, out))

    for f in failures:
        print("FAILED:", f)
    print("parse_scripts: %s" % ("every case holds" if not failures else "%d cases failed" % len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
