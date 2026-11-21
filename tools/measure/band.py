"""band.py: the speed measure's comparison and its history (T7a). No game, no clock, no machine: numbers and a file.

A figure is compared with the median of the last ten trusted runs that have the same key: computer, graphics card,
run kind (pc, pc-4cores, tablet), renderer, scene, picture size and rate setting, and the figure's own name. It is
`worse` only past the band: the largest of three spreads (3 x the median absolute deviation x 1.4826), 5 % of the
median and the figure's floor (FLOORS: so a median of 0 never gives a band of 0), on the figure's bad side; past it on
the good side it is `better`; with fewer than five trusted earlier runs it is `no baseline yet`, never `worse`. Only
the JUDGED figures get these verdicts; the others are shown (verdict `not judged`): one slow frame in three minutes is
not a change in the game.

The anchor (`measure.py anchor`) marks the lines of one commit, the start of the native work, in the `anchor` column. A
figure is also compared with the median of the anchor's lines of its key (anchor_judge), with the same band rule; that
comparison only says so, it never changes an exit code.

The history is one line per run, as the measure always kept it (gate-runs/measure-history.tsv; the gate gives its
place with --history), with columns added at the end: the key's parts the old lines lacked, and `marks`, the mark of
each compared figure of that line: `trusted` (counts for a baseline), `outlier` (worse once, fine when measured again),
`worse` (worse twice), `retired` (before an accepted slowdown). Lines written before the marks existed have none, so
they never count for a baseline. Columns are only ever added at the end, so an older file's lines keep their places: a line
written before `cpu_ms` or the cpu_work, cpu_game and cpu_render columns existed reads with those cells empty, never 0.

The pair (`measure.py pair`) compares two builds that ran side by side in the same minutes, so only the ratio of a figure is
judged, never a value against another day's. Its band of a figure is learned from pairs of one build against itself
(`measure.py pair-calibrate`) and kept in a file of its own (pair-bands.tsv); this module holds the arithmetic and the three
files (the bands, the rounds' lines in pairs.tsv, the accepted slowdowns in pair-accepted.tsv). Nothing there is compared with
the history above.
"""
import os
import pathlib
import statistics
import tempfile

TAB = "\t"
COLUMNS = ("commit", "time", "device", "renderer", "rate", "refresh_hz", "gap_max_ms", "gap_p99_ms", "late_per_min",
           "step_max_ms", "steps_over_18", "render_ms", "gpu_ms", "memory_mb", "run",
           "computer", "gpu", "scene", "size", "marks", "step_ms", "step_p99_ms", "anchor", "cpu_ms", "cpu_work_ms", "cpu_game_ms",
           "cpu_render_ms")
OLD_COLUMNS = 15  # the lines written before the key's parts and the marks were kept
# What makes two runs comparable (with the figure's own name): the history line's columns.
KEY_COLUMNS = ("computer", "gpu", "device", "renderer", "scene", "size", "rate")
# The figures compared, and which side of the median is bad. Every figure the measure keeps is a time, a count of
# misses or a size: higher is bad. A figure where lower is bad (pictures a second) is named "lower" here.
FIGURES = {"gap_max_ms": "higher", "gap_p99_ms": "higher", "late_per_min": "higher", "step_ms": "higher",
           "step_p99_ms": "higher", "step_max_ms": "higher", "steps_over_18": "higher", "render_ms": "higher",
           "gpu_ms": "higher", "memory_mb": "higher", "cpu_ms": "higher", "cpu_work_ms": "higher", "cpu_game_ms": "higher",
           "cpu_render_ms": "higher"}
# The figures that get a verdict. The others are shown only: largest single values, a count of slow steps, the step's time on
# the clock (it holds waits as well as work) and cpu_ms, the processor time of the whole process per step, which holds waiting
# too (a timer's spin, a present that blocks). The cost of a step is the processor time of the frame threads' work only: all
# together (cpu_work_ms), the game thread's (cpu_game_ms) and the render thread's (cpu_render_ms). Any other frame thread's
# cpu_<name>_ms is shown.
JUDGED = ("cpu_work_ms", "cpu_game_ms", "cpu_render_ms", "gap_p99_ms", "late_per_min", "render_ms", "gpu_ms", "memory_mb")
# The least a band is wide, by figure (a time in ms, late frames a minute, memory in MB); a figure not named has none.
FLOORS = {"gap_max_ms": 0.25, "gap_p99_ms": 0.25, "step_ms": 0.02, "step_p99_ms": 0.02, "render_ms": 0.02,
          "gpu_ms": 0.02, "late_per_min": 2.0, "memory_mb": 32.0, "cpu_ms": 0.02, "cpu_work_ms": 0.02, "cpu_game_ms": 0.02,
          "cpu_render_ms": 0.02}
ANCHOR_LEAST_LINES = 5
BASELINE_RUNS = 10
LEAST_RUNS = 5
MAD_SCALE = 1.4826
SPREADS = 3.0
FLOOR = 0.05
MARKS = ("trusted", "outlier", "worse", "retired")


def band_of(values, floor=0.0):
    """(median, width) of earlier trusted values: the width is the largest of 3 x MAD x 1.4826, 5 % of the median and
    `floor`."""
    median = statistics.median(values)
    mad = statistics.median(abs(v - median) for v in values)
    return median, max(SPREADS * MAD_SCALE * mad, FLOOR * abs(median), floor)


def judge(value, earlier, bad="higher", floor=0.0):
    """{verdict, median, band} for a value against earlier trusted values (oldest first; only the last ten count).
    verdict: worse, better, holds, `no baseline yet`, or `not measured` (value None: never compared, never 0)."""
    if value is None:
        return {"verdict": "not measured", "median": None, "band": None}
    values = list(earlier)[-BASELINE_RUNS:]
    if len(values) < LEAST_RUNS:
        return {"verdict": "no baseline yet", "median": None, "band": None, "earlier": len(values)}
    median, width = band_of(values, floor)
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


def commit_matches(row, commit):
    mine = row["commit"]
    return bool(mine) and bool(commit) and (mine.startswith(commit) or commit.startswith(mine))


def set_anchor(path, commit):
    """The anchor (the start of the native work): the lines of `commit` that have a trusted figure get the anchor flag, and
    no other line keeps it (there is one anchor). Returns (keys, lines); raises ValueError when no line of the commit has
    a trusted figure."""
    rows = read_history(path)
    commit = commit.strip()
    chosen = [r for r in rows if commit_matches(r, commit) and "trusted" in parse_marks(r["marks"]).values()]
    if not chosen:
        raise ValueError("no trusted line of %s in %s" % (commit, path))
    ids = {id(r) for r in chosen}
    for row in rows:
        row["anchor"] = "1" if id(row) in ids else ""
    write_all(path, rows)
    return len({key_of(r) for r in chosen}), len(chosen)


def anchor_lines(rows, key):
    return [r for r in rows if r["anchor"] == "1" and key_of(r) == key]


def anchor_values(rows, key, figure):
    """The numbers of the anchor's lines of one key and figure. A figure the line marks `retired` (an accepted slowdown
    came after it) still counts: the anchor stays where the native work started."""
    out = []
    for row in anchor_lines(rows, key):
        if parse_marks(row["marks"]).get(figure) not in ("trusted", "retired"):
            continue
        try:
            out.append(float(row[figure]))
        except ValueError:
            pass
    return out


def anchor_judge(value, anchor, bad="higher", floor=0.0):
    """{verdict, median, band, lines, percent} for a value against the anchor's numbers (all of them): `slower`, `faster`,
    `holds`, `not enough lines` (fewer than five) or `not measured`. percent: how far past the median, for `slower` and
    `faster` (None when the median is 0)."""
    if value is None:
        return {"verdict": "not measured", "median": None, "band": None, "lines": len(anchor), "percent": None}
    if len(anchor) < ANCHOR_LEAST_LINES:
        return {"verdict": "not enough lines", "median": None, "band": None, "lines": len(anchor), "percent": None}
    median, width = band_of(anchor, floor)
    over = value - median
    if bad == "lower":
        over = -over
    verdict = "slower" if over > width else "faster" if over < -width else "holds"
    percent = 100.0 * abs(value - median) / abs(median) if verdict != "holds" and median else None
    return {"verdict": verdict, "median": median, "band": width, "lines": len(anchor), "percent": percent}


def keep(pending, history):
    """The lines of the pending file (a landing's trusted lines, written beside the history) added to the history, each
    only once: a line already in the history is left. Returns (added, already there)."""
    rows = read_history(pending)
    there = {TAB.join(r[c] for c in COLUMNS) for r in read_history(history)}
    added = 0
    for row in rows:
        line = TAB.join(row[c] for c in COLUMNS)
        if line in there:
            continue
        add_row(history, row)
        there.add(line)
        added += 1
    return added, len(rows) - added


# ---- The pair ----
# The least a pair's band is wide, as a share of the ratio (0.05: the ratio may be 5 % off 1), by figure.
PAIR_LEAST = {"cpu_work_ms": 0.03, "cpu_game_ms": 0.03, "cpu_render_ms": 0.03, "render_ms": 0.05, "gpu_ms": 0.05, "memory_mb": 0.02, "gap_p99_ms": 0.25, "late_per_min": 0.50}
PAIR_ROUNDS = 3  # rounds before "worse"
BANDS_COLUMNS = ("computer", "gpu", "renderer", "scene", "size", "rate", "figure", "band", "ratios", "commit", "time")
BANDS_KEY = BANDS_COLUMNS[:6]
ROUNDS_COLUMNS = ("time", "base_commit", "cand_commit", "renderer", "round", "figure", "base", "cand", "ratio", "word")
ACCEPTED_COLUMNS = ("commit", "renderer", "figure", "reason", "date")


def pair_ratio(base, cand, figure):
    """(cand + f) / (base + f), f the figure's floor: 0 against 0 is 1, and a hundredth of a millisecond is not a change."""
    f = FLOORS.get(figure, 0.0)
    below = base + f
    return 1.0 if below == 0 and cand + f == 0 else (cand + f) / below if below else float("inf")


def pair_word(ratio, band):
    """worse past 1 + band, better under 1 - band, else ok; `no-baseline` when there is no band (not calibrated)."""
    if band is None:
        return "no-baseline"
    if ratio > 1.0 + band:
        return "worse"
    if ratio < 1.0 - band:
        return "better"
    return "ok"


def pair_band(ratios, figure):
    """The band of a figure from the ratios of one build against itself: max(least, 2 x the largest |r - 1|)."""
    return max(PAIR_LEAST[figure], 2.0 * max(abs(r - 1.0) for r in ratios))


def cells_file(path, columns, rows):
    """The whole file again: the header, then the rows (dicts), beside it first and then put in its place."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(dir=str(path.parent), prefix=path.name + ".", suffix=".tmp")
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as out:
        out.write(TAB.join(columns) + "\n")
        for row in rows:
            out.write(TAB.join(str(row[c]) for c in columns) + "\n")
    os.replace(temp, path)


def read_cells(path, columns):
    """The rows (dicts) of a file written by cells_file or add_cells; none when it does not exist; a line of another width is
    left out."""
    path = pathlib.Path(path)
    if not path.is_file():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines()[1:]:
        cells = line.split(TAB)
        if len(cells) == len(columns):
            rows.append(dict(zip(columns, cells)))
    return rows


def add_cells(path, columns, row):
    """One row added; the file is made with its header when it is not there."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file():
        path.write_text(TAB.join(columns) + "\n", encoding="utf-8", newline="\n")
    with open(path, "a", encoding="utf-8", newline="\n") as out:
        out.write(TAB.join(str(row[c]) for c in columns) + "\n")


def pair_key(context, renderer, rate):
    """The key of a pair's band: computer, graphics card, renderer, scene, size, rate."""
    return (context.get("computer", ""), context.get("gpu", ""), renderer, context.get("scene", ""), context.get("size", ""),
            str(rate))


def pair_bands(path, key):
    """{figure: band} of the calibration file's lines for a key (none: an empty dict: not calibrated)."""
    return {r["figure"]: float(r["band"]) for r in read_cells(path, BANDS_COLUMNS)
            if tuple(r[c] for c in BANDS_KEY) == tuple(key)}


def write_pair_bands(path, key, bands, commit, now):
    """The calibration lines of a key: {figure: (band, [ratios])} replace the key's older lines, the others stay."""
    rows = [r for r in read_cells(path, BANDS_COLUMNS) if tuple(r[c] for c in BANDS_KEY) != tuple(key)]
    for figure in sorted(bands):
        band, ratios = bands[figure]
        row = dict(zip(BANDS_KEY, key), figure=figure, band="%.4f" % band, ratios=",".join("%.4f" % r for r in ratios),
                   commit=commit, time=now)
        rows.append(row)
    cells_file(path, BANDS_COLUMNS, rows)


def accepted_reason(path, commit, renderer, figure):
    """The reason of the accepted line for this commit, renderer and figure, or None (a commit by its first characters)."""
    for r in read_cells(path, ACCEPTED_COLUMNS):
        mine = r["commit"]
        if (r["renderer"] == renderer and r["figure"] == figure and mine and commit
                and (mine.startswith(commit) or commit.startswith(mine))):
            return r["reason"]
    return None
