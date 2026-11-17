"""audit_make_tour.py OUT [--kit LUMAKIT]: the 102-stop galaxy tour as harness tests (one per stop), since
tools/verify/tour.sh no longer runs on the current build. Each test starts in the stop's stage and scenario, plays 600
frames with seeded random input (stick, jump, spin, crouch), takes two pictures and ends. It passes when the game
reaches the end with no crash, no stop in frames and no mod fault (the runner's own checks)."""
import json
import pathlib
import random
import sys

TOUR = pathlib.Path("D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean/tools/verify/tours/tour_enterable.txt")
out = pathlib.Path(sys.argv[1])
kit = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "--kit" else None
names = []
for index, line in enumerate(l for l in TOUR.read_text(encoding="utf8").splitlines() if l.strip() and not l.startswith("#")):
    stage, scenario = line.split()[0], int(line.split()[1])
    name = "%03d-%s-%d" % (index + 1, stage, scenario)
    r = random.Random(20261116 + index)
    steps = [{"at": 30, "picture": "arrive"}]
    at = 40
    while at < 560:
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
    steps.append({"at": 580, "picture": "after"})
    steps.append({"at": 600, "end": True})
    steps.sort(key=lambda s: s["at"])
    test = {"harness": 1, "name": name, "about": "%s scenario %d: 600 frames of seeded random play" % (stage, scenario),
            "save": {"spin": True}, "stage": {"name": stage, "scenario": scenario},
            "limit_seconds": 240, "steps": steps, "expect": []}
    if kit:
        test["mods"] = [kit]
    folder = out / name
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "test.json").write_text(json.dumps(test, indent=2) + "\n", encoding="utf8")
    names.append(name)
(out / "suite.json").write_text(json.dumps({"harness": 1, "tests": names}, indent=2) + "\n", encoding="utf8")
print(len(names), "tests in", out)
