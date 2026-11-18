"""coverage_exports.py: the game module's export list for a coverage build (native/CMakeLists.txt NEBULA_COVERAGE).

    python coverage_exports.py --cmake CMAKE --nm LLVM_NM --objects LIST --out DEF

The rows module exports every symbol (WINDOWS_EXPORT_ALL_SYMBOLS: CMake's __create_def over its objects), so a mod's
find_function finds the game's functions. In a coverage build the objects also hold the counters and records clang
adds (__profc_, __profd_, __covrec_, __llvm_): CMake's list cuts their names at the first '@' (it reads them as
stdcall names) and the link fails on the cut names. This makes the same list and leaves those out; no mod calls them.
"""
import argparse
import pathlib
import subprocess
import sys

COUNTERS = ("__profc_", "__profd_", "__profvp_", "__covrec_", "__llvm_")


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("--cmake", required=True)
    p.add_argument("--nm", required=True)
    p.add_argument("--objects", required=True)
    p.add_argument("--out", required=True)
    a = p.parse_args(argv)
    whole = pathlib.Path(a.out + ".all")
    # __create_def leaves a list newer than the objects as it is: an empty one a failed run left would stay.
    whole.unlink(missing_ok=True)
    # The target's objects less its compiled resources (.res), as CMake's own list has them.
    objects = pathlib.Path(a.out + ".objs")
    objects.write_text("".join(line + "\n" for line in pathlib.Path(a.objects).read_text(encoding="utf-8").splitlines()
                               if line.strip().lower().endswith((".obj", ".o"))), encoding="utf-8")
    r = subprocess.run([a.cmake, "-E", "__create_def", str(whole), str(objects), "--nm=" + a.nm])
    if r.returncode != 0 or not whole.exists() or whole.stat().st_size == 0:
        print("coverage_exports: no export list from %s (exit %d)" % (objects, r.returncode))
        return r.returncode or 1
    kept = [line for line in whole.read_text(encoding="utf-8", errors="replace").splitlines()
            if not line.strip().startswith(COUNTERS)]
    pathlib.Path(a.out).write_text("\n".join(kept) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
