"""pace_test.py: tools/gate/pace.py on run logs whose verdicts are known: the windows it uses, how it combines them,
each 4.3 verdict and each bar of brief AD (the game's misses, the jumps, the loads against main's readings), pass and FAIL;
the history it keeps; and the gate's suite tests/pace/gate.json (G10.1)."""
import json
import pathlib
import re
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import pace  # noqa: E402

LINE = ("[pace] seconds=9.983333 steps=600 steps_per_second=60.0000 pictures={pictures} displayed={displayed} "
        "generated={generated} refresh_ms={refresh} on_time={on_time} missed={missed} doubled=0 "
        "missed_system={system} jumps={jumps} jump_max_us={jump_max} animation_within_1ms={within} animation_p99_us=80.0 delay_avg_us=20000.0 delay_p99_us={delay} "
        "render_avg_us=3000.0 render_p99_us=4000.0 generated_render_avg_us=500.0 generated_render_p99_us=700.0 "
        "step_work_avg_us=3000.0 step_work_p99_us=4000.0 step_work_max_us={longest}")


def line(**k):
    v = dict(pictures=1200, displayed=1200, generated=600, refresh=8.333, on_time=1199, missed=0, within=1.0,
             delay=25000.0, longest=5000.0, system=0, jumps=0, jump_max=120.0)
    v.update(k)
    return LINE.format(**v)


def old_line(**k):
    """A line of a build before the jump count (G10.1): no missed_system, jumps or jump_max_us."""
    return re.sub(r" missed_system=\S+ jumps=\S+ jump_max_us=\S+", "", line(**k))


def write_run(work, name, lines, settings):
    folder = work / name / "d3d12"
    folder.mkdir(parents=True)
    (folder / "run.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (folder / "test.json").write_text(json.dumps({"name": name, "settings": settings}), encoding="utf-8")


def verdicts(work):
    return {r["run"].split("\\")[0].split("/")[0]: {m: v for m, _, _, v in r["rows"]}
            for r in json.loads((work / "pace.json").read_text(encoding="utf-8"))}


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    # The windows: none before frame 0, and not the first after it.
    log = "\n".join([line(missed=9), "[    1.0] extension: harness: frame 0 is game frame 900", line(missed=8),
                     line(), line()])
    w = pace.windows_of(log)
    check(len(w) == 2 and all(x["missed"] == 0 for x in w), "windows: after frame 0, the first after it left out")
    c = pace.combine(w)
    check(c["pictures"] == 2400 and abs(c["steps_per_second"] - 1198 / (2 * 9.983333)) < 1e-9, "combined counts and rate")
    # A line with a field this reader does not know (cpu_us_per_step, the processor time per step: tools/measure reads it) is read
    # as before: the same windows and combined figures, and the field's own value in the window.
    zero = "[    1.0] extension: harness: frame 0 is game frame 900"
    with_cpu = pace.windows_of("\n".join([zero, line()] + [line() + " cpu_us_per_step=3350.5"] * 2))
    check(len(with_cpu) == 2 and with_cpu[0]["cpu_us_per_step"] == 3350.5 and pace.combine(with_cpu) == pace.combine(
        pace.windows_of("\n".join([zero, line(), line(), line()]))), "a [pace] line with cpu_us_per_step is read as one without it")

    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        zero = "extension: harness: frame 0 is game frame 900"
        good = [zero, line(), line(), line()]
        write_run(work, "good", good, ["GALAXY_MAX_FPS=120", "GALAXY_VSYNC_INTERVAL=1"])
        write_run(work, "missed", [zero, line(), line(missed=1, on_time=1197), line()],
                  ["GALAXY_MAX_FPS=120", "GALAXY_VSYNC_INTERVAL=1"])
        write_run(work, "smeared", [zero, line(), line(within=0.97), line()], ["GALAXY_MAX_FPS=120"])
        # Delay bound with in-between pictures: 16.67 + 8.33 + 4.0 = 29.0 ms.
        write_run(work, "slow", [zero, line(), line(delay=30000.0), line()], ["GALAXY_MAX_FPS=120"])
        write_run(work, "long", [zero, line(), line(longest=19000.0), line()], ["GALAXY_MAX_FPS=120"])
        write_run(work, "unreported", [zero, line(), line(displayed=600, missed=0), line(displayed=600)],
                  ["GALAXY_MAX_FPS=120"])
        write_run(work, "empty", [zero, line()], ["GALAXY_MAX_FPS=120"])
        # R-009: a boot window before frame 0 with 36 pictures of 1200 (a lead held high) fails from the start; the
        # boot's usual few short of the rate pass; 144 on 120 Hz asks 120.
        write_run(work, "held", [line(pictures=36, displayed=36), zero, line(), line(), line()],
                  ["GALAXY_MAX_FPS=120", "GALAXY_VSYNC_INTERVAL=1"])
        write_run(work, "boot", [line(pictures=1140, displayed=1140, missed=30), zero, line(), line(), line()],
                  ["GALAXY_MAX_FPS=144", "GALAXY_VSYNC_INTERVAL=1"])
        code = pace.digest(work)
        v = verdicts(work)
        check(code == 1, "a FAIL among the runs: exit 1")
        check(all(x == "pass" for k, x in v["good"].items() if not k.startswith("in-between") and k != "loads"),
              "good: every row passes")
        check(v["good"]["loads"] == "recorded", "no reading of main's: the loads are recorded, not judged")
        check(v["missed"]["even pictures"] == "FAIL", "a missed refresh fails even pictures")
        check(v["smeared"]["animation error"] == "FAIL", "97% within 1 ms fails")
        check(v["slow"]["delay, in-between pictures on"] == "FAIL", "30 ms over a 29 ms bound fails")
        check(v["good"]["delay, in-between pictures on"] == "pass", "25 ms within 29 ms passes")
        check(v["long"]["long step"] == "FAIL", "a 19 ms step fails")
        check(v["unreported"]["even pictures"].startswith("not measured"), "half the pictures reported: not judged")
        check(v["empty"] == {"all": "FAIL"}, "a run with no measured window fails")
        shown = "pictures shown, from the start"
        check(v["held"][shown] == "FAIL", "36 pictures in a boot window fail from the start")
        check(v["held"]["even pictures"] == "pass", "the measured windows alone would have passed it")
        check(v["boot"][shown] == "pass" and v["good"][shown] == "pass", "95% of the rate in the boot window passes")

    # Bar 1: a miss whose present call came in time is the system's (reported, not refused); the game's fail. Bar 2: a jump fails.
    # A build before the jump count: not measured.
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        zero = "extension: harness: frame 0 is game frame 900"
        settings = ["GALAXY_MAX_FPS=0", "GALAXY_VSYNC_INTERVAL=1"]
        write_run(work, "system", [zero, line(), line(missed=2, system=2, on_time=1197), line()], settings)
        write_run(work, "game", [zero, line(), line(missed=2, system=1, on_time=1197), line()], settings)
        write_run(work, "jumped", [zero, line(), line(jumps=3, jump_max=4200.0), line()], settings)
        write_run(work, "old", [zero, old_line(), old_line(), old_line()], settings)
        pace.digest(work)
        v = verdicts(work)
        check(v["system"]["even pictures"] == "pass", "two misses, both the system's: even pictures pass")
        check(v["game"]["even pictures"] == "FAIL", "one of two misses the game's: even pictures fail")
        check(v["jumped"]["jumps"] == "FAIL" and v["system"]["jumps"] == "pass", "three jumps fail; none pass")
        check(v["old"]["jumps"].startswith("not measured"), "lines with no jumps: not measured, never a pass")
        runs = json.loads((work / "pace.json").read_text(encoding="utf-8"))
        jumped = [r for r in runs if r["run_key"] == "jumped/d3d12"]
        check(len(jumped) == 1 and any("4.20 ms" in value for m, value, _, _ in jumped[0]["rows"] if m == "jumps"),
              "the largest change in ms")

    # The loads (the windows before play): judged against the most of main's last readings of the same run; kept by pace.py keep;
    # with LOADS "none" (from G10.5 on) any miss or jump fails. A first run kept beside the deciding one (--again) is not read.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        zero = "extension: harness: frame 0 is game frame 900"
        settings = ["GALAXY_MAX_FPS=0", "GALAXY_VSYNC_INTERVAL=1"]
        boot = [line(missed=5, system=1, jumps=2), zero, line(missed=1), line(), line()]  # loads: 4 + 1 game misses, 2 jumps
        main_work = root / "main"
        write_run(main_work, "pace-match-30", boot, settings)
        history = root / "pace-history.tsv"
        check(pace.digest(main_work, str(history)) == 0, "main's run: no FAIL with no history")
        check(pace.keep(main_work, str(history), "0123456789abc") == 0 and history.is_file(), "keep writes the history")
        kept = pace.read_history(str(history))
        check(kept == {"pace-match-30/d3d12": [{"commit": "012345678", "load_missed": 5.0, "load_jumps": 2.0}]},
              "the history holds main's load figures by run: %r" % kept)
        same, worse = root / "same", root / "worse"
        write_run(same, "pace-match-30", boot, settings)
        write_run(worse, "pace-match-30", [line(missed=7, jumps=2), zero, line(missed=1), line(), line()], settings)
        write_run(worse / pace.FIRST, "pace-match-30", [zero, line(), line(jumps=9), line()], settings)
        pace.digest(same, str(history))
        pace.digest(worse, str(history))
        check(verdicts(same)["pace-match-30"]["loads"] == "pass", "as many load misses and jumps as main: pass")
        check(verdicts(worse)["pace-match-30"]["loads"] == "FAIL", "8 load misses against main's 5: FAIL")
        check(len(json.loads((worse / "pace.json").read_text(encoding="utf-8"))) == 1, "the first run (WORK/first) is not read")
        saved = pace.LOADS
        try:
            pace.LOADS = "none"
            pace.digest(same, str(history))
            check(verdicts(same)["pace-match-30"]["loads"] == "FAIL", "LOADS none: any load miss fails")
        finally:
            pace.LOADS = saved

    # The gate's suite: one test, the match cut to 30 s of play, shown, at the display's rate with VSync on.
    tree = pathlib.Path(__file__).resolve().parents[3]
    suite = json.loads((tree / "tests/pace/gate.json").read_text(encoding="utf-8"))
    match = json.loads((tree / "tests/pace/match/test.json").read_text(encoding="utf-8"))
    cut = json.loads((tree / "tests/pace" / suite["tests"][0] / "test.json").read_text(encoding="utf-8"))
    check(suite["tests"] == ["match-30"], "tests/pace/gate.json plays match-30 alone")
    check(cut["visible"] is True and cut["time"] == "real" and cut["settings"] == match["settings"] and cut["stage"] == match["stage"],
          "match-30 is the match: shown, real time, its settings and stage")
    check(cut["steps"][-1] == {"at": 1800, "end": True} and cut["steps"][:-1] == [s for s in match["steps"] if s["at"] < 1800],
          "match-30 is the match's steps up to 30 s after frame 0")

    if failures:
        print("pace_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("pace_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
