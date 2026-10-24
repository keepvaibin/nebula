"""console_address_audit.py <native dir> [<petari dir>]: fails (exit 1) on any native-built line that tests a host pointer
against the console's memory map: a pointer cast to an integer on the same line as an address-sized constant
(0x10000000 and up: MEM1 0x80000000, MEM2 0x90000000, uncached 0xC0000000, physical masks 0x7FFFFFFF/0x1FFFFFFF ...).
Host heaps live above 4 GiB, so such a test is constant natively (J3DMaterial::getMaterialAnm's `< 0xC0000000` would
reject every material animation). Console-only preprocessor branches (#ifndef TARGET_PC, the #else of #ifdef
TARGET_PC, __MWERKS__) are skipped. Pointer truncations to 32 bits are compile errors (-Werror=pointer-to-int-cast,
-Werror=void-pointer-to-int-cast), so only full-width casts reach this audit. Run by the module build."""
import os
import re
import sys

DIRS = ["petari/src", "petari/include", "petari/libs", "sdk", "host", "include"]
INTEGER = r"(?:uintptr_t|intptr_t|size_t|ptrdiff_t|u64|s64|u32|s32|unsigned\s+long(?:\s+long)?|long(?:\s+long)?)"
POINTER_TO_INTEGER = re.compile(
    r"reinterpret_cast\s*<\s*(?:const\s+)?" + INTEGER + r"\s*>"  # reinterpret_cast< uintptr_t >(p)
    r"|\(\s*(?:const\s+)?" + INTEGER + r"\s*\)\s*(?!\(?\s*[0-9])[A-Za-z_(&]"  # (uintptr_t)p, (u32)&x; not (u32)0x...
    r"|std::bit_cast\s*<\s*" + INTEGER + r"\s*>")
ADDRESS = re.compile(r"\b0[xX]([0-9A-Fa-f]{8})[uUlL]*\b")
# Lines that match the pattern but test no pointer: (file, text on the line, reason).
ALLOWED = [
    ("sdk/os/tests/os_scheduler_tests.c", "OSCachedToPhysical(mem1)",
     "checks the SDK's OSCachedToPhysical against the console definition for blocks of the arena"),
    ("petari/libs/JSystem/include/JSystem/JAudio2/JAISound.hpp", "(u32)id << 16", "an 8-bit sound group ID"),
]
CONSOLE_IFDEF = {"__MWERKS__"}


def console_branch(kind, cond):
    c = cond.split("//")[0].split("/*")[0].replace(" ", "").replace("\t", "")
    if kind == "ifdef":
        return c in CONSOLE_IFDEF
    if kind == "ifndef":
        return c == "TARGET_PC"
    return c in ("!defined(TARGET_PC)", "defined(__MWERKS__)", "__MWERKS__") or c.startswith("defined(__MWERKS__)")


def native_branch(kind, cond):
    c = cond.split("//")[0].split("/*")[0].replace(" ", "").replace("\t", "")
    return (kind == "ifdef" and c == "TARGET_PC") or (kind == "if" and c == "defined(TARGET_PC)")


def audit(root, petari):
    hits = []
    for d in DIRS:
        top = os.path.join(petari, d[len("petari/"):]) if d.startswith("petari/") else os.path.join(root, d)
        for base, _, files in os.walk(top):
            for f in sorted(files):
                if not f.endswith((".c", ".cpp", ".h", ".hpp", ".inc")):
                    continue
                path = os.path.join(base, f)
                rel = ("petari/" + os.path.relpath(path, petari)) if d.startswith("petari/") else os.path.relpath(path, root)
                rel = rel.replace("\\", "/")
                stack = []  # per #if: True while the open branch is console-only
                comment = False
                for n, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
                    m = re.match(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", line)
                    if m:
                        k, cond = m.group(1), m.group(2)
                        if k in ("ifdef", "ifndef", "if"):
                            stack.append({"console": console_branch(k, cond), "native": native_branch(k, cond)})
                        elif k == "else" and stack:
                            g = stack[-1]
                            g["console"], g["native"] = g["native"], g["console"]
                        elif k == "elif" and stack:
                            stack[-1]["console"] = stack[-1]["native"] = False
                        elif k == "endif" and stack:
                            stack.pop()
                        continue
                    if any(g["console"] for g in stack):
                        continue
                    code = line
                    if comment:
                        if "*/" not in code:
                            continue
                        code = code.split("*/", 1)[1]
                        comment = False
                    code = re.sub(r"/\*.*?\*/", " ", code)
                    if "/*" in code:
                        code, comment = code.split("/*", 1)[0], True
                    code = re.sub(r'"(?:\\.|[^"\\])*"', '""', code.split("//")[0])
                    if not POINTER_TO_INTEGER.search(code):
                        continue
                    if not any(int(v, 16) >= 0x10000000 for v in ADDRESS.findall(code)):
                        continue
                    if any(rel == f and text in line for f, text, _ in ALLOWED):
                        continue
                    hits.append(f"{rel}:{n}: {line.strip()}")
    return hits


def main():
    root = sys.argv[1]
    hits = audit(root, sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "petari"))
    for h in hits:
        print(h)
    if hits:
        print(f"console_address_audit: {len(hits)} test(s) of a host pointer against the console memory map; use the "
              "native meaning under #ifdef TARGET_PC (native/tools/console_address_audit.py)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
