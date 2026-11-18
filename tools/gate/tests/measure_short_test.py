"""measure_short_test.py: tools/gate/measure_short.py without the game (N-1043).

python -X utf8 -I tools/gate/tests/measure_short_test.py

- Each run measures the next renderer in turn (d3d12, vulkan, d3d11), by the history's count.
- With a stand-in for the two parts (a pacing run.log with [pace] lines, a benchmark result): the figures become one
  history line, and with fewer than three earlier lines of that renderer there is no spread yet (pass).
- A figure worse than the spread of the renderer's last ten lines fails, by name and in its own direction (a lower
  frame rate, a longer delay, steps a second further from 60); within the spread passes; other renderers' lines are
  not its spread.
- A part that measured nothing (no [pace] window, no complete benchmark) fails and says which.
Exit 0 when every case holds.
"""
import json
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import measure_short as ms  # noqa: E402

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


PACE = ("[pace] seconds=10.0 steps=601 steps_per_second=60.0000 pictures=1200 displayed=1200 generated=600 "
        "refresh_ms=8.333 on_time=1199 missed=0 doubled=0 animation_within_1ms={within} animation_p99_us=80.0 "
        "delay_avg_us=20000.0 delay_p99_us={delay} render_avg_us=3000.0 render_p99_us=4000.0 "
        "generated_render_avg_us=500.0 generated_render_p99_us=700.0 step_work_avg_us=3000.0 step_work_p99_us=4000.0 "
        "step_work_max_us=5000.0")


def fake(within=1.0, delay=25000.0, fps=1000.0, pace_ok=True, bench_ok=True):
    def runner(cmd, **kw):
        if "luma_test.py" in " ".join(cmd):
            work = pathlib.Path(cmd[cmd.index("--work") + 1]) / "rate-120" / "d3d12"
            work.mkdir(parents=True, exist_ok=True)
            lines = ["harness: frame 0 is game frame 1"] + [PACE.format(within=within, delay=delay)] * 3 if pace_ok else []
            (work / "run.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
        else:
            out = pathlib.Path(cmd[cmd.index("--benchmark-out") + 1])
            if bench_ok:
                out.write_text(json.dumps({"status": "complete", "summary": {"average_fps": fps, "low_1_fps": fps / 2}}),
                               encoding="utf-8")
    return runner


with tempfile.TemporaryDirectory() as tmp:
    t = pathlib.Path(tmp)
    history = t / "measure-history.tsv"

    def run(**k):
        args = ms.main.__globals__["argparse"].Namespace(bin=str(t / "bin"), pak="p.pak", paks="", work=str(t / "w"),
                                                        commit="a" * 40, renderer=k.pop("renderer", None),
                                                        history=str(history), lock="")
        return ms.run(args, runner=fake(**k))

    order = []
    for _ in range(9):
        before = len(ms.read_history(history))
        code = run()
        rows = ms.read_history(history)
        order.append(rows[-1]["renderer"])
        if before < 9:
            check(code == 0, "a run of known figures passes (line %d)" % len(rows))
    check(order == ["d3d12", "vulkan", "d3d11"] * 3, "the renderers in turn: %s" % order)
    rows = ms.read_history(history)
    check(rows[0]["bench_average_fps"] == 1000.0 and rows[0]["delay_p99_ms"] == 25.0
          and abs(rows[0]["steps_per_second"] - 60.0) < 0.01, "the figures are the parts': %s" % rows[0])

    check(run(renderer="d3d12", fps=1001.0) == 0, "within or better than the spread passes")
    check(run(renderer="d3d12", fps=900.0) == 1 and "bench_average_fps" in str(ms.compare(ms.read_history(history)[-1],
          ms.read_history(history)[:-1])), "a lower frame rate than every earlier run fails")
    code = run(renderer="vulkan", delay=30000.0)
    verdicts = dict((v[0], v[3]) for v in ms.compare(ms.read_history(history)[-1], ms.read_history(history)[:-1]))
    check(code == 1 and verdicts["delay_p99_ms"] == "worse" and verdicts["bench_average_fps"] == "holds",
          "a longer delay fails by name, the rest hold: %s" % verdicts)
    row = dict(ms.read_history(history)[0], steps_per_second=60.01, renderer="d3d11")
    check(dict((v[0], v[3]) for v in ms.compare(row, ms.read_history(history)))["steps_per_second"] == "worse",
          "steps a second further from 60 than every earlier run is worse")
    check(run(renderer="d3d11", pace_ok=False) == 1 and "no measured [pace] window" in ms.read_history(history)[-1]["notes"],
          "no pacing window fails and says so")
    check(run(renderer="d3d11", bench_ok=False) == 1 and "no complete result" in ms.read_history(history)[-1]["notes"],
          "no benchmark result fails and says so")

    fresh = t / "fresh.tsv"
    ms.append_history(fresh, dict(time="t", commit="c", renderer="d3d12", notes="-",
                                  **{f[0]: 1.0 for f in ms.FIGURES}))
    ms.append_history(fresh, dict(time="t", commit="c", renderer="vulkan", notes="-",
                                  **{f[0]: 9.0 for f in ms.FIGURES}))
    check(all(v[3] == "no spread" for v in ms.compare(ms.read_history(fresh)[-1], ms.read_history(fresh)[:-1])),
          "another renderer's lines are no spread for this one")

if failures:
    print("measure_short_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("measure_short_test.py: pass")
