"""doc_refs.py: file and line references in documents must exist (package T13; T10 runs it on docs/engine/*.md).

    python -X utf8 -I tools/verify/doc_refs.py [--root TREE] [--allow FILE] [--message] FILE [FILE ...]

A reference is text in single backticks that is exactly one of
    path/to/file.ext          path/to/file.ext:123          path/to/file.ext:123-145
(a path with at least one folder, forward slashes, no spaces, a file extension). It is reported when the file is not
in the tree (`--root`, default the tree this script is in) or when its line is past the file's last line.

Not checked: text with no folder (`renderer_common.cpp`: which one is meant is not known), paths that start with a
drive letter, `/`, `~` or a URL scheme (they point outside the tree by their form), text with a wildcard or a
placeholder (`*`, `<`, `{`), and anything inside a fenced code block.

--allow FILE: references that are known to point outside the tree (a branch's file, a file a step is about to make).
One per line: a reference as written (`path/file.ext` or `path/file.ext:123`) is allowed wherever it appears; a line
`path/to/document.md :: reference` allows it in that document only (the document may be a shell pattern such as
`docs/plan/native/*.md`, the reference may be `*` for every reference of those documents: a plan that names files
its steps are about to make); `#` starts a comment. A line number is cut off
before the comparison with a bare `path/file.ext` entry, so one entry allows every line of that file. The default
allow-list is `tools/verify/doc_refs_allow.txt` when it exists.

--message: the files are commit messages (or any text, not Markdown): the same references are read from the whole
text, with or without backticks.

Prints one line per bad reference, `FILE:LINE: reference: why`, and exits 1 when there is any, else 0.
"""
import argparse
import fnmatch
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
DEFAULT_ALLOW = HERE / "doc_refs_allow.txt"

REF = re.compile(r"^(?P<path>[A-Za-z0-9_.+-]+(?:/[A-Za-z0-9_.+@-]+)+\.[A-Za-z][A-Za-z0-9]{0,7})"
                 r"(?::(?P<a>\d+)(?:-(?P<b>\d+))?)?$")
SPAN = re.compile(r"`([^`\n]+)`")
# In a commit message: the same, not inside backticks, bounded by space or punctuation.
LOOSE = re.compile(r"(?<![\w/.:`-])([A-Za-z0-9_.+-]+(?:/[A-Za-z0-9_.+@-]+)+\.[A-Za-z][A-Za-z0-9]{0,7}(?::\d+(?:-\d+)?)?)(?![\w/`-])")
OUTSIDE = re.compile(r"^(?:[A-Za-z]:|/|~|\.\./|[A-Za-z][A-Za-z0-9+.-]*://)")


def read_allow(path):
    """(everywhere: set of references, only_in: {document: set of references})."""
    everywhere, only_in = set(), {}
    if not path or not pathlib.Path(path).is_file():
        return everywhere, only_in
    for raw in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if "::" in line:
            doc, ref = (s.strip() for s in line.split("::", 1))
            only_in.setdefault(doc.replace("\\", "/"), set()).add(ref)
        else:
            everywhere.add(line)
    return everywhere, only_in


def allowed(ref, doc, allow):
    everywhere, only_in = allow
    bare = ref.split(":", 1)[0]
    if ref in everywhere or bare in everywhere:
        return True
    for pattern, refs in only_in.items():
        if fnmatch.fnmatchcase(doc, pattern) and ("*" in refs or ref in refs or bare in refs):
            return True
    return False


_LINES = {}


def line_count(path):
    """The number of lines of a file (a last line with no newline counts); an empty file has none."""
    n = _LINES.get(path)
    if n is None:
        data = path.read_bytes()
        n = data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)
        _LINES[path] = n
    return n


def judge(text, root):
    """None when the reference is fine (or not one), else why not. `text`: what stood between the backticks."""
    m = REF.match(text.strip())
    if not m or OUTSIDE.match(text.strip()) or any(c in text for c in "*<>{}"):
        return None
    target = pathlib.Path(root) / m.group("path")
    if not target.is_file():
        return "no such file in the tree"
    if m.group("a"):
        first = int(m.group("a"))
        last = int(m.group("b")) if m.group("b") else first
        n = line_count(target)
        if last < first:
            return "the line range runs backwards"
        if first < 1 or last > n:
            return "line %d is past the end (the file has %d lines)" % (last if last > n else first, n)
    return None


def references(text, message=False):
    """[(line number, reference text)] in a document: inline backtick spans outside fenced blocks, or, for a message,
    every path-shaped word."""
    out, fenced = [], False
    for number, line in enumerate(text.splitlines(), 1):
        if not message and line.lstrip().startswith(("```", "~~~")):
            fenced = not fenced
            continue
        if fenced:
            continue
        found = LOOSE.findall(line) if message else SPAN.findall(line)
        out.extend((number, f) for f in found)
    return out


def check_text(name, text, root, allow, message=False):
    """The bad references of one text, as 'name:line: reference: why' lines."""
    bad = []
    for number, ref in references(text, message):
        why = judge(ref, root)
        if why and not allowed(ref.strip(), name.replace("\\", "/"), allow):
            bad.append("%s:%d: %s: %s" % (name, number, ref.strip(), why))
    return bad


def check_files(files, root, allow, message=False):
    bad = []
    for f in files:
        path = pathlib.Path(f)
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as e:
            bad.append("%s:0: (file): cannot be read: %s" % (f, e))
            continue
        try:
            name = path.resolve().relative_to(pathlib.Path(root).resolve()).as_posix()
        except ValueError:
            name = str(f).replace("\\", "/")
        bad.extend(check_text(name, text, root, allow, message))
    return bad


def main(argv=None):
    # A self-test that starts this tool as a child process runs it under the live-state guard (tools/gate/selftest_guard.py).
    import pathlib as _pathlib
    import sys as _sys
    _gate = str(_pathlib.Path(__file__).resolve().parents[1] / "gate")
    _sys.path.insert(0, _gate)
    try:
        import selftest_guard
    finally:
        _sys.path.remove(_gate)
    selftest_guard.install_if_asked()
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("files", nargs="+")
    p.add_argument("--root", default=str(TREE))
    p.add_argument("--allow", default=str(DEFAULT_ALLOW))
    p.add_argument("--message", action="store_true", help="the files are commit messages")
    args = p.parse_args(argv)
    bad = check_files(args.files, args.root, read_allow(args.allow), args.message)
    for line in bad:
        print(line)
    print("doc_refs: %s" % ("%d bad reference%s" % (len(bad), "" if len(bad) == 1 else "s") if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
