"""compiler_branches.py <petari root> [--full]: every preprocessor conditional in the code the native build compiles
whose condition names the compiler or target (__MWERKS__, TARGET_PC, __clang__, __GNUC__, __INTELLISENSE__,
NON_MATCHING, __ppc__/__PPC__), with each branch's text, so the branch the native build takes can be compared with
the console's one by one. find_console_only.py finds console branches with no alternative; this lists all of them,
including an #else that is present but does less (an empty macro, a dropped attribute)."""
import os
import re
import sys

root = sys.argv[1]
full = "--full" in sys.argv
dirs = ["src/Game", "src/JSystem", "src/nw4r", "src/RVLFaceLib", "src/RVL_SDK/gx", "src/RVL_SDK/gd", "src/RVL_SDK/mem",
        "src/RVL_SDK/kpad", "src/RVL_SDK/os", "include", "libs/JSystem/include", "libs/nw4r/include",
        "libs/RVLFaceLib/include", "libs/RVL_SDK/include", "libs/MSL_C++/include"]
names = re.compile(r"__MWERKS__|TARGET_PC|__clang__|__GNUC__|__INTELLISENSE__|NON_MATCHING|__ppc__|__PPC__|__CWCC__")
directive = re.compile(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)")

blocks = []
for d in dirs:
    for base, _, files in os.walk(os.path.join(root, d)):
        for f in sorted(files):
            if not (f.endswith((".c", ".cpp", ".h", ".hpp", ".inc")) or "." not in f):
                continue
            path = os.path.join(base, f)
            lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
            stack = []
            for n, line in enumerate(lines, 1):
                m = directive.match(line)
                if not m:
                    for b in stack:
                        b["branches"][-1][1].append(line)
                    continue
                k, cond = m.group(1), m.group(2).split("//")[0].strip()
                if k in ("ifdef", "ifndef", "if"):
                    for b in stack:
                        b["branches"][-1][1].append(line)
                    stack.append({"file": os.path.relpath(path, root).replace("\\", "/"), "line": n,
                                  "branches": [(k + " " + cond, [])], "match": bool(names.search(cond))})
                elif k in ("elif", "else") and stack:
                    for b in stack[:-1]:
                        b["branches"][-1][1].append(line)
                    stack[-1]["branches"].append((k + " " + cond, []))
                    if names.search(cond):
                        stack[-1]["match"] = True
                elif k == "endif" and stack:
                    b = stack.pop()
                    for o in stack:
                        o["branches"][-1][1].append(line)
                    if b["match"]:
                        blocks.append(b)

print(f"{len(blocks)} conditionals")
for b in blocks:
    sizes = " | ".join(f"{h.strip()} [{len([l for l in body if l.strip()])}]" for h, body in b["branches"])
    print(f"\n== {b['file']}:{b['line']}  {sizes}")
    if full:
        for h, body in b["branches"]:
            print(f"  #{h}")
            for l in body[:14]:
                print("    " + l.rstrip()[:150])
            if len(body) > 14:
                print(f"    ... ({len(body) - 14} more lines)")
