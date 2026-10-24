"""boundary.py <obj dir> <out dir>: the game module's external interface, from its objects (a module build's
CMakeFiles/smg_game.dir). Every symbol some object references and no object defines, demangled, grouped by owner
(RVL SDK area by prefix, MSL/C library, compiler runtime, C++ runtime), with how many objects reference it. Writes
boundary.tsv and prints a summary."""
import collections
import os
import re
import subprocess
import sys

objdir, out = sys.argv[1], sys.argv[2]
bin_ = r"C:\Program Files\LLVM\bin"
objs = sorted(os.path.join(b, f) for b, _, fs in os.walk(objdir) for f in fs if f.endswith((".o", ".obj")))
defined, refs = set(), collections.defaultdict(set)
for i in range(0, len(objs), 200):
    chunk = objs[i:i + 200]
    r = subprocess.run([os.path.join(bin_, "llvm-nm.exe"), "--no-sort"] + chunk, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    current = None
    for line in r.stdout.splitlines():
        if line.endswith(":") and not line.startswith(" "):
            current = os.path.basename(line[:-1])
            continue
        m = re.match(r"\s*([0-9a-fA-F]*)\s+([A-Za-z?-])\s+(\S.*)$", line)
        if not m:
            continue
        kind, name = m.group(2), m.group(3)
        if kind == "U":
            refs[name].add(current)
        elif kind not in ("-",):
            defined.add(name)
undefined = sorted(n for n in refs if n not in defined)
demangled = {n: n for n in undefined}
undname = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\undname.exe"
mangled = [n for n in undefined if n.startswith("?")]
for i in range(0, len(mangled), 100):
    r = subprocess.run([undname] + mangled[i:i + 100], capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    pending = None
    for line in r.stdout.splitlines():
        m = re.match(r'Undecoration of :- "(.*)"', line)
        if m:
            pending = m.group(1)
            continue
        m = re.match(r'is :- "(.*)"', line)
        if m and pending:
            demangled[pending] = m.group(1).replace(" __ptr64", "").replace("__cdecl ", "")
            pending = None

sdk = ["AI", "AR", "ARC", "AX", "AXFX", "BTE", "CARD", "DB", "DSP", "DVD", "EXI", "GD", "GX", "HBM", "IPC", "KPAD",
       "MEM", "MIX", "MTX", "C_MTX", "PSMTX", "PSVEC", "C_VEC", "VEC", "PSQUAT", "C_QUAT", "MTX", "NAND", "NWC24", "OS",
       "PAD", "SC", "SI", "TPL", "VF", "VI", "WENC", "WPAD", "WUD", "ESP", "ES", "NPDP", "USB", "CNT", "SYN", "SEQ",
       "ENC", "RSO", "ISFS", "IOS", "__OS", "__GX", "__DVD", "__AX", "__VI", "LC", "PPC", "EUART", "WUD", "TRK"]


def owner(sym, pretty):
    s = pretty
    if "operator new" in s or "operator delete" in s or s.startswith("__std_") or "type_info" in s or \
            sym.startswith(("__CxxFrameHandler", "_purecall", "__security", "_Init_thread", "_tls_", "__imp_", "_fltused",
                            "__chkstk", "_RTC", "__GSHandler", "_Thrd")) or "_Init_thread" in s:
        return "c++ runtime"
    if sym.startswith("nebula_msl_") or sym in ("_current_locale",):
        return "msl (nebula)"
    for p in sorted(sdk, key=len, reverse=True):
        if sym.startswith(p) and (len(sym) == len(p) or sym[len(p)].isupper() or sym[len(p)] in "_0123456789"):
            return "sdk:" + p.strip("_")
    if re.match(r"^[a-z_][a-z0-9_]*$", sym) or sym.startswith("_"):
        return "c library"
    return "other"


groups = collections.defaultdict(list)
with open(os.path.join(out, "boundary.tsv"), "w", encoding="utf-8") as fh:
    fh.write("symbol\tdemangled\towner\tobjects\n")
    for n in undefined:
        o = owner(n, demangled[n])
        groups[o].append(n)
        fh.write(f"{n}\t{demangled[n]}\t{o}\t{len(refs[n])}\n")
print(f"{len(objs)} objects, {len(defined)} defined symbols, {len(undefined)} undefined")
for o, names in sorted(groups.items(), key=lambda x: -len(x[1])):
    print(f"{len(names):5d}  {o}")
