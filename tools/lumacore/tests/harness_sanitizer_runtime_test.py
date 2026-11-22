"""harness_sanitizer_runtime_test.py: a run's own copy of the bin folder holds the sanitizer runtime that the game modules of a sanitizer
build import (B5, N-1062; luma_test.py copy_sanitizer_runtime), else the host cannot load the module: "cannot load the game module".

python -X utf8 -I tools/lumacore/tests/harness_sanitizer_runtime_test.py

  copied      a runtime DLL of the bin folder is in the run's bin folder, with the bytes it had, and is frozen under its name
  others      a file that is no runtime is not copied by this function (the runner copies the others by name)
  none        a bin folder of a build without sanitizers: nothing is copied, nothing is frozen
Exit 0 when it holds.
"""
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import luma_test  # noqa: E402

failures = []
work = pathlib.Path(tempfile.mkdtemp())

bin_folder, target = work / "bin", work / "run-bin"
bin_folder.mkdir()
target.mkdir()
(bin_folder / "clang_rt.asan_dynamic-x86_64.dll").write_bytes(b"runtime bytes")
(bin_folder / "smg_game.dll").write_bytes(b"module")
(bin_folder / "sanitize.txt").write_text("address\n", encoding="ascii")
frozen = {}
luma_test.copy_sanitizer_runtime(bin_folder, target, frozen)
if (target / "clang_rt.asan_dynamic-x86_64.dll").read_bytes() != b"runtime bytes":
    failures.append("the runtime is not in the run's bin folder with its bytes")
if set(frozen) != {"clang_rt.asan_dynamic-x86_64.dll"} or frozen["clang_rt.asan_dynamic-x86_64.dll"] != luma_test.sha256(target / "clang_rt.asan_dynamic-x86_64.dll"):
    failures.append("the runtime is not frozen under its name: %s" % frozen)
if sorted(p.name for p in target.iterdir()) != ["clang_rt.asan_dynamic-x86_64.dll"]:
    failures.append("something other than the runtime was copied: %s" % sorted(p.name for p in target.iterdir()))

plain_bin, plain_target = work / "plain-bin", work / "plain-run-bin"
plain_bin.mkdir()
plain_target.mkdir()
(plain_bin / "smg_game.dll").write_bytes(b"module")
frozen = {}
luma_test.copy_sanitizer_runtime(plain_bin, plain_target, frozen)
if frozen or list(plain_target.iterdir()):
    failures.append("a build without sanitizers: something was copied or frozen")

if failures:
    print("harness_sanitizer_runtime_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("harness_sanitizer_runtime_test.py: pass")
