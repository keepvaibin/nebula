"""harness_vsync_test.py: a fixed-step run is written VSync off; a test that names VSync keeps its own (N-1052).

python -X utf8 -I tools/lumacore/tests/harness_vsync_test.py

Writes three runs' settings with luma_test.write_run_settings into a temporary folder and reads config.ini:
- a fixed-step test (time steps, the default) gets vsync_interval=0;
- a real-time test that names GALAXY_VSYNC_INTERVAL=1 gets 1;
- a fixed-step test that names GALAXY_VSYNC_INTERVAL=1 gets 1.
Exit 0 when every case holds.
"""
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import luma_test  # noqa: E402

failures = []


def vsync(test):
    with tempfile.TemporaryDirectory() as folder:
        luma_test.write_run_settings(dict({"window": (640, 360), "language": None}, **test), pathlib.Path(folder))
        config = pathlib.Path(folder) / "config.ini"
        lines = config.read_text(encoding="utf-8").splitlines() if config.is_file() else []
        values = [l.partition("=")[2].strip() for l in lines if l.partition("=")[0].strip() == "vsync_interval"]
        return values[-1] if values else None


def case(name, test, want):
    got = vsync(test)
    print("%-4s %s: vsync_interval %s (want %s)" % ("pass" if got == want else "FAIL", name, got, want))
    if got != want:
        failures.append(name)


case("fixed-step run", {}, "0")
case("real-time run naming VSync", {"time": "real", "settings": ["GALAXY_VSYNC_INTERVAL=1"]}, "1")
case("fixed-step run naming VSync", {"settings": ["GALAXY_VSYNC_INTERVAL=1"]}, "1")
sys.exit(1 if failures else 0)
