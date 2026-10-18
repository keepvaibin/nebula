"""Lists loops that may spin on another thread's work: empty bodies, and short loops whose condition polls state
(a call or member read) with no OS call in the body. Natively such a loop never takes the interrupt that would
preempt it on the console; each listed site is reviewed (docs/NATIVE_PORT.md).
    spin_audit.py [native dir]"""
import os
import re
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..")
DIRS = ["petari/src", "petari/libs", "sdk", "host"]
BLOCKING = re.compile(r"OSYieldThread|OSSleep|VIWaitForRetrace|OSReceiveMessage|OSWaitCond|OSJamMessage|OSLockMutex|"
                      r"Sync\(|OSSuspendThread|waitForRetrace|__NebulaPoll|OSWaitSemaphore|waitTime|OSSendMessage|sleep")
POLLED = re.compile(r"\b(is\w*(Done|End|Load|Finish|Ready|Busy|Active|Wait|Complete|Exec|Run)\w*|"
                    r"\w*(Status|Busy|Done|Finished|Ready|Flag|flag|Count)\w*)")
SHORT = 250


def balanced(s, i, o, c):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == o:
            depth += 1
        elif s[j] == c:
            depth -= 1
            if depth == 0:
                return j
    return -1


def flat(text, n):
    return " ".join(text.split())[:n]


def loops(s):
    """(start, condition, body) of every while, do-while and for (;;) loop."""
    for m in re.finditer(r"\bwhile\s*\(", s):
        if s[:m.start()].rstrip().endswith("}"):
            continue  # a do-while's tail
        ce = balanced(s, m.end() - 1, "(", ")")
        rest = s[ce + 1:].lstrip()
        if rest.startswith("{"):
            bs = s.index("{", ce)
            body = s[bs + 1:balanced(s, bs, "{", "}")]
        else:
            body = rest[:rest.find(";") + 1].rstrip(";")
        yield m.start(), s[m.end():ce], body
    for m in re.finditer(r"\bdo\s*\{", s):
        be = balanced(s, m.end() - 1, "{", "}")
        t = re.match(r"\s*while\s*\(", s[be + 1:])
        if t:
            cs = be + t.end()
            yield m.start(), s[cs + 1:balanced(s, cs, "(", ")")], s[m.end():be]
    for m in re.finditer(r"\bfor\s*\(\s*;\s*;\s*\)\s*(\{\s*\}|;)", s):
        yield m.start(), "", ""


for d in DIRS:
    for base, _, files in os.walk(os.path.join(ROOT, d)):
        for f in files:
            if not f.endswith((".c", ".cpp", ".h", ".hpp", ".inc")):
                continue
            path = os.path.join(base, f)
            s = open(path, encoding="utf-8", errors="replace").read()
            for start, cond, body in loops(s):
                empty = not body.strip()
                polls = (re.search(r"\w\s*\(|->|\.", cond) and POLLED.search(cond) and len(body) <= SHORT
                         and not BLOCKING.search(body + cond))
                if not (empty or polls):
                    continue
                line = s.count("\n", 0, start) + 1
                print(f"{os.path.relpath(path, ROOT)}:{line}: ({flat(cond, 80)}) {{{flat(body, 90)}}}")
