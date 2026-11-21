"""pace_test.py: tools/gate/pace.py on run logs whose verdicts are known: the windows it uses, how it combines them,
and each 4.3 verdict, pass and FAIL."""
import json
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import pace  # noqa: E402

LINE = ("[pace] seconds=9.983333 steps=600 steps_per_second=60.0000 pictures={pictures} displayed={displayed} "
        "generated={generated} refresh_ms={refresh} on_time={on_time} missed={missed} doubled=0 "
        "animation_within_1ms={within} animation_p99_us=80.0 delay_avg_us=20000.0 delay_p99_us={delay} "
        "render_avg_us=3000.0 render_p99_us=4000.0 generated_render_avg_us=500.0 generated_render_p99_us=700.0 "
        "step_work_avg_us=3000.0 step_work_p99_us=4000.0 step_work_max_us={longest}")


def line(**k):
    v = dict(pictures=1200, displayed=1200, generated=600, refresh=8.333, on_time=1199, missed=0, within=1.0,
             delay=25000.0, longest=5000.0)
    v.update(k)
    return LINE.format(**v)


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
        code = pace.digest(work)
        v = verdicts(work)
        check(code == 1, "a FAIL among the runs: exit 1")
        check(all(x == "pass" for k, x in v["good"].items() if not k.startswith("in-between")), "good: every row passes")
        check(v["missed"]["even pictures"] == "FAIL", "a missed refresh fails even pictures")
        check(v["smeared"]["animation error"] == "FAIL", "97% within 1 ms fails")
        check(v["slow"]["delay, in-between pictures on"] == "FAIL", "30 ms over a 29 ms bound fails")
        check(v["good"]["delay, in-between pictures on"] == "pass", "25 ms within 29 ms passes")
        check(v["long"]["long step"] == "FAIL", "a 19 ms step fails")
        check(v["unreported"]["even pictures"].startswith("not measured"), "half the pictures reported: not judged")
        check(v["empty"] == {"all": "FAIL"}, "a run with no measured window fails")

    if failures:
        print("pace_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("pace_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
