"""find_console_only.py <petari root>: every console-only preprocessor branch in the kept code that has no native
alternative (#ifdef __MWERKS__, #if defined(__MWERKS__)..., #ifndef TARGET_PC without an #else/#elif), with its
size and whether it holds assembly. Such a branch vanishes from the native build; inside a function body it leaves
the function empty or partial, which compiles without a word."""
import os
import re
import sys

root = sys.argv[1]
dirs = ["src/Game", "src/JSystem", "src/nw4r", "src/RVLFaceLib", "include", "libs/JSystem/include", "libs/nw4r/include",
        "libs/RVLFaceLib/include", "libs/RVL_SDK/include", "libs/MSL_C++/include"]
asm = re.compile(r"(^|[^A-Za-z0-9_])(asm|__asm|__asm__)\b|\bps_|\bpsq_|\bmtspr\b|\bmfspr\b|\bfrsqrte\b|\bfres\b")


def console(kind, cond):
    c = cond.split("//")[0].split("/*")[0].replace(" ", "").replace("\t", "")
    if kind == "ifdef":
        return c == "__MWERKS__"
    if kind == "ifndef":
        return c == "TARGET_PC"
    return c.startswith("defined(__MWERKS__)") or c == "__MWERKS__" or c == "!defined(TARGET_PC)"


for d in dirs:
    for base, _, files in os.walk(os.path.join(root, d)):
        for f in sorted(files):
            path = os.path.join(base, f)
            if not os.path.isfile(path) or not (f.endswith((".c", ".cpp", ".h", ".hpp")) or "." not in f):
                continue
            lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
            stack = []
            depth = 0  # brace depth before each line, to tell file scope from function bodies
            for n, line in enumerate(lines, 1):
                m = re.match(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", line)
                if m:
                    k, cond = m.group(1), m.group(2)
                    if k in ("ifdef", "ifndef", "if"):
                        stack.append({"console": console(k, cond), "start": n, "alt": False, "asm": False,
                                      "depth": depth, "cond": (k + " " + cond.strip())[:60]})
                    elif k in ("elif", "else") and stack:
                        stack[-1]["alt"] = True
                        # The #else of a native-only test (#ifdef TARGET_PC) is console-only.
                        stack[-1]["else_console"] = k == "else" and stack[-1]["cond"].replace(" ", "") in ("ifdefTARGET_PC", "ifdefined(TARGET_PC)")
                    elif k == "endif" and stack:
                        g = stack.pop()
                        nested = any(s["console"] or s.get("else_console") for s in stack)
                        if g["console"] and not g["alt"] and not nested:
                            where = "body" if g["depth"] > 0 else "file"
                            rel = os.path.relpath(path, root).replace("\\", "/")
                            print(f"{rel}:{g['start']}\t{n - g['start'] - 1}\t{where}\t{'asm' if g['asm'] else '-'}\t{g['cond']}")
                    continue
                code = re.sub(r'"(?:\\.|[^"\\])*"', '""', line.split("//")[0])
                if stack and stack[-1]["console"] and asm.search(code):
                    stack[-1]["asm"] = True
                if not any(s["console"] and not s["alt"] for s in stack):
                    depth += code.count("{") - code.count("}")
