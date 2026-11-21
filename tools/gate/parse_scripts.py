"""parse_scripts.py: every PowerShell and Python script the tree tracks parses.

    python -X utf8 -I tools/gate/parse_scripts.py [--tree TREE] [FILE ...]

Without files: every tracked *.ps1 and *.py of the tree (git ls-files). PowerShell files are read by Windows
PowerShell's own parser (the one the build scripts run under), Python files are compiled by this Python. Prints one
line per fault (file:line: message) and a summary; exits 1 when any file does not parse. A script that does not parse
fails only when someone runs it, which for the release and Android scripts can be long after the change.
"""
import argparse
import base64
import os
import pathlib
import subprocess
import sys
import tempfile

# Windows PowerShell's parser over a list of files (one path per line, UTF-8): "path:line: message" per fault.
PARSE_PS1 = r"""
$bad = 0
foreach ($f in [System.IO.File]::ReadAllLines($env:PARSE_SCRIPTS_LIST)) {
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($f, [ref]$null, [ref]$errors)
    foreach ($e in $errors) { "{0}:{1}: {2}" -f $f, $e.Extent.StartLineNumber, $e.Message; $bad++ }
}
exit [int]($bad -ne 0)
"""


def tracked(tree):
    out = subprocess.run(["git", "-C", str(tree), "ls-files", "-z", "--", "*.ps1", "*.py"], capture_output=True,
                         check=True).stdout.decode("utf-8")
    # A tracked file deleted in the working tree is not a script the tree has.
    return [tree / p for p in out.split("\0") if p and (tree / p).is_file()]


def python_faults(files):
    faults = []
    for f in files:
        try:
            compile(f.read_bytes(), str(f), "exec", dont_inherit=True)
        except SyntaxError as e:
            faults.append("%s:%s: %s" % (f, e.lineno, e.msg))
        except ValueError as e:  # a NUL byte in the source
            faults.append("%s:0: %s" % (f, e))
    return faults


def powershell_faults(files):
    if not files:
        return []
    with tempfile.TemporaryDirectory() as d:
        listing = pathlib.Path(d) / "files.txt"
        listing.write_text("\n".join(str(f) for f in files), encoding="utf-8")
        encoded = base64.b64encode(PARSE_PS1.encode("utf-16-le")).decode("ascii")
        r = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
                           capture_output=True, env=dict(os.environ, PARSE_SCRIPTS_LIST=str(listing)))
    lines = [l for l in r.stdout.decode("utf-8", "replace").splitlines() if l.strip()]
    if r.returncode != 0 and not lines:
        lines = ["powershell: exit %d: %s" % (r.returncode, r.stderr.decode("utf-8", "replace").strip()[:400])]
    return lines


def main():
    # A self-test that starts this tool as a child process runs it under the live-state guard (tools/gate/selftest_guard.py).
    import pathlib as _pathlib
    import sys as _sys
    _gate = str(_pathlib.Path(__file__).resolve().parents[1] / "gate")
    _sys.path.insert(0, _gate)
    try:
        import selftest_guard
    finally:
        _sys.path.remove(_gate)
    selftest_guard.install_if_asked()
    p = argparse.ArgumentParser()
    p.add_argument("--tree", default=str(pathlib.Path(__file__).resolve().parent.parent.parent))
    p.add_argument("files", nargs="*")
    a = p.parse_args()
    files = [pathlib.Path(f) for f in a.files] if a.files else tracked(pathlib.Path(a.tree))
    ps1 = [f for f in files if f.suffix.lower() == ".ps1"]
    py = [f for f in files if f.suffix.lower() == ".py"]
    faults = powershell_faults(ps1) + python_faults(py)
    for line in faults:
        print(line)
    print("%d PowerShell and %d Python files: %s" % (len(ps1), len(py),
                                                      "all parse" if not faults else "%d faults" % len(faults)))
    return 1 if faults else 0


if __name__ == "__main__":
    sys.exit(main())
