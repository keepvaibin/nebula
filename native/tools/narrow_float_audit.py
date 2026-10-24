"""narrow_float_audit.py <build dir> [--jobs N]: fails (exit 1) on any float-to-integer conversion into a type narrower
than 32 bits in the game module's sources (the build's compile_commands.json; configure with
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON).

The module builds with -fno-strict-float-cast-overflow, so clang saturates such a conversion at the narrow type: 296
degrees as an s16 binary angle (x 182.04) becomes 32767, 180 degrees. The Wii converts to 32 bits (fctiwz, saturating
there) and its halfword or byte store keeps the low bits, which wraps: -11651, -64 degrees. Each such conversion goes
through 32 bits explicitly, (s16)(s32)(x). Allowed: the OSf32to* stand-ins for the quantized paired-single stores
(os.h, OSFastCast.h), which saturate on the console too, and Nebula's own SDK (sdk/)."""
import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

QUERY = """set output diag
set bind-root false
match castExpr(hasCastKind("CK_FloatingToIntegral"), hasType(hasCanonicalType(anyOf(asString("short"), \
asString("unsigned short"), asString("signed char"), asString("unsigned char"), asString("char"))))).bind("narrow")
"""
NOTE = re.compile(r'^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): note: "narrow" binds here')
ALLOWED = ("revolution/os.h", "revolution/os/OSFastCast.h")


def clang_query():
    found = shutil.which("clang-query")
    if found:
        return found
    for candidate in (r"C:\Program Files\LLVM\bin\clang-query.exe", "/usr/bin/clang-query"):
        if os.path.exists(candidate):
            return candidate
    sys.exit("narrow_float_audit: clang-query not found")


def run(tool, build, query, files):
    result = subprocess.run([tool, "-p", build, "-f", query, *files], capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    return result.stdout + result.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()
    database = pathlib.Path(args.build) / "compile_commands.json"
    if not database.exists():
        sys.exit(f"narrow_float_audit: {database} missing (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)")
    files = [e["file"] for e in json.loads(database.read_text(encoding="utf-8"))
             if e["file"].endswith((".c", ".cpp", ".cc"))]
    tool = clang_query()
    with tempfile.TemporaryDirectory() as tmp:
        query = os.path.join(tmp, "narrow_float.query")
        pathlib.Path(query).write_text(QUERY, encoding="utf-8")
        batches = [files[i:i + 16] for i in range(0, len(files), 16)]
        sites = set()
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            for output in pool.map(lambda b: run(tool, args.build, query, b), batches):
                for raw in output.splitlines():
                    m = NOTE.match(raw.strip())
                    if m:
                        sites.add((pathlib.Path(m["file"]).resolve().as_posix(), int(m["line"]), int(m["col"])))
    bad = sorted(s for s in sites if not s[0].endswith(ALLOWED) and "/native/sdk/" not in s[0])
    for path, line, col in bad:
        text = pathlib.Path(path).read_text(encoding="utf-8", errors="replace").splitlines()[line - 1].strip()
        print(f"{path}:{line}:{col}: float to a narrow integer saturates natively; convert through s32: {text[:120]}")
    print(f"narrow_float_audit: {len(files)} files, {len(sites)} narrow conversions, {len(bad)} not through 32 bits")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
