"""Finds the address of a block-scoped local kept past its block: `p = buf;` / `p = &obj;` where buf or obj is declared
in an inner block and p outside it (or p is a member, global or parameter). CodeWarrior keeps every local of a function
for its whole frame, so such code works on the console; clang ends the object's lifetime with its block and may reuse
the storage. Every .c/.cpp file of the game and its libraries is scanned; each hit is reviewed by hand.
    python scope_escape_audit.py <petari dir>"""
import os
import re
import sys

KEYWORDS = {"return", "if", "else", "while", "for", "switch", "case", "do", "goto", "break", "continue", "delete",
            "new", "sizeof", "throw", "typedef", "using", "namespace", "template", "operator"}
DECL = re.compile(r"(?:^|[;{}])\s*(?:static\s+|const\s+|volatile\s+|register\s+|struct\s+|unsigned\s+|signed\s+)*"
                  r"([A-Za-z_][\w:]*(?:\s*<[^;{}()]*?>)?)\s*[\*&]*\s+([A-Za-z_]\w*)\s*(\[[^\];]*\])?\s*(?=[;=({\[,])")
ASSIGN = re.compile(r"([A-Za-z_][\w\.\->\[\]]*)\s*=\s*(&\s*)?([A-Za-z_]\w*)\s*(\[[^\];]*\])?\s*(?:\+[^;]*)?;")


def strip(text):
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            seg = text[i:(n if j < 0 else j + 2)]
            out.append("\n" * seg.count("\n"))
            i = n if j < 0 else j + 2
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        elif c == "#":
            j = text.find("\n", i)
            while j > 0 and text[j - 1] == "\\":
                j = text.find("\n", j + 1)
            i = n if j < 0 else j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def scan(path):
    text = strip(open(path, encoding="utf-8", errors="replace").read())
    hits = []
    # scopes: list of (kind, {name: is_array}); kind "fn" marks a function body's outermost block
    scopes = []
    statement_start = 0
    paren = 0
    for i, c in enumerate(text):
        if c == "(":
            paren += 1
        elif c == ")":
            paren -= 1
        elif c == "{" and paren == 0:
            head = text[statement_start:i]
            inside_fn = any(k == "fn" for k, _ in scopes)
            is_fn = (not inside_fn) and re.search(r"\)\s*(const\s*)?(noexcept\s*)?(:[^{]*)?$", head.strip()) is not None \
                and not re.search(r"\b(if|while|for|switch)\s*\(", head)
            scopes.append(("fn" if is_fn else ("blk" if inside_fn else "ns"), {}))
            statement_start = i + 1
        elif c == "}" and paren == 0:
            if scopes:
                scopes.pop()
            statement_start = i + 1
        elif c == ";" and paren == 0:
            stmt = text[statement_start:i + 1]
            statement_start = i + 1
            if not any(k == "fn" for k, _ in scopes):
                continue
            depth = len(scopes)
            m = DECL.match(";" + stmt.strip())
            if m and m.group(1) not in KEYWORDS and m.group(2) not in KEYWORDS and "=" not in m.group(1):
                scopes[-1][1][m.group(2)] = m.group(3) is not None
            for a in ASSIGN.finditer(stmt):
                lhs, amp, rhs, index = a.group(1), a.group(2), a.group(3), a.group(4)
                rhs_depth = None
                for d in range(len(scopes) - 1, -1, -1):
                    if rhs in scopes[d][1]:
                        rhs_depth, is_array = d, scopes[d][1][rhs]
                        break
                if rhs_depth is None or not (amp or is_array):
                    continue
                if not amp and index:
                    continue  # buf[i]: an element's value
                base = re.match(r"[A-Za-z_]\w*", lhs).group(0)
                lhs_depth = None
                for d in range(len(scopes) - 1, -1, -1):
                    if base in scopes[d][1]:
                        lhs_depth = d
                        break
                fn_depth = next(d for d, (k, _) in enumerate(scopes) if k == "fn")
                if lhs_depth is None:
                    lhs_depth = fn_depth - 1  # member, global or parameter
                if lhs_depth < rhs_depth and (rhs_depth > fn_depth or lhs_depth < fn_depth):
                    line = text.count("\n", 0, i) + 1
                    kind = "block" if rhs_depth > fn_depth else "frame"
                    hits.append((line, kind, stmt.strip().replace("\n", " ")[:120]))
    return hits


def main():
    root = sys.argv[1]
    total = 0
    for top in ("src", "libs"):
        for base, _, files in os.walk(os.path.join(root, top)):
            for f in sorted(files):
                if f.endswith((".c", ".cpp")):
                    path = os.path.join(base, f)
                    for line, kind, stmt in scan(path):
                        total += 1
                        print("%s:%d: %s: %s" % (os.path.relpath(path, root).replace("\\", "/"), line, kind, stmt))
    print("hits:", total)


main()
