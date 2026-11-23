"""clock_domains_test.py: tools/check_clock_domains.py finds a steady_clock value used as a moment on the runtime's clock.

python -X utf8 -I tools/gate/tests/clock_domains_test.py

The tree itself holds none. Then, in a planted tree, each of these is refused with its file and line: the removed conversion qpc_of
of a steady_clock time point (the beta's frame wait, which put the Mac's frame wait to sleep for hours after the Mac had slept), and a
line that mixes steady_clock with qpc_now. A comment that names them, and two readings of steady_clock alone, pass. Exit 0 when every
case holds.
"""
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
TOOL = TREE / "tools" / "check_clock_domains.py"
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def run(tree):
    done = subprocess.run([sys.executable, "-X", "utf8", "-I", str(TOOL), str(tree)], capture_output=True, text=True)
    return done.returncode, done.stdout


def plant(root, files):
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


code, out = run(TREE)
check(code == 0, "the tree holds no steady_clock value used as a runtime-clock moment: " + out)
with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    plant(root, {
        "native/host/ok.cpp": "// qpc_of(steady) was the fault: a comment may say so\n"
                              "auto a = std::chrono::steady_clock::now();\nauto b = std::chrono::steady_clock::now() - a;\n"
                              "auto c = galaxy::qpc_now();\n",
    })
    code, out = run(root)
    check(code == 0, "readings of one clock and a comment pass: " + out)
with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    plant(root, {"native/host/old.cpp": "static auto next = Clock::now() + period;\nconst auto due = galaxy::qpc_of(next);\n"})
    code, out = run(root)
    check(code == 1 and "native/host/old.cpp:2" in out and "qpc_of(" in out, "the old frame wait's conversion is refused: " + out)
with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    plant(root, {"runtime/src/x.cpp": "const auto due = std::chrono::steady_clock::now().time_since_epoch().count() - galaxy::qpc_now();\n"})
    code, out = run(root)
    check(code == 1 and "runtime/src/x.cpp:1" in out, "a line that mixes the two clocks is refused: " + out)
with tempfile.TemporaryDirectory() as folder:
    code, out = run(pathlib.Path(folder) / "none")
    check(code == 2, "a folder that is not there cannot be read (exit 2)")

if failures:
    print("clock_domains_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("clock_domains_test.py: pass")
