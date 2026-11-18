"""samples_check.py: every LumaScript code block of the modding docs, built by LumaLink as a mod's code.

    python -X utf8 -I tools/modding/samples_check.py --lumalink <program> --lumalink-tree <checkout> --names <folder>
                                                     --work <folder> [--jobs n] [--only <page>] [--write]

Each block is built as the source of a mod of its own, beside a LumaKit built first from the checkout (a block that
uses kit. needs it). It is tried three ways and passes when one builds with no error: whole; as the body of an
`on start` handler (a block of statements); and with its declarations at the top and its other lines in `on start`.
A line `...` stands for code left out and is dropped. A tutorial's block is built with its worked example's luma.json
(docs/modding/examples/<the page's name>); any other block gets the options its `option` lines declare.

The comment on the line before a block can say more:
  <!-- sample: S<code> -->            the block shows that mistake: it passes when the build gives that code;
  <!-- sample: examples/<path> -->    the block is that file of a worked example, whole: it passes when it is the same
                                      (--write makes it so). The mod tests build and run the examples.
  <!-- sample file: source/<name> --> the block (C, C++ or any) is a file of the mod: it is written into every sample
                                      of its page at that path.
A tutorial's block with no comment passes too when it is a piece of its example: its lines in the order the example
has them, each as written there but for the indent and an end-of-line comment.

Not built here: the forms on the services' pages (the compiler prints them: lumalink services --forms, service_pages.py)
and the declarations on LumaKit's pages (kit_pages.py, from LumaKit's sources, which LumaKit's own build compiles).

One line per block that does not pass: the page, the line, the first error. Exit 0 only when every block passes.
"""
import argparse
import concurrent.futures
import json
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs" / "modding"
MARK = re.compile(r"^<!-- sample: (S\d{4}|examples/[A-Za-z0-9_./-]+) -->$")
LEFT_OUT = ("...", "// ...")
FILE_MARK = re.compile(r"^<!-- sample file: (source/[A-Za-z0-9_./-]+) -->$")


def blocks(only=None, files=None):
    """(page, line of the fence, text, mark or None, (first line, end line) of the text in the page) of each block;
    `files` (a dict) gets each page's files ({page: {path: text}})."""
    out = []
    for md in sorted(DOCS.rglob("*.md")):
        rel = md.relative_to(DOCS).as_posix()
        if rel.startswith(("examples/", "reference/lumakit/")) or (only and rel != only):
            continue
        lines = md.read_text(encoding="utf-8").replace("\r\n", "\n").split("\n")
        generated = rel.startswith("reference/services/")
        part_is_functions = False
        i = 0
        while i < len(lines):
            if lines[i].startswith("## "):
                part_is_functions = bool(re.match(r"^## (?:`?[a-z][a-z0-9.]*`? functions|Functions|`?[a-z]+\.[a-z.]+`?)$", lines[i]))
            m = re.match(r"^```(\w*)", lines[i])
            if not m:
                i += 1
                continue
            j = i + 1
            while j < len(lines) and not lines[j].startswith("```"):
                j += 1
            file_mark = FILE_MARK.match(lines[i - 1]) if i > 0 else None
            if file_mark:
                if files is not None:
                    files.setdefault(rel, {})[file_mark.group(1)] = "\n".join(lines[i + 1:j]) + "\n"
            elif m.group(1) == "lumascript":
                text = "\n".join(lines[i + 1:j]) + "\n"
                mark = MARK.match(lines[i - 1]) if i > 0 else None
                # A service function's form (a signature, not code) is the compiler's: service_pages.py.
                form = generated and part_is_functions and all(
                    re.match(r"^(?:wait )?[a-z_]+\.\w+\(.*\)(?: -> .+)?$", l) for l in text.strip().split("\n"))
                if not form:
                    out.append((rel, i + 1, text, mark.group(1) if mark else None, (i + 1, j)))
            i = j + 1
    return out


def lumalink_build(args, folder):
    return subprocess.run([args.lumalink, "build", str(folder), "--names", args.names, "--sdk", str(ROOT / "native" / "include"),
                           "--platform", "windows-x64", "--no-installed"], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


# An option a block declares (`option <key>: <type>`), as luma.json gives it: the kind for the type, a default.
OPTION = re.compile(r"^\s*option\s+([a-z_][a-z0-9_]*)\s*:\s*([a-z0-9]+)", re.M)
KINDS = {"bool": ("toggle", {"default": False}), "text": ("text", {"default": ""}),
         "f32": ("number", {"default": 1.5, "min": 0.5, "max": 100.5, "step": 0.5})}
for t in ("int", "i32", "u32", "i64", "u64", "i16", "u16", "i8", "u8"):
    KINDS[t] = ("number", {"default": 1, "min": 0, "max": 100, "step": 1})


def example_of(page):
    """A tutorial's worked example (docs/modding/examples/<the page's name>), or None."""
    if not page.startswith("tutorials/"):
        return None
    folder = DOCS / "examples" / pathlib.PurePosixPath(page).stem
    return folder if (folder / "luma.json").is_file() else None


def manifest_for(k, page, source):
    """The sample's luma.json: a tutorial's block takes its worked example's (its options, needs and text); any other
    gets the options its `option` lines declare and LumaKit when it uses kit."""
    example = example_of(page)
    if example is not None:
        manifest = json.loads((example / "luma.json").read_text(encoding="utf-8"))
        manifest["id"] = "sample-%d" % k
        manifest["code"] = {"platforms": ["windows-x64"]}
        return manifest
    manifest = {"format": "1.0", "id": "sample-%d" % k, "version": "1.0.0", "name": "Sample %d" % k,
                "summary": "A sample of the docs.", "authors": ["docs"],
                "game": {"id": "RMG", "regions": ["RMGE01", "RMGP01", "RMGJ01"]}, "code": {"platforms": ["windows-x64"]}}
    options = []
    for key, ty in OPTION.findall(source):
        if ty in KINDS:
            kind, extra = KINDS[ty]
            options.append(dict({"key": key, "kind": kind, "name": key}, **extra))
    if options:
        manifest["options"] = options
    if re.search(r"\bkit\.", source):
        manifest["needs"] = [{"id": "lumakit", "version": ">=1.0.0"}]
    return manifest


def code_of(line):
    """A line without its indent and its end-of-line `//` comment (one inside a string kept)."""
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
            return line[:i].strip()
        i += 1
    return line.strip()


def in_example(page, text):
    """Whether a tutorial's block is a piece of its worked example: its code lines (blank lines, comment lines and
    `...` aside) in one of the example's source files, in that order, each as written there (code_of)."""
    example = example_of(page)
    if example is None:
        return False
    want = [code_of(l) for l in text.split("\n") if l.strip() not in LEFT_OUT]
    want = [l for l in want if l]
    if not want:
        return False
    for f in sorted((example / "source").rglob("*.lum")):
        have = [code_of(l) for l in f.read_text(encoding="utf-8").replace("\r\n", "\n").split("\n")]
        i = 0
        for l in have:
            if i < len(want) and l == want[i]:
                i += 1
        if i == len(want):
            return True
    return False


ITEM = re.compile(r"^(?:pub\s+)?(?:use|option|let|var|fn|struct|enum|class|task|hook|wrap|replace|on|states|test|extern|export)\b")


def tries(text):
    """The three sources a block is built as: whole, in `on start`, and declarations at the top with the rest of its
    lines (each line at the left edge that starts no declaration, and the lines under it) in `on start`."""
    lines = [l for l in text.split("\n") if l.strip() not in LEFT_OUT]
    whole = "\n".join(lines) + "\n"
    inside = "on start {\n" + "".join("    " + l + "\n" if l else "\n" for l in lines) + "}\n"
    top, body, in_item = [], [], True
    for l in lines:
        if l and not l[0].isspace() and l[0] not in "})]":
            in_item = bool(ITEM.match(l)) or l.startswith("//") and in_item
        (top if in_item else body).append(l)
    mixed = "\n".join(top) + "\non start {\n" + "".join("    " + l + "\n" if l else "\n" for l in body) + "}\n"
    return [whole, inside] + ([mixed] if body and top else [])


def build(args, k, page, text, files):
    """The first error of each try, or None when one built (or, for a tutorial, when the block is a piece of its
    worked example)."""
    if in_example(page, text):
        return None
    said = []
    for n, source in enumerate(tries(text)):
        folder = pathlib.Path(args.work) / ("s%04d-%d" % (k, n))
        shutil.rmtree(folder, ignore_errors=True)
        (folder / "source").mkdir(parents=True)
        (folder / "luma.json").write_text(json.dumps(manifest_for(k, page, source), indent=1, ensure_ascii=False), encoding="utf-8")
        (folder / "source" / "main.lum").write_text(source, encoding="utf-8")
        for path, body in files.get(page, {}).items():
            (folder / path).parent.mkdir(parents=True, exist_ok=True)
            (folder / path).write_text(body, encoding="utf-8")
        r = lumalink_build(args, folder)
        shutil.rmtree(folder, ignore_errors=True)
        if r.returncode == 0:
            return None
        lines = (r.stdout + r.stderr).strip().splitlines()
        said.append(next((l for l in lines if re.search(r"\berror [SL]\d{4}\b", l)), lines[-1] if lines else "exit %d" % r.returncode))
    return said


def file_blocks(todo, write):
    """The blocks marked as a worked example's file: (page, line, why) of each that is not that file; with write,
    each is made that file in its page."""
    wrong = []
    edits = {}
    for page, line, text, mark, span in todo:
        if mark is None or not mark.startswith("examples/"):
            continue
        f = DOCS / mark
        if not f.is_file():
            wrong.append((page, line, "%s: no such file" % mark))
            continue
        want = f.read_text(encoding="utf-8").replace("\r\n", "\n")
        if not want.endswith("\n"):
            want += "\n"
        if text != want:
            if write:
                edits.setdefault(page, []).append((span, want))
            else:
                wrong.append((page, line, "not the same as %s (--write makes it so)" % mark))
    for page, spans in edits.items():
        p = DOCS / page
        raw = p.read_bytes().decode("utf-8")
        nl = "\r\n" if "\r\n" in raw else "\n"
        lines = raw.replace("\r\n", "\n").split("\n")
        for (first, end), want in sorted(spans, reverse=True):
            lines[first:end] = want[:-1].split("\n")
        p.write_bytes(nl.join(lines).encode("utf-8"))
        print("written: %s (%d blocks)" % (page, len(spans)))
    return wrong


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--lumalink", required=True)
    p.add_argument("--lumalink-tree", required=True)
    p.add_argument("--names", required=True)
    p.add_argument("--work", required=True)
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--only")
    p.add_argument("--write", action="store_true", help="make each block marked as a worked example's file that file")
    args = p.parse_args(argv)
    files = {}
    todo = blocks(args.only, files)
    wrong = ["%s:%d: %s" % w for w in file_blocks(todo, args.write)]
    if args.write:
        todo = blocks(args.only)
    work = pathlib.Path(args.work)
    if work.exists():
        shutil.rmtree(work)
    # The samples are built beside LumaKit (a mod's needs are found beside it), which is built first: its interface
    # (code/lumakit.lumi) is what `use kit.<module>` reads.
    kit = work / "lumakit"
    shutil.copytree(pathlib.Path(args.lumalink_tree) / "lumakit", kit)
    r = lumalink_build(args, kit)
    if r.returncode != 0:
        print((r.stdout + r.stderr).strip())
        print("samples: LumaKit did not build, so no sample was built")
        return 1
    built = [b for b in todo if b[3] is None or not b[3].startswith("examples/")]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        done = pool.map(lambda kb: (kb[1], build(args, kb[0], kb[1][0], kb[1][2], files)), enumerate(built))
        for (page, line, _, expected, _), said in done:
            if expected is None and said is not None:
                wrong.append("%s:%d: %s" % (page, line, " | ".join(dict.fromkeys(said))))
            elif expected is not None and (said is None or not any(expected in s for s in said)):
                wrong.append("%s:%d: built with no %s, which the page shows: %s" % (page, line, expected, said))
    for w in wrong:
        print(w)
    print("samples: %d of %d blocks as the docs show them (%d built, %d the worked examples' files)"
          % (len(todo) - len(wrong), len(todo), len(built), len(todo) - len(built)))
    return 0 if not wrong else 1


if __name__ == "__main__":
    sys.exit(main())
