"""find_asm.py <petari root>: inline assembly the native build still sees in the kept code: `asm { }`, `__asm { }`,
`asm(...)`/`__asm__(...)` statements and `asm` functions outside the console-only preprocessor branches. A branch
is console-only under `#ifdef __MWERKS__`, `#if defined(__MWERKS__)...`, `#ifndef TARGET_PC`, or the #else of a
TARGET_PC / !__MWERKS__ test; other conditions are taken as active."""
import os
import re
import sys

root = sys.argv[1]
dirs = ["src/Game", "src/JSystem", "src/nw4r", "src/RVLFaceLib", "include", "libs/JSystem/include", "libs/nw4r/include",
        "libs/RVLFaceLib/include", "libs/RVL_SDK/include", "libs/MSL_C++/include"]
asm = re.compile(r"(^|[^A-Za-z0-9_])(asm|__asm|__asm__)\s*(volatile\s*)?[({]|^\s*(static\s+)?(inline\s+)?asm\s+[A-Za-z_]")


def classify(kind, cond):
    """'console' when the branch is taken only on the console, 'pc' when only natively, None otherwise."""
    c = cond.split("//")[0].split("/*")[0].replace(" ", "").replace("\t", "")
    if kind == "ifdef":
        return {"__MWERKS__": "console", "TARGET_PC": "pc"}.get(c)
    if kind == "ifndef":
        return {"TARGET_PC": "console", "__MWERKS__": "pc"}.get(c)
    if c.startswith("defined(__MWERKS__)") or c == "__MWERKS__" or c == "!defined(TARGET_PC)":
        return "console"
    if c.startswith("defined(TARGET_PC)") or c == "!defined(__MWERKS__)":
        return "pc"
    return None


hits = 0
for d in dirs:
    for base, _, files in os.walk(os.path.join(root, d)):
        for f in files:
            path = os.path.join(base, f)
            if not os.path.isfile(path) or not (f.endswith((".c", ".cpp", ".h", ".hpp")) or "." not in f):
                continue
            lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
            stack = []  # per #if group: [current branch is console-only, an earlier branch was the pc branch]
            for n, line in enumerate(lines, 1):
                m = re.match(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", line)
                if m:
                    k, cond = m.group(1), m.group(2)
                    if k in ("ifdef", "ifndef", "if"):
                        c = classify(k, cond)
                        stack.append([c == "console", c, c == "pc"])
                    elif k == "elif" and stack:
                        c = classify(k, cond)
                        stack[-1][0] = stack[-1][2] or c == "console"
                        stack[-1][1] = c
                        stack[-1][2] = stack[-1][2] or c == "pc"
                    elif k == "else" and stack:
                        # The pc branch already ran: this one is console-only. After a console-only branch it is ours.
                        stack[-1][0] = stack[-1][2]
                    elif k == "endif" and stack:
                        stack.pop()
                    continue
                if any(s[0] for s in stack):
                    continue
                if asm.search(line.split("//")[0]):
                    hits += 1
                    print(f"{os.path.relpath(path, root)}:{n}: {line.strip()[:120]}".replace("\\", "/"))
print("hits", hits)
