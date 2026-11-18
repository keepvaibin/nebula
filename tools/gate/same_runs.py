"""same_runs.py: whether two gate runs of one commit gave the same pictures and the same game steps (docs/plan/
04-GATE-AND-MAIN.md section 7: the proof that running the gate's parts side by side changes nothing).

    python -X utf8 -I tools/gate/same_runs.py RUN_A RUN_B

RUN_A and RUN_B are gate run folders (gate-runs/<run>). For every harness run both hold at the same place
(runs-<label>-<renderer>/runs/<suite>/<test>/<renderer>), each shown picture's pixels (pictures.py's hash) and every
game step's hash (step_hash.py) must agree. A place only one run holds is named. Exit 0 when everything both hold
agrees and at least one place was compared, 1 otherwise.
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "verify"))
import pictures  # noqa: E402
import step_hash  # noqa: E402


def places(run):
    """{place: run folder} of a gate run's harness runs."""
    run = pathlib.Path(run)
    out = {}
    for result in sorted(run.glob("runs-*/runs/*/*/*/result.json")):
        out[str(result.parent.relative_to(run)).replace("\\", "/")] = result.parent
    return out


def picture_hashes(folder):
    return {p.name: pictures.pixel_hash(*pictures.load_image(p)) for p in pictures.shown_pictures(folder)}


def compare(a, b):
    """(ok, lines)."""
    pa, pb = places(a), places(b)
    lines, bad, compared = [], [], 0
    for place in sorted(set(pa) | set(pb)):
        if place not in pa or place not in pb:
            lines.append("%-70s only in %s" % (place, "the first" if place in pa else "the second"))
            bad.append(place)
            continue
        x, y = pa[place], pb[place]
        pruned = [w for w, f in (("first", x), ("second", y)) if (f / "pruned.txt").exists()]
        if pruned:
            lines.append("%-70s pruned in the %s (gate.py --keep-runs keeps a run whole)" % (place, " and ".join(pruned)))
            bad.append(place)
            continue
        notes = []
        hx, hy = picture_hashes(x), picture_hashes(y)
        if hx != hy:
            notes.append("pictures differ: %s" % ", ".join(sorted(n for n in set(hx) | set(hy) if hx.get(n) != hy.get(n))))
        if (x / "facts.jsonl").is_file() and step_hash.read_facts(x)[0]:
            same, text = step_hash.compare_runs(x, y)
            if not same:
                notes.append("steps: " + text)
            steps = text
        else:
            steps = "no step hashes"
        compared += 1
        if notes:
            bad.append(place)
            lines.append("%-70s %s" % (place, "; ".join(notes)))
        else:
            lines.append("%-70s same: %d pictures, %s" % (place, len(hx), steps))
    lines.append("same_runs: %d of %d places the same" % (compared - len([p for p in bad if p in pa and p in pb]), compared)
                 + ("; %d in one run only" % len([p for p in bad if not (p in pa and p in pb)]) if bad else ""))
    return compared > 0 and not bad, lines


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    ok, lines = compare(sys.argv[1], sys.argv[2])
    print("\n".join(lines))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
