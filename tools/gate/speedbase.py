"""speedbase.py: the kept builds a landing's speed check is judged against, and how the gate reads a pair (package B13, part B).

    python -X utf8 -I tools/gate/gate.py speed-base make   --bin BINS --commit SHA [--pak GAME.PAK]
    python -X utf8 -I tools/gate/gate.py speed-base anchor --bin BINS --commit SHA [--pak GAME.PAK] [--replace]
    python -X utf8 -I tools/gate/gate.py speed-base show

Two kept builds, in D:/NebulaWork/build/speed-base/ (the folder is one value, ROOT):

    base    the build of the last commit on main whose speed was judged. A landing's pair (tools/measure/measure.py pair) runs the
            candidate against it. A landing that judged its speed makes the candidate the base; one that did not leaves it.
    anchor  made once, on the day the native work starts, and never replaced by a landing that judged it. `speed/anchor` runs the
            candidate against it.

Each is a folder <kind>-<commit, 9 characters>-<time>/ with `bins/` (the build's bin folder, its pak.path pointing at the folder's own
game.pak), `game.pak` (a hard link to the pak the build was made for, a copy where a link cannot be made: no clean-up of the pak folders
takes it away) and `kept.json` (the commit, the time, the data's key). One file, `current.txt` for the base and `anchor.txt` for the
anchor, names the folder that is the current one. `base-history.tsv` has a row for each change of the base (made, moved, kept, rebased);
`anchor-history.tsv` one row for each anchor: its commit, the anchor before it and the last ratios judged against that one (the
product of the ratios along the rows is the drift since the first anchor).

The second half is how the gate reads what `measure.py pair` wrote (WORK/result.json and the --pending table): `read_pair`. It names no
figure: the figures, their bands and their lines are the pair's, read as data.
"""
import argparse
import datetime
import json
import os
import pathlib
import shutil
import statistics
import sys

ROOT = pathlib.Path("D:/NebulaWork/build/speed-base")
REGISTRY = pathlib.Path("D:/NebulaWork/build/game-data/registry.tsv")
BASE, ANCHOR = "base", "anchor"
CURRENT_FILE = {BASE: "current.txt", ANCHOR: "anchor.txt"}
BASE_HISTORY, ANCHOR_HISTORY, ANCHOR_LAST = "base-history.tsv", "anchor-history.tsv", "anchor-last.json"
BASE_COLUMNS = ("time", "event", "commit", "previous", "note")
ANCHOR_COLUMNS = ("time", "commit", "previous", "judged at", "ratios")
INFO = "kept.json"


class KeptError(Exception):
    """A kept build could not be read or made: the text is one plain line."""


def stamp(now=None):
    return (now or datetime.datetime.now()).strftime("%Y-%m-%d %H:%M:%S")


def tsv_line(cells):
    return "\t".join(str(c).replace("\t", " ").replace("\n", " ") for c in cells) + "\n"


def add_row(path, columns, row):
    """One row at the end of a tab-separated file (its header written when the file is new)."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    new = not path.exists()
    with open(path, "a", encoding="utf-8", newline="\n") as f:
        if new:
            f.write(tsv_line(columns))
        f.write(tsv_line([row.get(c, "") for c in columns]))


def read_rows(path, columns):
    try:
        lines = pathlib.Path(path).read_text(encoding="utf-8").splitlines()
    except OSError:
        return []
    return [dict(zip(columns, l.split("\t"))) for l in lines[1:] if l.strip()]


class Kept:
    """A kept build: `folder`, `commit`, `time`, `key`, and the places in it."""

    def __init__(self, kind, folder, info):
        self.kind, self.folder, self.info = kind, pathlib.Path(folder), info
        self.commit, self.time, self.key = info.get("commit", ""), info.get("time", ""), info.get("key", "")

    @property
    def bins(self):
        return self.folder / "bins"

    @property
    def pak(self):
        return self.folder / "game.pak"

    def size(self):
        """(bytes of the bin folder, bytes of the game data, whether the data is a link)."""
        total = 0
        for path in self.bins.rglob("*"):
            try:
                if path.is_file():
                    total += path.stat().st_size
            except OSError:
                pass
        try:
            data = self.pak.stat().st_size
        except OSError:
            data = 0
        return total, data, bool(self.info.get("linked"))


def read(kind, root=ROOT):
    """The current kept build of `kind` (`base` or `anchor`), or None when there is none. A file that names a folder that is not there,
    or a folder with no kept.json, is a KeptError: something took the build away."""
    root = pathlib.Path(root)
    try:
        name = (root / CURRENT_FILE[kind]).read_text(encoding="utf-8").strip()
    except OSError:
        return None
    folder = root / name
    try:
        info = json.loads((folder / INFO).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise KeptError("%s names %s, which cannot be read (%s: %s)" % (CURRENT_FILE[kind], folder, type(error).__name__, error))
    if not (folder / "bins").is_dir() or not (folder / "game.pak").is_file():
        raise KeptError("%s has no bins folder or no game.pak" % folder)
    return Kept(kind, folder, info)


def data_key(pak, registry=REGISTRY):
    """The key the game data goes by (tools/gate/gamedata.py's registry) when the pak is one of its entries, else the name of the
    folder the pak is in."""
    pak = pathlib.Path(pak)
    try:
        want = os.path.normcase(os.path.abspath(pak))
        for line in pathlib.Path(registry).read_text(encoding="utf-8").splitlines()[1:]:
            cells = line.split("\t")
            if len(cells) >= 2 and os.path.normcase(os.path.abspath(cells[1])) == want:
                return cells[0]
    except OSError:
        pass
    return pak.parent.name or str(pak)


def pak_of(bin_dir):
    """The game.pak a bin folder's pak.path names, or None."""
    try:
        text = (pathlib.Path(bin_dir) / "pak.path").read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return pathlib.Path(text) if text else None


def make(kind, bin_dir, commit, pak=None, root=ROOT, now=None, registry=REGISTRY, note=""):
    """Makes the kept build of `kind` from a built bin folder and replaces the current one (its folder is removed): the Kept. The new
    folder is complete before the file that names it changes; a copy that fails is removed and the old build stays."""
    root, bin_dir = pathlib.Path(root), pathlib.Path(bin_dir)
    if not bin_dir.is_dir():
        raise KeptError("%s is not a folder" % bin_dir)
    pak = pathlib.Path(pak) if pak else pak_of(bin_dir)
    if pak is None or not pak.is_file():
        raise KeptError("the game data of %s is not there (--pak, or a pak.path that names a file): %s" % (bin_dir, pak))
    now = now or datetime.datetime.now()
    try:
        old = read(kind, root)
    except KeptError:
        old = None
    name = "%s-%s-%s" % (kind, commit[:9], now.strftime("%Y%m%d-%H%M%S"))
    folder = root / name
    try:
        shutil.rmtree(folder, ignore_errors=True)
        folder.mkdir(parents=True)
        shutil.copytree(bin_dir, folder / "bins")
        linked = True
        try:
            os.link(pak, folder / "game.pak")
        except OSError:
            linked = False
            shutil.copy2(pak, folder / "game.pak")
        (folder / "bins" / "pak.path").write_text("%s\n" % (folder / "game.pak"), encoding="ascii")
        info = {"kind": kind, "commit": commit, "time": stamp(now), "key": data_key(pak, registry), "pak from": str(pak),
                "linked": linked}
        (folder / INFO).write_text(json.dumps(info, indent=1) + "\n", encoding="utf-8", newline="\n")
        current = root / CURRENT_FILE[kind]
        tmp = current.with_name(current.name + ".tmp")
        tmp.write_text(name + "\n", encoding="utf-8", newline="\n")
        os.replace(tmp, current)
    except OSError as error:
        shutil.rmtree(folder, ignore_errors=True)
        raise KeptError("the %s of %s could not be made: %s: %s" % (kind, commit[:9], type(error).__name__, error))
    if old is not None and old.folder != folder:
        shutil.rmtree(old.folder, ignore_errors=True)
    return Kept(kind, folder, info)


def remember_base(event, commit, previous, note, root=ROOT, now=None):
    add_row(pathlib.Path(root) / BASE_HISTORY, BASE_COLUMNS,
            {"time": stamp(now), "event": event, "commit": commit[:9], "previous": (previous or "-")[:9], "note": note})


def ratios_text(ratios):
    """`d3d12.cpu_work_ms=1.0123 ...`: the ratios of {renderer: {figure: ratio}}, sorted; "-" for none."""
    parts = ["%s.%s=%.4f" % (r, f, v) for r in sorted(ratios) for f, v in sorted(ratios[r].items())]
    return " ".join(parts) if parts else "-"


def note_anchor_ratios(ratios, anchor_commit, judged_at, root=ROOT, now=None):
    """The ratios of a judged `speed/anchor` (candidate over anchor), kept as the last ones against this anchor."""
    path = pathlib.Path(root) / ANCHOR_LAST
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"anchor": anchor_commit, "judged at": judged_at, "time": stamp(now), "ratios": ratios}, indent=1) + "\n",
                    encoding="utf-8", newline="\n")


def make_anchor(bin_dir, commit, pak=None, root=ROOT, now=None, registry=REGISTRY):
    """A new anchor: the kept build, and one row of anchor-history.tsv with the last ratios judged against the anchor before it
    (anchor-last.json: none for the first, `-` for an anchor that could not be judged)."""
    root = pathlib.Path(root)
    try:
        old = read(ANCHOR, root)
    except KeptError:
        old = None
    last = {}
    try:
        last = json.loads((root / ANCHOR_LAST).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        pass
    kept = make(ANCHOR, bin_dir, commit, pak, root, now, registry)
    judged = last.get("judged at", "-") if old is not None and last.get("anchor") == old.commit else "-"
    ratios = last.get("ratios", {}) if old is not None and last.get("anchor") == old.commit else {}
    add_row(root / ANCHOR_HISTORY, ANCHOR_COLUMNS, {"time": stamp(now), "commit": commit[:9], "previous": old.commit[:9] if old else "-",
                                                    "judged at": judged, "ratios": ratios_text(ratios)})
    (root / ANCHOR_LAST).unlink(missing_ok=True)
    return kept


def show(root=ROOT):
    """The lines `speed-base show` prints: the base and the anchor with commit, time, data key and size."""
    out = []
    for kind in (BASE, ANCHOR):
        try:
            kept = read(kind, root)
        except KeptError as error:
            out.append("%s: broken: %s" % (kind, error))
            continue
        if kept is None:
            out.append("%s: none" % kind)
            continue
        bins, data, linked = kept.size()
        out.append("%s: commit %s, made %s, game data %s, %s: bins %.0f MB, game.pak %.2f GB (%s)" % (
            kind, kept.commit[:9], kept.time, kept.key, kept.folder, bins / 1e6, data / 1e9,
            "hard link, no space of its own" if linked else "a copy"))
    return out


def main(argv):
    p = argparse.ArgumentParser(prog="gate.py speed-base", description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="command", required=True)
    for name, help_text in (("make", "make the base from a built bin folder"), ("anchor", "make the anchor from a built bin folder")):
        m = sub.add_parser(name, help=help_text)
        m.add_argument("--bin", required=True, help="the build's bin folder (bins)")
        m.add_argument("--commit", required=True)
        m.add_argument("--pak", default="", help="the game data the build was made for (default: the bin folder's pak.path)")
        m.add_argument("--root", default=str(ROOT))
        if name == "anchor":
            m.add_argument("--replace", action="store_true", help="make a new anchor where one exists (the anchor is made once)")
    s = sub.add_parser("show", help="the base and the anchor")
    s.add_argument("--root", default=str(ROOT))
    args = p.parse_args(argv)
    try:
        if args.command == "show":
            print("\n".join(show(args.root)))
            return 0
        if args.command == "make":
            old = None
            try:
                old = read(BASE, args.root)
            except KeptError:
                pass
            kept = make(BASE, args.bin, args.commit, args.pak or None, args.root)
            remember_base("made", args.commit, old.commit if old else None, "by hand", args.root)
        else:
            try:
                existing = read(ANCHOR, args.root)
            except KeptError:
                existing = None
            if existing is not None and not args.replace:
                print("gate.py speed-base: the anchor (%s) exists: it is made once; --replace makes a new one" % existing.commit[:9],
                      file=sys.stderr)
                return 2
            kept = make_anchor(args.bin, args.commit, args.pak or None, args.root)
    except KeptError as error:
        print("gate.py speed-base: %s" % error, file=sys.stderr)
        return 2
    print("\n".join(l for l in show(args.root) if l.startswith(kept.kind)))
    return 0


# --- Reading a pair ----------------------------------------------------------------------------------------------------------


class Reading:
    """What a pair said: `kind` is pass, worse, uncalibrated, tool or could-not; `text` the plain line(s); `tool` the renderers that did
    not run, with the pair's reason; `ratios` {renderer: {figure: ratio}} of the figures that were judged (for a pass)."""

    def __init__(self, kind, text, tool=None, ratios=None):
        self.kind, self.text, self.tool, self.ratios = kind, text, dict(tool or {}), ratios or {}


PENDING_COLUMNS = ("check", "renderer", "figure", "state", "line")
EXIT_OF_STATE = {"ok": 0, "better": 0, "no-baseline": 0, "accepted": 0, "worse": 1, "tool": 2}


def read_table(path):
    rows = read_rows(path, PENDING_COLUMNS)
    return [r for r in rows if len(r) == len(PENDING_COLUMNS) and r.get("renderer")]


def read_pair(work, pending, code, renderers):
    """A pair's result: Reading. From WORK/result.json (each renderer's state) and the --pending table (each figure's state and plain
    line); the exit code only as a cross-check. `worse` first (a figure worse in every round: the text names them as `renderer.figure`
    joined by +, then the pair's own lines), then `tool` (a renderer whose rounds did not run), then `uncalibrated` (a figure with no
    band for this PC), else `pass`. No readable result, a state the pair does not have, or an exit code that is not the states':
    `could-not`."""
    if code == 124:
        return Reading("could-not", "its time limit")
    if code == 125:
        return Reading("stopped", "the run was stopped")
    try:
        result = json.loads((pathlib.Path(work) / "result.json").read_text(encoding="utf-8"))
        runs = result["runs"]
        said = result.get("exit")
        states = {r: runs[r]["state"] for r in renderers}
        reasons = {r: runs[r].get("reason") or "" for r in renderers}
        figures = {r: runs[r].get("figures") or {} for r in renderers}
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        return Reading("could-not", "no readable result.json for %s (exit %d): %s: %s" % (
            ",".join(renderers), code, type(error).__name__, str(error).splitlines()[0] if str(error) else ""))
    bad = [r for r, s in states.items() if s not in EXIT_OF_STATE]
    if bad:
        return Reading("could-not", "result.json names the state %r for %s" % (states[bad[0]], bad[0]))
    want = 2 if "tool" in states.values() else 1 if "worse" in states.values() else 0
    if code != want or (isinstance(said, int) and said != code):
        return Reading("could-not", "exit %d and the states %s disagree" % (code, ", ".join("%s %s" % (r, s) for r, s in states.items())))
    try:
        table = read_table(pending) if pending else []
    except OSError:
        table = []
    lines = {(r["renderer"], r["figure"]): r for r in table}
    worse = [(r, f) for (r, f), row in lines.items() if row["state"] == "worse"]
    if "worse" in states.values():
        if not worse:
            return Reading("could-not", "state worse and no figure of the table is worse")
        names = "+".join("%s.%s" % rf for rf in sorted(worse))
        return Reading("worse", "worse: %s: %s" % (names, "; ".join(lines[rf]["line"] for rf in sorted(worse))))
    tool = {r: reasons[r] or "the pair did not run" for r, s in states.items() if s == "tool"}
    if tool:
        return Reading("tool", "; ".join("%s: %s" % (r, why) for r, why in tool.items()), tool)
    loose = sorted((r, f) for (r, f), row in lines.items() if row["state"] == "no-baseline")
    if loose or "no-baseline" in states.values():
        where = sorted({r for r, _ in loose} | {r for r, s in states.items() if s == "no-baseline"})
        return Reading("uncalibrated", "not calibrated on this PC: %s" % ", ".join(where))
    ratios = {}
    for r in renderers:
        for f, e in figures[r].items():
            got = [x for x in e.get("ratios") or [] if isinstance(x, (int, float))]
            if e.get("judged") and got:
                ratios.setdefault(r, {})[f] = statistics.median(got)
    parts = []
    for r in renderers:
        n = len(figures[r])
        extra = [row["line"] for (rr, f), row in sorted(lines.items()) if rr == r and row["state"] == "accepted"]
        parts.append("%s %s (%d figures%s)" % (r, states[r], n, "; " + "; ".join(extra) if extra else ""))
    return Reading("pass", "; ".join(parts), ratios=ratios)


def base_side_only(reading):
    """Whether every renderer that did not run says it was the base's side that could not (the pair's own wording: `the base side ...`):
    the caller then asks whether the candidate's side runs by itself."""
    return bool(reading.tool) and all(why.startswith("the base side") for why in reading.tool.values())


STORE_WORDS = "of store version"   # the runner's words for game data of another store version (tools/lumacore/luma_test.py run_one)
ENDED_CHARS = 300


def store_difference(reading):
    """Whether a renderer's reason for the base's side is a difference of store version (the game data and the build it is played
    with disagree). Never a reason for `cannot run this route`: the side is run with its own build's programs and data, so
    this is a fault of the tool or of the kept build, and the kept build stays."""
    return any(STORE_WORDS in why for why in reading.tool.values())


def base_side_ended(reading):
    """One line for a result's row: what the pair said of each renderer whose base side did not run (the program that ended and how,
    as the pair gives it), cut at ENDED_CHARS."""
    text = "; ".join("%s: %s" % (r, " ".join(why.split())) for r, why in sorted(reading.tool.items()))
    return text if len(text) <= ENDED_CHARS else text[:ENDED_CHARS - 3].rstrip() + "..."


def probe_ran(text, required=("steps_over_18", "render_ms")):
    """Whether the output of `measure.py pair-probe` shows a run that reached the stage: no `the run ended` line, and a number for each
    figure the pair itself requires of a side (measure.py REQUIRED, passed in)."""
    if "the run ended with exit" in text:
        return False
    found = {}
    for line in text.splitlines():
        cells = line.split()
        if len(cells) == 2:
            found[cells[0]] = cells[1]
    return all(found.get(name, "-") != "-" for name in required)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
