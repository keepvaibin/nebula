"""look_audit.py REF NEW OUT [--control REF2] [--level N] [--area N]: what a person would see differ, stop by stop.

Every folder holds a run.log and present_N.ppm pictures of one galaxy tour; a picture belongs to the tour line before it, so builds with different timing compare by stop and scene.
A NEW picture is compared with the closest REF picture of its stop in coarse cells (64x36, mean colour): a blob of cells over the level is flagged, and so is a large flat block of one colour that REF does not have there.
--control is a second run of the REF build: its blobs against REF set the area each stop must beat, so moving things and timing do not count.
OUT gets flagged.tsv, one side-by-side sheet per flagged stop (REF, NEW, cells over the level in red, flat blocks in yellow) and pages of up to four sheets.
"""

import argparse
import glob
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

CELLS_W, CELLS_H = 64, 36
FLAT_CELL = 8
FLAT_RANGE = 2
FLAT_MIN = 240
FLAT_REF_NEAR = 16
SHEET_W, SHEET_H = 480, 270

TOUR_LINE = re.compile(r"(?:tour:|\[galaxy-tour\]) (\d+)/\d+ (\S+) (\S+)")
PICTURE_LINE = re.compile(r"\[backbuffer-dump\] wrote .*[\\/](present_\d+\.ppm)")


def stops_of(folder):
    """{stop: (galaxy, scenario, [picture names])}; stop 0 is what comes before the first tour line."""
    stops = {0: ("boot", "-", [])}
    now = 0
    with open(os.path.join(folder, "run.log"), encoding="utf-8", errors="replace") as f:
        for line in f:
            m = TOUR_LINE.search(line)
            if m:
                now = int(m.group(1))
                stops[now] = (m.group(2), m.group(3), [])
                continue
            m = PICTURE_LINE.search(line)
            if m and os.path.exists(os.path.join(folder, m.group(1))):
                stops[now][2].append(m.group(1))
    return stops


def load(folder, name, cache={}):
    key = (folder, name)
    if key not in cache:
        a = np.asarray(Image.open(os.path.join(folder, name)).convert("RGB"), dtype=np.int16)
        h, w = a.shape[:2]
        cw, ch = w // CELLS_W, h // CELLS_H
        cells = a[: ch * CELLS_H, : cw * CELLS_W].reshape(CELLS_H, ch, CELLS_W, cw, 3).mean(axis=(1, 3))
        fh, fw = h // FLAT_CELL, w // FLAT_CELL
        fine = a[: fh * FLAT_CELL, : fw * FLAT_CELL].reshape(fh, FLAT_CELL, fw, FLAT_CELL, 3)
        spread = (fine.max(axis=(1, 3)) - fine.min(axis=(1, 3))).max(axis=2)
        cache[key] = (cells, spread <= FLAT_RANGE, fine.mean(axis=(1, 3)))
    return cache[key]


def components(mask):
    """Boxes of the connected parts of mask, largest first: (cells, x0, y0, x1, y1)."""
    seen = np.zeros(mask.shape, dtype=bool)
    h, w = mask.shape
    out = []
    for y0, x0 in zip(*np.nonzero(mask)):
        if seen[y0, x0]:
            continue
        seen[y0, x0] = True
        todo = [(y0, x0)]
        n = 0
        box = [x0, y0, x0, y0]
        while todo:
            y, x = todo.pop()
            n += 1
            box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
            for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    todo.append((ny, nx))
        out.append((n, int(box[0]), int(box[1]), int(box[2]), int(box[3])))
    out.sort(reverse=True)
    return out


def flat_blocks(new, refs):
    """Flat blocks of one colour in NEW that no REF picture has there: [(cells, x0, y0, x1, y1, colour)]."""
    _, flat, colour = new
    key = (colour // 8).astype(np.int32)
    key = key[..., 0] * 65536 + key[..., 1] * 256 + key[..., 2]
    out = []
    for k in np.unique(key[flat]):
        mask = flat & (key == k)
        if mask.sum() < FLAT_MIN:
            continue
        for part in components(mask):
            if part[0] < FLAT_MIN:
                break
            n, x0, y0, x1, y1 = part
            region = mask[y0 : y1 + 1, x0 : x1 + 1]
            c = colour[y0 : y1 + 1, x0 : x1 + 1][region].mean(axis=0)
            near = 0.0
            for _, _, ref_colour in refs:
                d = np.abs(ref_colour[y0 : y1 + 1, x0 : x1 + 1][region] - c).max(axis=1)
                near = max(near, float((d <= FLAT_REF_NEAR).mean()))
            if near < 0.5:
                out.append((n, x0, y0, x1, y1, tuple(int(v) for v in c)))
    out.sort(reverse=True)
    return out


def compare_stop(ref_dir, ref_names, new_dir, new_names, level):
    """The worst NEW picture of a stop against its closest REF picture."""
    worst = None
    refs = [load(ref_dir, n) for n in ref_names]
    for name in new_names:
        new = load(new_dir, name)
        best = None
        for ref_name, ref in zip(ref_names, refs):
            mask = np.abs(ref[0] - new[0]).max(axis=2) > level
            parts = components(mask)
            blob = parts[0][0] if parts else 0
            if best is None or blob < best["blob"]:
                best = {"blob": blob, "box": parts[0][1:] if parts else None, "ref": ref_name, "mask": mask}
        best["new"] = name
        best["flat"] = flat_blocks(new, refs)
        flat = best["flat"][0][0] if best["flat"] else 0
        rank = (best["blob"], flat)
        if worst is None or rank > (worst["blob"], worst["flat"][0][0] if worst["flat"] else 0):
            worst = best
    return worst


def sheet(path, title, ref_dir, new_dir, row):
    ref = Image.open(os.path.join(ref_dir, row["ref"])).convert("RGB").resize((SHEET_W, SHEET_H))
    new = Image.open(os.path.join(new_dir, row["new"])).convert("RGB")
    sx, sy = SHEET_W / new.width, SHEET_H / new.height
    new = new.resize((SHEET_W, SHEET_H))
    over = new.copy()
    draw = ImageDraw.Draw(over)
    cw, ch = SHEET_W / CELLS_W, SHEET_H / CELLS_H
    for y, x in zip(*np.nonzero(row["mask"])):
        draw.rectangle((x * cw, y * ch, (x + 1) * cw - 1, (y + 1) * ch - 1), outline=(255, 0, 0))
    for _, x0, y0, x1, y1, _ in row["flat"]:
        draw.rectangle((x0 * FLAT_CELL * sx, y0 * FLAT_CELL * sy, (x1 + 1) * FLAT_CELL * sx, (y1 + 1) * FLAT_CELL * sy), outline=(255, 255, 0), width=2)
    out = Image.new("RGB", (SHEET_W * 3, SHEET_H + 16), (0, 0, 0))
    ImageDraw.Draw(out).text((2, 2), title, fill=(255, 255, 255))
    out.paste(ref, (0, 16))
    out.paste(new, (SHEET_W, 16))
    out.paste(over, (SHEET_W * 2, 16))
    out.save(path)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("new")
    ap.add_argument("out")
    ap.add_argument("--control")
    ap.add_argument("--level", type=int, default=40)
    ap.add_argument("--area", type=int, default=6)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for old in glob.glob(os.path.join(a.out, "*.png")):
        os.remove(old)

    ref = stops_of(a.ref)
    new = stops_of(a.new)
    limit = {}
    floor = a.area
    if a.control:
        control = stops_of(a.control)
        blobs = {}
        for s, (galaxy, scenario, names) in control.items():
            if names and s in ref and ref[s][2]:
                blobs[s] = compare_stop(a.ref, ref[s][2], a.control, names, a.level)["blob"]
        if blobs:
            floor = max(a.area, int(np.percentile(list(blobs.values()), 90)))
            limit = {s: max(floor, int(b * 1.5)) for s, b in blobs.items()}
        print("control: %d stops, largest blob median %d, 90th %d, max %d; floor %d cells at level %d" % (len(blobs), np.median(list(blobs.values())), np.percentile(list(blobs.values()), 90), max(blobs.values()), floor, a.level))
    else:
        print("no control: floor %d cells at level %d" % (floor, a.level))

    rows = []
    pictures = 0
    for s in sorted(new):
        galaxy, scenario, names = new[s]
        pictures += len(names)
        if not names:
            continue
        if s not in ref or not ref[s][2]:
            print("stop %d %s %s: no REF pictures" % (s, galaxy, scenario))
            continue
        if ref[s][0] != galaxy or ref[s][1] != scenario:
            print("stop %d: NEW %s %s, REF %s %s" % (s, galaxy, scenario, ref[s][0], ref[s][1]))
            continue
        row = compare_stop(a.ref, ref[s][2], a.new, names, a.level)
        if row["blob"] > limit.get(s, floor) or row["flat"]:
            row.update(stop=s, galaxy=galaxy, scenario=scenario, limit=limit.get(s, floor))
            rows.append(row)

    with open(os.path.join(a.out, "flagged.tsv"), "w", encoding="utf-8", newline="\n") as tsv:
        tsv.write("stop\tgalaxy\tscenario\tnew\tref\tblob_cells\tlimit\tblob_box\tflat_cells\tflat_colour\tsheet\n")
        page = []
        pages = 0
        for row in rows:
            flat = row["flat"][0] if row["flat"] else None
            title = "stop %d %s %s  REF %s | NEW %s | blob %d (limit %d) flat %s" % (row["stop"], row["galaxy"], row["scenario"], row["ref"], row["new"], row["blob"], row["limit"], flat[0] if flat else 0)
            name = "s%03d_%s_%s.png" % (row["stop"], row["galaxy"], row["scenario"])
            page.append(sheet(os.path.join(a.out, name), title, a.ref, a.new, row))
            tsv.write("%d\t%s\t%s\t%s\t%s\t%d\t%d\t%s\t%d\t%s\t%s\n" % (row["stop"], row["galaxy"], row["scenario"], row["new"], row["ref"], row["blob"], row["limit"], row["box"], flat[0] if flat else 0, flat[5] if flat else "-", name))
            if len(page) == 4 or row is rows[-1]:
                out = Image.new("RGB", (page[0].width, sum(p.height for p in page)))
                y = 0
                for p in page:
                    out.paste(p, (0, y))
                    y += p.height
                pages += 1
                out.save(os.path.join(a.out, "page_%02d.png" % pages))
                page = []
    print("pictures %d stops %d flagged %d pages %d -> %s" % (pictures, len(new), len(rows), pages, a.out))
    for row in rows:
        flat = row["flat"][0] if row["flat"] else None
        print("  stop %3d %-28s %s  blob %4d/%-4d box %-22s flat %s %s" % (row["stop"], row["galaxy"], row["scenario"], row["blob"], row["limit"], row["box"], flat[0] if flat else 0, flat[5] if flat else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
