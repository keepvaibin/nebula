"""rules_doc_test.py: what docs/plan/04-GATE-AND-MAIN.md names inside backticks, the tools have (brief T10 part D).

    python -X utf8 -I tools/gate/tests/rules_doc_test.py [--work FOLDER]

Every command and every option the document names inside backticks is offered by that tool's own parser: gate.py (its levels and
options, `land` and its options, `data` and `speed-base` and their commands), plan.py (its commands, `repair` and its commands, the options of each),
measure.py (its commands and their options). The parsers are read from the tools themselves (gate.parser, plan.parser, and the parser
that measure.main and gamedata.main build, taken as they parse), so a name the tool lost fails here. No game, no build, no network.

How a span is read:
  - a span that holds `gate.py`, `plan.py` or `measure.py` (with or without a folder before it, and `python -X utf8 -I` before that):
    the first word after it that is not an option or an option's value is the command (a lower-case word: `land`, `next`, `keep`); it
    must be one the tool has. Each `--option` in the span must be one the tool offers for that command (or, for a span with no command,
    for the tool or any of its commands);
  - a span that names another program (a file ending .py, .ps1 or .sh that is none of the three, or `git`, `gh`, `cargo`) is that
    program's: its options are not read here;
  - any other span that has options (`--also`, `--main --move`) must have each of them offered by one of the three tools, or be
    in FOREIGN: options of the game and of the runner, each with the file that has it (a table that is itself checked).
Not read: text that is not inside backticks.

  the_document_names_only_what_the_tools_have   the real document
  the_document_has_no_bad_reference             tools/verify/doc_refs.py passes on it (a path outside the tree is written whole)
  an_unknown_command_is_found                   `gate.py approve_pictures`, `plan.py frobnicate`, `measure.py keep` taken out
  an_unknown_option_is_found                    in a span with a tool, in a bare span, one of another command
  what_the_tools_have_is_not_flagged            options with values, `data adopt`, a placeholder, a sentence with the tool in it
  foreign_options_are_checked                   each entry of FOREIGN is in its file
"""
import argparse
import contextlib
import io
import pathlib
import re
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(TREE / "tools" / "plan"))
sys.path.insert(0, str(TREE / "tools" / "measure"))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import doc_refs  # noqa: E402
import gamedata  # noqa: E402
import gate  # noqa: E402
import long_results  # noqa: E402
import measure  # noqa: E402
import plan  # noqa: E402
import speedbase  # noqa: E402

DOC = TREE / "docs" / "plan" / "04-GATE-AND-MAIN.md"
TOOLS = ("gate", "plan", "measure")
# Options of the game and of the runner that the document names with no tool in the span: the file that has each.
FOREIGN = {
    "--no-focus": "runtime/src",
    "--seconds": "runtime/src",
    "--gpu-validation": "tools/lumacore/luma_test.py",
    "--limit-scale": "tools/lumacore/luma_test.py",
    "--limit": "tools/lumacore/luma_test_android.py",
    "--no-daemon": "platform/android/build_apk.ps1",
}
OTHER_PROGRAMS = re.compile(r"(?:^|[\s/])[\w.-]+\.(?:py|ps1|sh)\b|^(?:git|gh|cargo)\s")
SPAN = re.compile(r"`([^`\n]+)`")
OPTION = re.compile(r"(?<![\w-])(--[a-z][a-z0-9-]*)")
TOOL_WORD = re.compile(r"^(?:[\w.-]+/)*(gate|landing|plan|measure)\.py$")
COMMAND_WORD = re.compile(r"^[a-z][a-z0-9_-]*$")


class Captured(Exception):
    pass


def capture(main, argv):
    """The parser a main() builds, taken at the moment it parses (nothing else of main() runs)."""
    saved = argparse.ArgumentParser.parse_args

    def grab(self, args=None, namespace=None):
        raise Captured(self)
    argparse.ArgumentParser.parse_args = grab
    try:
        main(argv)
    except Captured as taken:
        return taken.args[0]
    finally:
        argparse.ArgumentParser.parse_args = saved
    raise AssertionError("main(%r) built no parser" % (argv,))


class Node:
    """One parser: its options (option string -> whether it takes a value) and its commands (name -> Node)."""

    def __init__(self, parser):
        self.options = {}
        self.commands = {}
        self.choices = []                 # a positional with fixed choices (gamedata.py's command)
        for action in parser._actions:
            if isinstance(action, argparse._SubParsersAction):
                self.commands = {name: Node(sub) for name, sub in action.choices.items()}
            elif action.option_strings:
                for opt in action.option_strings:
                    self.options[opt] = action.nargs != 0
            elif action.choices:
                self.choices = list(action.choices)

    def every_option(self):
        out = set(self.options)
        for node in self.commands.values():
            out |= node.every_option()
        return out


def build_tools():
    quiet = io.StringIO()
    with contextlib.redirect_stderr(quiet):
        top = Node(gate.parser())
        top.commands = {"land": Node(gate.parser(land=True)), "data": Node(capture(gamedata.main, ["list"])),
                        "speed-base": Node(capture(speedbase.main, ["show"])), "long-results": Node(long_results.build_parser()),
                        "long-start": Node(gate.long_start_parser())}
        return {"gate": top, "plan": Node(plan.parser()), "measure": Node(capture(measure.main, ["keep", "x"]))}


def words_of(span):
    return span.replace("[", " ").replace("]", " ").split()


def read_span(span, tools, foreign=FOREIGN):
    """[problem] of one span: a command or an option the tools do not offer."""
    words = words_of(span)
    at = next((i for i, w in enumerate(words) if TOOL_WORD.match(w)), None)
    if at is None:
        if OTHER_PROGRAMS.search(span):
            return []
        every = set().union(*(node.every_option() for node in tools.values()))
        return ["option %s: none of gate.py, plan.py, measure.py has it" % o for o in OPTION.findall(span)
                if o not in every and o not in foreign]
    name = TOOL_WORD.match(words[at]).group(1)
    if name not in tools:
        return ["%s.py: it has no command line (it is a module)" % name] if len(words) > at + 1 and words[at + 1].startswith("-") else []
    node, problems, chain = tools[name], [], [name + ".py"]
    rest = words[at + 1:]
    seen = []                               # (option, the parser it stood at)
    commands = True                         # still reading commands (until the first word that is none)
    i = 0
    while i < len(rest):
        word = rest[i]
        found = OPTION.match(word)
        if found:
            option = found.group(1)
            seen.append((option, node))
            if node.options.get(option):
                i += 1                      # its value
            i += 1
            continue
        if commands and (node.commands or node.choices) and COMMAND_WORD.match(word):
            if word in node.commands:
                node = node.commands[word]
                chain.append(word)
            elif word in node.choices:
                chain.append(word)
            else:
                problems.append("command %s: %s has none of that name" % (word, " ".join(chain)))
                commands = False
        else:
            commands = False                # a placeholder or an argument: no command follows it
        i += 1
    for option, at_node in seen:
        if option in at_node.options:
            continue
        if at_node is tools[name] and len(chain) == 1 and option in at_node.every_option():
            continue                        # a span with no command: any command of the tool has it
        problems.append("option %s: %s does not offer it" % (option, " ".join(chain)))
    return problems


def read_document(text, tools, foreign=FOREIGN):
    """['line N: `span`: problem'] for every span of a document."""
    out = []
    for number, line in enumerate(text.splitlines(), 1):
        for span in SPAN.findall(line):
            for problem in read_span(span, tools, foreign):
                out.append("line %d: `%s`: %s" % (number, span, problem))
    return out


TOOL_TABLE = {}


def tools():
    if not TOOL_TABLE:
        TOOL_TABLE.update(build_tools())
    return TOOL_TABLE


class Rules(unittest.TestCase):
    def test_the_document_names_only_what_the_tools_have(self):
        found = read_document(DOC.read_text(encoding="utf-8"), tools())
        self.assertEqual(found, [], "\n" + "\n".join(found))

    def test_the_document_has_no_bad_reference(self):
        bad = doc_refs.check_files([DOC], TREE, doc_refs.read_allow(doc_refs.DEFAULT_ALLOW))
        self.assertEqual(bad, [], "\n" + "\n".join(bad))

    def test_an_unknown_command_is_found(self):
        for text, word in (("Run `gate.py approve_pictures` now.", "approve_pictures"), ("`plan.py frobnicate ID`", "frobnicate"),
                           ("`python -X utf8 -I tools/measure/measure.py purge`", "purge"), ("`gate.py data wipe`", "wipe")):
            found = read_document(text, tools())
            self.assertEqual(len(found), 1, (text, found))
            self.assertIn("command %s" % word, found[0])

    def test_an_unknown_option_is_found(self):
        cases = (("`gate.py land --nonesuch`", "option --nonesuch"), ("`plan.py next --github-again`", "option --github-again"),
                 ("`measure.py keep FILE --runs x`", "option --runs"), ("The option `--nonesuch` is a thing.", "option --nonesuch"),
                 ("`gate.py --main --full`", "option --full"), ("`--main --nonesuch`", "option --nonesuch"))
        for text, want in cases:
            found = read_document(text, tools())
            self.assertEqual(len(found), 1, (text, found))
            self.assertIn(want, found[0])

    def test_what_the_tools_have_is_not_flagged(self):
        fine = ("`gate.py --main --scope validate --serial`", "`gate.py land BRANCH --also NAMES`", "`gate.py land --runner-lock`",
                "`python -X utf8 -I tools/gate/gate.py data adopt`", "`plan.py start <step>`", "`plan.py owed ID \"text\"`",
                "`plan.py land --github-again`", "`measure.py run --bin BIN --work WORK --commit SHA [--runs ...] [--pending FILE]`",
                "`measure.py keep FILE [--history H]`", "`measure.py digest RUN_FOLDER`", "`--landing-runner <its process id>`",
                "`--also`", "`--tablet R52WA0FXE5J`", "`--no-focus 1`", "`luma_test.py run --gpu-validation 1`",
                "`pictures.py approve --new --runs <the folder>`", "`gh run view <run id> --log-failed --job <job id>`",
                "LANDING RUNNING: X since HH:MM. Run `plan.py land X` to wait for its end.", "`gate.py --mods short|full`",
                "`gate.py --long --only \"stress/*\"`", "`plan.py repair add --source S --kind K`", "`tools/measure/measure.py`", "`gate.py`")
        for text in fine:
            self.assertEqual(read_document(text, tools()), [], text)

    def test_a_tool_that_is_a_module_has_no_options(self):
        self.assertEqual(len(read_document("`landing.py --queue`", tools())), 1)
        self.assertEqual(read_document("the file `tools/gate/landing.py`", tools()), [])

    def test_foreign_options_are_checked(self):
        for option, where in FOREIGN.items():
            path = TREE / where
            files = sorted(path.rglob("*")) if path.is_dir() else [path]
            word = option[2:]
            self.assertTrue(any(f.is_file() and word in f.read_text(encoding="utf-8", errors="replace") for f in files),
                            "%s is not in %s" % (option, where))
        self.assertEqual(len(read_document("`--seconds`", tools(), {})), 1, "not in the table: not offered")


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("rules_doc_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
