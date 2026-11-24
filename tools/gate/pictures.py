"""pictures.py: the approved pictures of the harness's tests, checked and approved.

    python -X utf8 -I tools/gate/pictures.py check   --runs <work folder> [--place P] [--renderer R] [--complete] [--out <folder>]
                                                     [--changes <PICTURES-CHANGE.txt>] [--new-out <folder>]
    python -X utf8 -I tools/gate/pictures.py approve --runs <work folder> [--place P] [--renderer R] [--new] [--only TEST[/PICTURE] ...]
    python -X utf8 -I tools/gate/pictures.py driver-update --runs <work folder> [--place P] [--renderer R]

A test's pictures are the shown pictures its "picture" steps took (pictures/<name>.png in each run folder of a
luma_test.py work folder; the internal display copies, *.display.png, are not judged). An approved picture is named
in tests/pictures/approved.tsv by its place, suite, test, renderer and picture name, with the hash of its pixels (SHA-256 of
"W H\\n" and the RGB rows). Its pixels are in the picture store, outside the repository, since a picture of the game is
the game's content: NEBULA_PICTURES or --store, default D:/NebulaWork/pictures, one PNG per hash.

One list for every level and suite. A picture's row is found by where its test lives, not by the name a run gave its
suite: the "suite" column is the folder of the test under tests/ ("engine" for tests/engine/boot; luma_test.py writes it
into result.json as "test_path"), or for a test that is written into the run's folder, the tour it belongs to ("tour",
"tour-RMGP01", "tour-RMGJ01", and "tour@WxH" for a tour played in a window of that size). A picture approved by any passing
run counts in every level and under any suite name. Rows written before this ("tour-tablet") are read as "tour".

The machine is part of the key: the place says where the runs were drawn (the PC, or the tablet: taken from the run's
platform unless --place says), and each place and renderer names one adapter and driver (the "machine" rows). The PC's and
the tablet's Vulkan pictures are two sets of rows. A run on another adapter or driver is not judged against them: it is a
"machine" line, which the gate reads as env (the machine was not fit). Rows written before places existed have no place
column and are the PC's.

The kinds of a picture (tools/gate/kinds.py): same (pass), new (fail never: no approved row for this test, picture and
renderer on this machine, or a difference that the change declares), changed (fail: the pixels differ and nothing declares
it, or the size does), missing (fail: approved, but the run took none; with --complete, as a quick run takes fewer), machine
(env: the runs are of an adapter or driver that no row names, and their pictures are not judged). The tolerance is none:
a picture passes when its pixels are the approved picture's, bit for bit. A fixed-step run draws the same pixels each time
on one adapter and driver (measured: every tour picture of two gate runs identical on Direct3D 12, Vulkan and Direct3D 11),
so any changed pixel is a change in what the game shows.

A change that is meant is declared in PICTURES-CHANGE.txt at the root of the branch, one line each, naming a picture by its
suite and stop, its place, renderer and disc:
    <suite>/<stop> <picture> <place> <renderer> <disc> :: <reason>
Each of the five words is a shell-style pattern; `*` (or `all`) is any. The suite is the test's own place without the disc
(engine, tour, tour@1280x720: a stop of the tour of any disc is tour/<NNN-Stage-N>, the tour's own name without the disc prefix);
the picture is arrive, after or the name an engine test gives it; the place is pc or tablet; the renderer d3d12, vulkan or d3d11;
the disc RMGE01 (the US disc, and every engine test), RMGP01 or RMGJ01. Examples:
    tour/010-CannonFleetGalaxy-1 arrive pc vulkan RMGE01 :: the sky is another blue
    tour/010-CannonFleetGalaxy-1 * * * * :: the same stop on every renderer, disc and place
    engine/boot title pc * RMGE01 :: the title's fade is eased
A differing picture that a line names is new, not changed; one that no line names fails; a line that names no differing picture
is unused (the gate makes the landing tool: a stale declaration). The gate plays every stop a line names on every renderer, disc
and place the approved list has a row for, so a line can name any of them.

check: one row per picture. Exit 1 when a picture is changed or missing, or the runs are of another adapter or driver.
New and declared pictures go to <new-out>/entries.tsv (one line each, complete enough to become its approved row) with
their files; changed and new ones to <out>/review.html (default <runs>/pictures-review) beside the approved one and their
difference.

approve: takes the pictures of the runs that passed into the store and the manifest, and the renderer's adapter and
driver as its machine row. With --new only the pictures that have no approved row (the nightly run's way: the full tours
and the other discs, whose pictures only it takes); with --only, only those tests or test/picture names. A failed run's
pictures are refused. At a landing the gate does it by approve_landing below: one commit of the manifest and the removed
PICTURES-CHANGE.txt.

driver-update: after a graphics driver change. The runs are of an adapter and driver that no machine row names: each picture
is compared with the approved copy of the machine the row names (the same renderer, the same card maker). Equal, or no
channel of any pixel off by more than 1 of 255: approved for the new machine; otherwise listed as FAIL. It prints both counts
and writes the manifest; it moves nothing by itself.

Needs Pillow and numpy.
"""
import argparse
import fnmatch
import hashlib
import html
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

TREE = pathlib.Path(__file__).resolve().parent.parent.parent
MANIFEST = TREE / "tests" / "pictures" / "approved.tsv"
MANIFEST_NAME = "tests/pictures/approved.tsv"
CHANGES_NAME = "PICTURES-CHANGE.txt"
STORE = os.environ.get("NEBULA_PICTURES", "D:/NebulaWork/pictures")
HEADER = ("# The approved pictures of the harness's tests (tools/gate/pictures.py). Each picture row names the SHA-256 of\n"
          "# its pixels; the pixels are in the picture store (NEBULA_PICTURES), outside the repository.\n"
          "# machine  place  renderer  adapter  driver\n"
          "# picture  place  suite  test  renderer  picture  width  height  sha256\n")
# A tour's tests are written into the run's folder by gate_tour.py: [DISC-]NNN-Stage-N.
TOUR_NAME = re.compile(r"^(?:(RMGP01|RMGJ01)-)?\d{3}-[A-Za-z0-9_]+-\d+$")
# Suite names of rows written before the key was the test's own place: read as the tour they are.
LEGACY_SUITES = {"tour-tablet": "tour"}
# The columns of a new-pictures entry: complete enough to become an approved row.
ENTRY_COLUMNS = ("kind", "place", "suite", "test", "renderer", "picture", "width", "height", "sha256", "adapter", "driver",
                 "old_sha256", "declaration", "reason", "file")
KINDS_BAD = ("changed", "missing")


def load_image(path):
    from PIL import Image
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        return rgb.width, rgb.height, rgb.tobytes()


def pixel_hash(width, height, rgb):
    h = hashlib.sha256(b"%d %d\n" % (width, height))
    h.update(rgb)
    return h.hexdigest()


def adapter_of(result):
    m = re.match(r'\s*"([^"]*)"', result.get("gpu", ""))
    return m.group(1) if m else result.get("gpu", "unknown")


def maker_of(adapter):
    """The card maker: the adapter name's first word (NVIDIA, AMD, Intel, Adreno ...), lower case."""
    words = adapter.split()
    return words[0].lower() if words else ""


# --- The manifest -------------------------------------------------------------------------------------------------------


def read_manifest(path):
    machines, pictures = {}, {}
    if path.exists():
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.split("\t")
            # A row without its place (4 and 8 columns) was written before places existed: the PC's.
            if cols[0] in ("machine", "picture") and len(cols) in (4, 8):
                cols.insert(1, "pc")
            if cols[0] == "machine" and len(cols) == 5:
                machines[(cols[1], cols[2])] = (cols[3], cols[4])
            elif cols[0] == "picture" and len(cols) == 9:
                key = (cols[1], LEGACY_SUITES.get(cols[2], cols[2])) + tuple(cols[3:6])
                pictures[key] = (int(cols[6]), int(cols[7]), cols[8])
            else:
                raise SystemExit("%s line %d: not a machine or picture row" % (path, n))
    return machines, pictures


def write_manifest(path, machines, pictures):
    rows = ["machine\t%s\t%s\t%s\t%s" % (k + v) for k, v in sorted(machines.items())]
    rows += ["picture\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%s" % (k + v) for k, v in sorted(pictures.items())]
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(HEADER + "\n".join(rows) + "\n")


def store_path(store, sha):
    return pathlib.Path(store) / sha[:2] / (sha + ".png")


def put_in_store(store, png, sha):
    """The picture's file in the store under its hash, if it is not there."""
    target = store_path(store, sha)
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(png, target)


# --- The runs ------------------------------------------------------------------------------------------------------------


class Run:
    """One test's run: its suite as the approved list files it, test, renderer, place, folder and result."""

    def __init__(self, suite, test, renderer, place, folder, result):
        self.suite, self.test, self.renderer, self.place, self.folder, self.result = suite, test, renderer, place, folder, result

    @property
    def machine(self):
        return (adapter_of(self.result), self.result.get("driver", ""))


def place_of(result, place):
    """The place a run was drawn at: `place` when given (not auto), else the tablet for an Android run, else the PC."""
    if place and place != "auto":
        return place
    return "tablet" if str(result.get("platform", "")).startswith("android") else "pc"


def window_of(run):
    """The window a test asked for (WxH), from the test.json the runner keeps in the run's folder, else None."""
    try:
        raw = json.loads((run / "test.json").read_text(encoding="utf-8")).get("test", {})
        w, h = raw.get("window", (None, None))
        return "%dx%d" % (int(w), int(h)) if w and h else None
    except (OSError, ValueError, TypeError, AttributeError):
        return None


def suite_of(run, suite_dir, test, result):
    """The suite column of a run's rows: where the test lives under tests/ (the runner's test_path), else, for a test that is
    written into the run's folder, its tour ("tour", "tour-<disc>", "tour@WxH" when it asks for a window), else the folder
    the runner filed the run under."""
    path = result.get("test_path")
    if isinstance(path, str) and "/" in path:
        return path.rsplit("/", 1)[0]
    m = TOUR_NAME.match(test)
    if m:
        return ("tour-" + m.group(1) if m.group(1) else "tour") + (("@" + window_of(run)) if window_of(run) else "")
    return LEGACY_SUITES.get(suite_dir, suite_dir)


def runs_in(work, renderer=None, place="auto"):
    """The Runs under a luma_test.py work folder."""
    found = []
    for result_file in sorted(pathlib.Path(work).rglob("result.json")):
        run = result_file.parent
        if run.parent.parent.parent.name != "runs":
            continue
        result = json.loads(result_file.read_text(encoding="utf-8"))
        r = result.get("renderer") or run.name
        if renderer and r != renderer:
            continue
        test = result.get("test") or run.parent.name
        found.append(Run(suite_of(run, run.parent.parent.name, test, result), test, r, place_of(result, place), run, result))
    return found


def shown_pictures(run):
    folder = run / "pictures"
    return sorted(p for p in folder.glob("*.png") if not p.name.endswith(".display.png")) if folder.is_dir() else []


# --- Declared changes ----------------------------------------------------------------------------------------------------


class DeclarationError(Exception):
    pass


DECLARATION_FORM = ("`<suite>/<stop> <picture> <place> <renderer> <disc> :: <reason>` (five words, then the reason; `*` is any: "
                    "for example `tour/010-CannonFleetGalaxy-1 arrive pc vulkan RMGE01 :: the sky is another blue`)")
DISCS = ("RMGE01", "RMGP01", "RMGJ01")
US_DISC = "RMGE01"
DISC_SUFFIX = re.compile(r"-(RMGP01|RMGJ01)$")
DISC_PREFIX = re.compile(r"^(RMGP01|RMGJ01)-")


def disc_of(suite, test=""):
    """The disc a picture belongs to: the other discs' tours are suites `tour-<disc>`; every other test is the US disc's."""
    m = DISC_SUFFIX.search(suite.split("@", 1)[0])
    return m.group(1) if m else US_DISC


def stop_name(suite, test):
    """The suite and test a declaration names a picture by: the suite without its disc and the test without its disc prefix
    (`tour-RMGJ01` / `RMGJ01-010-CannonFleetGalaxy-1` is `tour/010-CannonFleetGalaxy-1`)."""
    base, at, window = suite.partition("@")
    return "%s%s%s/%s" % (DISC_SUFFIX.sub("", base), at, window, DISC_PREFIX.sub("", test))


def read_declarations(path):
    """[(line number, test pattern, picture pattern, place pattern, renderer pattern, disc pattern, reason)] of a PICTURES-CHANGE.txt
    (none for a file that is not there). A line that is not `<suite>/<stop> <picture> <place> <renderer> <disc> :: <reason>` is a
    DeclarationError (a line of the form before places and discs were named, three words, says so)."""
    path = pathlib.Path(path) if path else None
    if path is None or not path.is_file():
        return []
    out = []
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        head, sep, reason = line.partition(" :: ")
        words = head.split()
        if not sep or len(words) != 5 or not reason.strip():
            raise DeclarationError("%s line %d: expected %s%s" % (
                path.name, n, DECLARATION_FORM,
                "; a line of three words names no place and no disc" if sep and len(words) == 3 else ""))
        out.append((n,) + tuple(words) + (reason.strip(),))
    return out


def any_pattern(value, pattern):
    return pattern in ("*", "all") or fnmatch.fnmatchcase(value, pattern)


def declared_by(declarations, place, suite, test, picture, renderer):
    """The declarations that name a picture (all of them, to mark each used)."""
    named, disc = stop_name(suite, test), disc_of(suite, test)
    hits = []
    for d in declarations:
        _, test_pattern, picture_pattern, place_pattern, renderer_pattern, disc_pattern, _ = d
        if (fnmatch.fnmatchcase(named, test_pattern) and fnmatch.fnmatchcase(picture, picture_pattern)
                and any_pattern(place, place_pattern) and any_pattern(renderer, renderer_pattern) and any_pattern(disc, disc_pattern)):
            hits.append(d)
    return hits


def declared_stops(declarations, tour_tests):
    """The stops of the US tour (`tour_tests`: the test names NNN-Stage-N, in the tour's order) that a declaration names, in the
    tour's order: the stops whose pictures the landing must play on every renderer and disc that has a row for them."""
    return [t for t in tour_tests if any(fnmatch.fnmatchcase("tour/" + t, d[1]) for d in declarations)]


# A line that a step's landing used (mend M-006: a step plays the 12 quick stops) ends with this mark on main until the section's full
# check, whose approval removes the file: the pictures it named there are approved already, so that check does not call it stale.
STEP_MARK = "[approved at a step's landing]"


def used_lines(entries):
    """The line numbers of PICTURES-CHANGE.txt that a differing picture of `entries` (a landing's new-pictures lines) uses."""
    return {int(n) for e in entries if e["kind"] == "declared" for n in str(e["declaration"]).split(",") if n}


def unused_declarations(changes_path, entries):
    """The declarations (line, text) that no differing picture of `entries` uses and that no step's landing used before (STEP_MARK)."""
    used = used_lines(entries)
    return [(d[0], "%s %s %s %s %s :: %s" % (d[1:5] + (d[5], d[6]))) for d in read_declarations(changes_path)
            if d[0] not in used and not d[6].endswith(STEP_MARK)]


def mark_used(changes_path, used):
    """Ends each line of PICTURES-CHANGE.txt numbered in `used` with STEP_MARK (once), the file's other lines as they are."""
    path = pathlib.Path(changes_path)
    lines = path.read_bytes().decode("utf-8").splitlines(True)     # the file's own line ends kept
    for i, line in enumerate(lines):
        body = line.rstrip("\r\n")
        if i + 1 in used and not body.endswith(STEP_MARK):
            lines[i] = body + " " + STEP_MARK + line[len(body):]
    path.write_text("".join(lines), encoding="utf-8", newline="")


# --- Judging -------------------------------------------------------------------------------------------------------------


def judge(args, manifest=None):
    """Every picture of the runs, judged: [dict] with kind (same, new, declared, changed, missing, machine), key (the listing's
    name), and for pictures their place, suite, test, renderer, picture, png, w, h, sha, machine, old (the approved row's
    sha or None), message."""
    import numpy as np
    machines, approved = manifest if manifest is not None else read_manifest(args.manifest)
    declarations = read_declarations(getattr(args, "changes", None))
    runs = runs_in(args.runs, args.renderer, args.place)
    items, seen, other_machine = [], set(), set()
    for place, renderer in sorted({(r.place, r.renderer) for r in runs}):
        here = {r.machine for r in runs if (r.place, r.renderer) == (place, renderer)}
        want = machines.get((place, renderer))
        if want is None:
            items.append({"kind": "machine", "key": renderer, "message": "no approved pictures for this renderer at %s" % place})
            other_machine.add((place, renderer))
        elif here != {want}:
            items.append({"kind": "machine", "key": renderer, "message": "approved on %s driver %s; these runs: %s" % (
                want[0], want[1], ", ".join("%s driver %s" % h for h in sorted(here)))})
            other_machine.add((place, renderer))
    # The pixels of another adapter or driver may differ for that reason alone: they are not judged, the machine line says so.
    runs = [r for r in runs if (r.place, r.renderer) not in other_machine]
    for r in runs:
        for png in shown_pictures(r.folder):
            key = (r.suite, r.test, r.renderer, png.stem)
            seen.add((r.place,) + key)
            w, h, rgb = load_image(png)
            sha = pixel_hash(w, h, rgb)
            have = approved.get((r.place,) + key)
            item = {"kind": "same", "key": "/".join(key), "place": r.place, "suite": r.suite, "test": r.test,
                    "renderer": r.renderer, "picture": png.stem, "png": png, "w": w, "h": h, "sha": sha, "machine": r.machine,
                    "old": have[2] if have else None, "message": "", "declaration": [], "approved": None, "diff": None}
            items.append(item)
            if have is not None and have[2] == sha:
                continue
            if have is None:
                item.update(kind="new", message="no approved picture")
                continue
            hits = declared_by(declarations, r.place, r.suite, r.test, png.stem, r.renderer)
            source = store_path(args.store, have[2])
            if not source.exists():
                item.update(kind="changed", message="the approved picture %s is not in the store %s" % (have[2][:12], args.store))
            elif (have[0], have[1]) != (w, h):
                item.update(kind="changed", message="size %dx%d, approved %dx%d" % (w, h, have[0], have[1]), approved=source)
            else:
                a = np.frombuffer(load_image(source)[2], dtype=np.uint8).reshape(h, w, 3).astype(np.int16)
                b = np.frombuffer(rgb, dtype=np.uint8).reshape(h, w, 3).astype(np.int16)
                d = np.abs(a - b).max(axis=2)
                from PIL import Image
                item.update(kind="changed", approved=source, diff=Image.fromarray(np.clip(d * 16, 0, 255).astype(np.uint8), "L"),
                            message="%d of %d pixels, largest difference %d" % (int((d > 0).sum()), w * h, int(d.max())))
            if hits:
                item["declaration"] = [d[0] for d in hits]
                item.update(kind="declared", message="declared: %s (%s line %d)" % (hits[0][6], CHANGES_NAME, hits[0][0])
                            + "; " + item["message"])
    if getattr(args, "complete", False):
        suites = {(r.place, r.suite, r.renderer) for r in runs}
        for place, suite, test, renderer, name in sorted(approved):
            if (place, suite, renderer) in suites and (place, suite, test, renderer, name) not in seen:
                items.append({"kind": "missing", "key": "/".join((suite, test, renderer, name)),
                              "message": "approved, but the runs took no such picture"})
    return items


def check(args):
    try:
        items = judge(args)
    except DeclarationError as error:
        print("declarations: %s" % error)
        return 2
    for item in items:
        print("%-8s %s%s" % (item["kind"], item["key"], "  " + item["message"] if item["message"] else ""))
    judged = [i for i in items if i["kind"] != "machine"]
    same = sum(1 for i in judged if i["kind"] == "same")
    new = [i for i in items if i["kind"] in ("new", "declared")]
    print("pictures: %d of %d the same%s" % (same, len(judged), "; %d new or declared" % len(new) if new else ""))
    review = [i for i in items if i["kind"] in ("new", "declared", "changed")]
    if review:
        out = pathlib.Path(args.out) if args.out else pathlib.Path(args.runs) / "pictures-review"
        write_review(out, review, "python -X utf8 -I tools/gate/pictures.py approve --runs %s" % args.runs)
        print("review: %s" % (out / "review.html"))
    if getattr(args, "new_out", None):
        if new:
            write_entries(pathlib.Path(args.new_out), new)
        else:
            shutil.rmtree(args.new_out, ignore_errors=True)
    return 1 if any(i["kind"] in KINDS_BAD + ("machine",) for i in items) else 0


def write_entries(out, items):
    """<out>/entries.tsv: a line for each new or declared picture, and its file under <out>/files/."""
    if out.exists():
        shutil.rmtree(out)
    (out / "files").mkdir(parents=True)
    lines = ["\t".join(ENTRY_COLUMNS)]
    for i in items:
        name = i["sha"][:16] + ".png"
        shutil.copyfile(i["png"], out / "files" / name)
        lines.append("\t".join(str(c).replace("\t", " ").replace("\n", " ") for c in (
            i["kind"], i["place"], i["suite"], i["test"], i["renderer"], i["picture"], i["w"], i["h"], i["sha"],
            i["machine"][0], i["machine"][1], i["old"] or "-", ",".join(str(n) for n in i["declaration"]) or "-",
            i["message"] if i["kind"] == "declared" else "no approved picture", "files/" + name)))
    (out / "entries.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")


def read_entries(path):
    """The entries of an entries.tsv / new-pictures.tsv: [dict by ENTRY_COLUMNS], the file column as written."""
    path = pathlib.Path(path)
    if not path.is_file():
        return []
    rows = [l.split("\t") for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
    return [dict(zip(ENTRY_COLUMNS, r)) for r in rows[1:] if len(r) == len(ENTRY_COLUMNS)]


def gather(work, store=STORE, folder="new-pictures"):
    """A run's new and declared pictures, from every check's <work>/<folder>/<check>/entries.tsv: <work>/new-pictures.tsv (the
    entries, the file column relative to <work>), their files in <work>/<folder>/files, and the sheet to look at,
    <work>/<folder>/sheet/review.html (a declared picture beside the one it replaces). Returns the entries; with none,
    nothing is written (and an old file is removed)."""
    work = pathlib.Path(work)
    base = work / folder
    entries = []
    for entries_file in sorted(base.glob("*/entries.tsv")) if base.is_dir() else []:
        for e in read_entries(entries_file):
            e["source"] = entries_file.parent / e["file"]
            entries.append(e)
    if not entries:
        (work / "new-pictures.tsv").unlink(missing_ok=True)
        return []
    (base / "files").mkdir(parents=True, exist_ok=True)
    lines, items = ["\t".join(ENTRY_COLUMNS)], []
    for e in entries:
        name = e["sha256"][:16] + ".png"
        if not (base / "files" / name).exists():
            shutil.copyfile(e["source"], base / "files" / name)
        e["file"] = "%s/files/%s" % (folder, name)
        lines.append("\t".join(str(e[c]) for c in ENTRY_COLUMNS))
        old = store_path(store, e["old_sha256"]) if e["old_sha256"] != "-" else None
        items.append({"key": "%s/%s/%s/%s%s" % (e["suite"], e["test"], e["renderer"], e["picture"],
                                                 "" if e["place"] == "pc" else " (%s)" % e["place"]),
                      "new": base / "files" / name, "approved": old if old is not None and old.exists() else None, "diff": None,
                      "note": e["reason"] if e["kind"] == "declared" else ""})
    (work / "new-pictures.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    write_review(base / "sheet", items, "the gate approves these when the run passes and lands the change "
                 "(one commit of tests/pictures/approved.tsv); a declared one replaces its old row")
    return entries


def write_review(out, items, text):
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    parts = ["<!doctype html><meta charset=utf-8><title>Pictures to review</title>",
             "<style>body{font:14px sans-serif;background:#222;color:#ddd}img{width:32%;margin:0 .5%}"
             "h2{font-size:15px;margin:24px 0 6px}</style>",
             "<p>Approved, new, difference (times 16). To approve every picture here:<br><code>%s</code></p>" % html.escape(text)]
    for n, item in enumerate(items):
        name = "%03d" % n
        new = item["new"] if "new" in item else item["png"]
        shutil.copyfile(new, out / (name + "-new.png"))
        cells = []
        if item.get("approved") is not None:
            shutil.copyfile(item["approved"], out / (name + "-approved.png"))
            cells.append(name + "-approved.png")
        cells.append(name + "-new.png")
        if item.get("diff") is not None:
            item["diff"].save(out / (name + "-diff.png"))
            cells.append(name + "-diff.png")
        note = item.get("note") or item.get("message") or ""
        parts.append("<h2>%s%s</h2>%s" % (html.escape(item["key"]), (" :: " + html.escape(note)) if note else "",
                                         "".join('<img src="%s">' % c for c in cells)))
    (out / "review.html").write_text("\n".join(parts) + "\n", encoding="utf-8")


# --- Approving -----------------------------------------------------------------------------------------------------------


def approve(args):
    machines, approved = read_manifest(args.manifest)
    only = set(args.only or [])
    taken, refused = 0, []

    def chosen_of(run):
        return [p for p in shown_pictures(run.folder)
                if not only or run.test in only or "%s/%s" % (run.test, p.stem) in only]

    runs = [r for r in runs_in(args.runs, args.renderer, args.place) if chosen_of(r)]
    # One adapter and driver per place and renderer: runs of two are never approved together.
    drawn_on = {}
    for r in runs:
        drawn_on.setdefault((r.place, r.renderer), set()).add(r.machine)
    for (place, renderer), machines_here in sorted(drawn_on.items()):
        if len(machines_here) > 1:
            print("refused  %s: runs of more than one adapter or driver: %s" % (
                renderer, ", ".join("%s driver %s" % m for m in sorted(machines_here))))
            return 1
    for r in runs:
        chosen = chosen_of(r)
        if r.result.get("verdict") != "pass":
            refused.append("%s/%s: the run did not pass (%s)" % (r.test, r.renderer, r.result.get("verdict")))
            continue
        here = r.machine
        if machines.get((r.place, r.renderer), here) != here and not args.new_machine:
            refused.append("%s/%s: drawn on %s driver %s, the approved pictures at %s are of %s driver %s "
                           "(--new-machine replaces them)" % (r.test, r.renderer, here[0], here[1], r.place,
                                                              *machines[(r.place, r.renderer)]))
            continue
        machines[(r.place, r.renderer)] = here
        for png in chosen:
            w, h, rgb = load_image(png)
            sha = pixel_hash(w, h, rgb)
            key = (r.place, r.suite, r.test, r.renderer, png.stem)
            if args.new and key in approved:
                continue
            put_in_store(args.store, png, sha)
            if approved.get(key, (0, 0, ""))[2] != sha:
                approved[key] = (w, h, sha)
                taken += 1
                print("approved %s" % "/".join(key[1:]))
    write_manifest(args.manifest, machines, approved)
    for line in refused:
        print("refused  %s" % line)
    print("approved %d picture%s; %d run%s refused" % (taken, "" if taken == 1 else "s", len(refused),
                                                       "" if len(refused) == 1 else "s"))
    return 1 if refused else 0


def apply_entries(machines, approved, entries, store, base):
    """The manifest's machines and pictures with a landing's entries in them (a declared one replaces its old row), their
    files put in the store (under their hash, which is checked). `base`: the folder the entries' file column is relative to.
    Returns the number of rows set."""
    count = 0
    for e in entries:
        png = pathlib.Path(base) / e["file"]
        w, h, rgb = load_image(png)
        sha = pixel_hash(w, h, rgb)
        if sha != e["sha256"] or (w, h) != (int(e["width"]), int(e["height"])):
            raise ValueError("%s/%s/%s: its file is not the picture the entry names" % (e["suite"], e["test"], e["picture"]))
        machine = (e["place"], e["renderer"])
        if machines.get(machine) != (e["adapter"], e["driver"]):
            raise ValueError("%s/%s: the picture was drawn on %s driver %s, the rows name %s" % (
                e["test"], e["renderer"], e["adapter"], e["driver"],
                "no machine" if machine not in machines else "%s driver %s" % machines[machine]))
        put_in_store(store, png, sha)
        approved[(e["place"], e["suite"], e["test"], e["renderer"], e["picture"])] = (w, h, sha)
        count += 1
    return count


def tests_line(entries, limit=5):
    names = sorted({e["test"] for e in entries})
    shown = ", ".join(names[:limit])
    return shown + (" and %d more" % (len(names) - limit) if len(names) > limit else "")


def approve_landing(tree, work, store, recheck, git, say=print, message_file=None, carry=False):
    """The approval of a landing: the new and declared pictures a run left (<work>/new-pictures.tsv) become approved. One
    commit on top of the tested commit that touches only tests/pictures/approved.tsv and PICTURES-CHANGE.txt (removed), plain,
    with the repository's own identity: "pictures: N approved for <the tests>"; then `recheck()` runs the picture checks again
    on the same run folders and returns [(check, state)]; every state must be pass. Returns (sha, None) of the new commit,
    (None, None) when the run left nothing to approve, or (None, why) with the tree back at the tested commit (the pictures
    are in the store: files under their hash do no harm). `carry` (a step's landing, mend M-006): PICTURES-CHANGE.txt is not removed;
    each line a picture used is marked (STEP_MARK) and the file stays on main for the section's full check, whose approval removes it."""
    tree, work = pathlib.Path(tree), pathlib.Path(work)
    entries = read_entries(work / "new-pictures.tsv")
    if not entries:
        return None, None

    def sh(*a):
        return subprocess.run([git, "-C", str(tree), *a], capture_output=True, text=True)

    tested = sh("rev-parse", "HEAD").stdout.strip()
    if not tested:
        return None, "the tree has no commit to put the approval on"
    if sh("status", "--porcelain").stdout.strip():
        return None, "the tree has uncommitted changes: the approval is one commit of the two picture files"
    manifest = tree / MANIFEST_NAME
    changes = tree / CHANGES_NAME

    def undo(why):
        sh("reset", "--hard", "-q", tested)
        return None, why

    try:
        machines, approved = read_manifest(manifest)
        count = apply_entries(machines, approved, entries, store, work)
        write_manifest(manifest, machines, approved)
    except (ValueError, OSError, SystemExit) as error:
        return undo("the pictures could not be approved: %s" % error)
    paths = [MANIFEST_NAME]
    if changes.exists() and carry:
        try:
            mark_used(changes, used_lines(entries))
        except (OSError, UnicodeError) as error:
            return undo("%s could not be marked: %s" % (CHANGES_NAME, error))
        sh("add", "--", CHANGES_NAME)
        paths.append(CHANGES_NAME)
    elif changes.exists():
        if sh("ls-files", "--error-unmatch", CHANGES_NAME).returncode == 0:
            sh("rm", "-q", "-f", CHANGES_NAME)
        else:
            changes.unlink()
        paths.append(CHANGES_NAME)
    sh("add", "--", MANIFEST_NAME)
    subject = "pictures: %d approved for %s" % (count, tests_line(entries))
    if message_file is None:
        message_file = work / "approval-message.txt"
    pathlib.Path(message_file).write_text(subject + "\n", encoding="utf-8")
    done = sh("commit", "-q", "-F", str(message_file))
    if done.returncode != 0:
        return undo("the approval commit failed: %s" % (done.stderr or done.stdout).strip()[-300:])
    touched = sh("diff", "--name-only", tested, "HEAD").stdout.split()
    if sorted(touched) != sorted(set(touched) & set(paths)) or MANIFEST_NAME not in touched:
        return undo("the approval commit touches %s, not just the two picture files" % ", ".join(touched))
    states = recheck()
    bad = [(name, state) for name, state in states if state != "pass"]
    if bad or not states:
        return undo("the picture checks, run again with the approved pictures, are not all pass: %s" % (
            ", ".join("%s %s" % b for b in bad[:6]) if bad else "none ran"))
    sha = sh("rev-parse", "HEAD").stdout.strip()
    say("pictures: %d approved for %s (%s)" % (count, tests_line(entries), sha[:9]))
    return sha, None


# --- A driver change -----------------------------------------------------------------------------------------------------


def driver_update(args):
    import numpy as np
    machines, approved = read_manifest(args.manifest)
    runs = runs_in(args.runs, args.renderer, args.place)
    if not runs:
        print("driver-update: no run under %s" % args.runs)
        return 1
    drawn_on = {}
    for r in runs:
        drawn_on.setdefault((r.place, r.renderer), set()).add(r.machine)
    for (place, renderer), here in sorted(drawn_on.items()):
        if len(here) > 1:
            print("refused  %s at %s: runs of more than one adapter or driver: %s" % (
                renderer, place, ", ".join("%s driver %s" % m for m in sorted(here))))
            return 1
    ok, listed, news = 0, [], 0
    for (place, renderer), here in sorted(drawn_on.items()):
        (adapter, driver), = here
        want = machines.get((place, renderer))
        if want == (adapter, driver):
            print("driver-update: %s at %s: the rows already name %s driver %s; nothing to do" % (renderer, place, adapter, driver))
            continue
        if want is None:
            listed.append("%s at %s: no approved machine to compare with" % (renderer, place))
            continue
        if maker_of(want[0]) != maker_of(adapter):
            listed.append("%s at %s: the approved machine is %s, this is %s (another card maker: approve with "
                          "--new-machine after looking)" % (renderer, place, want[0], adapter))
            continue
        print("driver-update: %s at %s: %s driver %s -> %s driver %s" % (renderer, place, want[0], want[1], adapter, driver))
        machines[(place, renderer)] = (adapter, driver)
        for r in (x for x in runs if (x.place, x.renderer) == (place, renderer)):
            if r.result.get("verdict") != "pass":
                for png in shown_pictures(r.folder):
                    listed.append("%s/%s/%s/%s: the run did not pass (%s)" % (r.suite, r.test, r.renderer, png.stem,
                                                                              r.result.get("verdict")))
                continue
            for png in shown_pictures(r.folder):
                key = (place, r.suite, r.test, renderer, png.stem)
                name = "/".join(key[1:])
                w, h, rgb = load_image(png)
                sha = pixel_hash(w, h, rgb)
                have = approved.get(key)
                if have is None:
                    news += 1
                    print("new      %s  no approved picture; the usual way approves it" % name)
                    continue
                source = store_path(args.store, have[2])
                if have[2] == sha:
                    ok += 1
                    continue
                if not source.exists():
                    listed.append("%s: the approved picture %s is not in the store" % (name, have[2][:12]))
                    continue
                if (have[0], have[1]) != (w, h):
                    listed.append("%s: size %dx%d, approved %dx%d" % (name, w, h, have[0], have[1]))
                    continue
                a = np.frombuffer(load_image(source)[2], dtype=np.uint8).reshape(h, w, 3).astype(np.int16)
                b = np.frombuffer(rgb, dtype=np.uint8).reshape(h, w, 3).astype(np.int16)
                largest = int(np.abs(a - b).max())
                if largest <= 1:
                    put_in_store(args.store, png, sha)
                    approved[key] = (w, h, sha)
                    ok += 1
                    print("approved %s  largest difference %d" % (name, largest))
                else:
                    listed.append("%s: largest difference %d (over 1)" % (name, largest))
    write_manifest(args.manifest, machines, approved)
    for line in listed:
        print("FAIL     %s" % line)
    print("driver-update: %d picture%s approved for the new machine, %d listed as FAIL" % (ok, "" if ok == 1 else "s", len(listed)))
    return 1 if listed else 0


def main(argv=None):
    # A self-test that starts this tool as a child process runs it under the live-state guard (tools/gate/selftest_guard.py).
    import pathlib as _pathlib
    import sys as _sys
    _gate = str(_pathlib.Path(__file__).resolve().parents[1] / "gate")
    _sys.path.insert(0, _gate)
    try:
        import selftest_guard
    finally:
        _sys.path.remove(_gate)
    selftest_guard.install_if_asked()
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("command", choices=("check", "approve", "driver-update"))
    p.add_argument("--runs", required=True, help="a luma_test.py work folder")
    p.add_argument("--place", default="auto", help="where the runs were drawn: pc or tablet; by default each run's platform says")
    p.add_argument("--renderer", help="only this renderer's runs")
    p.add_argument("--manifest", type=pathlib.Path, default=MANIFEST)
    p.add_argument("--store", default=STORE)
    p.add_argument("--out", help="check: the review folder")
    p.add_argument("--new-out", help="check: where the new and declared pictures go (entries.tsv and files/)")
    p.add_argument("--changes", help="check: the branch's PICTURES-CHANGE.txt (default: none)")
    p.add_argument("--complete", action="store_true", help="check: an approved picture the runs did not take fails")
    p.add_argument("--only", nargs="*", help="approve: these tests, or test/picture names")
    p.add_argument("--new", action="store_true", help="approve: only the pictures that have no approved row")
    p.add_argument("--new-machine", action="store_true", help="approve: replace a renderer's adapter and driver")
    args = p.parse_args(argv)
    return {"check": check, "approve": approve, "driver-update": driver_update}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
