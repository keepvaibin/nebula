"""lumakit_pages_test.py: the LumaKit reference pages have no sentence with a lost word (M-034).

python -X utf8 -I tools/gate/tests/lumakit_pages_test.py [--pages <folder>]

The pages (docs/modding/reference/lumakit) were written from the kit's doc comments; where a comment lost its last
words before a line break, the page printed the gap: "the newest caller's . An empty list". A word, a closing quote
or bracket, then a space and a full stop, comma, semicolon or colon, outside code spans, is such a gap.
Exit 0 when no page has one.
"""
import pathlib
import re
import sys

TREE = pathlib.Path(__file__).resolve().parents[3]
PAGES = TREE / "docs" / "modding" / "reference" / "lumakit"
CODE = re.compile(r"`[^`\n]*`")
GAP = re.compile(r"[\w'\")\]] [.,;:](?=\s|$|\|)")


def gaps(folder):
    found = []
    for page in sorted(pathlib.Path(folder).glob("*.md")):
        for number, line in enumerate(page.read_bytes().decode("utf-8").splitlines(), 1):
            plain = CODE.sub("``", line)
            for m in GAP.finditer(plain):
                found.append("%s:%d: %r" % (page.name, number, plain[max(0, m.start() - 40):m.end() + 20]))
    return found


def main():
    folder = PAGES
    if len(sys.argv) == 3 and sys.argv[1] == "--pages":
        folder = pathlib.Path(sys.argv[2])
    elif len(sys.argv) != 1:
        print(__doc__)
        return 2
    pages = sorted(pathlib.Path(folder).glob("*.md"))
    if not pages:
        print("lumakit_pages_test: no pages in %s" % folder)
        return 1
    found = gaps(folder)
    for line in found:
        print("lumakit_pages_test: a lost word: " + line)
    print("lumakit_pages_test: %d pages, %d gaps" % (len(pages), len(found)))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
