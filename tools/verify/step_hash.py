"""step_hash.py: the step-hash replay. Two builds play the same tests with fixed steps; after every step the game half
writes the hash of every live actor's state (tools/test_driver/luma_test/luma_test.c, the "hash" step). A change to
the game's source that changes nothing the game does keeps every step's hash; the first step that differs, and the
first actor in it, say where it does.

    python -X utf8 -I tools/verify/step_hash.py compare A B
        A and B: two run folders (each with facts.jsonl), or two work folders of luma_test.py whose runs are paired by
        their place (runs/<suite>/<test>/<renderer>). Prints, per pair, the steps compared and the first that differs,
        with the first actor that differs where both runs wrote hash.actor facts at that step.
    python -X utf8 -I tools/verify/step_hash.py replay --bin-a BIN --bin-b BIN --work WORK [--renderer d3d12]
                                                       [--stops "Stage N" ...] [--paks PAKS]
        Writes the tour (tools/gate/gate_tour.py; every stop, or the ones named) with the hash on from its first step,
        plays it on both builds, and compares. Where a stop differs, both builds play it again with "hash detail" at
        the first step that differs, to name the actor.

Exit status: 0 when every pair has hashes and they are all the same, 1 otherwise.
"""
import argparse
import json
import os
import pathlib
import subprocess
import sys

TREE = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TREE / "tools" / "gate"))
import gate_tour  # noqa: E402


def read_facts(folder):
    """{frame: hash} and {frame: [actor records]} of one run folder's facts.jsonl."""
    steps, actors = {}, {}
    path = pathlib.Path(folder) / "facts.jsonl"
    if not path.is_file():
        return steps, actors
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            f = json.loads(line)
        except ValueError:
            continue
        if f.get("key") == "hash.step":
            steps[f["frame"]] = (f["value"]["h"], f["value"]["actors"])
        elif f.get("key") == "hash.actor":
            actors.setdefault(f["frame"], []).append(f["value"])
    return steps, actors


def compare_runs(a, b):
    """(same, text): whether two runs' hashes agree at every step both have, and what to say."""
    sa, aa = read_facts(a)
    sb, ab = read_facts(b)
    if not sa or not sb:
        return False, "no hash.step facts in %s" % (a if not sa else b)
    frames = sorted(set(sa) & set(sb))
    only = sorted(set(sa) ^ set(sb))
    for frame in frames:
        if sa[frame] != sb[frame]:
            text = "differs from step %d of %d (hash %s, %d actors / %s, %d actors)" % (
                frame, len(frames), sa[frame][0], sa[frame][1], sb[frame][0], sb[frame][1])
            first = first_actor(aa.get(frame), ab.get(frame))
            return False, text + ("; " + first if first else "")
    if only:
        return False, "the same over %d steps, but only one run has steps %d to %d" % (len(frames), only[0], only[-1])
    return True, "the same over %d steps" % len(frames)


def first_actor(a, b):
    """The first actor two detail lists differ in, as text; None without both lists."""
    if not a or not b:
        return None
    for x, y in zip(a, b):
        if x != y:
            fields = [k for k in ("name", "pos", "rot", "scale", "vel", "grav", "step") if x.get(k) != y.get(k)]
            return "first actor that differs: #%d %s (%s): %s / %s" % (
                x["i"], x["name"], ", ".join(fields) or "below 1/1000",
                {k: x.get(k) for k in fields} if fields else x["h"], {k: y.get(k) for k in fields} if fields else y["h"])
    if len(a) != len(b):
        longer = a if len(a) > len(b) else b
        return "the actors are the same up to #%d; then one run has %s more" % (min(len(a), len(b)), longer[min(len(a), len(b))]["name"])
    return None


def run_pairs(a, b):
    """Pairs of run folders: the folders themselves, or the runs of two work folders by their place."""
    a, b = pathlib.Path(a), pathlib.Path(b)
    if (a / "facts.jsonl").is_file() or (b / "facts.jsonl").is_file():
        return [(a.name, a, b)]
    pairs = []
    for facts in sorted(a.rglob("facts.jsonl")):
        place = facts.parent.relative_to(a)
        pairs.append((str(place), facts.parent, b / place))
    return pairs


def compare(a, b):
    pairs = run_pairs(a, b)
    if not pairs:
        print("step_hash.py: no runs under %s" % a)
        return 1, []
    failed = []
    for place, x, y in pairs:
        same, text = compare_runs(x, y)
        print("%-60s %s" % (place, text))
        if not same:
            failed.append((place, x, y))
    print("step_hash: %d of %d the same" % (len(pairs) - len(failed), len(pairs)))
    return (0 if not failed else 1), failed


def with_hash(suite_folder, detail=None):
    """Every test of a suite folder gets the hash from its first step, and "hash detail" at the frames asked
    ({test name: frame})."""
    suite = json.loads((suite_folder / "suite.json").read_text(encoding="utf-8"))
    for name in suite["tests"]:
        path = suite_folder / name / "test.json"
        test = json.loads(path.read_text(encoding="utf-8"))
        steps = [s for s in test["steps"] if "hash" not in s]
        steps.append({"at": 0, "hash": "on"})
        if detail and name in detail:
            steps.append({"at": detail[name], "hash": "detail"})
        steps.sort(key=lambda s: (s["at"], "end" in s))
        test["steps"] = steps
        path.write_text(json.dumps(test, indent=2) + "\n", encoding="utf-8")


def play(suite, bin_folder, work, renderer, paks):
    env = dict(os.environ)
    if paks:
        env["LUMA_TEST_PAKS"] = paks
    work.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, "-X", "utf8", "-I", str(TREE / "tools" / "lumacore" / "luma_test.py"), "run",
                    str(suite), "--bin", str(bin_folder), "--work", str(work), "--who", "step-hash",
                    "--renderer", renderer, "--lock", str(work.parent / "lock")], env=env, check=False)


def replay(args):
    work = pathlib.Path(args.work)
    suite = gate_tour.write(work / "tour", args.stops or None)
    with_hash(suite.parent)
    play(suite, args.bin_a, work / "a", args.renderer, args.paks)
    play(suite, args.bin_b, work / "b", args.renderer, args.paks)
    code, failed = compare(work / "a", work / "b")
    if failed:
        # Again with the actors' detail at each differing stop's first differing step.
        detail = {}
        for place, x, y in failed:
            sa, _ = read_facts(x)
            sb, _ = read_facts(y)
            common = sorted(set(sa) & set(sb))
            first = next((f for f in common if sa[f] != sb[f]), None)
            if first is not None:
                detail[pathlib.Path(place).parts[-2]] = first
        if detail:
            again = work / "tour-detail"
            suite = gate_tour.write(again, [" ".join(n.split("-")[-2:]) for n in detail])
            with_hash(again, detail)
            play(suite, args.bin_a, work / "a-detail", args.renderer, args.paks)
            play(suite, args.bin_b, work / "b-detail", args.renderer, args.paks)
            print("with each actor at the first step that differs:")
            compare(work / "a-detail", work / "b-detail")
    return code


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="command", required=True)
    c = sub.add_parser("compare")
    c.add_argument("a")
    c.add_argument("b")
    r = sub.add_parser("replay")
    r.add_argument("--bin-a", required=True)
    r.add_argument("--bin-b", required=True)
    r.add_argument("--work", required=True)
    r.add_argument("--renderer", default="d3d12")
    r.add_argument("--stops", nargs="*", default=[])
    r.add_argument("--paks", default=os.environ.get("LUMA_TEST_PAKS", ""))
    args = p.parse_args()
    if args.command == "compare":
        sys.exit(compare(args.a, args.b)[0])
    sys.exit(replay(args))


if __name__ == "__main__":
    main()
