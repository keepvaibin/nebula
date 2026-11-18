"""The LumaKit reference pages (docs/modding/reference/lumakit), generated from LumaKit's sources: each page's
"How the module works" (the module's header comment), its Declarations table and one section per public declaration
(the declaration as the source writes it: a struct, enum or class whole, a function's signature; then the comment
above it). The page's own text above "How the module works" and its Further reading are kept as written.

    python -X utf8 -I tools/modding/kit_pages.py <lumakit folder> [--pages <folder>] [--check]

<lumakit folder>: LumaKit in a LumaLink checkout (its source/). --pages: default docs/modding/reference/lumakit. Every
module with a public declaration must have its page.

--check writes nothing: it lists each page that differs from what the sources make, and exits 1 when one does.
"""
import argparse
import difflib
import pathlib
import re
import sys

KINDS = {"struct": "Struct", "enum": "Enum", "class": "Class", "fn": "Function", "task": "Task", "let": "Constant",
         "var": "Variable", "type": "Type"}
DECL = re.compile(r"^pub\s+(struct|enum|class|fn|task|let|var|type)\s+([A-Za-z_][A-Za-z0-9_]*)")


def module_of(page: str):
    """The module a page names in its 'The module's source is `lumakit/source/<path>`' line."""
    m = re.search(r"The module's source is `lumakit/source/([^`]+)`", page)
    return m.group(1) if m else None


# The docs are in US English; LumaKit's comments are not always. Outside code spans only.
US = [("colour", "color"), ("Colour", "Color"), ("behaviour", "behavior"), ("Behaviour", "Behavior"),
      ("grey", "gray"), ("Grey", "Gray"), ("cancelled", "canceled"), ("Cancelled", "Canceled"), ("centred", "centered"),
      ("Centred", "Centered"), ("centre", "center"),
      ("Centre", "Center"), ("favourite", "favorite"), ("licence", "license"), ("Licence", "License")]


def us_english(text):
    parts = text.split("`")
    for i in range(0, len(parts), 2):
        for a, b in US:
            parts[i] = re.sub(r"\b" + a, b, parts[i])
    return "`".join(parts)


def sentence(text):
    """A comment as a sentence: a capital first letter and a full stop."""
    text = text.strip()
    if not text:
        return ""
    text = text[0].upper() + text[1:]
    return us_english(text if text.endswith((".", "?", ":")) else text + ".")


def comment_text(lines):
    """A comment's lines as one paragraph: '// ' taken off, joined with spaces."""
    return re.sub(r" +", " ", " ".join(l.strip()[2:].strip() for l in lines)).strip()


def header(source):
    """The module's header comment: the comment lines at the top, before the first blank or code line; its
    'kit.x: ' prefix taken off and the first letter made a capital."""
    lines = []
    for l in source:
        if l.startswith("//"):
            lines.append(l)
        else:
            break
    text = comment_text(lines)
    text = re.sub(r"^kit\.[a-z.]+:\s*", "", text)
    return text[:1].upper() + text[1:]


def braces(line):
    """The line's opening braces less its closing ones, outside strings and a trailing // comment."""
    n = 0
    in_text = False
    i = 0
    while i < len(line):
        c = line[i]
        if in_text:
            if c == "\\":
                i += 1
            elif c == '"':
                in_text = False
        elif c == '"':
            in_text = True
        elif line.startswith("//", i):
            break
        elif c == "{":
            n += 1
        elif c == "}":
            n -= 1
        i += 1
    return n


def declarations(source):
    """(kind, name, declaration text, comment) of each public declaration, in source order."""
    out = []
    i = 0
    while i < len(source):
        m = DECL.match(source[i])
        if not m:
            i += 1
            continue
        kind, name = m.group(1), m.group(2)
        # The comment right above it (no blank line between), section rules ('// ----') left out.
        j = i - 1
        comment = []
        while j >= 0 and source[j].startswith("//") and not source[j].startswith("// ----"):
            comment.insert(0, source[j])
            j -= 1
        if kind in ("fn", "task"):
            # The signature: up to the body's opening brace, which may be on a later line.
            text = []
            k = i
            while True:
                line = source[k]
                if line.rstrip().endswith("{"):
                    text.append(line.rstrip()[:-1].rstrip())
                    break
                text.append(line.rstrip())
                k += 1
            decl = "\n".join(text)
            i = k + 1
        else:
            # Whole: through the line where its braces close (braces in strings and comments not counted).
            depth = 0
            k = i
            while True:
                depth += braces(source[k])
                if depth <= 0:
                    break
                k += 1
            decl = "\n".join(l.rstrip() for l in source[i:k + 1])
            i = k + 1
        out.append((kind, name, decl, comment_text(comment)))
    return out


def anchor(name):
    return name.lower()


def generated(source_path: pathlib.Path, rel: str):
    source = source_path.read_text(encoding="utf-8").split("\n")
    parts = ["### How the module works", "", sentence(header(source)), "",
             "The module's source is `lumakit/source/%s`." % rel, "", "## Declarations", "",
             "| Name | Kind | Description |", "|---|---|---|"]
    decls = declarations(source)
    for kind, name, _, comment in decls:
        parts.append("| [`%s`](#%s) | %s | %s |" % (name, anchor(name), KINDS[kind], sentence(comment).replace("|", "\\|")))
    parts.append("")
    for kind, name, decl, comment in decls:
        parts += ["### `%s`" % name, "", "```lumascript", decl, "```", ""]
        if comment:
            parts += [sentence(comment), ""]
    return "\n".join(parts) + "\n"


def page_from(page_text: str, body: str):
    start = page_text.index("### How the module works")
    end = page_text.index("## Further reading")
    return page_text[:start] + body + page_text[end:]


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("lumakit")
    p.add_argument("--pages", default=str(pathlib.Path(__file__).resolve().parents[2] / "docs" / "modding" / "reference" / "lumakit"))
    p.add_argument("--check", action="store_true")
    args = p.parse_args()
    kit, pages, check = pathlib.Path(args.lumakit), pathlib.Path(args.pages), args.check
    differ = []
    for page in sorted(pages.glob("*.md")):
        text = page.read_text(encoding="utf-8")
        rel = module_of(text)
        if rel is None:
            continue
        new = page_from(text, generated(kit / "source" / rel, rel))
        if new != text:
            differ.append(page.name)
            if check:
                sys.stdout.writelines(difflib.unified_diff(text.splitlines(True), new.splitlines(True),
                                                           "now/" + page.name, "made/" + page.name, n=0))
            else:
                page.write_text(new, encoding="utf-8", newline="\n")
    for name in differ:
        print(("differs: " if check else "written: ") + name)
    # Every module with a public declaration has its page.
    paged = {module_of(p.read_text(encoding="utf-8")) for p in pages.glob("*.md")}
    missing = [rel for rel in (f.relative_to(kit / "source").as_posix() for f in sorted((kit / "source").rglob("*.lum")))
               if rel not in paged and declarations((kit / "source" / rel).read_text(encoding="utf-8").split("\n"))]
    for rel in missing:
        print("no page: lumakit/source/%s" % rel)
    return 1 if (check and differ) or missing else 0


if __name__ == "__main__":
    sys.exit(main())
