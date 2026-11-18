"""pictures.py: the approved pictures of the harness's tests, checked and approved.

    python -X utf8 -I tools/gate/pictures.py check   --runs <work folder> [--place P] [--renderer R] [--complete] [--out <folder>]
    python -X utf8 -I tools/gate/pictures.py approve --runs <work folder> [--place P] [--renderer R] [--only TEST[/PICTURE] ...]

A test's pictures are the shown pictures its "picture" steps took (pictures/<name>.png in each run folder of a
luma_test.py work folder; the internal display copies, *.display.png, are not judged). An approved picture is named
in tests/pictures/approved.tsv by its place, suite, test, renderer and picture name, with the hash of its pixels (SHA-256 of
"W H\\n" and the RGB rows). Its pixels are in the picture store, outside the repository, since a picture of the game is
the game's content: NEBULA_PICTURES or --store, default D:/NebulaWork/pictures, one PNG per hash.

The tolerance is none: a picture passes when its pixels are the approved picture's, bit for bit. A fixed-step run
draws the same pixels each time on one adapter and driver (measured: every tour picture of two gate runs identical on
Direct3D 12, Vulkan and Direct3D 11), so any changed pixel is a change in what the game shows. The approved pictures
belong to one adapter and driver per place and renderer (the manifest's "machine" rows): a run on another one is a
failure that says so, as its pixels may differ for that reason alone. The place is where the runs were drawn (--place,
default pc; the tablet's runs are tablet), so the tablet's Vulkan pictures are approved against its own GPU and driver
beside the PC's (N-1040). Rows written before places existed have no place column and are the PC's.

check: one row per picture: same, changed (how many pixels, the largest channel difference), new (none approved), or
missing (approved, but the run took none: only with --complete, as a quick run takes fewer). Changed and new pictures
go to <out>/review.html (default <runs>/pictures-review) beside the approved one and their difference, with the
command that approves them. Exit 1 when any picture is not the same, or the runs are of another adapter or driver.

approve: takes the pictures of the runs that passed into the store and the manifest (with --only, only those tests or
test/picture names), and the renderer's adapter and driver as its machine row. A failed run's pictures are refused.
Needs Pillow and numpy.
"""
import argparse
import hashlib
import html
import json
import os
import pathlib
import re
import shutil
import sys

TREE = pathlib.Path(__file__).resolve().parent.parent.parent
MANIFEST = TREE / "tests" / "pictures" / "approved.tsv"
STORE = os.environ.get("NEBULA_PICTURES", "D:/NebulaWork/pictures")
HEADER = ("# The approved pictures of the harness's tests (tools/gate/pictures.py). Each picture row names the SHA-256 of\n"
          "# its pixels; the pixels are in the picture store (NEBULA_PICTURES), outside the repository.\n"
          "# machine  place  renderer  adapter  driver\n"
          "# picture  place  suite  test  renderer  picture  width  height  sha256\n")


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
                pictures[tuple(cols[1:6])] = (int(cols[6]), int(cols[7]), cols[8])
            else:
                raise SystemExit("%s line %d: not a machine or picture row" % (path, n))
    return machines, pictures


def write_manifest(path, machines, pictures):
    rows = ["machine\t%s\t%s\t%s\t%s" % (k + v) for k, v in sorted(machines.items())]
    rows += ["picture\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%s" % (k + v) for k, v in sorted(pictures.items())]
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(HEADER + "\n".join(rows) + "\n")


def runs_in(work, renderer):
    """(suite, test, renderer, run folder, result) for every run under a luma_test.py work folder."""
    found = []
    for result_file in sorted(pathlib.Path(work).rglob("result.json")):
        run = result_file.parent
        if run.parent.parent.parent.name != "runs":
            continue
        result = json.loads(result_file.read_text(encoding="utf-8"))
        r = result.get("renderer") or run.name
        if renderer and r != renderer:
            continue
        found.append((run.parent.parent.name, result.get("test") or run.parent.name, r, run, result))
    return found


def shown_pictures(run):
    folder = run / "pictures"
    return sorted(p for p in folder.glob("*.png") if not p.name.endswith(".display.png")) if folder.is_dir() else []


def store_path(store, sha):
    return pathlib.Path(store) / sha[:2] / (sha + ".png")


def check(args):
    import numpy as np
    from PIL import Image
    machines, approved = read_manifest(args.manifest)
    out = pathlib.Path(args.out) if args.out else pathlib.Path(args.runs) / "pictures-review"
    rows, review, bad, seen = [], [], 0, set()
    runs = runs_in(args.runs, args.renderer)
    for renderer in sorted({r[2] for r in runs}):
        here = {(adapter_of(res), res.get("driver", "")) for _, _, rr, _, res in runs if rr == renderer}
        want = machines.get((args.place, renderer))
        if want is None:
            rows.append(("machine", renderer, "no approved pictures for this renderer at %s" % args.place))
            bad += 1
        elif here != {want}:
            rows.append(("machine", renderer, "approved on %s driver %s; these runs: %s" % (
                want[0], want[1], ", ".join("%s driver %s" % h for h in sorted(here)))))
            bad += 1
    for suite, test, renderer, run, result in runs:
        for png in shown_pictures(run):
            key = (suite, test, renderer, png.stem)
            seen.add(key)
            w, h, rgb = load_image(png)
            sha = pixel_hash(w, h, rgb)
            have = approved.get((args.place,) + key)
            if have is not None and have[2] == sha:
                rows.append(("same", "/".join(key), ""))
                continue
            bad += 1
            item = {"key": "/".join(key), "new": png, "approved": None, "diff": None}
            if have is None:
                rows.append(("new", "/".join(key), "no approved picture"))
            else:
                source = store_path(args.store, have[2])
                if not source.exists():
                    rows.append(("changed", "/".join(key), "the approved picture %s is not in the store %s" % (have[2][:12], args.store)))
                elif (have[0], have[1]) != (w, h):
                    rows.append(("changed", "/".join(key), "size %dx%d, approved %dx%d" % (w, h, have[0], have[1])))
                    item["approved"] = source
                else:
                    a = np.frombuffer(load_image(source)[2], dtype=np.uint8).reshape(h, w, 3).astype(np.int16)
                    b = np.frombuffer(rgb, dtype=np.uint8).reshape(h, w, 3).astype(np.int16)
                    d = np.abs(a - b).max(axis=2)
                    rows.append(("changed", "/".join(key), "%d of %d pixels, largest difference %d" % (
                        int((d > 0).sum()), w * h, int(d.max()))))
                    item["approved"] = source
                    item["diff"] = Image.fromarray(np.clip(d * 16, 0, 255).astype(np.uint8), "L")
            review.append(item)
    if args.complete:
        renderers = {r[2] for r in runs}
        suites = {r[0] for r in runs}
        for place, *rest in sorted(approved):
            key = tuple(rest)
            if place == args.place and key[0] in suites and key[2] in renderers and key not in seen:
                rows.append(("missing", "/".join(key), "approved, but the runs took no such picture"))
                bad += 1
    for row in rows:
        print("%-8s %s%s" % (row[0], row[1], "  " + row[2] if row[2] else ""))
    same = sum(1 for r in rows if r[0] == "same")
    print("pictures: %d of %d the same" % (same, sum(1 for r in rows if r[0] != "machine")))
    if review:
        write_review(out, review, args)
        print("review: %s" % (out / "review.html"))
    return 1 if bad else 0


def write_review(out, items, args):
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    parts = ["<!doctype html><meta charset=utf-8><title>Pictures to review</title>",
             "<style>body{font:14px sans-serif;background:#222;color:#ddd}img{width:32%;margin:0 .5%}"
             "h2{font-size:15px;margin:24px 0 6px}</style>",
             "<p>Approved, new, difference (times 16). To approve every picture here:<br><code>python -X utf8 -I "
             "tools/gate/pictures.py approve --runs %s</code></p>" % html.escape(str(args.runs))]
    for n, item in enumerate(items):
        name = "%03d" % n
        shutil.copyfile(item["new"], out / (name + "-new.png"))
        cells = []
        if item["approved"] is not None:
            shutil.copyfile(item["approved"], out / (name + "-approved.png"))
            cells.append(name + "-approved.png")
        cells.append(name + "-new.png")
        if item["diff"] is not None:
            item["diff"].save(out / (name + "-diff.png"))
            cells.append(name + "-diff.png")
        parts.append("<h2>%s</h2>%s" % (html.escape(item["key"]), "".join('<img src="%s">' % c for c in cells)))
    (out / "review.html").write_text("\n".join(parts) + "\n", encoding="utf-8")


def approve(args):
    machines, approved = read_manifest(args.manifest)
    only = set(args.only or [])
    taken, refused = 0, []
    def chosen_of(test, run):
        return [p for p in shown_pictures(run) if not only or test in only or "%s/%s" % (test, p.stem) in only]

    runs = [r for r in runs_in(args.runs, args.renderer) if chosen_of(r[1], r[3])]
    # One adapter and driver per renderer: runs of two are never approved together.
    drawn_on = {}
    for _, _, renderer, _, result in runs:
        drawn_on.setdefault(renderer, set()).add((adapter_of(result), result.get("driver", "")))
    for renderer, machines_here in sorted(drawn_on.items()):
        if len(machines_here) > 1:
            print("refused  %s: runs of more than one adapter or driver: %s" % (
                renderer, ", ".join("%s driver %s" % m for m in sorted(machines_here))))
            return 1
    for suite, test, renderer, run, result in runs:
        chosen = chosen_of(test, run)
        if result.get("verdict") != "pass":
            refused.append("%s/%s: the run did not pass (%s)" % (test, renderer, result.get("verdict")))
            continue
        here = (adapter_of(result), result.get("driver", ""))
        if machines.get((args.place, renderer), here) != here and not args.new_machine:
            refused.append("%s/%s: drawn on %s driver %s, the approved pictures at %s are of %s driver %s "
                           "(--new-machine replaces them)" % (test, renderer, here[0], here[1], args.place,
                                                              *machines[(args.place, renderer)]))
            continue
        machines[(args.place, renderer)] = here
        for png in chosen:
            w, h, rgb = load_image(png)
            sha = pixel_hash(w, h, rgb)
            target = store_path(args.store, sha)
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(png, target)
            key = (args.place, suite, test, renderer, png.stem)
            if approved.get(key, (0, 0, ""))[2] != sha:
                approved[key] = (w, h, sha)
                taken += 1
                print("approved %s" % "/".join(key[1:]))
    write_manifest(args.manifest, machines, approved)
    for r in refused:
        print("refused  %s" % r)
    print("approved %d picture%s; %d run%s refused" % (taken, "" if taken == 1 else "s", len(refused),
                                                       "" if len(refused) == 1 else "s"))
    return 1 if refused else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("command", choices=("check", "approve"))
    p.add_argument("--runs", required=True, help="a luma_test.py work folder")
    p.add_argument("--place", default="pc", help="where the runs were drawn: pc (default) or tablet")
    p.add_argument("--renderer", help="only this renderer's runs")
    p.add_argument("--manifest", type=pathlib.Path, default=MANIFEST)
    p.add_argument("--store", default=STORE)
    p.add_argument("--out", help="check: the review folder")
    p.add_argument("--complete", action="store_true", help="check: an approved picture the runs did not take fails")
    p.add_argument("--only", nargs="*", help="approve: these tests, or test/picture names")
    p.add_argument("--new-machine", action="store_true", help="approve: replace a renderer's adapter and driver")
    args = p.parse_args()
    return check(args) if args.command == "check" else approve(args)


if __name__ == "__main__":
    sys.exit(main())
