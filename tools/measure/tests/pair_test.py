"""pair_test.py: measure.py's pair (B13 part A) without the game: the ratio and its floor, the band's three words, the three
rounds, the order of starting, a round made again, the files (pending, result.json, pairs.tsv, the bands, the accepted
lines), the span cut at the same game frames, the processors chosen from given tables, the gpu median, an older kept line.
A fake starter plants each side's run folder; nothing is started and nothing of the machine is read.

python -X utf8 -I tools/measure/tests/pair_test.py
"""
import argparse
import contextlib
import datetime
import io
import json
import pathlib
import re
import socket
import struct
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
import band  # noqa: E402
import measure as m  # noqa: E402

failures = []
checks = [0]


def check(ok, what):
    checks[0] += 1
    if not ok:
        failures.append(what)


# ---- Planted runs ----
def timeline(path, records, freq=1000):
    with open(path, "wb") as f:
        f.write(b"NBTL0001" + struct.pack("<5Q", 1, freq, len(records), len(records), 1) + b"\0" * 16)
        for q, e, a, b in records:
            f.write(struct.pack("<QQQIHH", int(q), a, b, 0, e, 1))


def stage_timeline(render_ms=2.0, gpu_ms=1.0, gap_ms=10.0, frames=5700):
    """A timeline at 1000 ticks a second: frame 10 at 100 ms, a game frame marker for each 100 frames up to 10 + `frames` (a
    frame a millisecond), a present every `gap_ms`, a walk of `render_ms` and a GPU frame of `gpu_ms` with each."""
    records = [(100 + k, m.GAME_FRAME, 10 + k, 0) for k in range(0, frames + 1, 100)]
    t, fence = 100.0, 0
    while t < 100 + frames:
        records += [(t, m.WAIT_END, 0, 0), (t + 3, m.WAIT_BEGIN, 0, 0)]
        records += [(t, m.WALK_BEGIN, 0, 0), (t + render_ms, m.WALK_END, 0, 0), (t + render_ms, m.PRESENT_END, fence, 0)]
        fence += 1
        records += [(t, m.GPU_BEGIN, fence, 0), (t + gpu_ms, m.GPU_END, fence, 0)]
        t += gap_ms
    return records


PACE = "[pace] seconds=10 steps=600 pictures=1200 refresh_ms=8.333 missed=0 step_work_max_us=5000 render_avg_us=1000%s\n"


def plant(folder, cpu_us=3000.0, render_ms=2.0, gpu_ms=1.0, gap_ms=10.0, mem_mb=300, windows=11, short=False, no_presents=False,
          game_us=None, render_us=None, whole_us=None, work=True):
    """One side's run folder under `folder` (runs/measure-pair/<renderer>/ is added by the caller's path). cpu_us is the frame
    threads' work per step, split 70 / 30 between the game and the render thread unless game_us and render_us say; whole_us is the
    whole process's figure (the work and the waiting: cpu_us + 500 unless given); work=False: a game that writes no work fields."""
    folder.mkdir(parents=True)
    game_us = cpu_us * 0.7 if game_us is None and cpu_us is not None else game_us
    render_us = cpu_us * 0.3 if render_us is None and cpu_us is not None else render_us
    whole_us = (cpu_us + 500.0) if whole_us is None and cpu_us is not None else whole_us
    extra = "" if cpu_us is None else " cpu_us_per_step=%.1f" % whole_us
    if work and cpu_us is not None:
        extra += " cpu_work_us_per_step=%.1f cpu_game_us_per_step=%.1f cpu_render_us_per_step=%.1f" % (
            game_us + render_us, game_us, render_us)
    (folder / "run.log").write_text('[gpu] adapter="Card A" vram-mb=1\n[Renderer] internal resolution 1280x720 -> 3840x2160\n'
                                    "harness: frame 0 is game frame 10\n" + (PACE % extra) * windows, encoding="utf-8")
    (folder / "test.json").write_text(json.dumps({"name": "measure-pair", "stage": {"name": "HoneyBeeKingdomGalaxy", "scenario": 1}}),
                                      encoding="utf-8")
    (folder / "facts.jsonl").write_text("".join(json.dumps({"key": "process.memory", "value": {"seconds": s, "bytes": mem_mb << 20}}) + "\n"
                                                for s in (0, 30, 60, 90)), encoding="utf-8")
    records = stage_timeline(render_ms, gpu_ms, gap_ms, frames=3000 if short else 5700)
    if no_presents:
        records = [r for r in records if r[1] not in (m.PRESENT_END, m.GPU_BEGIN, m.GPU_END)]
    timeline(folder / "timeline.bin", records)


CONTEXT = {"computer": socket.gethostname(), "gpu": "Card A", "scene": "HoneyBeeKingdomGalaxy:1", "size": "3840x2160"}


class Fake:
    """A starter whose games are scripted: `script(side, renderer, round, attempt)` gives plant()'s keywords, or an int (an exit
    code: nothing is planted). `starts` is the order the games were started in, `clock` a fake time."""
    def __init__(self, script):
        self.script, self.starts, self.logs = script, [], {}

    def __call__(self, command, env, log_path):
        folder = pathlib.Path(command[command.index("--work") + 1])
        renderer = command[command.index("--renderer") + 1]
        side = "base" if command[command.index("--bin") + 1] == "BASE" else "cand"
        number, attempt = (int(x) for x in re.search(r"r(\d+)-a(\d+)", str(folder)).groups())
        self.starts.append((side, number, attempt))
        what = self.script(side, renderer, number, attempt)
        text = "runner line\n"
        code = 0
        if isinstance(what, int):
            code, text = what, "runner: the game could not start\n"
        else:
            plant(folder / "runs" / "measure-pair" / renderer, **what)
        pathlib.Path(log_path).write_text(text, encoding="utf-8")
        return type("Started", (), {"wait": lambda self: code})()


def args_for(work, calibration, runs="d3d12", pending=None, base_commit="aaaaaaaaa", cand_commit="bbbbbbbbb", **more):
    return argparse.Namespace(base="BASE", cand="CAND", runs=runs, work=str(work), pending=str(pending or ""),
                              calibration=str(calibration), base_pak="", cand_pak="", base_commit=base_commit,
                              cand_commit=cand_commit, name="measure/step/pc", form="hidden", **more)


def calibrate(path, renderer="d3d12", **bands):
    key = band.pair_key(CONTEXT, renderer, "120")
    band.write_pair_bands(path, key, {f: (b, [1.0] * 5) for f, b in bands.items()}, "ccccccccc", "2026-11-21 12:00")


TABLES = ([{"mask": 0xFF, "group": 0, "efficiency": 0}], [])  # every processor: nothing is set


def go(script, runs="d3d12", bands=None, accepted=None, **more):
    folder = pathlib.Path(tempfile.mkdtemp())
    calibration = folder / "gate-runs" / "pair-bands.tsv"
    if bands:
        for renderer, b in bands.items():
            calibrate(calibration, renderer, **b)
    if accepted:
        # In these scenarios the three cost figures (all together, game, render) move together: a line for "cpu_work_ms" stands for
        # the three.
        for figure in (("cpu_work_ms", "cpu_game_ms", "cpu_render_ms") if accepted["figure"] == "cpu_work_ms" else (accepted["figure"],)):
            band.add_cells(calibration.parent / "pair-accepted.tsv", band.ACCEPTED_COLUMNS, dict(accepted, figure=figure))
    fake = Fake(script)
    args = args_for(folder / "work", calibration, runs, folder / "pending.tsv", **more)
    clock = iter(datetime.datetime(2026, 11, 21, 12, 0) + datetime.timedelta(seconds=90 * i) for i in range(1000))
    with contextlib.redirect_stdout(io.StringIO()) as out:
        code, said = m.pair_body(args, fake, TABLES, lambda: next(clock))
    result = json.loads((folder / "work" / "result.json").read_text(encoding="utf-8"))
    return code, said, fake, result, out.getvalue(), folder


BANDS = {"cpu_work_ms": 0.04, "cpu_game_ms": 0.04, "cpu_render_ms": 0.04, "render_ms": 0.05, "gpu_ms": 0.05, "memory_mb": 0.02, "gap_p99_ms": 0.25, "late_per_min": 0.5}

# ================= The ratio, with the floor =================
check(band.pair_ratio(0.0, 0.0, "cpu_work_ms") == 1.0, "0 against 0 is 1")
check(abs(band.pair_ratio(0.0, 0.01, "cpu_work_ms") - 3.0 / 2.0) < 1e-9, "a hundredth of a millisecond against 0: (0.01 + 0.02) / 0.02 = 1.5, "
      "inside no band of 0.5 or more but never a division by 0: %s" % band.pair_ratio(0.0, 0.01, "cpu_work_ms"))
check(abs(band.pair_ratio(3.21, 3.54, "cpu_work_ms") - (3.54 + 0.02) / (3.21 + 0.02)) < 1e-12, "the floor is added to both")
check(abs(band.pair_ratio(10.0, 20.0, "memory_mb") - 52.0 / 42.0) < 1e-12, "memory's floor is 32 MB: 10 against 20 MB is 1.24, not 2")
check(band.pair_ratio(0.0, 0.0, "unnamed") == 1.0 and band.pair_ratio(0.0, 1.0, "unnamed") == float("inf"),
      "a figure with no floor: 0 against 0 is 1, never an error")

# ================= The band and the three words =================
check(band.pair_word(1.0401, 0.04) == "worse" and band.pair_word(1.04, 0.04) == "ok" and band.pair_word(0.96, 0.04) == "ok" and
      band.pair_word(0.9599, 0.04) == "better", "worse past 1 + band, better under 1 - band, else ok")
check(band.pair_word(5.0, None) == "no-baseline", "no band: no-baseline, whatever the ratio")
check(abs(band.pair_band([1.0, 1.01, 0.99, 1.02, 1.0], "cpu_work_ms") - 0.04) < 1e-12, "band = 2 x the largest |r - 1| when that is the most")
check(band.pair_band([1.0, 1.001, 1.0, 1.0, 1.0], "cpu_work_ms") == 0.03, "band = the least (cpu_ms 0.03) when 2 x the spread is less")
check({f: band.PAIR_LEAST[f] for f in band.JUDGED} == {"cpu_work_ms": 0.03, "cpu_game_ms": 0.03, "cpu_render_ms": 0.03,
                                                      "render_ms": 0.05, "gpu_ms": 0.05, "memory_mb": 0.02, "gap_p99_ms": 0.25,
                                                      "late_per_min": 0.50}, "the least band of each figure")
check("cpu_ms" not in band.JUDGED and "cpu_ms" in band.FIGURES, "the whole process's processor time is shown, not judged")

# ================= One round: ok, and the order of starting =================
code, said, fake, result, out, folder = go(lambda side, r, n, a: {}, bands={"d3d12": BANDS})
check(code == 0 and result["exit"] == 0 and result["runs"]["d3d12"]["state"] == "ok" and result["runs"]["d3d12"]["rounds"] == 1,
      "two equal builds: ok, one round, exit 0: %s %s" % (code, result["runs"]["d3d12"]["state"]))
check(fake.starts == [("base", 1, 1), ("cand", 1, 1)], "round 1 starts the base first: %s" % fake.starts)
check(set(result["runs"]["d3d12"]["figures"]) == set(band.JUDGED) and all(
    f["verdict"] == "ok" for f in result["runs"]["d3d12"]["figures"].values()), "every judged figure ok: %s" % result["runs"]["d3d12"]["figures"])
pending = band.read_cells(folder / "pending.tsv", m.PENDING_COLUMNS)
check({r["figure"] for r in pending} == set(band.JUDGED) and all(r["state"] == "ok" and r["check"] == "measure/step/pc" and
                                                               r["renderer"] == "d3d12" for r in pending),
      "the pending file has a row for each figure: %s" % pending)
check(any(r["figure"] == "cpu_work_ms" and r["line"].startswith("measure/step/pc d3d12 cpu_work_ms ok: base 3.000, candidate 3.000 (+0.0%; the band is 4.0%)")
          for r in pending), "the line in plain words: %s" % [r["line"] for r in pending if r["figure"] == "cpu_work_ms"])
rounds = band.read_cells(folder / "gate-runs" / "pairs.tsv", band.ROUNDS_COLUMNS)
check(len(rounds) == len(band.JUDGED) and all(r["round"] == "1" and r["base_commit"] == "aaaaaaaaa" and
                                              r["cand_commit"] == "bbbbbbbbb" and r["word"] == "ok" for r in rounds),
      "pairs.tsv gets a line for each figure of the round: %s" % rounds[:2])
check(m.PAIR_FORM == "hidden" or m.PAIR_FORM == "visible", "the form is one of the two")
# The route both sides run is the single measure's, cut, in the form given.
route = json.loads((folder / "work" / "route" / "test.json").read_text(encoding="utf-8"))
original = json.loads((m.ROUTE / "test.json").read_text(encoding="utf-8"))
check(route["visible"] is False and route["steps"][-1] == {"at": m.ROUTE_END, "end": True} and route["stage"] == original["stage"]
      and route["settings"] == [("GALAXY_VSYNC_INTERVAL=0" if s.startswith("GALAXY_VSYNC_INTERVAL=") else s) for s in original["settings"]]
      and "GALAXY_VSYNC_INTERVAL=1" in original["settings"] and "GALAXY_MAX_FPS=120" in route["settings"] and len(route["steps"]) > 3 and all(s["at"] < m.ROUTE_END for s in route["steps"][:-1]),
      "the pair's route: hidden, the same stage and settings, ended at step %d: %s" % (m.ROUTE_END, route["steps"][-3:]))
visible = m.pair_route(folder / "other", "visible")
check(json.loads((visible / "test.json").read_text(encoding="utf-8"))["visible"] is True, "the visible form")

# ================= Worse in round 1, fine in round 2: ok, two rounds, the order swapped =================
def slow_once(side, r, n, a):
    return {"cpu_us": 3600.0} if side == "cand" and n == 1 else {}


code, said, fake, result, out, folder = go(slow_once, bands={"d3d12": BANDS})
check(code == 0 and result["runs"]["d3d12"]["state"] == "ok" and result["runs"]["d3d12"]["rounds"] == 2 and
      result["runs"]["d3d12"]["figures"]["cpu_work_ms"]["verdict"] == "ok",
      "worse in round 1 and ok in round 2 is ok, and two rounds ran: %s %s" % (code, result["runs"]["d3d12"]["state"]))
check(fake.starts == [("base", 1, 1), ("cand", 1, 1), ("cand", 2, 1), ("base", 2, 1)],
      "round 2 starts the candidate first: %s" % fake.starts)
rounds = band.read_cells(folder / "gate-runs" / "pairs.tsv", band.ROUNDS_COLUMNS)
check([r["word"] for r in rounds if r["figure"] == "cpu_work_ms"] == ["worse", "ok"], "pairs.tsv holds both rounds: %s" % [
    (r["round"], r["word"]) for r in rounds if r["figure"] == "cpu_work_ms"])

# ================= Worse three times: worse, exit 1, three rounds =================
def slow_always(side, r, n, a):
    return {"cpu_us": 3600.0} if side == "cand" else {}


code, said, fake, result, out, folder = go(slow_always, bands={"d3d12": BANDS})
check(code == 1 and result["exit"] == 1 and result["verdict"] == "WORSE" and result["runs"]["d3d12"]["state"] == "worse" and
      result["runs"]["d3d12"]["rounds"] == 3 and result["runs"]["d3d12"]["figures"]["cpu_work_ms"]["verdict"] == "worse",
      "worse in all three rounds: worse, exit 1, three rounds: %s %s" % (code, result["runs"]["d3d12"]["state"]))
check([s[0] for s in fake.starts] == ["base", "cand", "cand", "base", "base", "cand"],
      "the order: base first, candidate first, base first again: %s" % fake.starts)
line = next(r["line"] for r in band.read_cells(folder / "pending.tsv", m.PENDING_COLUMNS) if r["figure"] == "cpu_work_ms")
check(line == "measure/step/pc d3d12 cpu_work_ms worse: base 3.000, candidate 3.600 (+19.9%, +19.9%, +19.9% in three rounds; the band is 4.0%)",
      "the line of a worse figure: %s" % line)
check(all(f["verdict"] == "ok" for n, f in result["runs"]["d3d12"]["figures"].items() if not n.startswith("cpu_")),
      "the other figures stay ok: %s" % {n: f["verdict"] for n, f in result["runs"]["d3d12"]["figures"].items()})
check(result["runs"]["d3d12"]["figures"]["cpu_work_ms"]["median"] == 3.0 and result["runs"]["d3d12"]["figures"]["cpu_work_ms"]["value"] == 3.6,
      "result.json's figure: value the candidate's, median the base's")

# Round 3 is for the figures worse in both of the first two: a figure worse in rounds 1 and 3 but not in 2 is ok (render_ms),
# while cpu_ms, worse in all three, is worse.
def slow_cpu_and_flicker(side, r, n, a):
    if side != "cand":
        return {}
    return {"cpu_us": 3600.0, "render_ms": 3.0 if n in (1, 3) else 2.0}


code, said, fake, result, out, folder = go(slow_cpu_and_flicker, bands={"d3d12": BANDS})
figs = result["runs"]["d3d12"]["figures"]
check(code == 1 and figs["cpu_work_ms"]["verdict"] == "worse" and figs["render_ms"]["verdict"] == "ok" and len(fake.starts) == 6,
      "a figure worse in rounds 1 and 3 and not in 2 is ok: %s" % {n: f["verdict"] for n, f in figs.items()})

# Better is said from round 1 alone.
code, said, fake, result, out, folder = go(lambda side, r, n, a: {"cpu_us": 2000.0} if side == "cand" else {}, bands={"d3d12": BANDS})
check(code == 0 and result["runs"]["d3d12"]["state"] == "better" and result["runs"]["d3d12"]["rounds"] == 1 and len(fake.starts) == 2,
      "better from round 1 alone: %s %s" % (result["runs"]["d3d12"]["state"], fake.starts))

# ================= No calibration line: no-baseline, it passes =================
code, said, fake, result, out, folder = go(slow_always)
check(code == 0 and result["runs"]["d3d12"]["state"] == "no-baseline" and result["runs"]["d3d12"]["rounds"] == 1,
      "no calibration line: no-baseline, one round, exit 0: %s %s" % (code, result["runs"]["d3d12"]["state"]))
line = next(r["line"] for r in band.read_cells(folder / "pending.tsv", m.PENDING_COLUMNS) if r["figure"] == "cpu_work_ms")
check(line.startswith("measure/step/pc d3d12 cpu_work_ms no-baseline: ") and line.endswith("not calibrated on this PC)"), "and says so: %s" % line)
# A calibration of another key (another renderer) is no line for this one.
code, said, fake, result, out, folder = go(slow_always, bands={"vulkan": BANDS})
check(result["runs"]["d3d12"]["state"] == "no-baseline", "a band of another renderer is not this key's")
# A figure the calibration does not hold is no-baseline for that figure only.
code, said, fake, result, out, folder = go(slow_always, bands={"d3d12": {"render_ms": 0.05}})
check(result["runs"]["d3d12"]["figures"]["cpu_work_ms"]["verdict"] == "no-baseline" and result["runs"]["d3d12"]["figures"]["render_ms"]["verdict"] == "ok",
      "one figure without a band: %s" % {n: f["verdict"] for n, f in result["runs"]["d3d12"]["figures"].items()})

# ================= An accepted slowdown passes =================
accepted = {"commit": "bbbbbbbbb", "renderer": "d3d12", "figure": "cpu_work_ms", "reason": "the owner accepted it", "date": "2026-11-21"}
code, said, fake, result, out, folder = go(slow_always, bands={"d3d12": BANDS}, accepted=accepted)
line = next(r["line"] for r in band.read_cells(folder / "pending.tsv", m.PENDING_COLUMNS) if r["figure"] == "cpu_work_ms")
check(code == 0 and result["runs"]["d3d12"]["state"] == "accepted" and result["runs"]["d3d12"]["figures"]["cpu_work_ms"]["verdict"] == "accepted"
      and "accepted: the owner accepted it" in line and result["runs"]["d3d12"]["rounds"] == 3,
      "an accepted line for the candidate's commit: accepted, exit 0, still three rounds: %s %s" % (code, line))
other = dict(accepted, commit="ddddddddd")
code, said, fake, result, out, folder = go(slow_always, bands={"d3d12": BANDS}, accepted=other)
check(code == 1 and result["runs"]["d3d12"]["state"] == "worse", "the line of another commit does not accept this one")
other = dict(accepted, renderer="vulkan")
code, said, fake, result, out, folder = go(slow_always, bands={"d3d12": BANDS}, accepted=other)
check(code == 1, "nor the line of another renderer")
other = dict(accepted, figure="render_ms")
code, said, fake, result, out, folder = go(slow_always, bands={"d3d12": BANDS}, accepted=other)
check(code == 1, "nor the line of another figure")
code, said, fake, result, out, folder = go(slow_always, bands={"d3d12": BANDS}, accepted=dict(accepted, commit="bbbbbbbbbbbb"))
check(code == 0 and result["runs"]["d3d12"]["state"] == "accepted", "a commit written longer or shorter than the candidate's name matches by its start")
with contextlib.redirect_stdout(io.StringIO()):
    target = pathlib.Path(tempfile.mkdtemp()) / "pair-bands.tsv"
    check(m.main(["pair-accept", "--commit", "bbbbbbbbb", "--renderer", "d3d12", "--figure", "cpu_work_ms", "--reason", "slower by design",
                  "--calibration", str(target)]) == 0, "pair-accept writes a line")
    check(m.main(["pair-accept", "--commit", "bbbbbbbbb", "--renderer", "d3d12", "--figure", "step_ms", "--reason", "x",
                  "--calibration", str(target)]) == 2, "pair-accept refuses a figure that is not judged")
    check(m.main(["pair-accept", "--commit", "bbbbbbbbb", "--renderer", "d3d12", "--figure", "cpu_work_ms", "--reason", " ",
                  "--calibration", str(target)]) == 2, "pair-accept refuses an empty reason")
written = band.read_cells(target.parent / "pair-accepted.tsv", band.ACCEPTED_COLUMNS)
check(len(written) == 1 and written[0]["commit"] == "bbbbbbbbb" and written[0]["reason"] == "slower by design" and
      band.accepted_reason(target.parent / "pair-accepted.tsv", "bbbbbbbbb", "d3d12", "cpu_work_ms") == "slower by design" and
      band.accepted_reason(target.parent / "pair-accepted.tsv", "bbbbbbbbb", "d3d11", "cpu_work_ms") is None,
      "the accepted file's line, found by commit, renderer and figure: %s" % written)

# ================= A round that did not run is made again once; twice is `tool` =================
def fails_once(side, r, n, a):
    return 3 if (side == "base" and n == 1 and a == 1) else {}


code, said, fake, result, out, folder = go(fails_once, bands={"d3d12": BANDS})
check(code == 0 and result["runs"]["d3d12"]["state"] == "ok" and result["runs"]["d3d12"]["rounds"] == 1 and
      [(s[1], s[2]) for s in fake.starts] == [(1, 1), (1, 1), (1, 2), (1, 2)],
      "a side that ended early: the round is made once more, and then it is ok: %s %s" % (said, fake.starts))
code, said, fake, result, out, folder = go(lambda side, r, n, a: 3 if side == "cand" else {}, bands={"d3d12": BANDS})
check(code == 2 and result["exit"] == 2 and result["runs"]["d3d12"]["state"] == "tool" and len(fake.starts) == 4 and
      "the cand side ended with exit 3: runner: the game could not start" in result["runs"]["d3d12"]["reason"],
      "twice: tool, exit 2, the runner's own last lines: %s %s" % (code, result["runs"]["d3d12"]["reason"]))
pending = band.read_cells(folder / "pending.tsv", m.PENDING_COLUMNS)
check(len(pending) == 1 and pending[0]["state"] == "tool" and pending[0]["line"].startswith("measure/step/pc d3d12 tool: "),
      "the pending file says tool: %s" % pending)
code, said, fake, result, out, folder = go(lambda side, r, n, a: dict(short=True) if side == "base" else {}, bands={"d3d12": BANDS})
check(code == 2 and "did not give" in result["runs"]["d3d12"]["reason"] and "first 5400 steps" in result["runs"]["d3d12"]["reason"],
      "a stage that was not reached (the timeline stops short of the span): tool: %s" % result["runs"]["d3d12"]["reason"])
# Vulkan writes no present events: no gap figures, and the round still ran (only the figures both sides have are judged).
def no_presents(side, r, n, a):
    return {"no_presents": True}


code, said, fake, result, out, folder = go(no_presents, bands={"d3d12": BANDS})
figs = result["runs"]["d3d12"]["figures"]
check(code == 0 and "gap_p99_ms" not in figs and "cpu_work_ms" in figs and result["runs"]["d3d12"]["state"] == "ok",
      "a renderer with no present events: a round that ran, judged on the figures it has: %s %s" % (code, sorted(figs)))
# The cost is the frame threads' work, not the whole process's figure: a side whose whole-process time is far higher (waiting:
# a spin, a present) and whose work is the same reads ok; work added in the game thread only is worse for the game thread and for
# all together, ok for the render thread; in the render thread only, the other way round.
code, said, fake, result, out, folder = go(lambda side, r, n, a: {"whole_us": 9000.0} if side == "cand" else {}, bands={"d3d12": BANDS})
figs = result["runs"]["d3d12"]["figures"]
check(code == 0 and result["runs"]["d3d12"]["rounds"] == 1 and not any(n == "cpu_ms" for n in figs),
      "a whole-process time three times the base's with the same work: ok, one round, cpu_ms not judged: %s" % sorted(figs))
code, said, fake, result, out, folder = go(lambda side, r, n, a: {"game_us": 2100.0 + 300.0} if side == "cand" else {}, bands={"d3d12": BANDS})
figs = result["runs"]["d3d12"]["figures"]
check(code == 1 and figs["cpu_game_ms"]["verdict"] == "worse" and figs["cpu_work_ms"]["verdict"] == "worse" and
      figs["cpu_render_ms"]["verdict"] == "ok" and result["runs"]["d3d12"]["rounds"] == 3,
      "0.3 ms more in the game thread: game and work worse, render ok, three rounds: %s" % {n: f["verdict"] for n, f in figs.items()})
check(abs((figs["cpu_game_ms"]["value"] - figs["cpu_game_ms"]["median"]) - 0.3) < 1e-9,
      "and by the amount: the candidate's cpu_game_ms is the base's plus 0.3 ms")
code, said, fake, result, out, folder = go(lambda side, r, n, a: {"render_us": 900.0 + 300.0} if side == "cand" else {}, bands={"d3d12": BANDS})
figs = result["runs"]["d3d12"]["figures"]
check(code == 1 and figs["cpu_render_ms"]["verdict"] == "worse" and figs["cpu_work_ms"]["verdict"] == "worse" and
      figs["cpu_game_ms"]["verdict"] == "ok", "0.3 ms more in the render thread: render and work worse, game ok: %s" %
      {n: f["verdict"] for n, f in figs.items()})
# A side from before the work fields (a base built earlier): the cost figures are not compared, the round ran.
code, said, fake, result, out, folder = go(lambda side, r, n, a: {"work": False} if side == "base" else {}, bands={"d3d12": BANDS})
figs = result["runs"]["d3d12"]["figures"]
check(code == 0 and result["runs"]["d3d12"]["state"] == "ok" and not any(n.startswith("cpu_") for n in figs) and "gap_p99_ms" in figs,
      "a base that writes no work fields: no cost figure is compared, the others are: %s" % sorted(figs))
# What the pair says of its settings and the pictures a step.
code, said, fake, result, out, folder = go(lambda side, r, n, a: {}, bands={"d3d12": BANDS})
check(result["settings"] == {"GALAXY_MAX_FPS": "120", "GALAXY_VSYNC_INTERVAL": "0"} and result["runs"]["d3d12"]["pictures_per_step"] == 2.0,
      "result.json says the frame cap, VSync off and the pictures a step: %s %s" % (result["settings"], result["runs"]["d3d12"]["pictures_per_step"]))
# A tool fault in the second round (after a worse first) is the answer: tool.
code, said, fake, result, out, folder = go(lambda side, r, n, a: ({"cpu_us": 3600.0} if side == "cand" else {}) if n == 1 else 9,
                                           bands={"d3d12": BANDS})
check(code == 2 and result["runs"]["d3d12"]["state"] == "tool", "a round 2 that cannot run: tool, not worse")
# Several renderers: each has its own state; a tool fault in one makes the exit 2, a worse in another is still said.
code, said, fake, result, out, folder = go(lambda side, r, n, a: ({"cpu_us": 3600.0} if side == "cand" else {}) if r == "d3d12" else 1,
                                           runs="d3d12,vulkan", bands={"d3d12": BANDS})
check(code == 2 and result["runs"]["d3d12"]["state"] == "worse" and result["runs"]["vulkan"]["state"] == "tool",
      "two renderers: worse and tool, exit 2: %s" % {r: o["state"] for r, o in result["runs"].items()})

# ================= pair-calibrate =================
def spread(side, r, n, a):
    # Round n: the candidate's processor time is 1.00, 1.01, 0.99, 1.02, 1.00 of the base's.
    f = [1.0, 1.01, 0.99, 1.02, 1.0][n - 1]
    return {"cpu_us": 3000.0 * f if side == "cand" else 3000.0}


folder = pathlib.Path(tempfile.mkdtemp())
calibration = folder / "pair-bands.tsv"
fake = Fake(spread)
cargs = argparse.Namespace(bin="BASE", runs="d3d12", pairs=5, work=str(folder / "work"), calibration=str(calibration), base_pak="",
                           cand_pak="", commit="ccccccccc", form="hidden")
# Fake's side reading: the calibration runs one build twice, so both sides are given the same folder name BASE; the script tells
# the sides apart by the starter's order (the first start of a round is the base in odd rounds, the candidate in even ones).
state = {"n": 0}


def starter(command, env, log_path):
    state["n"] += 1
    number = (state["n"] + 1) // 2
    first = (state["n"] % 2) == 1
    base_first = number % 2 == 1
    side = "base" if first == base_first else "cand"
    command = list(command)
    command[command.index("--bin") + 1] = "BASE" if side == "base" else "CAND"
    return fake(command, env, log_path)


clock = iter(datetime.datetime(2026, 11, 21, 12, 0) + datetime.timedelta(minutes=i) for i in range(100))
with contextlib.redirect_stdout(io.StringIO()) as out:
    code, said = m.calibrate_body(cargs, starter, TABLES, lambda: next(clock))
bands = band.pair_bands(calibration, band.pair_key(CONTEXT, "d3d12", "120"))
check(code == 0 and set(bands) == set(band.JUDGED) - {"late_per_min"} | {"late_per_min"}, "calibrate: a band for every judged figure: %s" % bands)
ratios = band.read_cells(calibration, band.BANDS_COLUMNS)
cpu = next(r for r in ratios if r["figure"] == "cpu_work_ms")
check(len(cpu["ratios"].split(",")) == 5 and abs(bands["cpu_work_ms"] - 0.04) < 1e-3 and cpu["commit"] == "ccccccccc",
      "five ratios, cpu_work_ms band = 2 x the largest |r - 1| (0.04): %s %s" % (cpu, bands["cpu_work_ms"]))
check(abs(bands["render_ms"] - 0.05) < 1e-9 and bands["gap_p99_ms"] == 0.25 and bands["late_per_min"] == 0.5 and bands["memory_mb"] == 0.02 and
      bands["gpu_ms"] == 0.05, "an even figure gets its least: %s" % bands)
check(len(fake.starts) == 10 and [fake.starts[i][0] for i in (0, 2, 4, 6, 8)] == ["base", "cand", "base", "cand", "base"],
      "five rounds, the order of starting alternating: %s" % fake.starts)
# A second calibration replaces the key's older lines; another key's lines stay.
calibrate(calibration, "vulkan", cpu_work_ms=0.1)
state["n"] = 0
fake.starts.clear()
with contextlib.redirect_stdout(io.StringIO()):
    m.calibrate_body(cargs, starter, TABLES, lambda: next(clock))
rows = band.read_cells(calibration, band.BANDS_COLUMNS)
check(len([r for r in rows if r["renderer"] == "d3d12" and r["figure"] == "cpu_work_ms"]) == 1 and
      [r["band"] for r in rows if r["renderer"] == "vulkan"] == ["0.1000"], "replaced for its key, the others kept: %s" % len(rows))
# A round that cannot run ends that renderer's calibration with a tool fault and writes none of its lines.
state["n"] = 0
before = band.read_cells(calibration, band.BANDS_COLUMNS)
bad = Fake(lambda side, r, n, a: 5)
fake = bad
with contextlib.redirect_stdout(io.StringIO()):
    code, said = m.calibrate_body(cargs, starter, TABLES, lambda: next(clock))
check(code == 2 and band.read_cells(calibration, band.BANDS_COLUMNS) == before, "calibrate with a round that cannot run: exit 2, the file as it was")

# ================= The span: the same game frames for both sides =================
w = pathlib.Path(tempfile.mkdtemp()) / "t.bin"
timeline(w, stage_timeline(render_ms=2.0, gpu_ms=1.0, gap_ms=10.0, frames=5700))
whole = m.timeline_figures(w, 10)
cut = m.timeline_figures(w, 10, 5400)
check(cut and cut["gap_p99_ms"] == 10.0, "a cut timeline gives figures: %s" % cut)
# A gap after frame 10 + 5400 is outside the span; one before it is inside.
late = stage_timeline(frames=5700) + [(6400, m.PRESENT_END, 99999, 0)]
timeline(w, late)
check(m.timeline_figures(w, 10, 5400)["gap_max_ms"] == 10.0 and m.timeline_figures(w, 10)["gap_max_ms"] > 100.0,
      "what comes after the span is not in it: %s %s" % (m.timeline_figures(w, 10, 5400)["gap_max_ms"], m.timeline_figures(w, 10)["gap_max_ms"]))
early = [r for r in stage_timeline(frames=5700) if not (r[1] == m.PRESENT_END and 2000 <= r[0] < 2500)]
timeline(w, early)
check(m.timeline_figures(w, 10, 5400)["gap_max_ms"] > 100.0, "what comes inside it is")
timeline(w, stage_timeline(frames=3000))
check(m.timeline_figures(w, 10, 5400) == {} and m.timeline_figures(w, 10) != {}, "a timeline that stops short of the span gives no figures")
# Both sides are cut at the same game frames whatever their clocks: a slow side (3 ms between presents) reaches frame 5410 later.
timeline(w, stage_timeline(gap_ms=30.0, frames=5700))
slow = m.timeline_figures(w, 10, 5400)
check(slow["gap_p99_ms"] == 30.0, "a side with other times is cut at the same frames: %s" % slow)
log = "harness: frame 0 is game frame 10\n" + "".join(PACE % (" cpu_us_per_step=%d.0" % (1000 * (i + 1))) for i in range(12))
windows = m.pace_windows(log)
cut_windows = m.pace_windows(log, 5400)
check(len(windows) == 11 and len(cut_windows) == 9 and cut_windows[0]["cpu_us_per_step"] == 2000.0 and
      cut_windows[-1]["cpu_us_per_step"] == 10000.0, "the [pace] windows: the first left out, then nine of 600 steps: %s" % len(cut_windows))
check(len(m.pace_windows(log, 6000)) == 10 and len(m.pace_windows(log, 599)) == 0, "the windows that fit the steps given")
folder = pathlib.Path(tempfile.mkdtemp()) / "run"
plant(folder, cpu_us=3000.0)
(folder / "run.log").write_text("harness: frame 0 is game frame 10\n" + "".join(
    PACE % (" cpu_us_per_step=%d.0" % (1000 * (i + 1))) for i in range(12)), encoding="utf-8")
f_cut, f_all = m.figures(folder, 5400), m.figures(folder)
check(f_cut["cpu_ms"] == 6.0 and f_all["cpu_ms"] == 7.0, "cpu_ms over the span's windows (2 to 10: median 6) and over all (2 to 12: 7)")
(folder / "facts.jsonl").write_text("".join(json.dumps({"key": "process.memory", "value": {"seconds": s, "bytes": mb << 20}}) + "\n"
                                            for s, mb in ((0, 100), (60, 200), (90, 300), (95, 900))), encoding="utf-8")
check(m.figures(folder, 5400)["memory_mb"] == 300.0 and m.figures(folder)["memory_mb"] == 900.0,
      "memory: the facts of the span's seconds (60 steps a second)")

# ================= gpu_ms is the median of the frames' times =================
records = stage_timeline(gpu_ms=1.0, frames=1000)
records += [(1000, m.GPU_BEGIN, 5000, 0), (1090, m.GPU_END, 5000, 0)]  # one frame 90 ms long (another program held the card)
timeline(w, records)
check(m.timeline_figures(w, 10)["gpu_ms"] == 1.0, "one long frame does not move the median: %s" % m.timeline_figures(w, 10)["gpu_ms"])
only = [(100, m.GAME_FRAME, 10, 0), (110, m.PRESENT_END, 1, 0), (120, m.PRESENT_END, 2, 0), (130, m.PRESENT_END, 3, 0)]
only += [(200, m.GPU_BEGIN, 1, 0), (201, m.GPU_END, 1, 0), (203, m.GPU_BEGIN, 1, 0), (206, m.GPU_END, 1, 0)]  # frame 1: 1 + 3 ms
only += [(300, m.GPU_BEGIN, 2, 0), (302, m.GPU_END, 2, 0), (400, m.GPU_BEGIN, 3, 0), (406, m.GPU_END, 3, 0)]  # frames 2, 3: 2 and 6 ms
timeline(w, only)
check(m.timeline_figures(w, 10)["gpu_ms"] == 4.0, "a frame's spans are added (1 + 3 = 4), then the median of 4, 2, 6: %s" %
      m.timeline_figures(w, 10)["gpu_ms"])
# Not measured stays not measured.
timeline(w, [r for r in only if r[1] not in (m.GPU_BEGIN, m.GPU_END)])
check("gpu_ms" not in m.timeline_figures(w, 10), "no GPU times: not measured, never 0")

# ================= The processors both sides are held to =================
MB = 1 << 20


def core(mask, efficiency=0, group=0):
    return {"mask": mask, "group": group, "efficiency": efficiency}


def l3(mask, mb, group=0, kind=0):
    return {"level": 3, "size": mb * MB, "type": kind, "mask": mask, "group": group}


sixteen = [core(3 << (2 * i)) for i in range(16)]  # 16 cores of two threads: 32 logical processors
# One cache over every processor: every processor, nothing is set.
mask, text = m.choose_processors(sixteen, [l3(0xFFFFFFFF, 32)])
check(mask is None and "every logical processor (32)" in text, "one cache: nothing is set: %s" % text)
# Two caches of different size (the second half has the larger one): the processors of the larger.
mask, text = m.choose_processors(sixteen, [l3(0x0000FFFF, 32), l3(0xFFFF0000, 96)])
check(mask == 0xFFFF0000 and "96 MB" in text and "16 of 32" in text, "two caches of different size: the larger one's: %s %s" % (hex(mask or 0), text))
mask, text = m.choose_processors(sixteen, [l3(0xFFFF0000, 96), l3(0x0000FFFF, 32)])
check(mask == 0xFFFF0000, "the order of the table does not matter")
# Two of one size: the first (by the lowest processor).
mask, text = m.choose_processors(sixteen, [l3(0xFFFF0000, 32), l3(0x0000FFFF, 32)])
check(mask == 0x0000FFFF, "caches of one size: the first: %s" % hex(mask or 0))
# Two efficiency classes: the higher one's processors (a cache over all of them gives those of the class).
cores = [core(0xFF, 1), core(0xFF00, 0)]
mask, text = m.choose_processors(cores, [l3(0xFFFF, 24)])
check(mask == 0xFF and "efficiency class 1" in text, "two classes, one cache: the class with the higher number: %s" % text)
mask, text = m.choose_processors([core(0xFF, 0), core(0xFF00, 1)], [])
check(mask == 0xFF00, "two classes, no cache table: the class alone")
# The class first, the cache second: the larger cache is on the slower cores, so the faster cores' cache is used.
mask, text = m.choose_processors([core(0x0F, 1), core(0xF0, 1), core(0xF00, 0)], [l3(0x0F, 8), l3(0xF0, 12), l3(0xF00, 64)])
check(mask == 0xF0, "the efficiency class decides before the cache's size: %s" % hex(mask or 0))
# Processors that are not the highest class are never chosen even when their cache is the largest alone.
mask, text = m.choose_processors([core(0x0F, 1), core(0xF0, 0)], [l3(0xF0, 64), l3(0x0F, 8)])
check(mask == 0x0F, "a larger cache of the slower cores is not chosen")
# One class and one cache but not every processor in it: that cache's.
mask, text = m.choose_processors([core(0x0F), core(0xF0)], [l3(0x0F, 8)])
check(mask == 0x0F, "a cache that covers part of the machine: its processors")
check(m.choose_processors([], []) == (None, "the system gave no processor table: nothing is set"), "no table: nothing is set")
# The instruction cache and level 2 are not the level-3 cache.
mask, text = m.choose_processors([core(0x0F), core(0xF0)], [l3(0x0F, 99, kind=1), {"level": 2, "size": 99 * MB, "type": 0, "mask": 0xF0, "group": 0}])
check(mask is None, "only a unified or data level-3 cache counts")
# Several groups: only the first group's processors can be set, and that is a set even when it holds all of the group's.
mask, text = m.choose_processors([core(0xFF, 0, 0), core(0xFF, 0, 1)], [])
check(mask == 0xFF, "two processor groups: the first group's processors")


def record(relationship, body):
    return struct.pack("<II", relationship, 8 + len(body)) + body


def core_record(efficiency, mask, group):
    body = struct.pack("<BB20sH", 0, efficiency, b"\0" * 20, 1) + b"\0" * 0
    body += b"\0" * (24 - len(body)) + struct.pack("<QH6x", mask, group)
    return record(m.RELATION_CORE, body)


def cache_record(level, size, kind, mask, group):
    body = struct.pack("<BBHIIB", level, 8, 64, size, kind, 0)
    body += b"\0" * (30 - len(body)) + struct.pack("<H", 1) + struct.pack("<QH6x", mask, group)
    return record(m.RELATION_CACHE, body)


blob = (core_record(1, 0x3, 0) + record(1, b"\0" * 40) + core_record(0, 0xC, 0) + cache_record(3, 96 * MB, 0, 0x3, 0)
        + cache_record(1, 32 * 1024, 2, 0x3, 0) + cache_record(3, 32 * MB, 0, 0xC, 0))
cores_read, caches_read = m.parse_processor_information(blob)
check(cores_read == [{"mask": 3, "group": 0, "efficiency": 1}, {"mask": 12, "group": 0, "efficiency": 0}] and
      [(c["level"], c["size"], c["type"], c["mask"]) for c in caches_read] == [(3, 96 * MB, 0, 3), (1, 32768, 2, 3), (3, 32 * MB, 0, 12)],
      "the system's table read from its bytes (a record of another relation skipped): %s %s" % (cores_read, caches_read))
check(m.choose_processors(cores_read, caches_read)[0] == 0x3, "and chosen from")
check(m.parse_processor_information(blob[:-7]) == m.parse_processor_information(blob[:-len(cache_record(3, 32 * MB, 0, 0xC, 0))]),
      "a table cut short loses only the cut record")
if m.os.name == "nt":
    tables = m.machine_processor_tables()
    check(tables is not None and tables[0] and all(c["mask"] for c in tables[0]), "this machine's table is read")

# ================= The locks and the exit codes through main =================
locked = pathlib.Path(tempfile.mkdtemp()) / "measure.lock"
locked.mkdir()
(locked / "holder.txt").write_text("someone 2026-11-21 12:00:00 pid %d\n" % __import__("os").getpid(), encoding="utf-8")
with contextlib.redirect_stdout(io.StringIO()) as out:
    code = m.main(["pair", "--base", "B", "--cand", "C", "--work", str(pathlib.Path(tempfile.mkdtemp()) / "w"), "--lock", str(locked)])
check(code == 2 and "measurement lock" in out.getvalue(), "the pair takes the measurement lock: a held one is a tool fault: %s" % out.getvalue())
free = pathlib.Path(tempfile.mkdtemp()) / "measure.lock"
seen = []
real = m.pair_body
try:
    m.pair_body = lambda a, *rest: (seen.append(free.is_dir()) or (0, "ran"))
    with contextlib.redirect_stdout(io.StringIO()):
        code = m.main(["pair", "--base", "B", "--cand", "C", "--work", str(pathlib.Path(tempfile.mkdtemp()) / "w"), "--lock", str(free)])
finally:
    m.pair_body = real
check(code == 0 and seen == [True] and not free.exists(), "the lock is held while the pair runs and freed after: %s %s" % (seen, free.exists()))

if failures:
    print("pair_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("pair_test.py: pass (%d checks)" % checks[0])
