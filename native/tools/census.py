"""census.py <petari root> <out dir> [target] [host [obj]]: compile every C/C++ unit the native port keeps
(src/Game, src/JSystem, src/nw4r, src/RVLFaceLib) with clang for a 64-bit target, RMGE01. Without `obj` it is a
syntax check; with `obj` it builds -O2 objects into <out>/obj for the boundary analysis. `host` is the game module's
configuration (the game's own MSL_C++, Nebula's MSL interface over the host C library, no host C++ library,
TARGET_PC); without it, Petari's own console headers. Writes per-unit logs and errors.tsv and prints error kinds
by count and the units per library that compile clean."""
import collections
import concurrent.futures
import os
from pathlib import Path
import re
import subprocess
import sys

root, out = sys.argv[1], sys.argv[2]
target = sys.argv[3] if len(sys.argv) > 3 else "x86_64-unknown-linux-gnu"
host = len(sys.argv) > 4 and sys.argv[4] == "host"
objdir = os.path.abspath(os.path.join(out, "obj")) if len(sys.argv) > 5 and sys.argv[5] == "obj" else None
clang = r"C:\Program Files\LLVM\bin\clang++.exe"
os.makedirs(objdir or out, exist_ok=True)

if host:
    msl = str(Path(__file__).resolve().parent.parent / "msl" / "include")
    includes = [msl, "include", "libs/JSystem/include", "libs/MSL_C++/include", "libs/RVLFaceLib/include",
                "libs/RVL_SDK/include", "libs/nw4r/include", "build/RMGE01/include"]
    defines = ["VERSION=1", "RMGJ01=0", "RMGE01=1", "RMGP01=2", "RMGK01=3", "TARGET_PC=1"]
else:
    includes = ["include", "libs/JSystem/include", "libs/MSL_C++/include", "libs/MSL_C/include", "libs/MetroTRK/include",
                "libs/RVLFaceLib/include", "libs/RVL_SDK/include", "libs/Runtime/include", "libs/nw4r/include",
                "build/RMGE01/include"]
    defines = ["VERSION=1", "RMGJ01=0", "RMGE01=1", "RMGP01=2", "RMGK01=3"]
libs = ["src/Game", "src/JSystem", "src/nw4r", "src/RVLFaceLib"]
units = []
for lib in libs:
    for d, _, files in os.walk(os.path.join(root, lib)):
        for f in files:
            if f.endswith((".cpp", ".c")):
                units.append(os.path.relpath(os.path.join(d, f), root).replace("\\", "/"))
units.sort()


def flags(unit):
    c = unit.endswith(".c")
    lang = ["-x", "c", "-std=gnu11"] if c else ["-x", "c++", "-std=gnu++17"]
    if not host:
        return lang + ["-nostdinc", "-nostdinc++"]
    # The console's signed char and 16-bit wchar_t; JKRHeap's operator new takes every allocation; CodeWarrior's
    # `register` and constant narrowing accepted; MSL's interface first in every unit.
    # No MSVC leniency on Windows (the same diagnostics as every other host); pointers never squeezed into 32 bits.
    f = lang + ["-fsigned-char", "-fshort-wchar", "-fno-aligned-allocation", "-Wno-register", "-Wno-c++11-narrowing",
                "-fno-ms-compatibility", "-fno-delayed-template-parsing", "-Werror=pointer-to-int-cast",
                "-Werror=void-pointer-to-int-cast", "-Werror=int-to-pointer-cast", "-Werror=pointer-to-enum-cast",
                "-include", "nebula_prelude.h"]
    # C++ as CodeWarrior built it: no RTTI (the game never asks for it), no exceptions, unsized delete (JKRHeap's).
    return f if c else f + ["-nostdinc++", "-fno-rtti", "-fno-exceptions", "-fno-sized-deallocation"]


def check(unit):
    mode = ["-fsyntax-only"]
    if objdir:
        # Type punning through pointer casts is everywhere in the decompiled code; float-to-int casts saturate
        # (the console's __cvt_fp2unsigned did; out-of-range casts are undefined otherwise).
        mode = ["-c", "-O2", "-fno-strict-aliasing", "-fno-strict-float-cast-overflow", "-fno-exceptions",
                "-ffunction-sections", "-fdata-sections", "-o", os.path.join(objdir, unit.replace("/", "__") + ".o")]
    cmd = [clang, "--target=" + target, "-ferror-limit=0", "-Wno-everything", "-Werror=return-type", "-finput-charset=UTF-8", "-fno-color-diagnostics"] + \
          mode + flags(unit) + [f"-I{i}" for i in includes] + [f"-D{x}" for x in defines] + [unit]
    r = subprocess.run(cmd, cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace")
    errs = [l for l in r.stderr.splitlines() if ": error:" in l or ": fatal error:" in l or l.startswith("error:")]
    if r.returncode != 0 and not errs:
        errs = ["(exit " + str(r.returncode) + ") error: " + (r.stderr.strip().splitlines() or ["no output"])[-1]]
    with open(os.path.join(out, unit.replace("/", "__") + ".log"), "w", encoding="utf-8") as fh:
        fh.write(r.stderr)
    return unit, errs


def kind(line):
    msg = line.split("error:", 1)[1].strip()
    msg = re.sub(r"'[^']*'", "'X'", msg)
    msg = re.sub(r"\d+", "N", msg)
    return msg[:110]


results = {}
with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 8) as pool:
    for unit, errs in pool.map(check, units):
        results[unit] = errs
kinds = collections.Counter()
first = collections.Counter()
clean = collections.Counter()
total = collections.Counter()
with open(os.path.join(out, "errors.tsv"), "w", encoding="utf-8") as fh:
    fh.write("unit\tlibrary\terrors\tfirst\n")
    for unit in units:
        lib = next(l for l in libs if unit.startswith(l))
        errs = results[unit]
        total[lib] += 1
        if not errs:
            clean[lib] += 1
        for e in errs:
            kinds[kind(e)] += 1
        if errs:
            first[kind(errs[0])] += 1
        fh.write(f"{unit}\t{lib}\t{len(errs)}\t{kind(errs[0]) if errs else ''}\n")
print(f"target {target}: {len(units)} units")
for lib in libs:
    print(f"  {lib}: {clean[lib]} of {total[lib]} compile clean")
if kinds:
    print("error kinds (all errors):")
    for k, n in kinds.most_common(40):
        print(f"  {n:6d}  {k}")
    print("first error per failing unit:")
    for k, n in first.most_common(25):
        print(f"  {n:6d}  {k}")
