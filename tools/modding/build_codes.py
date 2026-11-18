"""docs/modding/reference/lumascript/build-codes.md: every code LumaLink's compiler gives when it builds a mod, made from
the compiler: its list of codes and notes (crates/lumascript/src/diag.rs, code::ALL and code::NOTES), each code's message
(LumaLink's text table, crates/lumalink-core/text/LumaLinkText.tsv, its US English row LumaLink.Script.S<code>), and
the things S0800 names (the same table's rows LumaLink.Script.NotYet.*, by which the emitter names them).

    python -X utf8 -I tools/modding/build_codes.py <LumaLink tree> [--page <file>] [--check]

--check writes nothing: it prints how the page differs from what the compiler makes, and exits 1 when it does.
"""
import argparse
import difflib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PAGE = ROOT / "docs" / "modding" / "reference" / "lumascript" / "build-codes.md"

HEAD = """---
title: Build codes
shortTitle: Build codes
intro: Every code LumaLink gives when it builds a mod's LumaScript, with its message.
contentType: reference
---

## About build codes

When LumaLink builds a mod, each mistake it finds in the scripts is a line with a code, the message and the place:

```
error S0012: `&&` is written `and` (source/main.lum:2:10)
```

An error stops the build: nothing is built until every error is put right. A note does not stop it. LumaLink lists every error and note of a build, not only the first. It writes the message in the language it speaks (`--language`, one of the game's 14).

In the messages below, a word in braces is filled in for each mistake: `{found}` is what the script has, `{expected}` what the rule wants, `{what}` the kind of thing, `{name}` a name.

Codes S0901 and above are script errors while the game runs, not build codes. For those, see [Errors](errors.md).
"""

TAIL = """
## Further reading

* [Errors](errors.md)
* [Detailed rules](detailed-rules.md)
* [Not in LumaScript](not-in-lumascript.md)
"""


def codes(diag):
    """[(number, constant)] in the order of code::ALL, and the set of notes."""
    consts = {m.group(1): int(m.group(2)) for m in re.finditer(r"pub const ([A-Z_0-9]+): u16 = (\d+);", diag)}
    body = re.search(r"pub const ALL: &\[u16\] = &\[(.*?)\];", diag, re.S).group(1)
    notes = re.search(r"pub const NOTES: &\[u16\] = &\[(.*?)\];", diag, re.S).group(1)
    listed = [n.strip() for n in body.split(",") if n.strip()]
    return [(consts[n], n) for n in listed], {consts[n.strip()] for n in notes.split(",") if n.strip()}


def messages(tsv):
    out = {}
    for line in tsv.split("\n"):
        cells = line.rstrip("\r").split("\t")
        if re.match(r"LumaLink\.Script\.S\d{4}$", cells[0]) and len(cells) > 1:
            out[int(cells[0][len("LumaLink.Script.S"):])] = cells[1]
    return out


def not_yet(tsv):
    """What S0800 names, each a list item: the US English of each phrase row LumaLink.Script.NotYet.<id> (the
    emitter names its construct by one; a word in braces is filled in), sorted. A row the same in every language is
    code, shown as code; a leading [ or - is escaped, so the item is not a check box or a list of its own."""
    found = {}
    for line in tsv.split("\n"):
        cells = line.rstrip("\r").split("\t")
        if cells[0].startswith("LumaLink.Script.NotYet.") and len(cells) > 1 and cells[1] != "this construct":
            code = len({c for c in cells[1:] if c}) == 1
            found[cells[1]] = "`%s`" % cells[1] if code else re.sub(r"^([\[-])", r"\\\1", cells[1])
    return [found[w] for w in sorted(found, key=lambda w: (w.lower().lstrip("{"), w))]


def cell(t):
    return t.replace("|", "\\|")


def made(tree):
    diag = (tree / "crates" / "lumascript" / "src" / "diag.rs").read_text(encoding="utf-8")
    tsv = (tree / "crates" / "lumalink-core" / "text" / "LumaLinkText.tsv").read_text(encoding="utf-8")
    listed, notes = codes(diag)
    said = messages(tsv)
    problems = []
    lines = [HEAD, "## Codes", "", "| Code | Kind | Message |", "|---|---|---|"]
    for number, const in sorted(listed):
        text = said.get(number)
        if text is None:
            problems.append("S%04d (%s): no message row" % (number, const))
            text = ""
        lines.append("| S%04d | %s | %s |" % (number, "note" if number in notes else "error", cell(text)))
    lines += ["", "## What S0800 names", "",
              "S0800 is a construct that LumaScript has and LumaLink cannot build yet. These are the constructs it names:", ""]
    lines += ["* %s" % cell(w) for w in not_yet(tsv)]
    lines.append(TAIL)
    return "\n".join(lines), problems


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("tree")
    p.add_argument("--page", default=str(PAGE))
    p.add_argument("--check", action="store_true")
    args = p.parse_args(argv)
    text, problems = made(pathlib.Path(args.tree))
    for problem in problems:
        print(problem)
    page = pathlib.Path(args.page)
    now = page.read_text(encoding="utf-8").replace("\r\n", "\n") if page.is_file() else ""
    if args.check:
        if now != text:
            sys.stdout.writelines(difflib.unified_diff(now.splitlines(True), text.splitlines(True), "page", "compiler", n=0))
            print("differs: %s" % page.name)
            return 1
        return 1 if problems else 0
    if now != text:
        page.write_text(text, encoding="utf-8", newline="\n")
        print("written: %s" % page.name)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
