"""long_sweep.py <petari root>: in the code the native port keeps, spell the console's 32-bit `long` as the SDK's
types: `long`, `signed long`, `long int` become s32 and `unsigned long` becomes u32 (the same types on the console,
where s32 and u32 are long; 64-bit on LP64 hosts otherwise). `long long` and `long double` stay. Comments, string
and character literals are left alone. types.h, which defines s32 and u32 for the console, is skipped."""
import os
import re
import sys

root = sys.argv[1]
dirs = ["src/Game", "src/JSystem", "src/nw4r", "src/RVLFaceLib", "include", "libs/JSystem/include",
        "libs/nw4r/include", "libs/RVLFaceLib/include", "libs/RVL_SDK/include"]
skip = {"libs/RVL_SDK/include/revolution/types.h"}
lexeme = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
word = re.compile(r"\b(?:(signed|unsigned)\s+)?long\b(?:(\s+)(long|double|int)\b)?")


def sub(m):
    if m.group(3) in ("long", "double"):
        return m.group(0)
    return "u32" if m.group(1) == "unsigned" else "s32"


total = 0
for d in dirs:
    for base, _, files in os.walk(os.path.join(root, d)):
        for f in files:
            if not f.endswith((".c", ".cpp", ".h", ".hpp", ".inc")):
                continue
            path = os.path.join(base, f)
            rel = os.path.relpath(path, root).replace("\\", "/")
            if rel in skip:
                continue
            raw = open(path, "rb").read().decode("utf-8")
            out, last, n = [], 0, 0
            for m in lexeme.finditer(raw):
                code = raw[last:m.start()]
                new, k = word.subn(sub, code)
                n += sum(1 for x in word.finditer(code) if x.group(3) not in ("long", "double"))
                out.append(new)
                out.append(m.group(0))
                last = m.end()
            code = raw[last:]
            n += sum(1 for x in word.finditer(code) if x.group(3) not in ("long", "double"))
            out.append(word.sub(sub, code))
            if n:
                open(path, "wb").write("".join(out).encode("utf-8"))
                total += n
                print(f"{n:4d} {rel}")
print("total", total)
