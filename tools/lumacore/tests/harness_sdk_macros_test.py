"""harness_sdk_macros_test.py: every mod form luma.h declares compiles warning-free, in C and C++, on the platforms'
targets.

python -X utf8 -I tools/lumacore/tests/harness_sdk_macros_test.py [--work <folder>]

An author may build with -Wall -Wextra -Werror (the harness's game half, tools/test_driver/luma_test/luma_test.c, is):
each of LUMA_MOD, LUMA_MOD_NO_IMPORTS, LUMA_LIBRARY_MOD, LUMA_MOD_CUT and LUMA_LIBRARY_MOD_CUT initializes every field
of LumaModInfo (a field added to the struct and not to a macro is -Wmissing-field-initializers in every such mod). Each
is compiled (syntax and warnings, no link) as C17 and as C++20 for Windows x64, Linux x64, Android arm64 and macOS
arm64, with LumaLink's freestanding flags. clang: LUMA_CLANG, else LLVM's folder, else clang on the path.
Exit 0 when every case compiles.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

TREE = pathlib.Path(__file__).resolve().parents[3]
FORMS = ("LUMA_MOD", "LUMA_MOD_NO_IMPORTS", "LUMA_LIBRARY_MOD", "LUMA_MOD_CUT", "LUMA_LIBRARY_MOD_CUT")
TARGETS = ("x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu", "aarch64-linux-android24", "arm64-apple-macos12.0")

SOURCE = r'''#include <luma.h>

#if defined(FORM_LUMA_LIBRARY_MOD) || defined(FORM_LUMA_LIBRARY_MOD_CUT)
#define LIBRARY 1
#endif
#if defined(FORM_LUMA_MOD_CUT) || defined(FORM_LUMA_LIBRARY_MOD_CUT)
#define CUT 1
#endif

#if !defined(FORM_LUMA_MOD_NO_IMPORTS)
static int32_t (*game_random)(int32_t, int32_t);
static const LumaImport imports[] = {
    LUMA_FUNCTION(game_random, "MR::getRandom", "s32(s32, s32)"),
};
#endif
static int32_t start(const LumaCore* core, const LumaMod* mod) {
    (void)core;
    (void)mod;
    return 0;
}
static void stop(void) {}
#if defined(LIBRARY)
static int32_t twice(int32_t x) { return x * 2; }
static const LumaExport exports[] = {
    LUMA_EXPORT_FUNCTION("twice", "s32(s32)", twice),
};
static void dependent_gone(const LumaMod* dependent) { (void)dependent; }
#endif
#if defined(CUT)
static void cut(const LumaCut* what) { (void)what; }
#endif

#if defined(FORM_LUMA_MOD)
LUMA_MOD(imports, start, stop);
#elif defined(FORM_LUMA_MOD_NO_IMPORTS)
LUMA_MOD_NO_IMPORTS(start, stop);
#elif defined(FORM_LUMA_LIBRARY_MOD)
LUMA_LIBRARY_MOD(imports, start, stop, exports, dependent_gone);
#elif defined(FORM_LUMA_MOD_CUT)
LUMA_MOD_CUT(imports, start, stop, cut);
#elif defined(FORM_LUMA_LIBRARY_MOD_CUT)
LUMA_LIBRARY_MOD_CUT(imports, start, stop, exports, dependent_gone, cut);
#endif
'''


def clang():
    given = os.environ.get("LUMA_CLANG")
    if given:
        return given
    for path in ("C:/Program Files/LLVM/bin/clang.exe", "/usr/bin/clang"):
        if os.path.isfile(path):
            return path
    return shutil.which("clang")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--work")
    args = parser.parse_args()
    compiler = clang()
    if compiler is None:
        print("FAIL no clang (LUMA_CLANG, LLVM's folder or the path)")
        return 1
    work = pathlib.Path(args.work) if args.work else pathlib.Path(tempfile.mkdtemp(prefix="sdk-macros-"))
    work.mkdir(parents=True, exist_ok=True)
    (work / "mod.c").write_text(SOURCE, encoding="utf-8")
    (work / "mod.cpp").write_text(SOURCE, encoding="utf-8")
    bad = 0
    cases = 0
    for target in TARGETS:
        for language, source, std in (("C", "mod.c", "-std=c17"), ("C++", "mod.cpp", "-std=c++20")):
            extra = ["-fno-exceptions", "-fno-rtti"] if language == "C++" else []
            for form in FORMS:
                cases += 1
                cmd = [compiler, "--target=" + target, std, *extra, "-fsyntax-only", "-ffreestanding", "-nostdlib",
                       "-fno-builtin-malloc", "-Wall", "-Wextra", "-Werror", "-D", "FORM_" + form,
                       "-I", str(TREE / "native" / "include"), str(work / source)]
                r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
                if r.returncode != 0:
                    bad += 1
                    first = next((l for l in r.stderr.splitlines() if "error" in l), r.stderr.strip()[:200])
                    print("FAIL %s %s %s: %s" % (form, language, target, first))
    print("%d of %d cases compile warning-free" % (cases - bad, cases))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
