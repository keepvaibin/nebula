"""harness_report_os_test.py: result.json is written when the system cannot say what processor it has (P-326).

python -X utf8 -I tools/lumacore/tests/harness_report_os_test.py

On a loaded Windows machine Python's platform.platform() asks WMI for the processor; the query times out and Python
raises KeyError, and the runner died before writing result.json (gate main 5d32f7c10, mods/d3d12 system/2221-facts:
"the runner left no result.json"). Here every platform call that reaches the processor raises as it did there; the
report must still be written, with the system named.
Exit 0 when it holds.
"""
import argparse
import json
import pathlib
import platform
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import luma_test  # noqa: E402


def broken(*_args, **_kwargs):
    raise KeyError("Architecture")


platform.platform = broken
platform.processor = broken
platform.machine = broken

work = pathlib.Path(tempfile.mkdtemp())
args = argparse.Namespace(commit="0123456789ab", module="")
failures = []
try:
    luma_test.report(work, "report-os", "result.json written", "d3d12", "pass", "", [], args, {})
except Exception as error:  # noqa: BLE001 - the fault is that it raised at all
    failures.append("report raised %s: %s" % (type(error).__name__, error))
result_file = work / "result.json"
if not result_file.exists():
    failures.append("no result.json")
else:
    result = json.loads(result_file.read_text(encoding="utf-8"))
    if not result.get("os"):
        failures.append("the system is not named: %r" % result.get("os"))
    if result.get("verdict") != "pass":
        failures.append("the verdict is not kept: %r" % result.get("verdict"))

if failures:
    print("harness_report_os_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("harness_report_os_test.py: pass")
