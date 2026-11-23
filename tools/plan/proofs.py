"""proofs.py: what a card's proof names, and whether the agent can use it (AH, after the trial).

A step card's "Proof" lists what is to be shown. The trial showed two kinds of proof the agent could not make as written: one named a
program the agent may not run (S0.2.6, proof 6: `tools/verify/size_probe.py`, which starts the game outside the agent's commands), and one a
profile that cannot be run. This reads a card's proofs and says, for each program, script or test the proof names, whether it exists on main,
whether AGENT.md forbids running it (the list under "Programs you never run"), and whether each option written beside a program is one that
program takes, where that can be read from its source. A self-test (tools/gate/tests/plan_cards_test.py) runs it over the cards of steps 12 to
138. It changes no card. Python standard library only.

What is read as a name:
  a program or script  a backtick span (or a bare word) ending .py, .ps1 or .sh, with the options and the sub-command written after it in the span;
  a test               a span that ends in `_tests`, `_tests.cpp`, `_tests.rs` or `_test.py`, or a snake_case or hyphened name in a span after
                       the word `test` or `tests` in the same sentence;
  a game option        `--diagnostics NAME` (NAME is one of launch_args.h's kDiagnosticNames) and any other `--option` written with no program:
                       it must be a string of native/host/host_main.cpp.
"""
import pathlib
import re
import subprocess

GIT = "C:/Program Files/Git/cmd/git.exe"
PROOF = re.compile(r"^- \*\*Proof\.\*\*(.*?)(?=^- \*\*|\Z)", re.S | re.M)
SPAN = re.compile(r"`([^`\n]+)`")
SCRIPT = re.compile(r"(?<![\w/.-])((?:[\w.-]+/)*[\w.-]+\.(?:py|ps1|sh))\b")
AFTER_TEST_PROGRAM = re.compile(r"`[\w.-]*_tests?(?:\.\w+)?`((?:[ ]*`[^`\n]+`)+)")
TEST_WORD = re.compile(r"\btests?\b")
SENTENCE_END = re.compile(r"[;.](?:\s|$)|\n")
IDENTIFIER = re.compile(r"^[a-z][a-z0-9]*(?:[_-][a-z0-9]+)+$")
TEST_SPAN = re.compile(r"^[\w.-]*_tests?(?:\.(?:cpp|rs|py))?$")
OPTION = re.compile(r"(?<![\w-])--[a-z][a-z0-9-]*")
FORBIDDEN_HEADING = "**Programs you never run.**"
# A name a card or a document mentions is not a test: the search for a test's name leaves out the plan's documents, the table files, this reader
# and its self-test.
NOT_SOURCE = (":(exclude)docs", ":(exclude)*.md", ":(exclude)*.tsv", ":(exclude)tools/plan/proofs.py", ":(exclude)tools/gate/tests/plan_cards_test.py")
HOST = "native/host/host_main.cpp"
LAUNCH_ARGS = "runtime/include/galaxy/launch_args.h"


def proof_of(card):
    """The text of a card's `Proof.` item (all its numbered lines), or ""."""
    m = PROOF.search(card or "")
    return m.group(1) if m else ""


def forbidden_programs(agent_md):
    """[path] of the programs AGENT.md says the agent never runs: the backtick paths of the list under `Programs you never run`, up to the next
    paragraph that starts with a bold word."""
    if FORBIDDEN_HEADING not in agent_md:
        return []
    part = agent_md.split(FORBIDDEN_HEADING, 1)[1]
    part = re.split(r"\n\*\*", part, 1)[0]
    out = []
    for line in part.splitlines():
        if line.lstrip().startswith("- "):
            out += [s for s in SPAN.findall(line) if "/" in s]
    return out


class Named:
    """What one proof names: `scripts` [(word, [options], subcommand)], `tests` [name], `options` [(option, value)] (game options with no program)."""

    def __init__(self):
        self.scripts, self.tests, self.options = [], [], []


def read(proof):
    """Everything the proof text names (see the module's text)."""
    out = Named()
    seen = set()
    for span in SPAN.findall(proof):
        words = span.split()
        found = SCRIPT.search(span)
        if found:
            rest = span[found.end():]
            opts = OPTION.findall(rest)
            sub = next((w for w in rest.split() if re.match(r"^[a-z][a-z-]*$", w)), "")
            key = (found.group(1), tuple(opts), sub)
            if key not in seen:
                seen.add(key)
                out.scripts.append((found.group(1), opts, sub))
            continue
        if OPTION.match(span):
            m = re.match(r"^(--[a-z][a-z0-9-]*)(?:\s+([\w-]+))?", span)
            if m and (m.group(1), m.group(2) or "") not in out.options:
                out.options.append((m.group(1), m.group(2) or ""))
            continue
        if len(words) == 1 and TEST_SPAN.match(span) and span not in out.tests:
            out.tests.append(span)
    for m in AFTER_TEST_PROGRAM.finditer(proof):                  # `post_process_tests` `view_cache_on_warp`: a test of that program
        for span in SPAN.findall(m.group(1)):
            if IDENTIFIER.match(span) and span not in out.tests:
                out.tests.append(span)
    for m in TEST_WORD.finditer(proof):
        rest = proof[m.end():m.end() + 240]
        end = SENTENCE_END.search(rest)
        for span in SPAN.findall(rest[:end.start()] if end else rest):
            if IDENTIFIER.match(span) and span not in out.tests:
                out.tests.append(span)
    for word in SCRIPT.findall(SPAN.sub(" ", proof)):          # a script written without backticks
        if not any(s[0] == word for s in out.scripts):
            out.scripts.append((word, [], ""))
    return out


class Repo:
    """The tracked files of a tree, and searches in them (one `git ls-files` and `git grep -w` for each name, kept)."""

    def __init__(self, tree):
        self.tree = pathlib.Path(tree)
        r = subprocess.run([GIT, "-C", str(self.tree), "ls-files"], capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.files = [l for l in r.stdout.splitlines() if l]
        self.by_name = {}
        for f in self.files:
            self.by_name.setdefault(f.rsplit("/", 1)[-1], []).append(f)
        self.cache = {}

    def source(self, path):
        return (self.tree / path).read_text(encoding="utf-8", errors="replace")

    def resolve(self, word):
        """The tracked file a program word stands for: the path as written, else the one file of that name (None when none, or more than one
        and no path is written)."""
        word = word.lstrip("./")
        if word in self.files:
            return word
        hits = self.by_name.get(word.rsplit("/", 1)[-1], [])
        if "/" in word:
            hits = [h for h in hits if h.endswith("/" + word)]
        return hits[0] if len(hits) == 1 else None

    def mentions(self, name):
        """Whether a tracked file holds `name` as a whole word (a test's name inside its program)."""
        if name not in self.cache:
            r = subprocess.run([GIT, "-C", str(self.tree), "grep", "-l", "-w", "-F", "-q", name, "--", ".", *NOT_SOURCE], capture_output=True, text=True)
            self.cache[name] = r.returncode == 0
        return self.cache[name]

    def host_options(self):
        text = self.source(HOST) if (self.tree / HOST).is_file() else ""
        return set(re.findall(r'"(--[a-z][a-z0-9-]*)"', text))

    def diagnostic_names(self):
        text = self.source(LAUNCH_ARGS) if (self.tree / LAUNCH_ARGS).is_file() else ""
        return set(re.findall(r'\{"([a-z0-9-]+)",\s*k\w+', text))


def problems(named, repo, forbidden):
    """[text] of what a proof names that the agent cannot use: a program that is not on main, a program on the forbidden list, an option the
    program does not take, a sub-command it does not have, a test that is not on main, a game option the game does not take."""
    out = []
    for word, opts, sub in named.scripts:
        path = repo.resolve(word)
        if path is None:
            out.append("%s is not a file on main (or its name is not one file's)" % word)
            continue
        if path in forbidden:
            out.append("%s is a program the agent never runs (AGENT.md, Programs you never run)" % path)
        src = repo.source(path)
        for o in opts:
            if '"%s"' % o not in src and "'%s'" % o not in src and not re.search(r"(?<![\w-])%s(?![\w-])" % re.escape(o), src):
                out.append("%s takes no option %s" % (path, o))
        if sub and '"%s"' % sub not in src and "'%s'" % sub not in src:
            out.append("%s has no sub-command %s" % (path, sub))
    for name in named.tests:
        base = re.sub(r"\.(cpp|rs|py)$", "", name)
        if name in repo.by_name or any(f.rsplit("/", 1)[-1].rsplit(".", 1)[0] == base for f in repo.files if base in f) or repo.mentions(base):
            continue
        out.append("test %s is not on main" % name)
    game, names = repo.host_options(), repo.diagnostic_names()
    for opt, value in named.options:
        if opt not in game:
            out.append("the game takes no option %s" % opt)
        elif opt == "--diagnostics" and value and value not in names:
            out.append("--diagnostics has no name %s" % value)
    return out


# The extra checks (docs/plan/native/step-checks.tsv, plan.py ALSO_CHECKS) and the words of a proof that name each. A proof that names one needs
# the step's line for it: the landing then runs it, and the proof is shown by a check, not by a sentence.
CHECK_WORDS = (
    ("stress", re.compile(r"stress lane|`stress`|under load|slowed machine|a slowed", re.I)),
    ("sanitizer/tour", re.compile(r"sanitizer", re.I)),
    ("soak", re.compile(r"`soak`", re.I)),
    ("soak/long", re.compile(r"`soak/long`", re.I)),
    ("replays/software", re.compile(r"software (?:graphics )?device", re.I)),
    ("replays/rates", re.compile(r"time rate", re.I)),
    ("speed/anchor", re.compile(r"`speed/anchor`", re.I)),   # the package's last step asks for it by rule (plan_names_test), not by a phrase
)


def checks_named(proof):
    """[check] of the extra checks a proof names (see CHECK_WORDS), in the table's order."""
    return [name for name, words in CHECK_WORDS if words.search(proof or "")]
