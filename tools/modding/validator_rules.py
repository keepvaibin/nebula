"""Writes docs/modding/reference/validator-rules.md: every code the validator reports, with its level and words, and
every phrase a finding's {why}, {what} or {kind} can be, in English, from the game's own text table.

  python -X utf8 -I tools/modding/validator_rules.py [--out FILE] [--check]

The words are the Validator.* rows of localization/NebulaText.tsv (its English column); the levels are
crates/luma-validate/src/lib.rs `level_of`. --check exits 1 when FILE differs from what it would write.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
GROUPS = [
    ("1", "The container", "The `.luma` file as a ZIP archive."),
    ("2", "luma.json", "The manifest. For every key, see [luma.json](luma-json.md)."),
    ("3", "Layers and files", "The content in `layers/`."),
    ("4", "Table patches", "Patches of the game's tables. For the keys, see [Table keys](table-keys.md)."),
    ("5", "Code", "The mod's built libraries in `code/`."),
    ("7", "Data for a library", "A mod's data for a library mod, in `kit/`."),
    ("8", "Preparing, loading and starting", "What the game finds when it prepares, loads and starts the mods."),
]
# Phrase groups by the first part of their key, in the order a modder meets them.
PHRASES = [
    ("Zip", "The ZIP archive"), ("Unpack", "Unpacking"), ("Limit", "Limits"), ("Name", "Entry names"),
    ("Clash", "Clashing names"), ("Kind", "Entry kinds"), ("Json", "JSON"), ("Expect", "Expected values"),
    ("Found", "Found values"), ("Game", "The game"), ("Media", "Media"), ("Option", "Options"),
    ("Perm", "Permissions"), ("TakesOver", "takes_over"), ("Save", "Saves"), ("Need", "Needs"),
    ("Conflict", "Conflicts"), ("Layer", "Layers"), ("Patch", "Patches"), ("Where", "Patch rows"),
    ("Select", "Selectors"), ("Column", "Columns"), ("Type", "Value types"), ("Code", "Code"), ("Lib", "Libraries"),
    ("Hook", "Hooks"), ("LeftOut", "Names the game's self-description leaves out"), ("Kit", "Library data"),
    ("Missing", "Rules not checked"), ("Open", "Opening a file"), ("Cook", "Preparing files"),
]


def texts():
    out = {}
    for line in (ROOT / "localization" / "NebulaText.tsv").read_text(encoding="utf-8").splitlines():
        cells = line.split("\t")
        if cells[0].startswith("Validator.") and len(cells) > 1:
            out[cells[0][len("Validator."):]] = cells[1]
    return out


def notes():
    lib = (ROOT / "crates" / "luma-validate" / "src" / "lib.rs").read_text(encoding="utf-8")
    m = re.search(r"pub fn level_of\(code: u16\) -> Level \{\s*match code \{\s*([0-9 |]+)=> Level::Note", lib)
    if not m:
        sys.exit("validator_rules.py: level_of not found in crates/luma-validate/src/lib.rs")
    return {int(n) for n in m.group(1).split("|")}


def cell(text):
    return text.replace("|", "\\|")


def page():
    words, note = texts(), notes()
    codes = sorted((k for k in words if re.fullmatch(r"L\d{3}", k)), key=lambda k: int(k[1:]))
    out = [
        "---",
        "title: Validator rules",
        "shortTitle: Validator rules",
        "intro: Every code the validator reports, with its level and its words, and every reason a finding can give.",
        "contentType: reference",
        "---",
        "",
        "<!-- Written by tools/modding/validator_rules.py from localization/NebulaText.tsv. Do not edit by hand. -->",
        "",
        "## About validator rules",
        "",
        "The validator checks a mod in LumaLink (`lumalink check`, `build` and `pack`) and again in the game, when it",
        "imports the mod and at each start. Both report the same codes with the same words, in the player's language.",
        "",
        "* An **Error** turns the mod off, or stops LumaLink.",
        "* A **Note** only tells you something. The mod stays on.",
        "",
        "In the words, `{mod}`, `{file}`, `{entry}` and the other names in braces are filled with the finding's own",
        "values. A `{why}`, `{what}` or `{kind}` is itself one of the [Reasons](#reasons) below.",
        "",
        "To print a code's words and what to do, run `lumalink explain CODE`, for example `lumalink explain L505`.",
        "",
    ]
    for digit, title, about in GROUPS:
        rows = [c for c in codes if c[1] == digit]
        if not rows:
            continue
        out += ["## %s (L%s00)" % (title, digit), "", about, "", "| Code | Level | Words |", "|---|---|---|"]
        for c in rows:
            out.append("| %s | %s | %s |" % (c, "Note" if int(c[1:]) in note else "Error", cell(words[c])))
        out.append("")
    out += ["## Reasons", "",
            "A finding's `{why}`, `{what}` or `{kind}` is one of these phrases, listed by its key.", ""]
    phrases = {k: v for k, v in words.items() if not re.fullmatch(r"L\d{3}", k)}
    used = set()
    for prefix, title in PHRASES:
        rows = sorted(k for k in phrases if k.split(".")[0] == prefix)
        if not rows:
            continue
        out += ["### %s" % title, "", "| Key | Words |", "|---|---|"]
        for k in rows:
            out.append("| `%s` | %s |" % (k, cell(phrases[k]) or "None"))
            used.add(k)
        out.append("")
    rest = sorted(set(phrases) - used)
    if rest:
        out += ["### Other reasons", "", "| Key | Words |", "|---|---|"]
        out += ["| `%s` | %s |" % (k, cell(phrases[k]) or "None") for k in rest]
        out.append("")
    out += ["## Further reading", "", "* [luma.json](luma-json.md)", "* [The .luma file format](luma-file-format.md)",
            "* [lumalink check](lumalink-commands/check.md)", ""]
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "docs" / "modding" / "reference" / "validator-rules.md"))
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    text = page()
    path = pathlib.Path(args.out)
    if args.check:
        have = path.read_text(encoding="utf-8") if path.is_file() else ""
        if have != text:
            print("validator_rules.py: %s is out of date; run tools/modding/validator_rules.py" % path)
            return 1
        return 0
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    print("validator_rules.py: wrote %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
