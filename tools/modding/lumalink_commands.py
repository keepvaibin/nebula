"""Writes docs/modding/reference/lumalink-commands/: one page for each LumaLink command, from that LumaLink's own help,
and the index of them.

  python -X utf8 -I tools/modding/lumalink_commands.py LUMALINK [--out FOLDER] [--check]

LUMALINK is the lumalink executable whose help the pages show. --check exits 1 when a page differs from what it would
write.
"""
import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SKIP = {"help"}
# For each command: an example (shell lines) and what to read next (title, path from the page).
EXAMPLES = {
    "new": ['lumalink new moon-jump --id moon.jump --name "Moon jump" --summary "Hold jump to rise." --author me'],
    "check": ["lumalink check moon-jump"],
    "pack": ["lumalink pack moon-jump"],
    "list": ["lumalink list moon-jump.luma", "lumalink list tables --game game.pak Kuribo", "lumalink list sounds JUMP"],
    "extract": ["lumalink extract moon-jump.luma --output moon-jump-files", "lumalink extract LayoutData/CoinCounter.arc --output game-files"],
    "explain": ["lumalink explain L505"],
    "verify": ["lumalink verify moon-jump.luma --source moon-jump"],
    "build": ["lumalink build moon-jump"],
    "run": ["lumalink run moon-jump"],
    "test": ["lumalink test moon-jump"],
    "format": ["lumalink format moon-jump/source"],
    "add": ["lumalink add need lumakit moon-jump",
            "lumalink add option balls moon-jump --kind number --default 3 --name Balls",
            "lumalink add file title.arc moon-jump --at LayoutData/Title.arc"],
    "convert": ["lumalink convert MyRiivolutionMod moon-jump"],
    "lsp": ["lumalink lsp"],
    "services": ["lumalink services"],
    "update": ["lumalink update"],
}
# Sections a command's help cannot hold, after its arguments.
SECTIONS = {
    "test": [
        "## The test kit", "",
        "The installed game has no test parts. `lumalink test` uses the test kit that LumaLink Setup installs in the "
        "`test` folder beside the `lumalink` program. The folder holds a `kit.json` file. To use a kit in another "
        "folder, set `LUMA_TEST_KIT` to it.", "",
        "From the kit, `lumalink test` takes:", "",
        "* Nebula's test runner, `tools/lumacore/luma_test.py`, run with Python. Give another runner with `--runner` "
        "or `LUMA_TEST_RUNNER`, and another Python with `--python` or `LUMALINK_PYTHON`.",
        "* The test extension that the game loads for each run.",
        "* The source of the test's game half, `luma_test.c`. LumaLink builds it against the installed game's SDK "
        "before the tests run.", "",
        "The kit is built for Windows x64 and Linux x64. On other systems, including Windows on ARM, `lumalink test` "
        "stops with this message:", "",
        "```text",
        "this LumaLink has no test kit for this system: lumalink test runs on Windows x64 and Linux x64. Reinstall "
        "LumaLink with its Setup, or set LUMA_TEST_KIT",
        "```", "",
        "When `--bin` names a game build's folder that has its own test extension, `lumalink test` uses that build's "
        "parts and no kit.", "",
    ],
}

READ = {
    "new": [("Quickstart for Nebula mods", "../../get-started/quickstart.md"), ("luma.json", "../luma-json.md")],
    "check": [("Validator rules", "../validator-rules.md")],
    "pack": [("Packing and sharing a mod", "../../how-tos/packing-and-sharing-a-mod.md")],
    "build": [("About LumaScript", "../../concepts/lumascript.md")],
    "test": [("Testing a mod", "../../how-tos/testing-a-mod.md"), ("Test files", "../test-files.md")],
    "add": [("luma.json", "../luma-json.md")],
    "convert": [("Converting existing mods", "../../how-tos/converting-existing-mods.md")],
    "lsp": [("Setting up an editor", "../../how-tos/setting-up-an-editor.md")],
    "explain": [("Validator rules", "../validator-rules.md")],
}


def help_of(exe, *args):
    out = subprocess.run([exe, *args, "--help"], capture_output=True, text=True, encoding="utf-8")
    if out.returncode != 0:
        sys.exit("lumalink_commands.py: %s %s --help failed: %s" % (exe, " ".join(args), out.stderr))
    return out.stdout.replace("\r\n", "\n")


def commands(exe):
    text = help_of(exe)
    block = text.split("Commands:\n", 1)[1].split("\n\n", 1)[0]
    out = []
    for line in block.splitlines():
        m = re.match(r"  (\S+)\s+(.*)", line)
        if m and m.group(1) not in SKIP:
            out.append((m.group(1), m.group(2)))
    return out


def sections(text):
    """The help's description, usage line, arguments and options (each a list of (name, words))."""
    about, rest = text.split("\nUsage: ", 1)
    usage, rest = rest.split("\n", 1)
    parts = {"Arguments": [], "Options": []}
    current = None
    for line in rest.splitlines():
        if line.rstrip(":") in parts and not line.startswith(" "):
            current = line.rstrip(":")
            continue
        if current is None or not line.strip():
            continue
        m = re.match(r"\s+(-\S.*?|<\S+>)\s{2,}(.*)", line)
        if m:
            parts[current].append([m.group(1).strip(), m.group(2).strip()])
        elif parts[current]:
            parts[current][-1][1] += " " + line.strip()
    return about.strip(), usage.strip(), parts


def cell(text):
    return text.replace("|", "\\|")


def page(name, summary, text):
    about, usage, parts = sections(text)
    out = ["---", "title: lumalink %s" % name, "shortTitle: %s" % name, "intro: %s" % summary.replace(":", " -"),
           "contentType: reference", "---", "",
           "<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->", "",
           "## About lumalink %s" % name, "", about, ""]
    if name in EXAMPLES:
        out += ["## Example", "", "```shell"] + EXAMPLES[name] + ["```", ""]
    out += ["## Usage", "", "```text", usage, "```", ""]
    if parts["Arguments"]:
        out += ["## Arguments", "", "| Argument | Description |", "|---|---|"]
        out += ["| `%s` | %s |" % (a, cell(w)) for a, w in parts["Arguments"]]
        out.append("")
    out += SECTIONS.get(name, [])
    if parts["Options"]:
        out += ["## Options", "", "| Option | Description |", "|---|---|"]
        out += ["| `%s` | %s |" % (o, cell(w)) for o, w in parts["Options"]]
        out.append("")
    out += ["Every message and help text is available in each of the game's 14 languages, chosen with `--language`."]
    if name in ("check", "add", "pack", "explain"):
        out += ["Findings carry the validator's codes, the same ones the game shows."]
    out.append("")
    if name in READ:
        out += ["## Further reading", ""] + ["* [%s](%s)" % r for r in READ[name]] + [""]
    return "\n".join(out)


def index(cmds):
    out = ["---", "title: LumaLink commands", "shortTitle: LumaLink commands",
           "intro: Every command of the lumalink command-line tool, with its arguments and options.",
           "contentType: reference", "children:"]
    out += ["  - /%s" % n for n, _ in cmds]
    out += ["---", "", "<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->",
            "", "## About LumaLink commands", "",
            "Run `lumalink COMMAND --help` to print any command's help, or `lumalink --help` for the list.", "",
            "| Command | Description |", "|---|---|"]
    out += ["| [`%s`](%s.md) | %s |" % (n, n, cell(s)) for n, s in cmds]
    out += ["", "## Options of every command", "", "| Option | Description |", "|---|---|",
            "| `--language LANGUAGE` | The language LumaLink and the validator use: one of the game's 14, by its name in the "
            "game (`UsEnglish`, `EuGerman`, `JpJapanese` and so on). The default is `UsEnglish`. |",
            "| `-h`, `--help` | Prints the help. |", "| `-V`, `--version` | Prints LumaLink's version. |", ""]
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("lumalink")
    ap.add_argument("--out", default=str(ROOT / "docs" / "modding" / "reference" / "lumalink-commands"))
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    cmds = commands(args.lumalink)
    pages = {"index.md": index(cmds)}
    for name, summary in cmds:
        pages[name + ".md"] = page(name, summary, help_of(args.lumalink, name))
    out = pathlib.Path(args.out)
    stale = []
    for file, text in pages.items():
        path = out / file
        if args.check:
            if not path.is_file() or path.read_text(encoding="utf-8") != text:
                stale.append(file)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
    if args.check and stale:
        print("lumalink_commands.py: out of date: %s" % ", ".join(stale))
        return 1
    print("lumalink_commands.py: %d pages %s %s" % (len(pages), "checked in" if args.check else "written to", out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
