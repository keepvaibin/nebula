"""guarded.py SCRIPT [ARGS...]: runs a self-test with the live-state guard installed (tools/gate/selftest_guard.py).

The gate's harness/self check starts every self-test through this, with NEBULA_SELF_TEST=1 and NEBULA_SELF_TEST_LOG=<a file of its
own in the script's work folder>: the guard is installed first, then the script runs as `__main__` with its own arguments. Not a test
itself (the gate runs only *_test.py).
"""
import pathlib
import runpy
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import selftest_guard  # noqa: E402

if len(sys.argv) < 2:
    raise SystemExit("guarded.py SCRIPT [ARGS...]")
script = str(pathlib.Path(sys.argv[1]).resolve())
sys.argv = [script] + sys.argv[2:]
selftest_guard.install_if_asked()
sys.path.remove(str(pathlib.Path(__file__).resolve().parent.parent))
runpy.run_path(script, run_name="__main__")
