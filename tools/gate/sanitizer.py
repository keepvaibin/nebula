"""sanitizer.py: the sanitizer tour's judgement of a run (package B5, ledger rows N-1062, N-1088), without the game.

`sanitizer/tour` (gate.py sanitizer_part) plays the tour on the game modules built with the sanitizers (build_windows.ps1
-Sanitize): the build has no recovery (-fno-sanitize-recover=all), so the first report of a stop ends the game, and the runner
sees a crash. A report is a finding about the game, never about the test. What the sanitizers print (the game's run.log):
  AddressSanitizer   ==<pid>==ERROR: AddressSanitizer: <kind> on address ...        then  #<n> 0x<pc> in <function> <file>:<line>
  UndefinedBehavior  <file>:<line>:<column>: runtime error: <what>                  then, with print_stacktrace=1, the same frames

A report is named by the first frame of the game's code in it, the function (`nebula_game_run`; a frame the symbolizer gives no name
is `smg_game.dll+0x1826f4d54`, which an edit of the game moves: such a report is listed again, or is a fail). A frame is the game's when
it is in the game module (smg_game.dll, smg_game_plain.dll) or in a source file of the tree's native folder. The line of a source is
not part of the name: an edit above it moves it. tools/gate/sanitizer-known.tsv (frame, ledger row, the report's first words) lists
the reports main has today: a listed one is counted and shown, any other is a fail with the report's first line as its message. A stop
that ended by a listed report counts as passed in this check: it ends there, and what it would have played after is not judged.
"""
import pathlib
import re

HERE = pathlib.Path(__file__).resolve().parent
KNOWN = HERE / "sanitizer-known.tsv"
KNOWN_COLUMNS = ("frame", "ledger row", "first words")
# The sanitizers' options of the games this check starts: UndefinedBehavior prints its stack, as AddressSanitizer does.
ENV = {"UBSAN_OPTIONS": "print_stacktrace=1"}

TIME = re.compile(r"^\[\s*\d+\.\d{3}\]\s*")
ADDRESS = re.compile(r"^==\d+==ERROR: (\w*Sanitizer): (.*)$")
UNDEFINED = re.compile(r"^(\S.*?):(\d+):(\d+): runtime error: (.*)$")
# A frame, as the sanitizer's runtime writes it on Windows: `#1 0x7ff7 in name+0x8c (C:\dir\module.dll+0x1826f4d54)`, the same with no
# name (`#1 0x7ff7  (C:\dir\module.exe+0x14)`), or with the source it was found in (`#1 0x7ff7 in name C:\dir\file.cpp:12:3`).
FRAME_MODULE = re.compile(r"^\s*#\d+ 0x[0-9a-fA-F]+(?: in (.+?))?(?:\+0x[0-9a-fA-F]+)?\s+\(([^()]*?)\+(0x[0-9a-fA-F]+)\)\s*$")
FRAME_SOURCE = re.compile(r"^\s*#\d+ 0x[0-9a-fA-F]+ in (.+?) ([A-Za-z]:[^:]*|/[^:]*):\d+(?::\d+)?\s*$")
# The game's code: the game modules, and the sources of the tree's native folder (the game, with the Petari copy of its sources),
# whichever drive and work folder the build is in. The host's own code is not the game's.
GAME_MODULES = ("smg_game.dll", "smg_game_plain.dll")
GAME = re.compile(r"[/\\]native[/\\]")


class KnownError(Exception):
    """sanitizer-known.tsv cannot be read: each problem is one line."""

    def __init__(self, problems):
        Exception.__init__(self, "\n".join(problems))
        self.problems = problems


def read_known(path=None):
    """{frame: (ledger row, first words)} of the known list. A comment starts with '#'; the first other line is the header
    `frame<TAB>ledger row<TAB>first words`; a row has the three cells and a ledger row that is an id of the ledger (P-12,
    N-1090) or the word `pending` (the planner writes the row)."""
    path = pathlib.Path(path or KNOWN)
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise KnownError(["%s: cannot be read: %s" % (path.name, error)])
    problems, known, header = [], {}, False
    for n, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        cells = [c.strip() for c in line.split("\t")]
        if not header:
            if cells != list(KNOWN_COLUMNS):
                problems.append("%s:%d: the first line is the header: %s" % (path.name, n, "<TAB>".join(KNOWN_COLUMNS)))
            header = True
            continue
        if len(cells) != 3 or not all(cells):
            problems.append("%s:%d: a row has three cells (%s), none empty" % (path.name, n, ", ".join(KNOWN_COLUMNS)))
        elif not re.fullmatch(r"[PN]-\d+|pending", cells[1]):
            problems.append("%s:%d: the ledger row is an id like P-12 or N-1090, or pending, not %r" % (path.name, n, cells[1]))
        elif cells[0] in known:
            problems.append("%s:%d: %s is listed twice" % (path.name, n, cells[0]))
        else:
            known[cells[0]] = (cells[1], cells[2])
    if not header and not problems:
        problems.append("%s: no header line" % path.name)
    if problems:
        raise KnownError(problems)
    return known


def short_function(text):
    """The function's name without its parameters: `Foo::bar(int, char*)` is `Foo::bar`; a template's brackets are kept whole."""
    text = text.strip()
    depth = 0
    for i, ch in enumerate(text):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth = max(0, depth - 1)
        elif ch == "(" and depth == 0:
            return text[:i].strip() or text
    return text


def file_name(path):
    return re.split(r"[/\\]", path)[-1]


def game_frame(line):
    """The name of a stack line that is a frame of the game's code (see the top), else None."""
    seen = FRAME_MODULE.match(line)
    if seen:
        function, module, offset = seen.group(1), file_name(seen.group(2)), seen.group(3)
        if module.lower() not in GAME_MODULES:
            return None
        return short_function(function) if function else "%s+%s" % (module, offset)
    seen = FRAME_SOURCE.match(line)
    if seen and GAME.search(seen.group(2).replace("\\", "/")):
        return short_function(seen.group(1))
    return None


class Report:
    def __init__(self, first, frame):
        self.first, self.frame = first, frame

    def line(self):
        """The report in one line, as a result's message: its first line and where in the game it is."""
        return "%s%s" % (self.first, " at " + self.frame if self.frame else " (no frame of the game's code)")


def read_reports(text):
    """[Report] of a run.log: each sanitizer report with its first line (without the process id and the time) and the first
    frame of the game's code in the lines after it, up to the next report. An undefined-behaviour report names the code it is
    in by the function of the first frame of its stack, else by its own file and line."""
    lines = [TIME.sub("", l) for l in text.splitlines()]
    reports = []
    for i, line in enumerate(lines):
        address, undefined = ADDRESS.match(line), UNDEFINED.match(line)
        if not address and not undefined:
            continue
        frame = None
        if address:
            first = "%s: %s" % (address.group(1), address.group(2))
        else:
            first = "runtime error: %s" % undefined.group(4)
            if GAME.search(undefined.group(1).replace("\\", "/")):
                frame = "%s:%s" % (file_name(undefined.group(1)), undefined.group(2))
        for follow in lines[i + 1:i + 100]:
            if ADDRESS.match(follow) or UNDEFINED.match(follow):
                break
            seen = game_frame(follow)
            if seen:
                frame = seen
                break
        reports.append(Report(first, frame))
    return reports


def judge_results(results, logs, known):
    """(results, final, totals) for a lane's {test: (kind, message)} and {test: log text}: a test whose log holds a report that is
    not in the known list is a fail with the first such report as its message; a test that did not pass and whose every report is
    listed passes (it ended by that report). Both are final (the same on every run: not run again alone). `totals` {frame: count} of
    the listed reports of every log."""
    out, final, totals = dict(results), set(), {}
    for test, (kind, message) in results.items():
        reports = read_reports(logs.get(test, ""))
        if not reports:
            continue
        unlisted = [r for r in reports if r.frame not in known]
        for r in reports:
            if r.frame in known:
                totals[r.frame] = totals.get(r.frame, 0) + 1
        if unlisted:
            out[test] = ("fail", "sanitizer report: %s" % unlisted[0].line())
            final.add(test)
        elif kind != "pass":
            out[test] = ("pass", "ended by the listed sanitizer report at %s (%s)" % (reports[0].frame, known[reports[0].frame][0]))
            final.add(test)
    return out, final, totals


def summary(totals, known):
    """The words for a check's detail: the listed reports counted with their ledger rows."""
    listed = ["%s x%d (%s)" % (f, c, known[f][0]) for f, c in sorted(totals.items())]
    return "listed sanitizer reports: " + ", ".join(listed) if listed else ""
