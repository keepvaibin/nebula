"""soak.py: the soak run (file 00 section 4.2): two hours of random input per renderer, in one process each.

The game plays stages of the tour (tools/verify/tours/tour_enterable.txt) in turn, each for a minute of seeded random
play (stick, jump, spin, crouch, the pointer, now and then the pause menu), in real time. The stages come round in
laps: a lap is LAP stages chosen by the seed, played in the same order every lap.

A renderer passes when
  - its run reaches its end with no crash, no stop in frames, no stage that fails to load and no mod fault (the
    harness's own checks: luma_test.py's verdict), and
  - its memory does not grow: the process's own memory (the harness's process.memory fact every 30 seconds:
    Windows' private bytes, the driver's too; Linux's and Android's resident anonymous memory; Apple's footprint) has,
    in every lap after the second, a median at most BOUND_MB above the second lap's. The first lap is left out: it
    fills what is kept once (the pipeline library, caches that hold the stages' data). Three laps at least are needed.

    python -X utf8 -I tools/gate/soak.py run --bin BIN --pak PAK --work WORK [--renderers d3d12,vulkan,d3d11]
                                            [--minutes 120] [--lap 12] [--stage-seconds 60] [--seed N]
    python -X utf8 -I tools/gate/soak.py write OUT [--minutes ...]     the suite only
    python -X utf8 -I tools/gate/soak.py judge RUN_FOLDER              a run's memory, judged again

`run` starts every renderer at once (each its own process, work folder and harness lock), prints one line per
renderer and exits 0 only when every one passed.
"""
import argparse
import json
import pathlib
import random
import shutil
import statistics
import subprocess
import sys
import threading

TREE = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gate_tour  # noqa: E402
LIST = TREE / "tools" / "verify" / "tours" / "tour_enterable.txt"
STEPS_PER_SECOND = 60
MEMORY_SECONDS = 30
BOUND_MB = 32.0
LOAD_FRAMES = 300  # input starts this long after a stage is asked for (the load and its opening take some of it)
# The pause menu is a block of its own: a press, PAUSE_WAIT frames with no input at all, a press that closes it, and the menu's way
# out. The first plan sent a random pointer and A into the open menu: that pressed its Settings button, the overlay stayed open
# (scripted pad input cannot close it) and the stage that followed never got past its opening camera (ledger N-1089; the same plan
# without its pause presses passed both stages). A press is not made in the first PAUSE_AFTER frames of a stage (the stage's opening
# lasts about 900 of them; a press the game does not take would leave the second press to open the menu), nor where the block
# would not end before the stage does.
PAUSE_WAIT = 60
PAUSE_BLOCK = 3 + PAUSE_WAIT + 3 + 60  # the opening press, the wait, the closing press, the menu's way out
PAUSE_AFTER = 1200
# The lives: random play dies, a stage costs lives, and with none left the game ends the run at its title screen (the second stall of
# N-1089: Mario was out of lives in the Bowser stage, the game went to the title and the file select, the plan's pointer opened the
# Settings overlay there, and the next stage move never reached its opening camera). Every stage starts with LIVES lives, as a player
# who takes a 1-Up now and then would have, and the lives left at its end are a fact (soak.lives.<stage number>).
LIVES = 99


def stops():
    lines = [l for l in LIST.read_text(encoding="utf-8").splitlines() if l.strip() and not l.startswith("#")]
    return [(l.split()[0], int(l.split()[1])) for l in lines]


def save_for(chosen):
    """The run's one save: the spin, and every Hungry Luma seed a chosen stop's line feeds (gate_tour's list words)."""
    save = {"spin": True}
    for stage, scenario, extra in gate_tour.stops_with_save():
        if (stage, scenario) in chosen and "tico_seeds" in extra:
            save.setdefault("tico_seeds", {}).update(extra["tico_seeds"])
    return save


def random_play(r, start, end, stage_start=None):
    """Seeded random input from frame start to end (exclusive): the tour's moves, the pointer, the pause menu (a block: PAUSE_BLOCK).
    stage_start: the frame the stage was asked for (default `start`)."""
    steps = []
    at = start
    stage_start = start if stage_start is None else stage_start
    while at < end - 30:
        kind = r.choice(["stick"] * 6 + ["jump"] * 3 + ["spin"] * 2 + ["crouch", "pointer", "b"] + ["pause"] * (1 if r.random() < 0.05 else 0))
        if kind == "pause" and (at - stage_start < PAUSE_AFTER or at + PAUSE_BLOCK >= end - 30):
            kind = "stick"
        if kind == "stick":
            frames = min(r.randint(20, 120), end - at - 1)
            steps.append({"at": at, "stick": [round(r.uniform(-1, 1), 2), round(r.uniform(-1, 1), 2)], "frames": frames})
            at += frames + r.randint(2, 10)
        elif kind == "jump":
            steps.append({"at": at, "hold": ["A"], "frames": r.choice([3, 8, 15, 25])})
            at += 30
        elif kind == "spin":
            steps.append({"at": at, "shake": 8})
            at += 25
        elif kind == "crouch":
            steps.append({"at": at, "hold": ["Z"], "frames": r.choice([5, 20])})
            at += 30
        elif kind == "pointer":
            frames = r.randint(10, 60)
            steps.append({"at": at, "pointer": [round(r.uniform(0, 1), 2), round(r.uniform(0, 1), 2)], "frames": frames})
            at += frames + 5
        elif kind == "b":
            steps.append({"at": at, "hold": ["B"], "frames": 5})
            at += 20
        else:
            steps.append({"at": at, "hold": ["PLUS"], "frames": 3})
            steps.append({"at": at + 3 + PAUSE_WAIT, "hold": ["PLUS"], "frames": 3})
            at += PAUSE_BLOCK
    return steps


def plan(minutes, lap, stage_seconds, seed):
    """(the stages of a lap, frames per stage, total frames)."""
    r = random.Random(seed)
    every = stops()
    chosen = r.sample(every, min(lap, len(every)))
    per = stage_seconds * STEPS_PER_SECOND
    total = max(1, (minutes * 60 * STEPS_PER_SECOND) // per) * per
    return chosen, per, total


def test_for(minutes, lap, stage_seconds, seed):
    chosen, per, total = plan(minutes, lap, stage_seconds, seed)
    r = random.Random(seed + 1)
    steps = []
    for n, at in enumerate(range(0, total, per)):
        stage, scenario = chosen[n % len(chosen)]
        if at:
            steps.append({"at": at, "stage": {"name": stage, "scenario": scenario}})
        steps.append({"at": at + (LOAD_FRAMES if at else 30) - 1, "call": "GameDataFunction::addPlayerLeft", "signature": "void(int)",
                      "args": [LIVES]})
        steps.append({"at": at + per - 2, "call": "GameDataFunction::getPlayerLeft", "signature": "s32()", "args": [],
                      "fact": "soak.lives.%d" % n})
        steps += random_play(r, at + (LOAD_FRAMES if at else 30), at + per, at)
    steps.append({"at": total, "end": True})
    steps.sort(key=lambda s: s["at"])
    first = chosen[0]
    return {"harness": 1, "name": "soak",
            "about": "%d minutes of seeded random play (seed %d) through %d stages a lap, %d seconds each, in real time"
                     % (minutes, seed, len(chosen), stage_seconds),
            "save": save_for(chosen), "stage": {"name": first[0], "scenario": first[1]}, "time": "real",
            "memory": MEMORY_SECONDS, "limit_seconds": int(minutes * 60 * 1.25) + 900, "steps": steps, "expect": []}


def write(out, minutes=120, lap=12, stage_seconds=60, seed=20261117):
    out = pathlib.Path(out)
    folder = out / "soak"
    folder.mkdir(parents=True, exist_ok=True)
    test = test_for(minutes, lap, stage_seconds, seed)
    (folder / "test.json").write_text(json.dumps(test, indent=1) + "\n", encoding="utf-8")
    suite = out / "suite.json"
    suite.write_text(json.dumps({"harness": 1, "tests": ["soak"]}, indent=2) + "\n", encoding="utf-8")
    return suite


def judge(samples, lap_frames):
    """samples: (test frame, bytes) of process.memory. Returns (passes, detail)."""
    laps = {}
    for frame, value in samples:
        if frame >= 0 and isinstance(value, int):
            laps.setdefault(frame // lap_frames, []).append(value)
    medians = [statistics.median(laps[k]) / (1 << 20) for k in sorted(laps)]
    if len(medians) < 3:
        return False, "%d laps of memory; three are needed to judge growth" % len(medians)
    base = medians[1]
    worst = max(m - base for m in medians[2:])
    detail = "lap medians %s MB; the most above lap 2: %+.1f MB (bound %.0f)" % (
        " ".join("%.0f" % m for m in medians), worst, BOUND_MB)
    return worst <= BOUND_MB, detail


def memory_of(run_folder):
    facts = run_folder / "facts.jsonl"
    samples = []
    if facts.is_file():
        for line in facts.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                f = json.loads(line)
            except ValueError:
                continue
            if f.get("key") == "process.memory" and isinstance(f.get("value"), dict):
                samples.append((f.get("frame", -1), f["value"].get("bytes")))
    return samples


def run_all(a):
    suite = write(pathlib.Path(a.work) / "suite", a.minutes, a.lap, a.stage_seconds, a.seed)
    chosen, per, total = plan(a.minutes, a.lap, a.stage_seconds, a.seed)
    lap_frames = per * len(chosen)
    results = {}

    def one(renderer):
        work = pathlib.Path(a.work) / renderer
        work.mkdir(parents=True, exist_ok=True)
        with (work / "harness.out").open("wb") as log:
            code = subprocess.run([sys.executable, "-X", "utf8", "-I", str(TREE / "tools/lumacore/luma_test.py"), "run",
                                   str(suite), "--bin", a.bin, "--pak", a.pak, "--work", str(work), "--renderer", renderer,
                                   "--who", a.who, "--lock", str(work / "harness.lock")],
                                  stdout=log, stderr=subprocess.STDOUT).returncode
        # The run's copies of the programs are not evidence: removed so the drive does not fill.
        for d in work.rglob("bin"):
            if d.is_dir():
                shutil.rmtree(d, ignore_errors=True)
        folders = [f.parent for f in work.rglob("facts.jsonl")]
        ok, detail = judge(memory_of(folders[0]), lap_frames) if folders else (False, "no facts")
        results[renderer] = (code == 0 and ok, "harness exit %d; %s" % (code, detail))

    threads = [threading.Thread(target=one, args=(r,)) for r in a.renderers.split(",")]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    for renderer, (ok, detail) in sorted(results.items()):
        print("%-8s %s  %s" % (renderer, "pass" if ok else "FAIL", detail))
    return 0 if results and all(ok for ok, _ in results.values()) else 1


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    for name in ("run", "write"):
        s = sub.add_parser(name)
        if name == "write":
            s.add_argument("out")
        else:
            s.add_argument("--bin", required=True)
            s.add_argument("--pak", required=True)
            s.add_argument("--work", required=True)
            s.add_argument("--renderers", default="d3d12,vulkan,d3d11")
            s.add_argument("--who", default="soak")
        s.add_argument("--minutes", type=int, default=120)
        s.add_argument("--lap", type=int, default=12, help="stages a lap")
        s.add_argument("--stage-seconds", type=int, default=60)
        s.add_argument("--seed", type=int, default=20261117)
    j = sub.add_parser("judge")
    j.add_argument("run_folder")
    j.add_argument("--lap", type=int, default=12)
    j.add_argument("--stage-seconds", type=int, default=60)
    a = p.parse_args(argv)
    if a.command == "write":
        print(write(a.out, a.minutes, a.lap, a.stage_seconds, a.seed))
        return 0
    if a.command == "judge":
        ok, detail = judge(memory_of(pathlib.Path(a.run_folder)), a.lap * a.stage_seconds * STEPS_PER_SECOND)
        print("%s  %s" % ("pass" if ok else "FAIL", detail))
        return 0 if ok else 1
    return run_all(a)


if __name__ == "__main__":
    sys.exit(main())
