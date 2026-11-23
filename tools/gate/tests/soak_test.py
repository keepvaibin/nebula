"""soak_test.py: the soak run's test and its memory judgment (tools/gate/soak.py), without the game.

1. The test it writes is one the harness reads (luma_test.read_test): real time, process.memory every 30 seconds, a
   stage asked for at every stage's start after the first, the end at the last frame, input only inside the run.
2. The judgment: flat memory passes; memory that keeps growing past the bound fails; the first lap's growth is left
   out; a run of fewer than three laps cannot pass.
   A run of eight whole laps or more is judged from lap 6 (the growth a lap of the laps from there on, at most
   32 / (laps - 2) MB, and the lowest of the last three laps within 32 MB of lap 6): the recorded laps of the long run of
   11-22 pass, the same plus a ramp of 4 MB a lap are refused, a ramp under the sight passes, a lap's spike that falls back
   passes and memory that stays is refused, for each run length.
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

    # 2b. A run of eight whole laps or more (the nightly's ten) is judged from lap 6 (judge_long). The laps are 12 minutes of 60 steps
    #     a second; a lap's samples are every 30 seconds, all of the lap's median, so that the laps' medians are what the test names.
    whole = 12 * 60 * 60

    def run_of(medians, last=1.0):
        """The samples of a run whose lap medians (MB) are `medians`; the last lap's samples reach `last` of its length."""
        out = []
        for k, m in enumerate(medians):
            end = whole if k < len(medians) - 1 else int(whole * last)
            out += [(k * whole + f, int(m * (1 << 20))) for f in range(0, end - 1800 + 1, 1800)]
        return out

    # The long run of 11-22 (run 20261122-064409-c89e4e551-long, soak.log): the medians the old judge refused (+41.1, +36.7, +34.4).
    recorded = {"d3d11": [661, 677, 692, 696, 699, 701, 718, 703, 708, 716],
                "d3d12": [812, 830, 843, 844, 849, 854, 865, 867, 862, 867],
                "vulkan": [1194, 1213, 1224, 1236, 1240, 1244, 1242, 1241, 1243, 1247]}
    for renderer, medians in recorded.items():
        ok, said = soak.judge(run_of(medians), whole)
        check(ok and "from lap 6 on" in said, "%s: the recorded laps of 11-22 pass: %s" % (renderer, said))
        ok, said = soak.judge(run_of([m + 4.0 * k for k, m in enumerate(medians)]), whole)
        check(not ok, "%s: the recorded laps and a ramp of 4 MB a lap are refused: %s" % (renderer, said))
    flat = [900.0] * 10
    check(soak.judge(run_of(flat), whole)[0], "ten flat laps pass")
    check(soak.judge(run_of([900.0 + min(k, 5) * 6.0 for k in range(10)]), whole)[0], "warming up to lap 6, then flat, passes")
    for step in (4.1, 6.0, 10.0):
        check(not soak.judge(run_of([900.0 + step * k for k in range(10)]), whole)[0], "a ramp of %.1f MB a lap in ten laps is refused: %s" % (step, soak.judge(run_of([900.0 + step * k for k in range(10)]), whole)[1]))
    for step in (3.9, 3.0, 0.0):
        check(soak.judge(run_of([900.0 + step * k for k in range(10)]), whole)[0], "a ramp of %.1f MB a lap in ten laps is under the sight and passes" % step)
    # A lap's spike that falls back is not growth; memory that rises and stays is.
    for lap_no in (3, 6, 7, 8, 9, 10):
        spiked = list(flat)
        spiked[lap_no - 1] += 80.0
        check(soak.judge(run_of(spiked), whole)[0], "a spike of 80 MB in lap %d that falls back passes: %s" % (lap_no, soak.judge(run_of(spiked), whole)[1]))
    two = list(flat)
    two[6], two[7] = two[6] + 80.0, two[7] + 80.0
    check(soak.judge(run_of(two), whole)[0], "two laps of spike that fall back pass (a slope of the pairs' median)")
    for stay in (2, 3, 4):
        stepped = flat[:10 - stay] + [flat[0] + 60.0] * stay
        ok, said = soak.judge(run_of(stepped), whole)
        check(not ok, "a rise of 60 MB that stays for the last %d laps is refused: %s" % (stay, said))
    # A rise at lap 7 that stays for four laps moves the pairs' median slope not at all: the lowest of the last three laps shows it.
    ok, said = soak.judge(run_of([900.0] * 6 + [960.0] * 4), whole)
    check(not ok and "+0.00 MB a lap" in said, "a rise of 60 MB at lap 7 that stays is refused by the lowest of the last laps: %s" % said)
    ok, said = soak.judge(run_of([900.0] * 6 + [920.0] * 4), whole)
    check(ok, "a rise of 20 MB at lap 7 that stays is inside the bound: %s" % said)
    check(soak.judge(run_of([900.0, 900.0, 960.0, 960.0, 960.0, 960.0, 960.0, 960.0, 960.0, 960.0]), whole)[0],
          "a rise of 60 MB between laps 2 and 6 is warming up to the long rule, which sees growth from lap 6 on")
    # The sight by run length: growth a lap refused, BOUND_MB / (laps - 2).
    for laps, under, over in ((8, 5.0, 5.5), (10, 3.9, 4.1), (12, 3.1, 3.3), (20, 1.7, 1.9)):
        check(soak.judge(run_of([900.0 + under * k for k in range(laps)]), whole)[0], "%d laps: %.1f MB a lap passes" % (laps, under))
        check(not soak.judge(run_of([900.0 + over * k for k in range(laps)]), whole)[0], "%d laps: %.1f MB a lap is refused" % (laps, over))
    # Runs of fewer whole laps are judged as before: against lap 2, the most above it (the short soak's three laps, the last one cut).
    ok, said = soak.judge(run_of([900.0 + 7.0 * k for k in range(7)]), whole)
    check(not ok and "the most above lap 2: +35.0 MB" in said, "seven laps, 7 MB a lap: the old rule refuses (the long rule would not: 6.4 is its sight for seven): %s" % said)
    ok, said = soak.judge(run_of([900.0 + 4.0 * k for k in range(7)]), whole)
    check(ok and "the most above lap 2: +20.0 MB" in said, "seven laps, 4 MB a lap: the old rule reads +20.0: %s" % said)
    cut = run_of([900.0 + 4.0 * k for k in range(8)], last=0.5)
    ok, said = soak.judge(cut, whole)
    check("the most above lap 2" in said, "seven whole laps and half of the eighth are judged by the old rule: %s" % said)
    ok, said = soak.judge(run_of([900.0 + 4.0 * k for k in range(9)], last=0.5), whole)
    check("from lap 6 on" in said and "over 8 laps" in said, "eight whole laps and half of the ninth: the long rule on the eight: %s" % said)
    short = run_of([803.0, 813.0, 825.0], last=0.5)
    check(soak.judge(short, whole)[1] == "lap medians 803 813 825 MB; the most above lap 2: +12.0 MB (bound 32)",
          "the short soak's three laps read as before: %s" % soak.judge(short, whole)[1])

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

    # 6. A soak that ends by a crash or a hang says so first, per renderer, with the second, the exception and the stage (T11-B, the logs of
    #    the planted crash after 18000 steps: tools/gate/tests/soak_crash); a memory sentence is left out when the run ended early.
    data = pathlib.Path(__file__).resolve().parent / "soak_crash"
    want = {"d3d11": "crashed at 300.1 s: exception 0xC0000005 at NebulaD11.exe+0x234316 (thread 62664), address 0x0, in stage FactoryGalaxy",
            "d3d12": "crashed at 299.5 s: exception 0xC0000005 at Nebula.exe+0x3eddf6 (thread 51712), address 0x0, in stage FactoryGalaxy",
            "vulkan": "crashed at 294.7 s: exception 0xC0000005 at NebulaVk.exe+0x239f16 (thread 23520), address 0x0, in stage FactoryGalaxy"}
    for renderer, text in want.items():
        # The run's log is kept as run-log.txt (the repository ignores *.log): the run's folder is made with it as run.log.
        folder = pathlib.Path(tempfile.mkdtemp(prefix="soak-crash-"))
        for name in ("result.json",):
            (folder / name).write_bytes((data / renderer / name).read_bytes())
        (folder / "run.log").write_bytes((data / renderer / "run-log.txt").read_bytes())
        early = soak.ended_early(folder)
        check(early == text, "%s: the crash is named by its second, exception and stage: %s" % (renderer, early))
        passes, line = soak.verdict_line(4, False, "1 laps of memory; three are needed to judge growth", early)
        check(not passes and line == "%s (harness exit 4)" % text and "laps" not in line, "%s: the line says the crash first, no memory sentence: %s" % (renderer, line))
    with tempfile.TemporaryDirectory() as tmp:
        folder = pathlib.Path(tmp)
        (folder / "result.json").write_text(json.dumps({"verdict": "hung", "seconds": 143.2, "reason":
            "the game completed no step for 120 s while its process was alive (it stood at frame 8600); no stall dump was written"}), encoding="utf-8")
        early = soak.ended_early(folder)
        check(early == "hung at 143.2 s: no frame for 120 s (it stood at frame 8600)", "a hang says the seconds without a frame: %s" % early)
        (folder / "result.json").write_text(json.dumps({"verdict": "pass", "seconds": 7200.0}), encoding="utf-8")
        check(soak.ended_early(folder) == "", "a run that ended by itself says nothing early")
        passes, line = soak.verdict_line(0, False, "3 laps of memory grew by 40 MB", "")
        check(not passes and line == "harness exit 0; 3 laps of memory grew by 40 MB", "a run that ended by itself keeps the memory sentence: %s" % line)
        passes, line = soak.verdict_line(0, True, "flat", "")
        check(passes, "a flat run that ended by itself passes")

    if failures:
        print("soak_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("soak_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
