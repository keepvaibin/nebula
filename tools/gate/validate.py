"""validate.py: the validate lane's judgement of a run (package T12), without the game.

The lane plays the 12-stop tour (the nightly run: the whole tour) one copy at a time with the graphics API's debug layer on
(the runner's `--gpu-validation 1`: Direct3D 11's and Direct3D 12's debug layer, and Vulkan's Khronos validation layer where
the game finds one). A message of a layer is a mistake in how the game drives the card; it is the same on every run.

What the game prints for a layer message (runtime/src/render):
  Direct3D 12   [D3D12-LIVE-ERR] id <number>: <text> | at <addresses>      renderer_d3d12.cpp (errors and corruption only)
  Direct3D 11   [D3D11-LIVE-ERR] id=<number> <text>, and [D3D11-LIVE-WARN]  d3d11_context.cpp report_messages
  Vulkan        [VK-LIVE-ERR] id <VUID>: <text>, and [VK-LIVE-WARN]         vk_context.cpp debug_message
and at its start, whether the layer is really on (it is off when it is not installed):
  [gpu] d3d12 debug-layer=1 | [gpu] d3d11 debug-layer=1 | [vulkan] device="..." ... validation=1

An error has an id. tools/gate/validate-known.tsv (renderer, id, ledger row, the message's first words) lists the errors main
has today: a listed one is counted and reported, any other is a fail with the message as its first line. Warnings are counted
only. A run whose time limit passed while the game still moved is tool (the limit of this lane is too short), never fail.
"""
import pathlib
import re

HERE = pathlib.Path(__file__).resolve().parent
KNOWN = HERE / "validate-known.tsv"
KNOWN_COLUMNS = ("renderer", "id", "ledger row", "first words")
RENDERERS = ("d3d12", "vulkan", "d3d11")
# The runner's limits times this (--limit-scale): the layer makes the game slow.
LIMIT_SCALE = 4
# The words a layer's absence is told in (one plain line in the summary, one in tool-faults.tsv a day at most per renderer).
LAYER_NAMES = {"d3d12": "Direct3D debug layer", "d3d11": "Direct3D debug layer", "vulkan": "Vulkan validation layer"}
# The runner's messages for a run that reached its time limit while the game still stepped (luma_test.py evaluate).
LIMIT_MESSAGES = ("too-slow:", "not-reached:")

MESSAGE = re.compile(r"^(?:\[\s*\d+\.\d{3}\]\s*)?\[(D3D12|D3D11|VK)-LIVE-(ERR|WARN)\]\s+id[ =]([-\w.]+):?\s*(.*)$")
LAYER_LINES = {
    "d3d12": re.compile(r"\[gpu\] d3d12 debug-layer=(\d)"),
    "d3d11": re.compile(r"\[gpu\] d3d11 debug-layer=(\d)"),
    "vulkan": re.compile(r"^\[vulkan\] device=.* validation=(\d)", re.MULTILINE),
}
FAMILY = {"D3D12": "d3d12", "D3D11": "d3d11", "VK": "vulkan"}


class KnownError(Exception):
    """validate-known.tsv cannot be read: each problem is one line."""

    def __init__(self, problems):
        Exception.__init__(self, "\n".join(problems))
        self.problems = problems


def read_known(path=None):
    """{(renderer, id): (ledger row, first words)} of the known list. A comment starts with '#'; the first other line is the
    header `renderer<TAB>id<TAB>ledger row<TAB>first words`; a row has the four cells, a renderer of RENDERERS and a ledger row
    that is an id of the ledger (P-12, N-1090) or the word `pending` (the planner writes the row)."""
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
        if len(cells) != 4 or not all(cells):
            problems.append("%s:%d: a row has four cells (%s), none empty" % (path.name, n, ", ".join(KNOWN_COLUMNS)))
        elif cells[0] not in RENDERERS:
            problems.append("%s:%d: the renderer is one of %s, not %r" % (path.name, n, ", ".join(RENDERERS), cells[0]))
        elif not re.fullmatch(r"[PN]-\d+|pending", cells[2]):
            problems.append("%s:%d: the ledger row is an id like P-12 or N-1090, or pending, not %r" % (path.name, n, cells[2]))
        elif (cells[0], cells[1]) in known:
            problems.append("%s:%d: %s %s is listed twice" % (path.name, n, cells[0], cells[1]))
        else:
            known[(cells[0], cells[1])] = (cells[2], cells[3])
    if not header and not problems:
        problems.append("%s: no header line" % path.name)
    if problems:
        raise KnownError(problems)
    return known


def layer_state(text, renderer):
    """Whether a run's log says the layer is on: True, False, or None when it has no such line (the game did not get so far)."""
    found = LAYER_LINES[renderer].findall(text)
    if not found:
        return None
    return found[-1] == "1"


class Messages:
    """What a run's log holds of the layer: `errors` {id: [count, first text]}, `warnings` the number of warning messages."""

    def __init__(self):
        self.errors = {}
        self.warnings = 0

    def add(self, other):
        self.warnings += other.warnings
        for key, (count, text) in other.errors.items():
            mine = self.errors.setdefault(key, [0, text])
            mine[0] += count


def read_messages(text, renderer):
    """The layer messages of a run's log that belong to `renderer`'s layer."""
    out = Messages()
    for line in text.splitlines():
        m = MESSAGE.match(line)
        if m is None or FAMILY[m.group(1)] != renderer:
            continue
        if m.group(2) == "WARN":
            out.warnings += 1
        else:
            entry = out.errors.setdefault(m.group(3), [0, m.group(4).split(" | at")[0].strip()])
            entry[0] += 1
    return out


def first_words(text, limit=60):
    return " ".join(text.split())[:limit]


def judge_run(renderer, text, known, expected_on=True):
    """(kind, message, messages) of one test's run log read for the layer, for a test whose own result is a pass: fail with the
    first unlisted error's message, tool when the layer was not on in this run, else pass."""
    messages = read_messages(text, renderer)
    if expected_on and layer_state(text, renderer) is False:
        return "tool", "the %s was not on in this run (the game's start line says debug-layer=0 / validation=0)" % LAYER_NAMES[
            renderer], messages
    unlisted = [(i, v) for i, v in messages.errors.items() if (renderer, i) not in known]
    if unlisted:
        ident, (count, message) = unlisted[0]
        more = " (and %d more ids)" % (len(unlisted) - 1) if len(unlisted) > 1 else ""
        return "fail", "%s error %s%s, %d time%s: %s" % (
            LAYER_NAMES[renderer], ident, more, count, "" if count == 1 else "s", first_words(message, 200)), messages
    return "pass", "", messages


def judge_limits(message):
    """The kind and message of a result that reached its time limit while the game still moved (the runner's too-slow, which the
    gate would run again alone, or not-reached): tool, in the lane's own words; None for any other result."""
    if message.startswith(LIMIT_MESSAGES):
        return "tool", "the validate lane's time limit is too short for this test (%d times the usual): %s" % (LIMIT_SCALE, message)
    return None


def judge_results(renderer, results, logs, known, expected_on=True):
    """(results, final, totals) for a lane's {test: (kind, message)} and {test: log text}: a result that reached its time limit is
    tool, a pass whose log holds an unlisted error is fail, a pass whose layer was not on is tool; both final (the same on every run, so
    not run again alone). `totals` is the Messages of every log: what the lane counted."""
    out, final, totals = dict(results), set(), Messages()
    for test, (kind, message) in results.items():
        text = logs.get(test, "")
        messages = read_messages(text, renderer)
        totals.add(messages)
        limit = judge_limits(message) if kind in ("env", "fail") else None
        if limit is not None:
            out[test] = limit
            final.add(test)
        elif kind == "pass":
            judged, why, _ = judge_run(renderer, text, known, expected_on)
            if judged != "pass":
                out[test] = (judged, why)
                final.add(test)
    return out, final, totals


def summary(renderer, totals, known):
    """The words for a check's detail: the listed errors counted with their ledger rows, and the warnings."""
    listed = ["%s x%d (%s)" % (i, c, known[(renderer, i)][0]) for i, (c, _) in sorted(totals.errors.items())
              if (renderer, i) in known]
    parts = []
    if listed:
        parts.append("listed layer errors: " + ", ".join(listed))
    if totals.warnings:
        parts.append("%d layer warning%s" % (totals.warnings, "" if totals.warnings == 1 else "s"))
    return "; ".join(parts)


def not_seen(known, seen_by_renderer):
    """The listed ids a whole tour no longer showed: [(renderer, id, ledger row, first words)]. `seen_by_renderer` {renderer:
    set of ids}; a renderer that did not run is left out."""
    return [(r, i, row, words) for (r, i), (row, words) in sorted(known.items())
            if r in seen_by_renderer and i not in seen_by_renderer[r]]


def fault_due(faults, check, now):
    """Whether tool-faults.tsv (`faults`: its rows as dicts with time and check) has no line for this check from today yet: a layer that
    is not installed is told there once a day at most for each renderer."""
    today = now.date().isoformat()
    return not any(f.get("check") == check and str(f.get("time", "")).startswith(today) for f in faults)


# The layers this PC is known not to have (docs/plan/native/AGENT.md section 9: the Vulkan validation layer is not installed here): their absence
# is the check's own plain line and no line of tool-faults.tsv, so the progress page shows no fault for what is expected (T11-B).
EXPECTED_ABSENT = ("vulkan",)


def missing_layer_line(renderer):
    """The one plain line of a renderer whose layer is not on."""
    return "not run: the %s is not installed on this PC" % LAYER_NAMES[renderer]
