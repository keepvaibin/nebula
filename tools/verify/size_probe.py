"""size_probe.py --bin BIN --out OUT --stop "Stage N" --renderer d3d12 --sizes 1280x720 640x360 [--software 0|1] [--pak GAME.PAK]

Plays one recorded stop at each window size (hash on from step 0), names the first step where the first size and each other size differ,
then plays both again with "hash detail" at that step and lists the actors that differ (name, fields).

P-340: a recorded play compares only at the window size it was recorded at; this shows where and in which actors a run at another size
parts from it. The comparison itself is tools/verify/step_hash.py's (its facts of a run, its hash detail).
"""
import argparse
import json
import pathlib
import sys

TREE = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TREE / "tools" / "verify"))
sys.path.insert(0, str(TREE / "tools" / "gate"))
import gate_tour  # noqa: E402
import step_hash  # noqa: E402

PAK = "D:/NebulaWork/build/us-pak-37/game.pak"
FIELDS = ("name", "pos", "rot", "scale", "vel", "grav", "step")


def play(out, stop, size, renderer, bin_dir, software, paks, detail=None):
    suite = gate_tour.write(out / "tour", [stop], window=size)
    step_hash.with_hash(suite.parent, detail)
    names = json.loads(pathlib.Path(suite).read_text(encoding="utf-8"))["tests"]
    step_hash.play(suite, pathlib.Path(bin_dir), out / "run", renderer, paks, software)
    runs = step_hash.runs_by_test(out / "run")
    return runs[names[0]]


def first_step(sa, sb):
    """The first step both runs have a hash for where the hashes differ, or None."""
    return next((f for f in sorted(set(sa) & set(sb)) if sa[f] != sb[f]), None)


def differing_actors(la, lb):
    """[(index, name, the fields that differ)] of the actors the two details list at one step, side by side."""
    differ = []
    for x, y in zip(la, lb):
        if x != y:
            fields = [k for k in FIELDS if x.get(k) != y.get(k)]
            differ.append((x["i"], x["name"], fields or ["below 1/1000"]))
    return differ


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("--bin", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--stop", required=True)
    p.add_argument("--renderer", default="d3d12")
    p.add_argument("--sizes", nargs="+", required=True)
    p.add_argument("--software", type=int, default=0)
    p.add_argument("--pak", default=PAK, help="the US game.pak (default %s)" % PAK)
    a = p.parse_args(argv)
    paks = "RMGE01=%s" % a.pak
    out = pathlib.Path(a.out)
    sizes = [tuple(int(v) for v in s.split("x")) for s in a.sizes]
    runs = {}
    for size in sizes:
        runs[size] = play(out / ("%dx%d" % size), a.stop, size, a.renderer, a.bin, a.software, paks)
        result = json.loads((runs[size] / "result.json").read_text(encoding="utf-8"))
        print("%dx%d: %s %s" % (size[0], size[1], result.get("verdict"), result.get("reason", "")), flush=True)
    ref = sizes[0]
    sa, _ = step_hash.read_facts(runs[ref])
    for size in sizes[1:]:
        sb, _ = step_hash.read_facts(runs[size])
        first = first_step(sa, sb)
        print("%dx%d against %dx%d: steps %d / %d, first step that differs: %s" % (size[0], size[1], ref[0], ref[1], len(sa), len(sb), first), flush=True)
        if first is None:
            continue
        name = runs[ref].parent.name
        runs_a = play(out / ("%dx%d-detail" % ref), a.stop, ref, a.renderer, a.bin, a.software, paks, {name: first})
        runs_b = play(out / ("%dx%d-detail" % size), a.stop, size, a.renderer, a.bin, a.software, paks, {name: first})
        _, aa = step_hash.read_facts(runs_a)
        _, ab = step_hash.read_facts(runs_b)
        la, lb = aa.get(first, []), ab.get(first, [])
        print("detail at step %d: %d actors / %d actors" % (first, len(la), len(lb)))
        print(step_hash.first_actor(la, lb))
        differ = differing_actors(la, lb)
        print("actors that differ at step %d: %d of %d" % (first, len(differ), len(la)))
        for i, nm, fields in differ[:25]:
            print("   #%d %s (%s)" % (i, nm, ", ".join(fields)))


if __name__ == "__main__":
    main()
