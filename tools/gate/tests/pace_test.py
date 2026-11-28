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
        check(v["good"]["loads"] == "pass", "no miss and no jump in the load windows: the loads pass")
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

    # The loads (the load windows: the boot's before frame 0 and the first after it): any of the game's missed refreshes or any jump
    # fails (G10.5, brief AD bar 1: the whole run; M-013's report against main's readings ended with it). The history pace.py keep
    # writes is main's record and judges nothing. A first run kept beside the deciding one (--again) is not read.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        zero = "extension: harness: frame 0 is game frame 900"
        settings = ["GALAXY_MAX_FPS=0", "GALAXY_VSYNC_INTERVAL=1"]
        boot = [line(missed=5, system=1, jumps=2), zero, line(missed=1), line(), line()]  # loads: 4 + 1 game misses, 2 jumps
        main_work = root / "main"
        write_run(main_work, "pace-match-30", boot, settings)
        history = root / "pace-history.tsv"
        check(pace.digest(main_work, str(history)) == 1, "main's run with load misses: a FAIL")
        check(verdicts(main_work)["pace-match-30"]["loads"] == "FAIL", "5 game misses and 2 jumps in the load windows: FAIL")
        check(pace.keep(main_work, str(history), "0123456789abc") == 0 and history.is_file(), "keep writes the history")
        kept = pace.read_history(str(history))
        check(kept == {"pace-match-30/d3d12": [{"commit": "012345678", "load_missed": 5.0, "load_jumps": 2.0}]},
              "the history holds main's load figures by run: %r" % kept)
        cases = {
            "clean": [line(), zero, line(), line(), line()],
            "system": [line(missed=3, system=3), zero, line(), line(), line()],
            "one-miss": [line(missed=1), zero, line(), line(), line()],
            "one-jump": [line(jumps=1, jump_max=1500.0), zero, line(), line(), line()],
            "after-zero": [line(), zero, line(jumps=1, jump_max=1500.0), line(), line()],
            "old": [old_line(), zero, line(), line(), line()],
        }
        same = root / "same"
        for name, lines in cases.items():
            write_run(same, name, lines, settings)
        write_run(same / pace.FIRST, "clean", [zero, line(), line(jumps=9), line()], settings)
        pace.digest(same, str(history))
        v = verdicts(same)
        check(v["clean"]["loads"] == "pass", "no miss and no jump in the loads: pass, whatever main's history holds")
        check(v["system"]["loads"] == "pass", "three misses all the system's: pass (bar 1's own split)")
        check(v["one-miss"]["loads"] == "FAIL", "one game miss in a load window: FAIL")
        check(v["one-jump"]["loads"] == "FAIL", "one jump in a load window: FAIL")
        check(v["after-zero"]["loads"] == "FAIL", "a jump in the first window after frame 0 (a load's, the stage's start): FAIL")
        check(v["old"]["loads"] == "FAIL", "a load window whose line carries no jumps cannot pass")
        check(len(json.loads((same / "pace.json").read_text(encoding="utf-8"))) == len(cases), "the first run (WORK/first) is not read")
        check(not hasattr(pace, "LOADS") and not hasattr(pace, "LOAD_GROSS") and not hasattr(pace, "LOAD_SEEN"),
              "M-013's switch, gross break and seen figures are gone")

    # The display lead in play (M-009, brief AD): a change of the lead in play is itself a jump. The real logs of G10.2's landing
    # (gate run 20261126-153832): Direct3D 12 stepped the lead 1->2 in the first play and 2->1 in the second, 88.55% and 89.94% of
    # the animation within 1 ms, delay p99 23.22 and 23.40 ms against 22.52. The old figure no longer refuses; the lead does.
    # G10.5: the line's covered flag says whose a change is (1: a closed wipe or a scene loading, a load's; 0: play's); in play one
    # step up is filed "lead moved" only where its window shows LEAD_MISSES of the game's missed refreshes, every other change fails
    # (M-012's one change either way, a stopgap until the flag, is gone). A line without the flag (a build before G10.5) is a load's
    # between the wipe closing and the scene ready.
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        zero = "extension: harness: frame 0 is game frame 900"
        settings = ["GALAXY_MAX_FPS=0", "GALAXY_VSYNC_INTERVAL=1"]
        up, down = "[display-lead] 1->2 refreshes at qpc 1 covered=0", "[display-lead] 2->1 refreshes at qpc 2 covered=0"
        old_up, old_down = "[display-lead] 1->2 refreshes at qpc 1", "[display-lead] 2->1 refreshes at qpc 2"
        smeared = line(within=0.8855, delay=23220.0, generated=1800, refresh=4.167)
        missed2 = line(missed=2, on_time=1198, within=0.8855, delay=23220.0, generated=1800, refresh=4.167)
        plain = line(generated=1800, refresh=4.167, delay=20000.0)
        wipe, destroyed, ready = ("context: star pointer mode -1 held under the wipe", "game: scene: [destroy Scene: Game]",
                                  "game: scene: [scene ready]")
        cases = {
            "still": [zero, plain, plain, plain],
            "up": [zero, plain, up, smeared, plain],
            "up-missed2": [zero, plain, up, missed2, plain],
            "twice": [zero, plain, up, down, missed2, plain],
            "twiceup": [zero, plain, up, "[display-lead] 2->3 refreshes at qpc 3 covered=0", missed2, plain],
            "down": [zero, plain, down, smeared, plain],
            "down-missed2": [zero, plain, down, missed2, plain],
            "realdown": [zero, plain, "[display-lead] 2->1 refreshes at qpc 11247074565219 covered=1", smeared, plain],
            "covered-up": [zero, plain, "[display-lead] 2->3 refreshes at qpc 4 covered=1", smeared, plain],
            "flag-in-load": [zero, plain, wipe, destroyed, down, ready, plain, plain],
            "old-down": [zero, plain, old_down, smeared, plain],
            "boot": [up, plain, zero, plain, plain, plain],
            "load": [zero, plain, wipe, destroyed, old_up, old_down, ready, plain, plain],
            "afterload": [zero, plain, wipe, destroyed, ready, old_up, missed2, plain],
        }
        for name, lines in cases.items():
            write_run(work, name, lines, settings)
        pace.digest(work)
        v = verdicts(work)
        lead = "display lead in play"
        check(v["still"].get(lead) == "pass", "no lead line: pass")
        check(v["up"].get(lead) == "FAIL", "a step up in play with no misses in its window: FAIL: %s" % v["up"].get(lead))
        check(v["up-missed2"].get(lead) == "lead moved", "a step up in a window of two missed refreshes: filed 'lead moved'")
        check(v["twice"].get(lead) == "FAIL", "a step up and a step down in play: FAIL")
        check(v["twiceup"].get(lead) == "FAIL", "two steps up in play: FAIL")
        check(v["down"].get(lead) == "FAIL" and v["down-missed2"].get(lead) == "FAIL", "a step down in play fails, misses or none")
        check(v["realdown"].get(lead) == "pass", "the real Vulkan line (2->1) with covered=1 is a load's: %s" % v["realdown"].get(lead))
        check(v["covered-up"].get(lead) == "pass", "a step up with covered=1 is a load's, outside the load lines too")
        check(v["flag-in-load"].get(lead) == "FAIL", "covered=0 between the wipe and the scene ready is play's: the flag decides")
        check(v["old-down"].get(lead) == "FAIL", "a line without the flag in play, a step down: FAIL")
        check(v["boot"].get(lead) == "pass", "a lead line before frame 0 (the boot) is not play's")
        check(v["load"].get(lead) == "pass", "lines without the flag between the wipe closing and the scene ready are a load's")
        check(v["afterload"].get(lead) == "lead moved", "a step after the scene ready is play's")
        for name in ("up", "twice", "down"):
            check(v[name]["animation error"] == "recorded" and v[name]["delay, in-between pictures on"] == "recorded",
                  "%s: the animation figure and the delay are reports when the lead moved in play" % name)
        check(v["still"]["delay, in-between pictures on"] == "pass", "no lead change: the delay is judged as before")
        # What stays refused: a delay over its bound with the lead still, a missed refresh, a jump, beside a moved lead.
        write_run(work, "slow-still", [zero, plain, line(delay=40000.0, generated=1800, refresh=4.167), plain], settings)
        write_run(work, "up-missed", [zero, plain, up, line(missed=1, on_time=1197, generated=1800, refresh=4.167), plain], settings)
        write_run(work, "up-jumped", [zero, plain, up, line(missed=2, on_time=1198, jumps=2, jump_max=3000.0, generated=1800,
                                                            refresh=4.167), plain], settings)
        pace.digest(work)
        v = verdicts(work)
        check(v["slow-still"]["delay, in-between pictures on"] == "FAIL", "a delay over its bound with the lead still: FAIL")
        check(v["up-missed"]["even pictures"] == "FAIL" and v["up-missed"].get(lead) == "FAIL",
              "one missed refresh: bar 1 refuses, and a step up after one miss is not the repeated misses' step")
        check(v["up-jumped"]["jumps"] == "FAIL" and v["up-jumped"].get(lead) == "lead moved", "bar 2 still refuses beside a moved lead")
        runs = json.loads((work / "pace.json").read_text(encoding="utf-8"))
        got = {r["run_key"]: r.get("leads") for r in runs}
        check(got["up-missed2/d3d12"] == [{"before": 1, "after": 2, "load": False, "misses": 2.0}],
              "the lead and its window's misses are kept: %r" % got["up-missed2/d3d12"])
        check(got["load/d3d12"] == [{"before": 1, "after": 2, "load": True, "misses": 0.0}, {"before": 2, "after": 1, "load": True, "misses": 0.0}],
              "leads in a load are kept, marked: %r" % got["load/d3d12"])
        check(got["realdown/d3d12"][0]["load"] is True and got["flag-in-load/d3d12"][0]["load"] is False, "the flag marks the lead's owner")
        check(pace.LEAD_MISSES == 2, "the step up waits for two missed refreshes in its window")

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

    # G10.5: the rows of the stored baseline refuse by rate (BASELINE_REFUSES): at 120 and 60 Hz the mean refreshes from a step to
    # the screen and the jumps a minute, the jumps held to 0 (BASELINE_HELD, brief AD bar 2) whatever the stored bound; G10.4m's gross
    # break of the jumps (BASELINE_GROSS) is gone. A device run prints what the screen showed and the renderer's depth report.
    ft = pace.frametimeline
    check(pace.BASELINE_REFUSES == {"120": ("refreshes_mean", "jumps_per_minute"), "60": ("refreshes_mean", "jumps_per_minute")}
          and all(row in ft.ROWS for rows in pace.BASELINE_REFUSES.values() for row in rows),
          "the shipped rows of the baseline: at both rates the mean refreshes and the jumps: %s" % (pace.BASELINE_REFUSES,))
    check(pace.BASELINE_HELD == {"jumps_per_minute": 0.0} and not hasattr(pace, "BASELINE_GROSS"),
          "the jumps a minute held to 0; no gross break: %s" % (pace.BASELINE_HELD,))
    stored = {"method": "latency", "start": "step", "rates": {
        "120": {"bounds": {"late_share": 0.2, "refreshes_mean": 4.2, "jumps_per_minute": 160.0, "events_per_minute": 557.0}},
        "60": {"bounds": {"late_share": 0.0, "refreshes_mean": 3.07, "jumps_per_minute": 28.0, "events_per_minute": 5.3}}}}

    def device_rows(hz, late, mean, events, jumps):
        report = {"method": "latency", "start": "step", "refresh_ms": 1000.0 / hz, "frames": 100, "refresh_frames": 100, "misses": 0,
                  "refreshes": {b: 0 for b in ft.BINS}, "causes": {c: 0 for c in ft.CAUSES}, "buffer_stuffing": 0, "gpu_frames": 0,
                  "late_share": late, "three_refresh_share": 0.9, "unnamed_share": late, "refreshes_mean": mean, "notes": [],
                  "shown": {"pairs": 90, "repeated": 5, "skipped": 1, "shifted": 0, "events_per_minute": events, "jumps_per_minute": jumps}}
        return {m: (v, verdict) for m, v, _t, verdict in pace.frametimeline_rows({"frametimeline": report, "present_timing": None}, stored, "base.json")}
    clean = device_rows(120, 0.1, 4.0, 300.0, 0.0)
    check(clean[pace.BASELINE_ROW][1] == "pass" and pace.SCREEN_ROW in clean and clean[pace.SCREEN_ROW][1] == "recorded",
          "a clean 120 Hz run, no jump: the baseline row passes, the screen row is printed: %s" % (clean[pace.BASELINE_ROW],))
    sleepy = device_rows(120, 0.1, 4.8, 1200.0, 450.0)
    check(sleepy[pace.BASELINE_ROW][1] == "FAIL" and "refusing: refreshes_mean, jumps_per_minute" in sleepy[pace.BASELINE_ROW][0],
          "the sleep's run at 120 Hz (mean refreshes 4.8, 450 jumps a minute): refused by those rows: %s" % (sleepy[pace.BASELINE_ROW],))
    one = device_rows(120, 0.1, 4.0, 300.0, 0.5)  # one jump in a two-minute run, under the stored bound of 160
    check(one[pace.BASELINE_ROW][1] == "FAIL" and "refusing: jumps_per_minute" in one[pace.BASELINE_ROW][0] and "(bound 0.0)" in one[pace.BASELINE_ROW][0],
          "one jump in a run at 120 Hz is refused against the held 0, not the stored 160: %s" % (one[pace.BASELINE_ROW],))
    depth60 = device_rows(60, 0.0, 3.6, 5.0, 0.0)  # the depth plant: every picture a refresh later
    check(depth60[pace.BASELINE_ROW][1] == "FAIL" and "refusing: refreshes_mean" in depth60[pace.BASELINE_ROW][0],
          "a run at 60 Hz one refresh deeper (mean 3.6 against 3.07): refused by the mean refreshes: %s" % (depth60[pace.BASELINE_ROW],))
    sleepy60 = device_rows(60, 0.003, 3.0, 25.0, 6.0)
    check(sleepy60[pace.BASELINE_ROW][1] == "FAIL" and "refusing: jumps_per_minute" in sleepy60[pace.BASELINE_ROW][0]
          and "late_share" not in sleepy60[pace.BASELINE_ROW][0].split("refusing:")[1],
          "the sleep's run at 60 Hz (6 jumps a minute): refused by the jumps; the late share beyond its bound refuses nothing: %s" % (
              sleepy60[pace.BASELINE_ROW],))
    late60 = device_rows(60, 0.003, 3.0, 25.0, 0.0)
    check(late60[pace.BASELINE_ROW][1] == "recorded" and "a report: late_share, events_per_minute" in late60[pace.BASELINE_ROW][0],
          "at 60 Hz the late share and the events beyond their bounds, no jump: a report: %s" % (late60[pace.BASELINE_ROW],))
    # The renderer's depth report (renderer_vk.cpp, G10.5): summed after frame 0 and recorded.
    depth_log = "\n".join(["[present-timing] 120 frames: x; depth: steps in play 0, late after a mode change or stage load 3, seconds above two steps 0.0",
                           "extension: harness: frame 0 is game frame 900",
                           "[present-timing] 1200 frames: x; depth: steps in play 1, late after a mode change or stage load 2, seconds above two steps 0.0",
                           "[present-timing] 1200 frames: x; depth: steps in play 0, late after a mode change or stage load 0, seconds above two steps 2.5"])
    check(pace.depth_of(depth_log) == (1, 2, 2.5), "the depth figures after frame 0 are summed: %r" % (pace.depth_of(depth_log),))
    check(pace.depth_of("[present-timing] 1 frames: 0 late") is None and pace.depth_row(None)[3].startswith("not measured"),
          "no depth report: not measured")
    check(pace.depth_row((1, 2, 2.5))[3] == "recorded", "the depth row is a report")

    if failures:
        print("pace_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("pace_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
