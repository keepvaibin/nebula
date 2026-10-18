"""brace_cases.py <petari root> <census log dir>: for every "cannot jump from switch statement to this case label"
in the census logs, give each case whose declaration the jump bypasses its own braces (CodeWarrior allowed the jump;
standard C++ does not). The case's body runs from its label to the next label at the same indentation. A variable
another case still uses would no longer compile, so a clean compile afterwards shows the scopes were case-local."""
import collections
import glob
import os
import re
import sys

root, logs = sys.argv[1], sys.argv[2]
label = re.compile(r"^(\s*)(case\b.*|default\s*):\s*(//.*)?$")
sites = collections.defaultdict(set)
for log in glob.glob(os.path.join(logs, "*.log")):
    lines = open(log, encoding="utf-8").read().splitlines()
    for i, l in enumerate(lines):
        if "cannot jump from switch statement to this case label" not in l:
            continue
        for n in lines[i + 1:i + 40]:
            m = re.match(r"(.*?):(\d+):\d+: note: jump bypasses variable initialization", n)
            if m:
                sites[m.group(1).replace("\\", "/")].add(int(m.group(2)))
            elif ": error:" in n:
                break
for path, decls in sorted(sites.items()):
    full = os.path.join(root, path)
    raw = open(full, "rb").read().decode("utf-8")
    crlf = "\r\n" in raw
    src = raw.replace("\r\n", "\n").split("\n")
    regions = set()
    for d in decls:
        i = d - 1
        while i >= 0 and not label.match(src[i]):
            i -= 1
        assert i >= 0, (path, d)
        indent = label.match(src[i]).group(1)
        j = d
        while j < len(src):
            m = label.match(src[j])
            if m and m.group(1) == indent:
                break
            if src[j].strip() == "}" and len(src[j]) - len(src[j].lstrip()) < len(indent):
                break
            j += 1
        regions.add((i, j, indent))
    for i, j, indent in sorted(regions, reverse=True):
        assert not src[i].rstrip().endswith("{"), (path, i + 1)
        while j - 1 > i and src[j - 1].strip() == "":
            j -= 1
        src.insert(j, indent + "}")
        src[i] = src[i].rstrip() + " {"
    text = "\n".join(src)
    open(full, "wb").write((text.replace("\n", "\r\n") if crlf else text).encode("utf-8"))
    print(path, sorted(r[0] + 1 for r in regions))
