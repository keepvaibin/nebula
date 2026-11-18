"""Whether two mod_tests.py runs of the same tests on one renderer left the same results: each test's verdict, its
pictures pixel for pixel, and its facts, without what no two runs share whatever their load: the facts a test measures
(times and costs; MEASURED), the run's own folder in a path, the harness frame a picture's record was written at (the
picture is of the game frame its record names), and the order of facts written in one frame. Used to find how many
tests may run at once (mod_tests.py --parallel): a run with N at a time must equal a run one at a time.

    same_mod_runs.py <work one at a time> <work N at a time> --renderer d3d12 [--out report.txt]

Exit 0 only when every test both runs have is the same in both. Tests in one run only are listed, not judged.
"""
import argparse
import json
import pathlib
import re
import sys

import numpy
from PIL import Image

# Facts that measure time or cost: their values are the machine's, not the game's. A key ends in one of these words
# (after the last '.' or '_').
MEASURED = re.compile(r"(^|[._])(ms|us|ns|seconds|secs|ticks|cost|time|times|elapsed|fps|rate|guard_cost|guard_time|"
                      r"handover_ms|duration|took)$|(^|[._])cost_")
# A Mods page row whose value is a measured time (its label).
MEASURED_LABELS = ("CODE TIME",)


def tests_of(work: pathlib.Path, renderer: str):
    """{label/suite/test: its run folder} for every result.json under the run's renderer folder."""
    found = {}
    root = work / renderer
    for result in root.rglob("result.json"):
        folder = result.parent
        if folder.name not in (renderer, "-"):
            continue
        rel = folder.parent.relative_to(root).as_posix()
        if "/second/" in "/" + rel + "/":
            continue
        found[rel] = folder
    return found


def facts(folder: pathlib.Path, work: pathlib.Path):
    """The test's facts as compared: measured ones left out, the run's folder written <work>, a picture's record at the
    frame it was asked for, and sorted by frame (the facts of one frame in any order)."""
    path = folder / "facts.jsonl"
    if not path.exists():
        return None
    spellings = {str(work.resolve()), str(work)}
    spellings |= {s.replace("\\", "/") for s in spellings} | {s.replace("/", "\\") for s in spellings}
    spellings |= {json.dumps(s)[1:-1] for s in list(spellings)}
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        for s in sorted(spellings, key=len, reverse=True):
            line = line.replace(s, "<work>")
        try:
            row = json.loads(line)
        except ValueError:
            out.append((float("inf"), line))
            continue
        if not isinstance(row, dict) or not isinstance(row.get("key"), str):
            out.append((float("inf"), line))
            continue
        key, value = row["key"], row.get("value")
        if MEASURED.search(key):
            continue
        if isinstance(value, dict) and value.get("label") in MEASURED_LABELS:
            continue
        frame = row.get("frame", 0)
        if key.startswith("picture.") and isinstance(value, dict) and "asked_at" in value:
            frame = value["asked_at"]
            row = {"frame": frame, "key": key, "value": value}
        out.append((frame if isinstance(frame, (int, float)) else float("inf"), json.dumps(row, sort_keys=True)))
    out.sort()
    return [line for _, line in out]


def pictures(folder: pathlib.Path):
    pics = folder / "pictures"
    if not pics.is_dir():
        return {}
    return {p.name: p for p in pics.glob("*.png") if not p.name.endswith(".display.png")}


def same_pixels(a: pathlib.Path, b: pathlib.Path):
    if a.read_bytes() == b.read_bytes():
        return True, ""
    ia, ib = Image.open(a), Image.open(b)
    if ia.size != ib.size:
        return False, "size %s / %s" % (ia.size, ib.size)
    ia, ib = ia.convert("RGBA"), ib.convert("RGBA")
    if ia.tobytes() == ib.tobytes():
        return True, ""
    d = numpy.abs(numpy.asarray(ia, dtype=numpy.int16) - numpy.asarray(ib, dtype=numpy.int16))
    return False, "%d pixels differ, by up to %d of 255" % (int(numpy.count_nonzero(d.max(axis=2))), int(d.max()))


def compare(one: pathlib.Path, many: pathlib.Path, renderer: str):
    a, b = tests_of(one, renderer), tests_of(many, renderer)
    rows = []
    for name in sorted(set(a) & set(b)):
        problems = []
        ra = json.loads((a[name] / "result.json").read_text(encoding="utf-8"))
        rb = json.loads((b[name] / "result.json").read_text(encoding="utf-8"))
        if ra.get("verdict") != rb.get("verdict"):
            problems.append("verdict %s / %s (%s)" % (ra.get("verdict"), rb.get("verdict"), rb.get("reason", "")))
        fa, fb = facts(a[name], one), facts(b[name], many)
        if (fa is None) != (fb is None):
            problems.append("facts in one run only")
        elif fa is not None and fa != fb:
            at = next((i for i, (x, y) in enumerate(zip(fa, fb)) if x != y), min(len(fa), len(fb)))
            problems.append("facts differ at line %d: %s / %s" % (at + 1, fa[at:at + 1], fb[at:at + 1]))
        pa, pb = pictures(a[name]), pictures(b[name])
        if set(pa) != set(pb):
            problems.append("pictures in one run only: %s" % sorted(set(pa) ^ set(pb)))
        for pic in sorted(set(pa) & set(pb)):
            same, why = same_pixels(pa[pic], pb[pic])
            if not same:
                problems.append("%s: %s" % (pic, why))
        rows.append((name, problems))
    only = sorted(set(a) ^ set(b))
    return rows, only


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("one", type=pathlib.Path, help="the run one at a time (mod_tests.py --work)")
    p.add_argument("many", type=pathlib.Path, help="the run with several at a time")
    p.add_argument("--renderer", required=True, choices=("d3d12", "vulkan", "d3d11"))
    p.add_argument("--out", type=pathlib.Path)
    args = p.parse_args()
    rows, only = compare(args.one, args.many, args.renderer)
    lines = []
    for name, problems in rows:
        if problems:
            lines.append("DIFFER %s: %s" % (name, "; ".join(problems)))
    for name in only:
        lines.append("ONLY-ONE %s" % name)
    differ = sum(1 for _, pr in rows if pr)
    lines.append("same_mod_runs: %d of %d tests the same; %d in one run only" % (len(rows) - differ, len(rows), len(only)))
    text = "\n".join(lines) + "\n"
    sys.stdout.write(text)
    if args.out:
        args.out.write_text(text, encoding="utf-8")
    return 0 if differ == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
