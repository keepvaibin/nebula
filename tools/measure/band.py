"""band.py: the speed measure's comparison and its history (T7a). No game, no clock, no machine: numbers and a file.

A figure is compared with the median of the last ten trusted runs that have the same key: computer, graphics card,
run kind (pc, pc-4cores, tablet), renderer, scene, picture size and rate setting, and the figure's own name. It is
`worse` only past the larger of three spreads (3 x the median absolute deviation x 1.4826) and 5 % of the median, on
the figure's bad side; past it on the good side it is `better`; with fewer than five trusted earlier runs it is
`no baseline yet`, never `worse`.

The history is one line per run, as the measure always kept it (gate-runs/measure-history.tsv; the gate gives its
place with --history), with columns added at the end: the key's parts the old lines lacked, and `marks`, the mark of
each compared figure of that line: `trusted` (counts for a baseline), `outlier` (worse once, fine when measured again),
`worse` (worse twice), `retired` (before an accepted slowdown). Lines written before the marks existed have none, so
they never count for a baseline.
"""
import os
import pathlib
import statistics
import tempfile

TAB = "\t"
COLUMNS = ("commit", "time", "device", "renderer", "rate", "refresh_hz", "gap_max_ms", "gap_p99_ms", "late_per_min",
           "step_max_ms", "steps_over_18", "render_ms", "gpu_ms", "memory_mb", "run",
           "computer", "gpu", "scene", "size", "marks")
OLD_COLUMNS = 15  # the lines written before the key's parts and the marks were kept
# What makes two runs comparable (with the figure's own name): the history line's columns.
KEY_COLUMNS = ("computer", "gpu", "device", "renderer", "scene", "size", "rate")
# The figures compared, and which side of the median is bad. Every figure the measure keeps is a time, a count of
# misses or a size: higher is bad. A figure where lower is bad (pictures a second) is named "lower" here.
FIGURES = {"gap_max_ms": "higher", "gap_p99_ms": "higher", "late_per_min": "higher", "step_max_ms": "higher",
           "steps_over_18": "higher", "render_ms": "higher", "gpu_ms": "higher", "memory_mb": "higher"}
BASELINE_RUNS = 10
LEAST_RUNS = 5
MAD_SCALE = 1.4826
SPREADS = 3.0
FLOOR = 0.05
MARKS = ("trusted", "outlier", "worse", "retired")


def band_of(values):
    """(median, width) of earlier trusted values: the width is the larger of 3 x MAD x 1.4826 and 5 % of the median."""
    median = statistics.median(values)
    mad = statistics.median(abs(v - median) for v in values)
    return median, max(SPREADS * MAD_SCALE * mad, FLOOR * abs(median))


def judge(value, earlier, bad="higher"):
    """{verdict, median, band} for a value against earlier trusted values (oldest first; only the last ten count).
    verdict: worse, better, holds, `no baseline yet`, or `not measured` (value None: never compared, never 0)."""
    if value is None:
        return {"verdict": "not measured", "median": None, "band": None}
    values = list(earlier)[-BASELINE_RUNS:]
    if len(values) < LEAST_RUNS:
        return {"verdict": "no baseline yet", "median": None, "band": None, "earlier": len(values)}
    median, width = band_of(values)
    over = value - median
    if bad == "lower":
        over = -over
    if over > width:
        verdict = "worse"
    elif over < -width:
        verdict = "better"
    else:
        verdict = "holds"
    return {"verdict": verdict, "median": median, "band": width, "earlier": len(values)}


def parse_marks(text):
    out = {}
    for part in text.split(";"):
        name, _, mark = part.partition("=")
        if name and mark in MARKS:
            out[name] = mark
    return out


def format_marks(marks):
    return ";".join("%s=%s" % (name, marks[name]) for name in sorted(marks))


def key_of(row):
    return tuple(row.get(c, "") for c in KEY_COLUMNS)


def row_of(cells):
    """A history line's cells as a dict; the added columns of an old line are empty."""
    cells = list(cells) + [""] * (len(COLUMNS) - len(cells))
    return dict(zip(COLUMNS, cells))


def read_history(path):
    path = pathlib.Path(path)
    if not path.is_file():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines()[1:]:
        cells = line.split(TAB)
        if OLD_COLUMNS <= len(cells) <= len(COLUMNS):
            rows.append(row_of(cells))
    return rows


def write_all(path, rows):
    """The whole file again (the header's change, accept), beside it first and then put in its place."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(dir=str(path.parent), prefix=path.name + ".", suffix=".tmp")
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as out:
        out.write(TAB.join(COLUMNS) + "\n")
        for row in rows:
            out.write(TAB.join(row[c] for c in COLUMNS) + "\n")
    os.replace(temp, path)


def add_row(path, row):
    """One line added. A file with the old header (15 columns) gets the new header first: its old lines stay."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    header = TAB.join(COLUMNS)
    if path.is_file():
        first = path.read_text(encoding="utf-8").split("\n", 1)[0]
        if first != header:
            write_all(path, read_history(path))
    else:
        path.write_text(header + "\n", encoding="utf-8", newline="\n")
    with open(path, "a", encoding="utf-8", newline="\n") as out:
        out.write(TAB.join(row[c] for c in COLUMNS) + "\n")


def trusted_values(rows, key, figure):
    """The numbers of the trusted lines of one key and figure, oldest first (a figure not measured, "-", is not one)."""
    out = []
    for row in rows:
        if key_of(row) != key or parse_marks(row["marks"]).get(figure) != "trusted":
            continue
        try:
            out.append(float(row[figure]))
        except ValueError:
            pass
    return out


def accept(path, commit, renderer="", scene=""):
    """Restart the baseline from `commit`: for each key (of the renderer and scene given, or all) whose lines include
    the commit, the lines before its first line stop counting (trusted -> retired) and its `worse` marks from that line
    on become trusted. Returns (keys changed, lines changed); raises ValueError when no line has the commit."""
    rows = read_history(path)
    commit = commit.strip()

    def at_commit(row):
        mine = row["commit"]
        return bool(mine) and bool(commit) and (mine.startswith(commit) or commit.startswith(mine))

    def chosen(row):
        return ((not renderer or renderer in (row["renderer"], row["device"]))
                and (not scene or scene == row["scene"]))
    if not any(at_commit(r) and chosen(r) for r in rows):
        raise ValueError("no line of %s in %s%s" % (commit, path, (" for " + (renderer or scene)) if renderer or scene
                                                    else ""))
    first = {}
    for i, row in enumerate(rows):
        if chosen(row) and at_commit(row):
            first.setdefault(key_of(row), i)
    lines = 0
    for i, row in enumerate(rows):
        start = first.get(key_of(row))
        if start is None or not chosen(row):
            continue
        marks = parse_marks(row["marks"])
        before = dict(marks)
        for name, mark in marks.items():
            if i < start and mark == "trusted":
                marks[name] = "retired"
            elif i >= start and mark == "worse":
                marks[name] = "trusted"
        if marks != before:
            row["marks"] = format_marks(marks)
            lines += 1
    write_all(path, rows)
    return len(first), lines
