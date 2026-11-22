"""soak_test.py: the soak run's test and its memory judgment (tools/gate/soak.py), without the game.

1. The test it writes is one the harness reads (luma_test.read_test): real time, process.memory every 30 seconds, a
   stage asked for at every stage's start after the first, the end at the last frame, input only inside the run.
2. The judgment: flat memory passes; memory that keeps growing past the bound fails; the first lap's growth is left
   out; a run of fewer than three laps cannot pass.
3. The pause menu is a block of two presses with nothing between them, in the middle of a stage only (N-1089: a pointer and A sent
   into the open menu pressed its Settings button, the overlay stayed open, and the stage move that followed never got past its
   opening camera).
4. The lives: every stage starts with soak.LIVES lives (a call of the game's own function after its stage was asked for) and its
   lives at the end are a fact (N-1089: with none left the game ended the run at its title screen and the next stage move stalled).
5. The gate's verdict (gate.soak_result): the soak's output lands in the check's log, which is not the file the soak
   writes (N-1065: both were soak.log, and the copy onto itself failed the check).
"""
import json
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "lumacore"))
import gate  # noqa: E402
import luma_test  # noqa: E402
import soak  # noqa: E402


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        suite = soak.write(pathlib.Path(tmp), minutes=10, lap=3, stage_seconds=60, seed=7)
        test = luma_test.read_test(suite.parent / "soak")
        raw = test["raw"]
        check(test["time"] == "real" and test["memory"] == 30, "real time, memory every 30 s: %s %s" % (test["time"], test["memory"]))
        stages = [s["at"] for s in raw["steps"] if "stage" in s]
        check(stages == [3600 * k for k in range(1, 10)], "a stage at each minute after the first: %s" % stages)
        ends = [s["at"] for s in raw["steps"] if "end" in s]
        check(ends == [36000], "the end at the last frame: %s" % ends)
        check(all(0 < s["at"] < 36000 for s in raw["steps"] if "end" not in s), "every input inside the run")
        check(test["limit"] >= 10 * 60, "the limit covers the run: %s" % test["limit"])
        names = [(s["stage"]["name"], s["stage"]["scenario"]) for s in raw["steps"] if "stage" in s]
        first = (raw["stage"]["name"], raw["stage"]["scenario"])
        lap = [first] + names[:2]
        check(names[2:5] == lap and len(set(lap)) == 3, "the laps repeat one order of three stages: %s" % names)
        again = json.loads((soak.write(pathlib.Path(tmp) / "b", minutes=10, lap=3, stage_seconds=60, seed=7).parent
                            / "soak" / "test.json").read_text(encoding="utf-8"))
        check(again == raw, "the same seed writes the same test")

    # 3. The pause menu: pairs of presses, nothing between a pair, never in a stage's first PAUSE_AFTER frames nor so late that the block
    # runs into the next stage (the default plan: 120 minutes, 12 stages a lap).
    plan = soak.test_for(120, 12, 60, 20261117)["steps"]
    per = 60 * soak.STEPS_PER_SECOND
    presses = [s for s in plan if s.get("hold") == ["PLUS"]]
    check(len(presses) >= 20 and len(presses) % 2 == 0, "the plan presses pause in pairs, often enough to be a test: %d" % len(presses))
    for first, second in zip(presses[0::2], presses[1::2]):
        check(second["at"] - first["at"] == 3 + soak.PAUSE_WAIT, "the second press closes the menu after the wait: %s %s" % (first, second))
        between = [s for s in plan if first["at"] < s["at"] < second["at"]]
        check(not between, "no input inside an open pause menu at %d: %s" % (first["at"], between[:2]))
        start = first["at"] // per * per
        check(first["at"] - start >= soak.PAUSE_AFTER, "no pause in a stage's first %d frames: %d" % (soak.PAUSE_AFTER, first["at"] - start))
        check(second["at"] + 3 + 60 < start + per, "the pause block ends inside its stage: %d" % second["at"])
        after = [s for s in plan if second["at"] < s["at"] < second["at"] + 60]
        check(not after, "the menu's way out is left alone at %d: %s" % (second["at"], after[:2]))

    # 4. The lives, in the default plan: a refill and a read at every stage of the 120 minutes.
    refills = [s for s in plan if s.get("call") == "GameDataFunction::addPlayerLeft"]
    reads = [s for s in plan if s.get("call") == "GameDataFunction::getPlayerLeft"]
    check(len(refills) == 120 and len(reads) == 120, "a refill and a read for each of the 120 stages: %d %d" % (len(refills), len(reads)))
    check(all(s["args"] == [soak.LIVES] and s["signature"] == "void(int)" for s in refills), "the refill gives %d lives" % soak.LIVES)
    check([s["fact"] for s in reads] == ["soak.lives.%d" % n for n in range(120)], "the lives read at each stage's end is its own fact")
    for n, (give, read) in enumerate(zip(refills, reads)):
        check(n * per <= give["at"] < n * per + soak.LOAD_FRAMES and read["at"] >= (n + 1) * per - 2 - 1,
              "the refill is at the stage's start and the read at its end: %d %d" % (give["at"], read["at"]))

    lap = 1000
    flat = [(f, 900 << 20) for f in range(0, 5 * lap, 50)]
    check(soak.judge(flat, lap)[0], "flat memory passes: %s" % soak.judge(flat, lap)[1])
    warm = [(f, (700 if f < lap else 900) << 20) for f in range(0, 5 * lap, 50)]
    check(soak.judge(warm, lap)[0], "the first lap's growth is left out: %s" % soak.judge(warm, lap)[1])
    leak = [(f, (900 << 20) + f * 20000) for f in range(0, 5 * lap, 50)]
    check(not soak.judge(leak, lap)[0], "steady growth fails: %s" % soak.judge(leak, lap)[1])
    small = [(f, (900 << 20) + f * 2000) for f in range(0, 5 * lap, 50)]
    check(soak.judge(small, lap)[0], "growth inside the bound passes: %s" % soak.judge(small, lap)[1])
    check(not soak.judge(flat[:len(flat) * 2 // 5], lap)[0], "two laps cannot pass")
    check(not soak.judge([(0, None)] * 100, lap)[0], "no bytes cannot pass")

    class Ended:
        def wait(self):
            return 0

    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        real_popen = subprocess.Popen
        subprocess.Popen = lambda *a, **k: Ended()
        try:
            process, soak_log = gate.start_soak(type("G", (), {"work": work, "bin": "b", "args": type("A", (), {
                "pak": "p", "soak_minutes": 1, "build_root": tmp})()})())
        finally:
            subprocess.Popen = real_popen
        check_log = work / "soak.log"  # Gate.check's log of the check named "soak"
        check(pathlib.Path(soak_log) != check_log, "the soak writes another file than the check's log: %s" % soak_log)
        pathlib.Path(soak_log).write_text("d3d12    pass  3 laps" + chr(10), encoding="utf-8")
        ok, said = gate.soak_result(process, pathlib.Path(soak_log))(check_log)
        check(ok and "3 laps" in said and "3 laps" in check_log.read_text(encoding="utf-8"),
              "the verdict and the soak's output in the check's log: %s %s" % (ok, said))
        ok, said = gate.soak_result(process, check_log)(check_log)
        check(ok, "a soak log that is the check's log is read, not copied onto itself: %s" % said)

    if failures:
        print("soak_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("soak_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
