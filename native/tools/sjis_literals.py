"""sjis_literals.py <petari root> [--apply]: narrow string and character literals holding non-ASCII text, which the
console's CodeWarrior build encodes in Shift-JIS (CP932) and clang in UTF-8. Hashes of literals (the save's VLE1 table)
and comparisons with Shift-JIS strings from files then differ natively. With --apply each such literal becomes the
octal escapes of its CP932 bytes (octal, because a Shift-JIS trail byte can be 0x5C, a backslash, and a hex escape
would run into a following digit), with the text kept in a comment. Wide literals (L"...") are left alone: they are
UTF-16 on both. Prints every literal and file changed."""
import os
import re
import sys

root = sys.argv[1]
apply = "--apply" in sys.argv
dirs = ["src/Game", "src/JSystem", "src/nw4r", "src/RVLFaceLib", "src/RVL_SDK", "include", "libs/JSystem/include",
        "libs/nw4r/include", "libs/RVLFaceLib/include", "libs/RVL_SDK/include"]

# Tokens that matter for finding literals: comments, string and character literals.
token = re.compile(r'//[^\n]*|/\*.*?\*/|(?P<prefix>u8|[LuU])?(?P<quote>["\'])(?P<body>(?:[^"\'\\\n]|\\.|(?!(?P=quote))["\'])*)(?P=quote)',
                   re.S)


def escape(body):
    """The literal's body with every non-ASCII character as CP932 octal escapes; existing escapes kept."""
    out = []
    for ch in body:
        if ord(ch) < 128:
            out.append(ch)
        else:
            out.extend("\\%03o" % b for b in ch.encode("cp932"))
    return "".join(out)


total = changed_files = 0
for d in dirs:
    for base, _, files in os.walk(os.path.join(root, d)):
        for f in sorted(files):
            if not f.endswith((".c", ".cpp", ".h", ".hpp", ".inc")):
                continue
            path = os.path.join(base, f)
            raw = open(path, "rb").read()
            text = raw.decode("utf-8")
            pieces = []
            last = 0
            hits = 0
            for m in token.finditer(text):
                if m.group("quote") is None or m.group("prefix"):
                    continue
                body = m.group("body")
                if all(ord(ch) < 128 for ch in body):
                    continue
                hits += 1
                rel = os.path.relpath(path, root).replace("\\", "/")
                line = text.count("\n", 0, m.start()) + 1
                print(f"{rel}:{line}: {m.group(0)}")
                q = m.group("quote")
                replacement = q + escape(body) + q + " /* " + body.replace("*/", "* /") + " */"
                pieces.append(text[last:m.start()])
                pieces.append(replacement)
                last = m.end()
            if hits:
                total += hits
                changed_files += 1
                if apply:
                    pieces.append(text[last:])
                    out = "".join(pieces)
                    open(path, "wb").write(out.encode("utf-8"))
print(f"{total} literals in {changed_files} files" + (" rewritten" if apply else ""))
