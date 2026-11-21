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
    python -X utf8 -I tools/verify/step_hash.py record WORK [--out tests/replays]
        Keeps the hashes of the recorded stops (RECORDED_STOPS) from a work folder's tour runs, one file a stop
        (tests/replays/<test>.tsv: step, hash, actors). Their runs must have passed with every step hashed.
    python -X utf8 -I tools/verify/step_hash.py check WORK [--against WORK2] [--some]
        The gate's replays: a work folder's tour runs against the recordings (every recorded stop must have run,
        unless --some), or against another work folder's runs of the same tests (a renderer or a device against
        Direct3D 12).

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

# The stops whose hashes are kept (tests/replays): the gate's twelve quick stops and eight more (a ship, a garden, a
# beach, the last boss, a reversing world, a rolling ball, a launch-star stage, a ghost stage). A change to the game's
# source that changes what the game does changes them; one that is meant to is recorded again (record).
RECORDED_STOPS = [
    "HoneyBeeKingdomGalaxy 1", "OceanRingGalaxy 1", "EggStarGalaxy 1", "KoopaBattleVs1Galaxy 1",
    "SandClockGalaxy 1", "IceVolcanoGalaxy 1", "HellProminenceGalaxy 1", "TeresaMario2DGalaxy 1",
    "SurfingLv1Galaxy 1", "CannonFleetGalaxy 1", "OceanPhantomCaveGalaxy 4", "FactoryGalaxy 1",
    "BattleShipGalaxy 1", "CosmosGardenGalaxy 1", "HeavenlyBeachGalaxy 1", "KoopaBattleVs3Galaxy 1",
    "ReverseKingdomGalaxy 1", "TamakoroExLv1Galaxy 1", "StarDustGalaxy 1", "PhantomGalaxy 1",
]
RECORDINGS = TREE / "tests" / "replays"


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


BIT_FIELDS = ("pos", "rot", "scale", "vel", "grav")


def bit_values(bits):
    """{"pos.x": float, ...} of a hash.actor's "bits" (15 floats as 8 hex digits each, the hash's order)."""
    import struct
    out = {}
    for f, name in enumerate(BIT_FIELDS):
        for c, axis in enumerate("xyz"):
            word = bits[(f * 3 + c) * 8:(f * 3 + c + 1) * 8]
            out["%s.%s" % (name, axis)] = struct.unpack("<f", struct.pack("<I", int(word, 16)))[0]
    return out


def struct_bits(value):
    import struct
    return struct.pack("<f", value)


def first_actor(a, b):
    """The first actor two detail lists differ in, as text; None without both lists. Values the same to 1/1000 are
    named from their bits where both runs wrote them: each float that differs, with both values in full."""
    if not a or not b:
        return None
    for x, y in zip(a, b):
        if x != y:
            fields = [k for k in ("name", "pos", "rot", "scale", "vel", "grav", "step") if x.get(k) != y.get(k)]
            if not fields and x.get("bits") and y.get("bits") and x["bits"] != y["bits"]:
                xv, yv = bit_values(x["bits"]), bit_values(y["bits"])
                differ = ["%s %r / %r" % (k, xv[k], yv[k]) for k in xv if struct_bits(xv[k]) != struct_bits(yv[k])]
                return "first actor that differs: #%d %s (bits): %s" % (x["i"], x["name"], "; ".join(differ))
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


def plain_steps(steps):
    """read_facts' steps as {frame: (hash text, actors)}."""
    return {frame: (str(h), int(n)) for frame, (h, n) in steps.items()}


def runs_by_test(work):
    """{test name: run folder} of a luma_test.py work folder (runs/<suite>/<test>/<renderer>)."""
    return {f.parent.name: f for f in sorted(pathlib.Path(work).glob("runs/*/*/*")) if f.is_dir()}


def passed(folder):
    try:
        return json.loads((pathlib.Path(folder) / "result.json").read_text(encoding="utf-8")).get("verdict") == "pass"
    except (OSError, ValueError):
        return False


def recorded_tests():
    """The tour's test names of RECORDED_STOPS."""
    want = {tuple(s.split()) for s in RECORDED_STOPS}
    names = [gate_tour.test_for(i, stage, scenario)[0] for i, stage, scenario in gate_tour.stops()
             if (stage, str(scenario)) in want]
    if len(names) != len(want):
        raise SystemExit("step_hash.py: a recorded stop is not in the tour list")
    return names


def read_recording(path):
    steps = {}
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            frame, h, n = line.split("\t")
            steps[int(frame)] = (h, int(n))
    return steps


def same_steps(a, b, what):
    """(same, text) for two {frame: (hash, actors)}: every step of each in the other, and the same."""
    if not a or not b:
        return False, "no hashes in %s" % what
    for frame in sorted(set(a) & set(b)):
        if a[frame] != b[frame]:
            return False, "differs from step %d (hash %s, %d actors / %s, %d actors)" % ((frame,) + a[frame] + b[frame])
    if set(a) != set(b):
        return False, "the same where both have steps, but %d steps are in one only" % len(set(a) ^ set(b))
    return True, "the same over %d steps" % len(a)


def hash_lines(text):
    """The lines of a recording that hold a step's hash (the comment line that names the build is not one)."""
    return [l for l in text.splitlines() if l and not l.startswith("#")]


def record(work, out):
    """Writes one file for each recorded stop. A file whose hash lines are the ones the run gives is left as it is: the build it names
    in its first line is not a change by itself. Prints how many files were left and how many written."""
    runs = runs_by_test(work)
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    left = written = 0
    for name in recorded_tests():
        run = runs.get(name)
        if run is None or not passed(run):
            raise SystemExit("step_hash.py record: %s has no passed run in %s" % (name, work))
        steps = plain_steps(read_facts(run)[0])
        if sorted(steps) != list(range(gate_tour.FRAMES)):
            raise SystemExit("step_hash.py record: %s has %d hashed steps, not 0 to %d" % (name, len(steps), gate_tour.FRAMES - 1))
        commit = json.loads((run / "result.json").read_text(encoding="utf-8")).get("commit", "unknown")
        lines = ["# %s: the hash of every live actor after each step (step_hash.py record, a build of %s)" % (name, commit)]
        lines += ["%d\t%s\t%d" % (f, h, n) for f, (h, n) in sorted(steps.items())]
        text = "\n".join(lines) + "\n"
        path = out / (name + ".tsv")
        try:
            same = hash_lines(path.read_text(encoding="utf-8")) == hash_lines(text)
        except OSError:
            same = False
        if same:
            left += 1
        else:
            path.write_text(text, encoding="utf-8")
            written += 1
    print("step_hash: %d stops recorded in %s: %d files left as they were, %d written" % (len(RECORDED_STOPS), out, left, written))
    return 0


def check(work, against=None, some=False, recordings=RECORDINGS):
    """(ok, lines): a work folder's runs against the recordings, or against another work folder's runs of the same
    tests. Against the recordings, a recorded stop that did not run fails (unless `some`); against runs, the tests
    both ran and passed are compared and the rest are named. Either way a run that failed is named, not compared: its
    own check says why."""
    runs = runs_by_test(work)
    if against is None:
        names = recorded_tests()
        other = {n: plain_steps_of_file(recordings / (n + ".tsv")) for n in names}
    else:
        theirs = runs_by_test(against)
        names = sorted(set(runs) | set(theirs))
        other = {n: theirs.get(n) for n in names}
    lines, same, differ, skipped = [], 0, [], []
    for name in names:
        run, ref = runs.get(name), other.get(name)
        if ref is None:
            skipped.append(name + (" (not recorded)" if against is None else " (not run there)"))
            continue
        if run is None:
            (skipped if some or against is not None else differ).append(name + " (not run)")
            continue
        if not passed(run) or (against is not None and not passed(ref)):
            skipped.append(name + " (a run did not pass)")
            continue
        mine = plain_steps(read_facts(run)[0])
        theirs_steps = ref if against is None else plain_steps(read_facts(ref)[0])
        ok, text = same_steps(mine, theirs_steps, name)
        lines.append("%-40s %s" % (name, text))
        if ok:
            same += 1
        else:
            differ.append("%s (%s)" % (name, text))
    summary = "%d of %d the same" % (same, same + len(differ))
    if differ:
        summary += "; not: " + ", ".join(differ[:6]) + (" ..." if len(differ) > 6 else "")
    if skipped:
        summary += "; not compared: " + ", ".join(skipped[:6]) + (" ..." if len(skipped) > 6 else "")
    lines.append(summary)
    return same > 0 and not differ, lines


def plain_steps_of_file(path):
    return read_recording(path) if pathlib.Path(path).is_file() else None


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
    w = sub.add_parser("record")
    w.add_argument("work")
    w.add_argument("--out", default=str(RECORDINGS))
    k = sub.add_parser("check")
    k.add_argument("work")
    k.add_argument("--against")
    k.add_argument("--some", action="store_true")
    args = p.parse_args()
    if args.command == "compare":
        sys.exit(compare(args.a, args.b)[0])
    if args.command == "record":
        sys.exit(record(args.work, args.out))
    if args.command == "check":
        ok, lines = check(args.work, args.against, args.some)
        print("\n".join(lines))
        sys.exit(0 if ok else 1)
    sys.exit(replay(args))


if __name__ == "__main__":
    main()
