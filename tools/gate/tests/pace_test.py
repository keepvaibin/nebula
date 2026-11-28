"""pace_test.py: tools/gate/pace.py on run logs whose verdicts are known: the windows it uses, how it combines them,
each 4.3 verdict and each bar of brief AD (the game's misses, the jumps, the loads against main's readings), pass and FAIL;
the [platform] lines of what the platform offers (G10.3: the header, and a device run's row when one is missing);
the history it keeps; the gate's suite tests/pace/gate.json (G10.1); and run's harness locks and quiet wait (a fake harness)."""
import contextlib
import io
import json
import pathlib
import re
import sys
import tempfile
import types

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
        check(all(x == "pass" for k, x in v["good"].items() if not k.startswith("in-between") and k not in ("loads", "animation error")),
              "good: every row passes")
        check(v["good"]["animation error"] == "recorded", "good: the animation error is a report")
        check(v["good"]["loads"] == "recorded", "no reading of main's: the loads are recorded, not judged")
        check(v["missed"]["even pictures"] == "FAIL", "a missed refresh fails even pictures")
        check(v["smeared"]["animation error"] == "recorded" and v["smeared"]["jumps"] == "pass",
              "97% within 1 ms is reported (brief AD: the old figure refuses nothing); the jumps judge")
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

    # The loads (the windows before play): a report with LOADS "main" (M-013), refused only above twice the most of the readings of
    # the renderer; kept by pace.py keep; with LOADS "none" (from G10.5 on) any miss or jump fails. A first run kept beside the
    # deciding one (--again) is not read.
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
        check(verdicts(same)["pace-match-30"]["loads"] == "recorded", "as many load misses and jumps as main: recorded")
        check(verdicts(worse)["pace-match-30"]["loads"] == "recorded", "8 load misses against main's 5: recorded, a report")
        check(len(json.loads((worse / "pace.json").read_text(encoding="utf-8"))) == 1, "the first run (WORK/first) is not read")
        saved = pace.LOADS
        try:
            pace.LOADS = "none"
            pace.digest(same, str(history))
            check(verdicts(same)["pace-match-30"]["loads"] == "FAIL", "LOADS none: any load miss fails")
        finally:
            pace.LOADS = saved

    # The loads row is a report until G10.5 (M-013, the planner's order of 11-27): it prints "recorded" and refuses only a gross break,
    # a count above twice the most of the renderer's readings since G10.1 landed, misses and jumps separately. The real figures:
    # Vulkan's load windows gave 6, 11, 24, 27 missed on identical code (G10.3 6, G10.1 11, G10.2 24, G10.4 27), Direct3D 12's 44 to 47.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        zero = "extension: harness: frame 0 is game frame 900"
        settings = ["GALAXY_MAX_FPS=0", "GALAXY_VSYNC_INTERVAL=1"]
        history = root / "pace-history.tsv"
        check(pace.LOAD_GROSS == 2, "the gross break is twice the most")

        def keep_history(run, readings):
            rows = ["\t".join(pace.HISTORY_COLUMNS)] + ["\t".join(["2026-11-26 12:19:08", "7ca0d4e54", run, str(m), str(j), "2", "0", "0", "0"])
                                                      for m, j in readings]
            history.write_text("\n".join(rows) + "\n", encoding="utf-8")

        def loads_of(n, missed, jumps, readings, renderer="d3d12", seen=False):
            keep_history("pace-match-30/" + renderer, readings)
            work = root / ("run%d" % n)
            folder = work / "pace-match-30" / renderer
            folder.mkdir(parents=True)
            (folder / "run.log").write_text("\n".join([line(missed=missed, jumps=jumps), zero, line(), line(), line()]) + "\n", encoding="utf-8")
            (folder / "test.json").write_text(json.dumps({"name": "pace-match-30", "settings": settings}), encoding="utf-8")
            saved = dict(pace.LOAD_SEEN)
            try:
                if not seen:
                    pace.LOAD_SEEN.clear()
                code = pace.digest(work, str(history))
            finally:
                pace.LOAD_SEEN.clear()
                pace.LOAD_SEEN.update(saved)
            check(code == (1 if any(v == "FAIL" for v in verdicts(work)["pace-match-30"].values()) else 0),
                  "digest's exit status follows the FAIL rows")
            return verdicts(work)["pace-match-30"]["loads"]

        vk = [(6, 46), (11, 46), (24, 44), (27, 44)]
        check(loads_of(1, 54, 44, vk, "vulkan") == "recorded", "Vulkan, readings 6, 11, 24, 27: 54 missed (twice the most) is recorded")
        check(loads_of(2, 55, 44, vk, "vulkan") == "FAIL", "Vulkan: 55 missed, above twice the most: FAIL")
        check(loads_of(3, 22, 44, [(11, 46)], "vulkan") == "recorded" and loads_of(19, 23, 44, [(11, 46)], "vulkan") == "FAIL",
              "one reading of 11: 22 missed is recorded, 23 FAIL")
        check(loads_of(4, 27, 92, vk, "vulkan") == "recorded", "Vulkan: 92 jumps (twice the most, 46) is recorded")
        check(loads_of(5, 27, 93, vk, "vulkan") == "FAIL", "Vulkan: 93 jumps, above twice the most: FAIL")
        dx = [(44, 162), (46, 169), (47, 159)]
        check(loads_of(6, 94, 162, dx) == "recorded", "Direct3D 12, readings 44, 46, 47: 94 missed is recorded")
        check(loads_of(7, 95, 162, dx) == "FAIL", "Direct3D 12: 95 missed: FAIL")
        check(loads_of(8, 45, 338, dx) == "recorded" and loads_of(9, 45, 339, dx) == "FAIL", "Direct3D 12: 338 jumps recorded, 339 FAIL")
        check(loads_of(10, 5000, 5000, [], "d3d12") == "recorded", "no readings of the renderer: recorded, never refused")
        check(loads_of(11, 5000, 5000, [(3, 10)], "metal") == "FAIL", "a renderer's own readings count: 3 missed, 10 jumps: 5000 FAIL")
        check(loads_of(12, 30, 20, [(3, 10)], "metal") == "FAIL" and loads_of(13, 6, 20, [(3, 10)], "metal") == "recorded",
              "metal: 30 missed above 6 fails, 6 missed and 20 jumps (twice the most) is recorded")
        check(loads_of(14, 54, 44, [(6, 46)], "vulkan", seen=True) == "recorded" and loads_of(15, 55, 44, [(6, 46)], "vulkan", seen=True) == "FAIL",
              "the most seen before the history (LOAD_SEEN: Vulkan 27) counts when the history holds less")
        check(pace.LOAD_SEEN == {"vulkan": (27, 46), "d3d12": (47, 172)}, "LOAD_SEEN holds the most seen since G10.1: %r" % pace.LOAD_SEEN)
        check(loads_of(16, 94, 344, [(44, 162)], seen=True) == "recorded" and loads_of(17, 95, 344, [(44, 162)], seen=True) == "FAIL"
              and loads_of(18, 94, 345, [(44, 162)], seen=True) == "FAIL", "Direct3D 12 with the seen most (47 missed, 172 jumps): 94 and 344 pass, above FAIL")

    # The display lead in play (M-009, brief AD): a change of the lead in play is itself a jump. The real logs of G10.2's landing
    # (gate run 20261126-153832): Direct3D 12 stepped the lead 1->2 in the first play and 2->1 in the second, 88.55% and 89.94% of
    # the animation within 1 ms, delay p99 23.22 and 23.40 ms against 22.52. The old figure no longer refuses; the lead does.
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        zero = "extension: harness: frame 0 is game frame 900"
        settings = ["GALAXY_MAX_FPS=0", "GALAXY_VSYNC_INTERVAL=1"]
        up, down = "[display-lead] 1->2 refreshes at qpc 1", "[display-lead] 2->1 refreshes at qpc 2"
        smeared = line(within=0.8855, delay=23220.0, generated=1800, refresh=4.167)
        plain = line(generated=1800, refresh=4.167, delay=20000.0)
        wipe, destroyed, ready = ("context: star pointer mode -1 held under the wipe", "game: scene: [destroy Scene: Game]",
                                  "game: scene: [scene ready]")
        cases = {
            "still": [zero, plain, plain, plain],
            "up": [zero, plain, up, smeared, plain],
            "twice": [zero, plain, up, down, smeared, plain],
            "twiceup": [zero, plain, up, "[display-lead] 2->3 refreshes at qpc 3", smeared, plain],
            "down": [zero, plain, down, smeared, plain],
            "downup": [zero, plain, down, up, smeared, plain],
            "twodown": [zero, plain, down, "[display-lead] 3->2 refreshes at qpc 3", smeared, plain],
            "realdown": [zero, plain, "[display-lead] 2->1 refreshes at qpc 11247074565219", smeared, plain],
            "boot": [up, plain, zero, plain, plain, plain],
            "load": [zero, plain, wipe, destroyed, up, down, ready, plain, plain],
            "afterload": [zero, plain, wipe, destroyed, ready, up, smeared, plain],
        }
        for name, lines in cases.items():
            write_run(work, name, lines, settings)
        pace.digest(work)
        v = verdicts(work)
        lead = "display lead in play"
        check(v["still"].get(lead) == "pass", "no lead line: pass")
        check(v["up"].get(lead) == "lead moved", "one step up in play: filed 'lead moved', refused nothing: %s" % v["up"].get(lead))
        check(v["twice"].get(lead) == "FAIL", "a step up and a step down in play: FAIL")
        check(v["twiceup"].get(lead) == "FAIL", "two steps up in play: FAIL")
        # M-012: until G10.5 (no covered flag on the line yet) ONE lead change in a run, up or down, is filed "lead moved"; two fail.
        check(v["down"].get(lead) == "lead moved", "one step down in play: filed 'lead moved', refused nothing: %s" % v["down"].get(lead))
        check(v["realdown"].get(lead) == "lead moved", "the real Vulkan line of 20261126-233516 (2->1): filed 'lead moved': %s" % v["realdown"].get(lead))
        check(v["downup"].get(lead) == "FAIL", "a step down and a step up in play: FAIL")
        check(v["twodown"].get(lead) == "FAIL", "two steps down in play: FAIL")
        check(v["boot"].get(lead) == "pass", "a lead line before frame 0 (the boot) is not play's")
        check(v["load"].get(lead) == "pass", "lead changes between the wipe closing and the scene ready are a load's")
        check(v["afterload"].get(lead) == "lead moved", "a step after the scene ready is play's")
        for name in ("up", "twice", "down"):
            check(v[name]["animation error"] == "recorded" and v[name]["delay, in-between pictures on"] == "recorded",
                  "%s: the animation figure and the delay are reports when the lead moved in play" % name)
        check(v["still"]["delay, in-between pictures on"] == "pass", "no lead change: the delay is judged as before")
        # What stays refused: a delay over its bound with the lead still, a missed refresh, a jump, beside a moved lead.
        write_run(work, "slow-still", [zero, plain, line(delay=40000.0, generated=1800, refresh=4.167), plain], settings)
        write_run(work, "up-missed", [zero, plain, up, line(missed=1, on_time=1197, generated=1800, refresh=4.167), plain], settings)
        write_run(work, "up-jumped", [zero, plain, up, line(jumps=2, jump_max=3000.0, generated=1800, refresh=4.167), plain], settings)
        pace.digest(work)
        v = verdicts(work)
        check(v["slow-still"]["delay, in-between pictures on"] == "FAIL", "a delay over its bound with the lead still: FAIL")
        check(v["up-missed"]["even pictures"] == "FAIL" and v["up-missed"].get(lead) == "lead moved", "bar 1 still refuses beside a moved lead")
        check(v["up-jumped"]["jumps"] == "FAIL", "bar 2 still refuses beside a moved lead")
        runs = json.loads((work / "pace.json").read_text(encoding="utf-8"))
        got = {r["run_key"]: r.get("leads") for r in runs}
        check(got["up/d3d12"] == [{"before": 1, "after": 2, "load": False, "misses": 0.0}], "the lead and its window's misses are kept: %r" % got["up/d3d12"])
        check(got["load/d3d12"] == [{"before": 1, "after": 2, "load": True, "misses": 0.0}, {"before": 2, "after": 1, "load": True, "misses": 0.0}],
              "leads in a load are kept, marked: %r" % got["load/d3d12"])
        # From G10.5 on (LOADS "none") a step up counts only after repeated misses in its window; every other change fails.
        saved = pace.LOADS
        try:
            pace.LOADS = "none"
            write_run(work, "none-missed", [zero, plain, up, line(missed=2, on_time=1198, generated=1800, refresh=4.167), plain], settings)
            pace.digest(work)
            v = verdicts(work)
            check(v["up"].get(lead) == "FAIL", "LOADS none: a step up with no misses fails")
            check(v["none-missed"].get(lead) == "lead moved", "LOADS none: a step up in a window of two missed refreshes is reported")
            check(v["twice"].get(lead) == "FAIL" and v["down"].get(lead) == "FAIL" and v["still"].get(lead) == "pass", "LOADS none: steps down and a second step fail")
        finally:
            pace.LOADS = saved

    # What the platform offers (G10.3): a device run prints the game's [platform] lines in its header and holds one line of each kind
    # with a value, the frame timeline's at the run's own rate; a line missing, a "not supported" with no reason, or a frame timeline
    # of another rate only is a FAIL of the row. A PC run has no such row.
    offers = ["[platform] hint-features: not supported (the system has no APerformanceHint_isFeatureSupported, API 36)",
              "[platform] hint-calls: present: APerformanceHint_setThreads; absent: APerformanceHint_isFeatureSupported",
              "[platform] frame-timeline: 120.0 Hz: expected present less frame time 2.00 refreshes (2.00 to 2.00 over 32 callbacks; "
              "preferred timeline 1 of 3)",
              "[platform] game-mode: standard; sustained performance: not supported",
              "[platform] thermal-thresholds: light=0.700 moderate=0.850 severe=1.000",
              "[platform] vulkan-images: surface minimum 4, maximum 64; asked 5, made 5 (PRESENT_MODE_FIFO)"]
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        zero = "extension: harness: frame 0 is game frame 900"
        settings = ["GALAXY_MAX_FPS=120", "GALAXY_VSYNC_INTERVAL=1"]

        def device_run(name, lines):
            write_run(work, name, lines + [zero, line(), line(), line()], settings)
            (work / name / "d3d12" / "exit-info.txt").write_text("exit 0\n", encoding="utf-8")

        device_run("all", offers + offers[2:3])  # a line written again is printed once
        device_run("nothermal", [o for o in offers if "thermal-thresholds" not in o])
        device_run("noreason", offers[1:] + ["[platform] hint-features: not supported"])
        device_run("otherrate", [o.replace("120.0 Hz", "60.0 Hz") for o in offers])
        device_run("unsupported", [o for o in offers if "frame-timeline" not in o] +
                   ["[platform] frame-timeline: not supported (the system has no AChoreographer_postVsyncCallback with its frame "
                    "timelines, API 33)"])
        write_run(work, "pc", offers + [zero, line(), line(), line()], settings)
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            pace.digest(work)
        blocks = {}
        for text in printed.getvalue().splitlines():
            if not text.startswith("  "):
                blocks[text.split("\\")[0].split("/")[0]] = current = []
            else:
                current.append(text)
        v = verdicts(work)
        row = pace.PLATFORM_ROW
        check(v["all"].get(row) == "pass", "every line, the frame timeline at 120 Hz: pass (%s)" % v["all"].get(row))
        check(v["nothermal"].get(row) == "FAIL", "the thermal thresholds' line missing: FAIL")
        check(v["noreason"].get(row) == "FAIL", "'not supported' with no reason is no value: FAIL")
        check(v["otherrate"].get(row) == "FAIL", "a frame timeline at 60 Hz only, in a run at 120 Hz: FAIL")
        check(v["unsupported"].get(row) == "pass", "a frame timeline not supported, with its reason: pass")
        check(row not in v["pc"], "a PC run has no row of what the platform offers")
        runs = {r["run_key"]: r for r in json.loads((work / "pace.json").read_text(encoding="utf-8"))}
        missing = [value for m, value, _, _ in runs["nothermal/d3d12"]["rows"] if m == row]
        check(missing == ["4 of 5; missing: thermal-thresholds"], "the row names the missing kind: %s" % missing)
        header = [p for p in blocks.get("all", []) if p.startswith("  platform ")]
        check(header == ["  platform " + o[len("[platform] "):] for o in offers],
              "the header prints the run's lines in their order, a line written again once: %s" % header)
        check(len([p for p in blocks.get("pc", []) if p.startswith("  platform ")]) == len(offers), "a PC run's header prints its lines too")

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

    # The harness reads a list of tests only from a file named suite.json: pace.py turns another list into runs it reads (the landing
    # of 11-26 09:51 gave it tests/pace/gate.json, read as a test folder). A list that cannot be played stops pace.py at once.
    pace_dir = tree / "tests" / "pace"
    check(pace.harness_calls(str(pace_dir / "gate.json")) == [[str(pace_dir / "match-30")]], "gate.json: match-30 from its own folder")
    check(pace.harness_calls(str(pace_dir / "tablet.json")) == [[str(pace_dir / "suite.json"), "--test", "rate-60", "--test", "rate-120"]],
          "tablet.json: one run of suite.json with --test for its two tests")
    check(pace.harness_calls(str(pace_dir / "short.json")) == [[str(pace_dir / "suite.json"), "--test", "rate-120"]], "short.json")
    check(pace.harness_calls(str(pace_dir / "suite.json")) == [[str(pace_dir / "suite.json")]], "suite.json as it is")
    check(pace.harness_calls(str(pace_dir / "match")) == [[str(pace_dir / "match")]], "a test folder as it is")
    with tempfile.TemporaryDirectory() as tmp:
        bad = pathlib.Path(tmp)
        (bad / "list.json").write_text(json.dumps({"harness": 1, "tests": ["nowhere"]}), encoding="utf-8")
        (bad / "odd.json").write_text(json.dumps({"tests": "match"}), encoding="utf-8")
        for name, what in (("list.json", "no test.json"), ("odd.json", "a list of tests is"), ("gone.json", "is not a test folder")):
            try:
                pace.harness_calls(str(bad / name))
                check(False, "%s: refused" % name)
            except SystemExit as said:
                check(what in str(said), "%s: %s" % (name, said))

    # pace.py run: the harness gets a run lock of its own for each run (it refuses a run without one: "give --lock or
    # LUMA_TEST_LOCK"), the run played again another. A machine still busy after the quiet wait plays nothing more: the
    # renderers are named in pace-run.json, the exit is NOT_QUIET (the machine's), and a FAIL that was played stays a FAIL.
    def run_args(work, **k):
        a = dict(bin="B", work=str(work), suite=str(tree / "tests/pace/gate.json"), renderers="d3d12,vulkan", device="", apk="", diagnostics="", paks="",
                 lock=str(work.parent / "measure.lock"), lock_wait=0.0, quiet_minutes=None, history="", again=False)
        a.update(k)
        return types.SimpleNamespace(**a)

    def fake_harness(commands, failing=()):
        def run(command, env=None, check=False):
            commands.append(command)
            folder = pathlib.Path(command[command.index("--work") + 1])
            bad = folder.name in failing and not any(c[command.index("--work") + 1] == str(folder) for c in commands[:-1])
            run_folder = folder / "match-30" / folder.name
            run_folder.mkdir(parents=True)
            (run_folder / "run.log").write_text("\n".join(["extension: harness: frame 0 is game frame 900", line(),
                                                           line(jumps=3 if bad else 0), line()]) + "\n", encoding="utf-8")
            (run_folder / "test.json").write_text(json.dumps({"settings": ["GALAXY_MAX_FPS=0", "GALAXY_VSYNC_INTERVAL=1"]}),
                                                  encoding="utf-8")
            return types.SimpleNamespace(returncode=0)
        return types.SimpleNamespace(run=run)

    saved = (pace.subprocess, pace.machine_quiet)
    try:
        with tempfile.TemporaryDirectory() as tmp:
            work = pathlib.Path(tmp) / "w"
            commands = []
            pace.subprocess = fake_harness(commands, failing=("vulkan",))
            code = pace.run(run_args(work, again=True))
            locks = [c[c.index("--lock") + 1] if "--lock" in c else None for c in commands]
            check(code == 0 and len(commands) == 3, "d3d12 once, vulkan twice (a FAIL, played again): exit %d, %d runs" % (code, len(commands)))
            check(locks == [str(work / "harness-d3d12.lock"), str(work / "harness-vulkan.lock"), str(work / "harness-vulkan-again.lock")],
                  "each run has its own harness lock in WORK: %s" % locks)
            check(not pathlib.Path(tmp, "measure.lock").exists(), "the measurement lock is given back")

            looks = []
            pace.machine_quiet = lambda minutes: looks.append(minutes) or (False, "still busy after 30 minutes: Nebula.exe (pid 9)")
            busy_work, commands = pathlib.Path(tmp) / "busy", []
            pace.subprocess = fake_harness(commands)
            code = pace.run(run_args(busy_work, quiet_minutes=30.0, again=True))
            said = json.loads((busy_work / "pace-run.json").read_text(encoding="utf-8"))
            check(code == pace.NOT_QUIET and commands == [] and looks == [30.0],
                  "a busy machine: nothing played, one wait, exit NOT_QUIET (exit %d, %d runs, waits %s)" % (code, len(commands), looks))
            check(sorted(said["not_quiet"]) == ["d3d12", "vulkan"] and said["harness_exit"] == {"d3d12": None, "vulkan": None},
                  "pace-run.json names both renderers as not played: %s" % said)

            answers = iter([(True, ""), (False, "still busy after 30 minutes: Nebula.exe (pid 9)")])
            pace.machine_quiet = lambda minutes: next(answers)
            fail_work, commands = pathlib.Path(tmp) / "fail", []
            pace.subprocess = fake_harness(commands, failing=("d3d12",))
            code = pace.run(run_args(fail_work, quiet_minutes=30.0))
            said = json.loads((fail_work / "pace-run.json").read_text(encoding="utf-8"))
            check(code == 1 and len(commands) == 1 and list(said["not_quiet"]) == ["vulkan"],
                  "d3d12 played on a quiet machine with a FAIL, vulkan not played: exit 1 (exit %d, %d runs, %s)" % (
                      code, len(commands), said))
    finally:
        pace.subprocess, pace.machine_quiet = saved

    # G10.4m: the rows of the stored baseline refuse by rate (BASELINE_REFUSES), and a device run prints what the screen showed.
    ft = pace.frametimeline
    check(sorted(pace.BASELINE_REFUSES) == ["120", "60"] and pace.BASELINE_REFUSES["60"] == ()
          and pace.BASELINE_REFUSES["120"] == ("refreshes_mean", "jumps_per_minute")
          and all(row in ft.ROWS for rows in pace.BASELINE_REFUSES.values() for row in rows),
          "the shipped rows of the baseline: at 120 Hz the mean refreshes and the jumps, at 60 Hz none: %s" % (pace.BASELINE_REFUSES,))
    stored = {"method": "latency", "start": "step", "rates": {
        "120": {"bounds": {"late_share": 0.2, "refreshes_mean": 4.2, "jumps_per_minute": 160.0, "events_per_minute": 557.0}},
        "60": {"bounds": {"late_share": 0.0, "refreshes_mean": 3.07, "jumps_per_minute": 0.0, "events_per_minute": 5.3}}}}

    def device_rows(hz, late, mean, events, jumps):
        report = {"method": "latency", "start": "step", "refresh_ms": 1000.0 / hz, "frames": 100, "refresh_frames": 100, "misses": 0,
                  "refreshes": {b: 0 for b in ft.BINS}, "causes": {c: 0 for c in ft.CAUSES}, "buffer_stuffing": 0, "gpu_frames": 0,
                  "late_share": late, "three_refresh_share": 0.9, "unnamed_share": late, "refreshes_mean": mean, "notes": [],
                  "shown": {"pairs": 90, "repeated": 5, "skipped": 1, "shifted": 0, "events_per_minute": events, "jumps_per_minute": jumps}}
        return {m: (v, verdict) for m, v, _t, verdict in pace.frametimeline_rows({"frametimeline": report, "present_timing": None}, stored, "base.json")}
    clean = device_rows(120, 0.1, 4.0, 300.0, 100.0)
    check(clean[pace.BASELINE_ROW][1] == "pass" and pace.SCREEN_ROW in clean and clean[pace.SCREEN_ROW][1] == "recorded",
          "a clean 120 Hz run: the baseline row passes, the screen row is printed: %s" % (clean[pace.BASELINE_ROW],))
    sleepy = device_rows(120, 0.1, 4.8, 1200.0, 450.0)
    check(sleepy[pace.BASELINE_ROW][1] == "FAIL" and "refusing: refreshes_mean, jumps_per_minute" in sleepy[pace.BASELINE_ROW][0],
          "the sleep's run at 120 Hz (mean refreshes 4.8, 450 jumps a minute): refused by those rows only: %s" % (sleepy[pace.BASELINE_ROW],))
    sleepy60 = device_rows(60, 0.003, 3.1, 25.0, 6.0)
    check(sleepy60[pace.BASELINE_ROW][1] == "recorded" and "a report: late_share, refreshes_mean, events_per_minute, jumps_per_minute" in sleepy60[pace.BASELINE_ROW][0],
          "the same run at 60 Hz: beyond its bounds and a report, nothing refused: %s" % (sleepy60[pace.BASELINE_ROW],))

    if failures:
        print("pace_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("pace_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
