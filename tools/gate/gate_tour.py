"""gate_tour.py: the galaxy tour as harness tests, written from tools/verify/tours/tour_enterable.txt.

Each stop starts in its stage and scenario, plays 600 frames of seeded random input (stick, jump, spin, crouch),
takes a picture on arrival and one at the end, and ends. It passes when the game reaches the end with no crash, no
stop in frames and no mod fault (the harness's own checks). The seeds are the audit's (20261116 + the stop's index),
so a stop plays the same input as it did in docs/audit-2026-11-17.

The input starts when the player has control: the game half's stage setup waits, up to 7,200 frames, until the
opening camera, the scenario starter and any demo are over and Mario takes input (luma_test.c stage_ready), and the
steps count from there. A stage with a long opening sequence is played as long as any other.

With a disc (RMGP01, RMGJ01), every test carries it (the harness's "disc") and its name the disc's prefix, so the
tour runs on that disc's game.pak, whose data (text, layouts, fonts) is its own.

With a window size, every stop plays in a window of that size: the game runs the actors the picture shows (its
clipping follows the picture's shape), so the PC's steps can match a device's only at the device's screen size.

    python -X utf8 -I tools/gate/gate_tour.py OUT [--disc DISC] [STAGE SCENARIO ...]
"""
import json
import pathlib
import random
import sys

TREE = pathlib.Path(__file__).resolve().parent.parent.parent
LIST = TREE / "tools" / "verify" / "tours" / "tour_enterable.txt"
FRAMES = 600


def stops():
    """(index, stage, scenario) for every line of the tour list."""
    return [(i, stage, scenario) for i, (stage, scenario, _) in enumerate(stops_with_save())]


def stops_with_save():
    """(stage, scenario, save) for every line of the tour list: "Stage scenario [tico_seed=N:COUNT ...]"; save holds
    what a line's extra words add to the stop's save ({"tico_seeds": {"N": COUNT}})."""
    out = []
    for line in LIST.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        words = line.split()
        save = {}
        for word in words[2:]:
            key, _, value = word.partition("=")
            seed, _, count = value.partition(":")
            if key != "tico_seed" or not seed.isdigit() or not count.isdigit():
                raise SystemExit("tour list: %r is not tico_seed=N:COUNT" % word)
            save.setdefault("tico_seeds", {})[seed] = int(count)
        out.append((words[0], int(words[1]), save))
    return out


def test_for(index, stage, scenario, disc=None, window=None):
    r = random.Random(20261116 + index)
    steps = [{"at": 30, "picture": "arrive"}]
    at = 40
    while at < FRAMES - 40:
        kind = r.choice(["stick", "stick", "stick", "jump", "spin", "crouch"])
        if kind == "stick":
            frames = r.randint(20, 90)
            steps.append({"at": at, "stick": [round(r.uniform(-1, 1), 2), round(r.uniform(-1, 1), 2)], "frames": frames})
            at += frames + r.randint(2, 10)
        elif kind == "jump":
            steps.append({"at": at, "hold": ["A"], "frames": r.choice([3, 8, 15, 25])})
            at += 30
        elif kind == "spin":
            steps.append({"at": at, "shake": 8})
            at += 25
        else:
            steps.append({"at": at, "hold": ["Z"], "frames": r.choice([5, 20])})
            at += 30
    steps.append({"at": FRAMES - 20, "picture": "after"})
    steps.append({"at": FRAMES, "end": True})
    steps.sort(key=lambda s: s["at"])
    name = "%s%03d-%s-%d" % ("%s-" % disc if disc else "", index + 1, stage, scenario)
    test = {"harness": 1, "name": name,
            "about": "%s scenario %d%s: %d frames of seeded random play" % (stage, scenario, " on %s" % disc if disc else "",
                                                                             FRAMES),
            "save": dict({"spin": True}, **stops_with_save()[index][2]), "stage": {"name": stage, "scenario": scenario},
            "limit_seconds": 240, "steps": steps, "expect": []}
    if disc:
        test["disc"] = disc
    if window:
        test["window"] = [int(window[0]), int(window[1])]
    return name, test


def write(out, only=None, disc=None, window=None):
    """Writes the tour (or the stops named in only, as "Stage scenario") under out, for disc when given (else the
    run's own pak), in a window of that size when given; returns its suite.json."""
    out = pathlib.Path(out)
    want = None if only is None else {tuple(s.split()) for s in only}
    names = []
    for index, stage, scenario in stops():
        if want is not None and (stage, str(scenario)) not in want:
            continue
        name, test = test_for(index, stage, scenario, disc, window)
        folder = out / name
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "test.json").write_text(json.dumps(test, indent=2) + "\n", encoding="utf-8")
        names.append(name)
    if want is not None and len(names) != len(want):
        found = {tuple(n.split("-")[-2:]) for n in names}
        raise ValueError("not in the tour list: %s" % sorted(" ".join(w) for w in want - found))
    suite = out / "suite.json"
    suite.write_text(json.dumps({"harness": 1, "tests": names}, indent=2) + "\n", encoding="utf-8")
    return suite


if __name__ == "__main__":
    args = sys.argv[1:]
    disc = None
    if "--disc" in args:
        i = args.index("--disc")
        disc = args[i + 1]
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    pairs = [" ".join(args[i:i + 2]) for i in range(1, len(args), 2)] or None
    print(write(args[0], pairs, disc))
