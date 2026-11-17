"""soak_test.py: the soak run's test and its memory judgment (tools/gate/soak.py), without the game.

1. The test it writes is one the harness reads (luma_test.read_test): real time, process.memory every 30 seconds, a
   stage asked for at every stage's start after the first, the end at the last frame, input only inside the run.
2. The judgment: flat memory passes; memory that keeps growing past the bound fails; the first lap's growth is left
   out; a run of fewer than three laps cannot pass.
"""
import json
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "lumacore"))
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

    if failures:
        print("soak_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("soak_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
