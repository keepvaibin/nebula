"""second_part_test.py: the speed measure's second part (T7a, B2a) with planted run folders and fakes: no game, no machine.
One section for each numbered point of the brief: 1 the step figures and which figures are judged, 2 the band's floors,
3 the three more reasons for "not trusted", 4 --pending and keep, 5 result.json, 6 the anchor, 7 the tablet's gap figures.
(The processors' watch and the card's left-out compositor are quiet_test.py's.)

python -X utf8 -I tools/measure/tests/second_part_test.py
"""
import argparse
import contextlib
import io
import json
import pathlib
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


def windows_timeline(steps, gap_spike=None):
    """A Windows run's timeline at 1000 ticks a second: frame 10, a step for each time in `steps` (ms), a present every
    10 ms for 3 seconds (one gap of `gap_spike` ms when given)."""
    records = [(100, m.GAME_FRAME, 10, 0)]
    t = 100
    for d in steps:
        records += [(t, m.WAIT_END, 0, 0), (t + d, m.WAIT_BEGIN, 0, 0)]
        t += d + 10
    t = 100
    for i in range(300):
        t += gap_spike if (gap_spike and i == 150) else 10
        records.append((t, m.PRESENT_END, i, 0))
    return records


PACE = ("[pace] seconds=10 steps=600 refresh_ms=8.333 missed=%d step_work_max_us=5000 render_avg_us=%d%s\n")
CONTEXT = {"computer": socket.gethostname(), "gpu": "Card A", "scene": "S:1", "size": "3840x2160"}


def write_run(folder, render_us=1000, steps=None, mem_mb=300, watch=None, gap_spike=None, pace_extra="", missed=0):
    folder.mkdir(parents=True)
    (folder / "run.log").write_text('[gpu] adapter="Card A" vram-mb=1\n'
                                    "[Renderer] internal resolution 1280x720 -> 3840x2160\n"
                                    "harness: frame 0 is game frame 10\n" + (PACE % (missed, render_us, pace_extra)) * 3,
                                    encoding="utf-8")
    (folder / "test.json").write_text(json.dumps({"name": "measure-route", "stage": {"name": "S", "scenario": 1}}),
                                      encoding="utf-8")
    (folder / "facts.jsonl").write_text(json.dumps({"key": "process.memory", "value": {"bytes": mem_mb << 20}}) + "\n",
                                        encoding="utf-8")
    if steps is not None:
        timeline(folder / "timeline.bin", windows_timeline(steps, gap_spike))
    if watch:
        (folder / "window-watch.txt").write_text(watch, encoding="utf-8")


def make_runner(spec, calls=None):
    """A harness that plants a run folder: `spec(renderer, n)` gives write_run's keywords for the n-th call of that renderer
    (or None for a harness that fails and plants nothing)."""
    count = {}

    def runner(command, env=None, check=False):
        renderer = command[command.index("--renderer") + 1]
        work = pathlib.Path(command[command.index("--work") + 1])
        count[renderer] = count.get(renderer, 0) + 1
        what = spec(renderer, count[renderer])
        if calls is not None:
            calls.append((renderer, work.name))
        if what is None:
            return type("D", (), {"returncode": 1})()
        write_run(work / "runs" / "measure-route" / renderer, **what)
        return type("D", (), {"returncode": 0})()
    return runner


class Machine:
    """A machine with scripted answers (the processors' use as (percent, text), the last one stays)."""
    def __init__(self, quiet=None, cpu=None):
        self.quiet, self.cpu = list(quiet or []), list(cpu or [])

    def wait_quiet(self):
        return self.quiet.pop(0) if self.quiet else (True, "")

    def card_use(self):
        return 0.0, "other programs use the card 0.0%"

    def cpu_watch(self):
        machine = self

        class Watch:
            def begin(self):
                pass

            def end(self):
                return (machine.cpu.pop(0) if len(machine.cpu) > 1 else machine.cpu[0]) if machine.cpu else (
                    0.0, "other programs use the processors 0.0%")
        return Watch()


def go(history, runs, spec, commit, machine=None, pending="", anchor_note=None):
    args = argparse.Namespace(runs=runs, bin="B", android_bin="", apk="", pak="", device="", device_lock="", paks="",
                              commit=commit, history=str(history), pending=pending)
    calls, work = [], pathlib.Path(tempfile.mkdtemp())
    with contextlib.redirect_stdout(io.StringIO()) as out:
        code, said = m.measure_runs(args, work, make_runner(spec, calls), machine=machine or Machine())
    result = json.loads((work / "result.json").read_text(encoding="utf-8"))
    return code, said, calls, result, out.getvalue()


def steady(**over):
    base = {"render_us": 1000, "steps": [4.0] * 200, "mem_mb": 300, "pace_extra": " cpu_work_us_per_step=4000.0"}
    base.update(over)
    return lambda renderer, n: dict(base)


def new_history(runs=6, **over):
    history = pathlib.Path(tempfile.mkdtemp()) / "measure-history.tsv"
    for n in range(runs):
        go(history, "d3d12", steady(**over), "%09d" % (n + 1))
    return history


# ================= 1. The step figures, and which figures are judged =================
folder = pathlib.Path(tempfile.mkdtemp()) / "run"
write_run(folder, steps=[4.0] * 200 + [300.0], gap_spike=500)
f = m.figures(folder)
check(abs(f["step_ms"] - 4.0) < 1e-9 and abs(f["step_p99_ms"] - 4.0) < 1e-9 and abs(f["step_max_ms"] - 300.0) < 1e-9
      and f["steps_over_18"] == 1, "step_ms is the median, step_p99_ms the 99th percentile of the steps' work: %s" % f)
check(abs(f["gap_max_ms"] - 500.0) < 1e-9 and abs(f["gap_p99_ms"] - 10.0) < 1e-9, "the gap figures beside them: %s" % f)
check(band.JUDGED == ("cpu_work_ms", "cpu_game_ms", "cpu_render_ms", "gap_p99_ms", "late_per_min", "render_ms", "gpu_ms", "memory_mb")
      and set(band.JUDGED) <= set(band.FIGURES) and set(band.FIGURES) - set(band.JUDGED) ==
      {"gap_max_ms", "step_ms", "step_p99_ms", "step_max_ms", "steps_over_18", "cpu_ms"},
      "the judged figures are the frame threads' work and five more; the step's time on the clock and the whole process's processor "
      "time (it holds waiting) are shown only: %s" % (band.JUDGED,))
check("cpu_ms" not in f, "a run whose [pace] lines have no cpu_us_per_step has no cpu_ms (not 0): %s" % f)
folder_cpu = pathlib.Path(tempfile.mkdtemp()) / "run"
write_run(folder_cpu, steps=[4.0] * 200, pace_extra=" cpu_us_per_step=3210.0")
check(abs(m.figures(folder_cpu)["cpu_ms"] - 3.21) < 1e-9, "cpu_ms is cpu_us_per_step in ms: %s" % m.figures(folder_cpu))
(folder_cpu / "run.log").write_text('[gpu] adapter="Card A" vram-mb=1\nharness: frame 0 is game frame 10\n'
                                    + (PACE % (0, 1000, " cpu_us_per_step=1000.0")) + (PACE % (0, 1000, " cpu_us_per_step=9000.0"))
                                    + (PACE % (0, 1000, " cpu_us_per_step=2000.0")) + (PACE % (0, 1000, " cpu_us_per_step=3000.0")),
                                    encoding="utf-8")
check(abs(m.figures(folder_cpu)["cpu_ms"] - 3.0) < 1e-9, "cpu_ms is the median of the windows after the first: 9, 2, 3 -> 3 ms")
(folder_cpu / "run.log").write_text('[gpu] adapter="Card A" vram-mb=1\nharness: frame 0 is game frame 10\n'
                                    + (PACE % (0, 1000, " cpu_us_per_step=1000.0")) + (PACE % (0, 1000, ""))
                                    + (PACE % (0, 1000, " cpu_us_per_step=3000.0")), encoding="utf-8")
check(abs(m.figures(folder_cpu)["cpu_ms"] - 3.0) < 1e-9, "a window with no cpu_us_per_step is left out of the median")
row = m.history_row(f, "abc", "pc", "d3d12", "120", folder, context=CONTEXT)
check(row["step_ms"] == "4.000" and row["step_p99_ms"] == "4.000" and band.COLUMNS[-6:] == (
    "step_ms", "step_p99_ms", "anchor", "cpu_ms", "cpu_work_ms", "cpu_game_ms", "cpu_render_ms")[-6:],
      "the cost figures are history columns, added at the end: %s" % (band.COLUMNS[-6:],))
check(row["cpu_ms"] == "-" and row["cpu_work_ms"] == "-" and row["cpu_game_ms"] == "-" and row["cpu_render_ms"] == "-",
      "a cost not measured is -: %s" % row["cpu_work_ms"])
# The frame threads' work from the [pace] lines: the median over the windows of each field, all together, game and render
# judged; any other frame thread's shown; a line without them gives no figure (not 0).
folder_work = pathlib.Path(tempfile.mkdtemp()) / "run"
write_run(folder_work, steps=[4.0] * 200, pace_extra=" cpu_us_per_step=9000.0 cpu_work_us_per_step=3000.0 cpu_game_us_per_step=2000.0 "
                                                   "cpu_render_us_per_step=1000.0 cpu_step_worker_us_per_step=250.0")
fw = m.figures(folder_work)
check(abs(fw["cpu_work_ms"] - 3.0) < 1e-9 and abs(fw["cpu_game_ms"] - 2.0) < 1e-9 and abs(fw["cpu_render_ms"] - 1.0) < 1e-9 and
      abs(fw["cpu_step_worker_ms"] - 0.25) < 1e-9 and abs(fw["cpu_ms"] - 9.0) < 1e-9,
      "the cost figures and the whole process's, from the [pace] lines: %s" % fw)
rowf = m.history_row(fw, "abc", "pc", "d3d12", "120", folder_work, context=CONTEXT)
check(rowf["cpu_work_ms"] == "3.000" and rowf["cpu_game_ms"] == "2.000" and rowf["cpu_render_ms"] == "1.000" and rowf["cpu_ms"] == "9.000",
      "and in the history line: %s" % rowf)
(folder_work / "run.log").write_text('[gpu] adapter="Card A" vram-mb=1\nharness: frame 0 is game frame 10\n'
                                    + "".join(PACE % (0, 1000, " cpu_game_us_per_step=%d.0 cpu_step_worker_us_per_step=%d.0" % (g, g // 10))
                                              for g in (1000, 9000, 2000, 3000)), encoding="utf-8")
fw = m.figures(folder_work)
check(abs(fw["cpu_game_ms"] - 3.0) < 1e-9 and abs(fw["cpu_step_worker_ms"] - 0.3) < 1e-9,
      "each cost figure is the median of the windows after the first (9, 2, 3 -> 3 ms), a spike does not move it: %s" % fw)
check("cpu_work_ms" not in m.figures(folder) and "cpu_game_ms" not in m.figures(folder), "a line without the work fields: no cost figure")
check(m.judge_figures(rowf, fw, [])[0]["figure"] in band.FIGURES, "the judged list is the figures' list")
verdicts = {v["figure"]: v for v in m.judge_figures(rowf, fw, [])}
check(verdicts["cpu_ms"]["judged"] is False and verdicts["cpu_work_ms"]["judged"] is True and verdicts["cpu_game_ms"]["judged"] is True and
      verdicts["cpu_render_ms"]["judged"] is True, "cpu_ms is not judged, the three cost figures are")
# An older history file (20 columns) keeps its lines in their places when the new columns come.
old_history = pathlib.Path(tempfile.mkdtemp()) / "h.tsv"
old_cells = ["old", "2026-11-20 10:00", "pc", "d3d12", "120", "240.0", "10.00", "9.00", "1.00", "5.00", "0", "0.100",
             "-", "300", "R", "PC1", "Card A", "S:1", "100x100", "render_ms=trusted"]
old_history.write_text("\t".join(band.COLUMNS[:20]) + "\n" + "\t".join(old_cells) + "\n", encoding="utf-8")
band.add_row(old_history, row)
rows = band.read_history(old_history)
check(len(rows) == 2 and rows[0]["marks"] == "render_ms=trusted" and rows[0]["step_ms"] == "" and rows[0]["anchor"] == ""
      and rows[0]["memory_mb"] == "300" and rows[1]["step_ms"] == "4.000", "an older file's lines keep their places: %s" % rows[0])
check(rows[0]["cpu_ms"] == "" and band.trusted_values(rows, band.key_of(rows[0]), "cpu_ms") == [] and
      band.anchor_values(rows, band.key_of(rows[0]), "cpu_ms") == [],
      "a line written before cpu_ms reads it as empty and counts it for no baseline, never as 0: %s" % rows[0]["cpu_ms"])
rows[0]["marks"] = "cpu_ms=trusted"
check(band.trusted_values(rows, band.key_of(rows[0]), "cpu_ms") == [], "even marked trusted, an empty cell is no number")
check(old_history.read_text(encoding="utf-8").splitlines()[0] == "\t".join(band.COLUMNS), "the file takes the new header")

# The processor time past its band is worse, twice: WORSE (exit 1). The step's time on the clock is not judged any more: six
# milliseconds a step against four holds.
history = new_history()
code, said, calls, result, out = go(history, "d3d12", steady(pace_extra=" cpu_work_us_per_step=6000.0"), "%09d" % 7)
check(code == 1 and said == ["d3d12 WORSE: cpu_work_ms"] and len(calls) == 2, "cpu_work_ms is judged: %s %s" % (code, said))
code, said, calls, result, out = go(history, "d3d12", steady(steps=[6.0] * 200), "%09d" % 7)
check(code == 0 and said == ["d3d12 holds"] and len(calls) == 1 and
      result["runs"]["d3d12"]["figures"]["step_ms"]["value"] == 6.0, "step_ms 6 against 4 is shown and not judged: %s %s" % (code, said))
# The shown figures are never judged: the longest step, the slow steps and the longest gap far outside any band change
# nothing (no second measure, exit 0) and still go to the history.
code, said, calls, result, out = go(history, "d3d12", steady(steps=[4.0] * 200 + [300.0], gap_spike=500), "%09d" % 8)
fig = result["runs"]["d3d12"]["figures"]
check(code == 0 and len(calls) == 1 and said == ["d3d12 holds"],
      "a slow step and a long gap alone are not worse: %s %s" % (code, said))
check(all(fig[n]["verdict"] == "not judged" and fig[n]["judged"] is False for n in
          ("gap_max_ms", "step_ms", "step_p99_ms", "step_max_ms", "steps_over_18"))
      and fig["step_max_ms"]["value"] == 300.0 and fig["cpu_work_ms"]["judged"] is True, "they are shown, not judged: %s" % fig)
last = [r for r in band.read_history(history)][-1]
check(last["step_max_ms"] == "300.00" and band.parse_marks(last["marks"]).get("step_max_ms") == "trusted",
      "and kept in the history: %s" % last["marks"])

# ================= 2. The floor of each figure's band =================
check(band.FLOORS == {"gap_max_ms": 0.25, "gap_p99_ms": 0.25, "step_ms": 0.02, "step_p99_ms": 0.02, "render_ms": 0.02,
                      "gpu_ms": 0.02, "late_per_min": 2.0, "memory_mb": 32.0, "cpu_ms": 0.02, "cpu_work_ms": 0.02, "cpu_game_ms": 0.02,
                      "cpu_render_ms": 0.02},
      "the floors are the brief's: %s" % band.FLOORS)
for name, floor in (("gap_p99_ms", 0.25), ("cpu_work_ms", 0.02), ("render_ms", 0.02), ("gpu_ms", 0.02),
                    ("late_per_min", 2.0), ("memory_mb", 32.0)):
    zero = [0.0] * 6
    f_ = band.FLOORS[name]
    check(band.judge(f_, zero, floor=f_)["verdict"] == "holds" and band.judge(f_ + 0.001, zero, floor=f_)["verdict"] == "worse"
          and band.judge(0.0, zero, floor=f_)["band"] == f_, "%s: median 0 gives a band of its floor %s" % (name, floor))
check(band.band_of([100.0] * 6, 3.0) == (100.0, 5.0), "the 5 % of the median is the band when it is the largest")
check(band.band_of([1000.0] * 6, 32.0) == (1000.0, 50.0), "memory: 5 % of 1000 MB beats the floor of 32")
check(abs(band.band_of([10.0, 11.0, 12.0, 13.0, 14.0], 0.25)[1] - 3 * 1.4826) < 1e-9,
      "the spread (3 x 1.4826 x the median absolute deviation 1) is the band when it is the largest")
check(band.band_of([0.0] * 6) == (0.0, 0.0), "without a floor a median of 0 gives 0 (what the floor is for)")
# Through the whole measure: render time 0 in six runs, then 0.01 ms (inside the floor) holds, 0.03 ms is worse.
zero_history = new_history(render_us=0)
code, said, calls, result, out = go(zero_history, "d3d12", steady(render_us=10), "%09d" % 7)
fig = result["runs"]["d3d12"]["figures"]["render_ms"]
check(code == 0 and fig["verdict"] == "holds" and fig["median"] == 0.0 and abs(fig["band"] - 0.02) < 1e-12,
      "median 0, 0.01 ms: inside the 0.02 floor: %s" % fig)
code, said, calls, result, out = go(zero_history, "d3d12", steady(render_us=30), "%09d" % 8)
check(code == 1 and said == ["d3d12 WORSE: render_ms"], "median 0, 0.03 ms: worse, twice: %s %s" % (code, said))
# Memory: a rise of 20 MB over 300 (the floor 32 is wider than 5 %) holds; 40 MB is worse.
code, said, calls, result, out = go(history, "d3d12", steady(mem_mb=320), "%09d" % 9)
check(code == 0 and result["runs"]["d3d12"]["figures"]["memory_mb"]["verdict"] == "holds"
      and abs(result["runs"]["d3d12"]["figures"]["memory_mb"]["band"] - 32.0) < 1e-9, "memory +20 MB holds, band 32")
code, said, calls, result, out = go(history, "d3d12", steady(mem_mb=340), "%09d" % 10)
check(code == 1 and "memory_mb" in said[0], "memory +40 MB is worse: %s" % said)

# ================= 3. Not trusted, three more reasons (the processors' watch and dwm.exe are quiet_test.py's) =================
for kind, text in (("covered", "covered first at 3.0 s, 80 of the quarter seconds\n"),
                   ("minimized", "minimized first at 3.0 s, 80 of the quarter seconds\n"),
                   ("active", "active first at 3.0 s, 80 of the quarter seconds\n"),
                   ("pointer", "pointer first at 3.0 s, 80 of the quarter seconds\n")):
    h = pathlib.Path(tempfile.mkdtemp()) / "h.tsv"
    code, said, calls, result, out = go(h, "d3d12", steady(watch=text), "%09d" % 1)
    reason = result["runs"]["d3d12"]["reason"]
    plain = {"covered": "the window was covered by other windows", "minimized": "the window was minimized",
             "active": "the window had the keyboard focus", "pointer": "the pointer moved over the window"}[kind]
    check(code == 3 and result["runs"]["d3d12"]["state"] == "not-trusted" and plain in reason and not h.exists()
          and said == ["d3d12 not trusted (%s)" % reason] and "3.0 s, 80 of the quarter seconds" in reason,
          "a window %s: not trusted with its own plain reason, no line: %s %s" % (kind, code, reason))
check(m.window_disturbed(folder) is None, "a folder with no watch file: nothing reached")
(folder / "window-watch.txt").write_text("covered first at 1.0 s, 4 of the quarter seconds\npointer first at 2.0 s, 9 of the "
                                         "quarter seconds\nsomething new first at 5 s\n", encoding="utf-8")
check(m.window_disturbed(folder) == "the window was covered by other windows, first at 1.0 s, 4 of the quarter seconds; the "
      "pointer moved over the window, first at 2.0 s, 9 of the quarter seconds; something new first at 5 s",
      "each line of the watch is its own reason: %s" % m.window_disturbed(folder))
# Other programs used the processors: more than 10 % is not trusted, exactly 10 % is trusted, not read is trusted and said.
h = pathlib.Path(tempfile.mkdtemp()) / "h.tsv"
code, said, calls, result, out = go(h, "d3d12", steady(), "%09d" % 1,
                                    Machine(cpu=[(10.5, "other programs use the processors 10.5% (qemu.exe 9.0%)")]))
check(code == 3 and said == ["d3d12 not trusted (other programs used the processors)"] and not h.exists()
      and result["runs"]["d3d12"]["reason"] == "other programs used the processors: other programs use the processors 10.5% "
      "(qemu.exe 9.0%)", "the processors used by others over 10 %%: not trusted, no line: %s" % said)
code, said, calls, result, out = go(h, "d3d12", steady(), "%09d" % 1, Machine(cpu=[(10.0, "x")]))
check(code == 0 and h.exists(), "exactly 10 %% of the processors is not more: trusted: %s" % said)
h2 = pathlib.Path(tempfile.mkdtemp()) / "h.tsv"
code, said, calls, result, out = go(h2, "d3d12", steady(), "%09d" % 1, Machine(cpu=[(None, "processor use not read")]))
check(code == 0 and h2.exists() and "processor use not read" in out, "a watch that could not read: trusted, said so: %s" % out)
check(m.quiet.CPU_LIMIT == 10.0, "the limit is 10 %")
# The second measure may be the one that was not trusted: the first (worse) is not written.
h3 = new_history()
size = len(band.read_history(h3))
code, said, calls, result, out = go(h3, "d3d12", lambda r, n: dict(steady(pace_extra=" cpu_work_us_per_step=6000.0")(r, n)), "%09d" % 7,
                                    Machine(cpu=[(0.0, "x"), (30.0, "other programs use the processors 30.0%")]))
check(code == 3 and len(calls) == 2 and len(band.read_history(h3)) == size
      and result["runs"]["d3d12"]["state"] == "not-trusted" and "second measure" in result["runs"]["d3d12"]["reason"],
      "the second measure busy: not trusted, nothing written: %s %s" % (code, said))

# ================= 4. A landing's measure does not write the history: --pending, keep =================
history = new_history()
before = history.read_text(encoding="utf-8")
pending = pathlib.Path(tempfile.mkdtemp()) / "pending.tsv"


def worse_then_fine(renderer, n):
    return {"render_us": 1500 if n == 1 else 1000, "steps": [4.0] * 200, "mem_mb": 300}


pending.write_text("stale\n", encoding="utf-8")
code, said, calls, result, out = go(history, "d3d12", worse_then_fine, "%09d" % 7, pending=str(pending))
check(history.read_text(encoding="utf-8") == before, "with --pending the history is not touched")
kept = band.read_history(pending)
check(code == 0 and len(calls) == 2 and len(kept) == 2 and band.parse_marks(kept[0]["marks"])["render_ms"] == "outlier"
      and band.parse_marks(kept[1]["marks"]) == {"render_ms": "trusted"},
      "the figures were compared with the history as usual (worse once, again, fine) and the two lines are in FILE: %s %s" % (
          code, [r["marks"] for r in kept]))
check(result["pending"] == str(pending), "result.json names the pending file: %s" % result["pending"])
# keep: the lines go to the history, once.
with contextlib.redirect_stdout(io.StringIO()) as o:
    c1 = m.main(["keep", str(pending), "--history", str(history)])
rows_after = band.read_history(history)
check(c1 == 0 and [r["commit"] for r in rows_after].count("000000007") == 2 and "kept 2 line(s)" in o.getvalue(),
      "keep adds the pending lines to the history: %s" % o.getvalue())
with contextlib.redirect_stdout(io.StringIO()) as o:
    c2 = m.main(["keep", str(pending), "--history", str(history)])
check(c2 == 0 and [r["commit"] for r in band.read_history(history)].count("000000007") == 2 and "kept 0 line(s)" in o.getvalue()
      and "(2 already there)" in o.getvalue(), "keep twice adds nothing twice: %s" % o.getvalue())
with contextlib.redirect_stdout(io.StringIO()) as o:
    check(m.main(["keep", str(pending) + ".missing", "--history", str(history)]) == 2, "keep of a file that is not there: exit 2")
# Without --pending the lines go to the history at once (the nightly run).
n_before = len(band.read_history(history))
go(history, "d3d12", steady(), "%09d" % 8)
check(len(band.read_history(history)) == n_before + 1, "no --pending: the history gets the line at once")
# run() starts the pending file anew: a stale file is not kept again.
lock = pathlib.Path(tempfile.mkdtemp()) / "measure.lock"
pending.write_text("stale\n", encoding="utf-8")
args = argparse.Namespace(work=str(pathlib.Path(tempfile.mkdtemp())), commit="0123456789ab", gate_lock="", lock=str(lock),
                          board="", who="t", wait=1.0, runs="d3d12", history=str(history), pending=str(pending),
                          quiet_minutes=0.0)
real = m.measure_runs
try:
    m.measure_runs = lambda a, w: (0, ["d3d12 fake"])
    with contextlib.redirect_stdout(io.StringIO()):
        m.run(args)
finally:
    m.measure_runs = real
check(pending.read_text(encoding="utf-8") == "\t".join(band.COLUMNS) + "\n", "run() empties an old pending file to its header")

# The command line: --pending reaches run(), and a run without it has none.
seen_args = []
real_run = m.run
try:
    m.run = lambda a: seen_args.append(a) or 0
    m.main(["run", "--bin", "B", "--work", "W", "--commit", "C", "--pending", "P.tsv"])
    m.main(["run", "--bin", "B", "--work", "W", "--commit", "C"])
finally:
    m.run = real_run
check(seen_args[0].pending == "P.tsv" and seen_args[1].pending == "", "--pending FILE on the command line: %s" % [a.pending for a in seen_args])

# ================= 4b. The tablet's measure takes no measurement lock and waits for no quiet PC =================
def lock_args(runs, lock):
    return argparse.Namespace(work=str(pathlib.Path(tempfile.mkdtemp())), commit="0123456789ab", gate_lock="", lock=str(lock), board="",
                              who="t", wait=1.0, runs=runs, history=str(pathlib.Path(tempfile.mkdtemp()) / "h.tsv"), pending="",
                              quiet_minutes=0.0)


held = pathlib.Path(tempfile.mkdtemp()) / "measure.lock"
held.mkdir()
(held / "holder.txt").write_text("measure.py other D:/w 2026-11-21 08:00:00 pid %d\n" % __import__("os").getpid(), encoding="utf-8")
ran = []
real = m.measure_runs
try:
    m.measure_runs = lambda a, w: ran.append(a.runs) or (0, ["fake"])
    with contextlib.redirect_stdout(io.StringIO()) as o:
        code = m.run(lock_args("tablet", held))
    check(code == 0 and ran == ["tablet"] and (held / "holder.txt").exists(),
          "runs=tablet beside a PC measure that holds the lock: runs, and leaves the lock as it was: %s %s" % (code, o.getvalue()))
    ran.clear()
    free = pathlib.Path(tempfile.mkdtemp()) / "measure.lock"
    seen_lock = []
    m.measure_runs = lambda a, w: seen_lock.append(free.exists()) or (0, ["fake"])
    with contextlib.redirect_stdout(io.StringIO()):
        m.run(lock_args("tablet", free))
    check(seen_lock == [False] and not free.exists(), "runs=tablet makes no measurement lock folder: %s" % seen_lock)
    m.measure_runs = lambda a, w: ran.append(a.runs) or (0, ["fake"])
    with contextlib.redirect_stdout(io.StringIO()) as o:
        code = m.run(lock_args("d3d12,tablet", held))
    check(code == 2 and ran == [] and "is held" in o.getvalue(), "a PC run beside it is refused: %s %s" % (code, o.getvalue()))
    with contextlib.redirect_stdout(io.StringIO()) as o:
        code = m.run(lock_args("d3d12", held))
    check(code == 2 and ran == [], "so is d3d12 alone")
    seen_lock.clear()
    m.measure_runs = lambda a, w: seen_lock.append(free.exists()) or (0, ["fake"])
    with contextlib.redirect_stdout(io.StringIO()):
        m.run(lock_args("d3d12,tablet", free))
    check(seen_lock == [True] and not free.exists(), "a PC run takes the lock and frees it")
finally:
    m.measure_runs = real
check(m.tablet_only(argparse.Namespace(runs="tablet")) and not m.tablet_only(argparse.Namespace(runs="tablet,d3d12")) and
      not m.tablet_only(argparse.Namespace(runs="")), "tablet_only names the tablet and nothing else")


class NoPcMachine:
    """A machine that fails the test when the PC's quiet, card or processor checks are asked for."""
    def wait_quiet(self):
        raise AssertionError("the tablet's measure waited for a quiet PC")

    def card_use(self):
        raise AssertionError("the tablet's measure read the card")

    def cpu_watch(self):
        raise AssertionError("the tablet's measure watched the processors")


class FakeTabletDevice:
    def answers(self):
        return True

    def shell(self, command):
        return 0, "0\n"


class FakeGate:
    def take_device_lock_wait(self, path, who, minutes):
        pathlib.Path(path).mkdir()
        return None


def tablet_harness(command, env=None, check=False):
    if "prepare" in command or "remove" in command:
        return type("D", (), {"returncode": 0})()
    write_run(pathlib.Path(command[command.index("--work") + 1]) / "runs" / "measure-route" / "vulkan", steps=None)
    return type("D", (), {"returncode": 0})()


targs = argparse.Namespace(runs="tablet", bin="B", android_bin="AB", apk="T.apk", pak="G.pak", device="SERIAL",
                           device_lock=str(pathlib.Path(tempfile.mkdtemp()) / "tablet.lock"), device_wait=1.0, paks="",
                           commit="0123456789ab", history=str(pathlib.Path(tempfile.mkdtemp()) / "h.tsv"), pending="")
with contextlib.redirect_stdout(io.StringIO()):
    code, said = m.measure_runs(targs, pathlib.Path(tempfile.mkdtemp()), tablet_harness, FakeTabletDevice(), FakeGate(), NoPcMachine())
check(code == 0, "the tablet's measure asks the PC for nothing: %s %s" % (code, said))

# ================= 5. result.json =================
history = new_history()
code, said, calls, result, out = go(history, "d3d12", steady(), "%09d" % 7)
run_ = result["runs"]["d3d12"]
check(set(result) == {"commit", "history", "pending", "exit", "verdict", "runs"} and result["exit"] == 0 and
      result["verdict"] == "ok" and result["commit"] == "000000007" and result["pending"] is None,
      "result.json's top: %s" % sorted(result))
check(run_["state"] == "ok" and run_["reason"] == "" and run_["device"] == "pc" and run_["renderer"] == "d3d12",
      "an ordinary run is ok: %s" % {k: v for k, v in run_.items() if k != "figures"})
step = run_["figures"]["cpu_work_ms"]
check(set(step) == {"value", "median", "band", "judged", "verdict", "anchor"} and step["value"] == 4.0 and
      step["median"] == 4.0 and abs(step["band"] - 0.2) < 1e-9 and step["judged"] is True and step["verdict"] == "holds",
      "a figure's value, median, band, judged and verdict: %s" % step)
check(set(run_["figures"]) == set(band.FIGURES), "every figure is in it: %s" % sorted(run_["figures"]))
check(run_["figures"]["gpu_ms"]["verdict"] == "not measured" and run_["figures"]["gpu_ms"]["value"] is None,
      "a figure not measured says so")
# Each state.
code, said, calls, result, out = go(history, "d3d12", steady(pace_extra=" cpu_work_us_per_step=2000.0"), "%09d" % 8)
check(result["runs"]["d3d12"]["state"] == "better" and "cpu_work_ms" in result["runs"]["d3d12"]["reason"] and code == 0,
      "better: %s" % result["runs"]["d3d12"]["reason"])
code, said, calls, result, out = go(pathlib.Path(tempfile.mkdtemp()) / "new.tsv", "d3d12", steady(), "%09d" % 1)
check(result["runs"]["d3d12"]["state"] == "no-baseline" and code == 0, "no baseline yet: %s" % result["runs"]["d3d12"]["state"])
code, said, calls, result, out = go(history, "d3d12", steady(pace_extra=" cpu_work_us_per_step=6000.0"), "%09d" % 9)
run_ = result["runs"]["d3d12"]
check(code == 1 and result["exit"] == 1 and result["verdict"] == "WORSE" and run_["state"] == "worse" and
      run_["reason"].startswith("worse twice: cpu_work_ms 6 (median 4, band 0.2)") and
      run_["figures"]["cpu_work_ms"]["verdict"] == "worse", "worse twice: the figure, its median and its band: %s" % run_["reason"])
code, said, calls, result, out = go(history, "d3d12", worse_then_fine, "%09d" % 10)
check(code == 0 and result["runs"]["d3d12"]["state"] == "ok" and
      result["runs"]["d3d12"]["reason"] == "worse in render_ms once, fine when measured again", "ok after a second measure")
code, said, calls, result, out = go(history, "d3d12", steady(), "%09d" % 11, Machine(quiet=[(False, "still busy after 30 minutes: cl.exe (pid 4)")]))
check(code == 3 and result["exit"] == 3 and result["runs"]["d3d12"]["state"] == "not-trusted" and
      "cl.exe" in result["runs"]["d3d12"]["reason"] and result["runs"]["d3d12"]["figures"] == {}, "not trusted: %s" % result["runs"])
code, said, calls, result, out = go(history, "vulkan", lambda r, n: None, "%09d" % 12)
check(code == 2 and result["exit"] == 2 and result["runs"]["vulkan"]["state"] == "tool" and
      "harness exit 1" in result["runs"]["vulkan"]["reason"], "tool: %s" % result["runs"])
code, said, calls, result, out = go(history, "d3d12,tablet", steady(), "%09d" % 13)
check(result["runs"]["tablet"]["state"] == "left-out" and result["runs"]["d3d12"]["state"] == "ok" and code == 0,
      "a run not asked for is left-out (the tablet with no device): %s" % result["runs"]["tablet"])
# result.json is also written when the measure does not start or stops inside.
lock = pathlib.Path(tempfile.mkdtemp()) / "measure.lock"
lock.mkdir()
(lock / "holder.txt").write_text("someone 2026-11-21 00:00:00\n", encoding="utf-8")
work = pathlib.Path(tempfile.mkdtemp())
args = argparse.Namespace(work=str(work), commit="0123456789ab", gate_lock="", lock=str(lock), board="", who="t", wait=1.0,
                          runs="d3d12,vulkan", history=str(history), pending="", quiet_minutes=0.0)
with contextlib.redirect_stdout(io.StringIO()):
    code = m.run(args)
stopped = json.loads((work / "result.json").read_text(encoding="utf-8"))
check(code == 2 and stopped["exit"] == 2 and stopped["runs"]["vulkan"]["state"] == "tool" and
      "held" in stopped["runs"]["d3d12"]["reason"], "a held measure lock: result.json says tool: %s" % stopped)
shutil_lock = pathlib.Path(tempfile.mkdtemp()) / "measure.lock"
args.lock = str(shutil_lock)


def boom(a, w):
    raise KeyError("boom")


try:
    m.measure_runs = boom
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        code = m.run(args)
finally:
    m.measure_runs = real
stopped = json.loads((work / "result.json").read_text(encoding="utf-8"))
check(code == 2 and "the measure stopped: KeyError" in stopped["runs"]["d3d12"]["reason"], "a stop inside: result.json says tool")

# ================= 6. The anchor =================
ARGS_FIG = {"render_ms": 1.0}


def put(history, commit, render, n=1, mark="trusted", **key):
    for _ in range(n):
        ctx = dict(CONTEXT)
        ctx.update(key)
        row_ = m.history_row({"render_ms": render}, commit, "pc", "d3d12", "120", "R", context=ctx, marks={"render_ms": mark})
        band.add_row(history, row_)


creep = pathlib.Path(tempfile.mkdtemp()) / "creep.tsv"
put(creep, "aaaaaaaaa", 1.0, 5)                      # the start of the native work
put(creep, "bbbbbbbbb", 1.9, 10)                     # the rolling median followed a slow creep
put(creep, "aaaaaaaaa", 1.0, 1, computer="OTHERPC")  # another computer's line of that commit is not this key's
with contextlib.redirect_stdout(io.StringIO()) as o:
    check(m.main(["anchor", "--commit", "aaaaaaaa", "--history", str(creep)]) == 0, "anchor of a commit with lines: exit 0")
check("anchor: the start of the native work is aaaaaaaa: 6 trusted line(s) for 2 key(s)" in o.getvalue(),
      "anchor says what it marked: %s" % o.getvalue())
rows = band.read_history(creep)
check([r["anchor"] for r in rows].count("1") == 6 and all(r["anchor"] == ("1" if r["commit"] == "aaaaaaaaa" else "")
                                                          for r in rows), "only that commit's lines are marked")
code, said, calls, result, out = go(creep, "d3d12", steady(render_us=1900), "%09d" % 1)
fig = result["runs"]["d3d12"]["figures"]["render_ms"]
check(fig["verdict"] == "holds" and code == 0, "the rolling median follows the creep: holds (%s)" % fig["verdict"])
check(fig["anchor"]["verdict"] == "slower" and abs(fig["anchor"]["percent"] - 90.0) < 1e-6 and fig["anchor"]["median"] == 1.0 and
      fig["anchor"]["says"] == "slower than at the start of the native work by 90%" and fig["anchor"]["lines"] == 5,
      "the anchor says slower by 90 %%: %s" % fig["anchor"])
check("render_ms: slower than at the start of the native work by 90%" in out and result["runs"]["d3d12"]["anchor"] ==
      {"state": "ok", "lines": 5}, "and it is printed: %s" % out)
check(result["exit"] == 0 and code == 0 and said == ["d3d12 holds"], "the comparison never changes the exit code")
step = result["runs"]["d3d12"]["figures"]["cpu_work_ms"]["anchor"]
check(step["verdict"] == "not enough lines" and step["lines"] == 0, "a figure with no anchor values compares nothing: %s" % step)
check(result["runs"]["d3d12"]["figures"]["step_max_ms"]["anchor"] is None, "a figure that is not judged is not compared")
# Inside the band against the anchor: nothing is said.
code, said, calls, result, out = go(creep, "d3d12", steady(render_us=1010), "%09d" % 2)
check(result["runs"]["d3d12"]["figures"]["render_ms"]["anchor"]["verdict"] == "holds" and "slower than" not in out,
      "inside the anchor's band: nothing said")
# Faster than the anchor is recorded and not printed.
fast = pathlib.Path(tempfile.mkdtemp()) / "fast.tsv"
put(fast, "aaaaaaaaa", 2.0, 5)
m.main(["anchor", "--commit", "aaaaaaaaa", "--history", str(fast)])
code, said, calls, result, out = go(fast, "d3d12", steady(render_us=1000), "%09d" % 3)
check(result["runs"]["d3d12"]["figures"]["render_ms"]["anchor"]["verdict"] == "faster" and "slower than" not in out,
      "faster than the anchor: recorded, not said")
# Fewer than five anchor lines: "anchor: not enough lines", nothing compared.
few = pathlib.Path(tempfile.mkdtemp()) / "few.tsv"
put(few, "aaaaaaaaa", 1.0, 4)
put(few, "bbbbbbbbb", 3.0, 8)
m.main(["anchor", "--commit", "aaaaaaaaa", "--history", str(few)])
code, said, calls, result, out = go(few, "d3d12", steady(render_us=3000), "%09d" % 4)
check("anchor: not enough lines (4 of 5)" in out and result["runs"]["d3d12"]["anchor"] == {"state": "not enough lines", "lines": 4}
      and result["runs"]["d3d12"]["figures"]["render_ms"]["anchor"]["verdict"] == "not enough lines" and
      "slower than" not in out, "four anchor lines: not enough lines, nothing compared: %s" % out)
none = new_history()
code, said, calls, result, out = go(none, "d3d12", steady(), "%09d" % 7)
check("anchor: not enough lines (0 of 5)" in out, "no anchor set at all says so too")
# An anchor line a later accept retired still counts; a second anchor replaces the first; an unknown commit is refused.
retired = pathlib.Path(tempfile.mkdtemp()) / "r.tsv"
put(retired, "aaaaaaaaa", 1.0, 5)
m.main(["anchor", "--commit", "aaaaaaaaa", "--history", str(retired)])
put(retired, "bbbbbbbbb", 2.0, 6)
band.accept(retired, "bbbbbbbbb")
check(band.anchor_values(band.read_history(retired), band.key_of(band.read_history(retired)[0]), "render_ms") == [1.0] * 5 and
      all(band.parse_marks(r["marks"])["render_ms"] == "retired" for r in band.read_history(retired)[:5]),
      "an accepted slowdown retires the old lines but the anchor keeps them")
with contextlib.redirect_stdout(io.StringIO()) as o:
    check(m.main(["anchor", "--commit", "bbbbbbbbb", "--history", str(retired)]) == 0 and
          [r["anchor"] for r in band.read_history(retired)].count("1") == 6, "a second anchor moves it")
    before = retired.read_text(encoding="utf-8")
    check(m.main(["anchor", "--commit", "deadbeef0", "--history", str(retired)]) == 2 and
          retired.read_text(encoding="utf-8") == before, "an unknown commit is refused, nothing changes")
# Only trusted lines are marked.
odd = pathlib.Path(tempfile.mkdtemp()) / "odd.tsv"
put(odd, "aaaaaaaaa", 1.0, 3, mark="outlier")
with contextlib.redirect_stdout(io.StringIO()):
    check(m.main(["anchor", "--commit", "aaaaaaaaa", "--history", str(odd)]) == 2, "a commit with no trusted line is refused")

# ================= 7. The tablet's gap figures =================
# The Android game writes no game-frame and no step-wait events; the render thread's content-time events (a = the list's game
# frame) mark the stage's first frame instead. Frame 10 is the first picture shown with a list of frame 10 or later.
records = [(50, m.PRESENT_END, 0, 0), (60, m.PRESENT_END, 1, 0), (900, m.PRESENT_END, 2, 0),  # before frame 10: left out
           (95, m.CONTENT_TIME, 9, 0)]
t = 100
records.append((t, m.CONTENT_TIME, 10, 500000))
for i in range(300):
    t += 400 if i == 100 else 10
    records.append((t, m.PRESENT_END, 10 + i, 0))
    records.append((t - 5, m.WALK_BEGIN, i, 0))
    records.append((t - 3, m.WALK_END, i, 0))
tablet = pathlib.Path(tempfile.mkdtemp()) / "vulkan"
tablet.mkdir()
timeline(tablet / "timeline.bin", sorted(records))
f = m.timeline_figures(tablet / "timeline.bin", 10)
check(abs(f.get("gap_max_ms", 0) - 400.0) < 1e-9 and abs(f.get("gap_p99_ms", 0) - 10.0) < 1e-9,
      "a timeline with no game-frame events: gap figures from the content-time marker: %s" % f)
check("step_max_ms" not in f and "step_ms" not in f and abs(f.get("render_ms", 0) - 2.0) < 1e-9,
      "no step-wait events: no step figures from the timeline: %s" % f)
check(m.timeline_figures(tablet / "timeline.bin", 999) == {}, "a frame the stage never reached: no figures")
# With both markers the game's own marker wins (a Windows run).
both = sorted(records + [(2000, m.GAME_FRAME, 10, 0), (2000, m.PRESENT_END, 7, 0), (2010, m.PRESENT_END, 8, 0)])
timeline(tablet / "both.bin", both)
f = m.timeline_figures(tablet / "both.bin", 10)
check(abs(m.timeline_figures(tablet / "both.bin", 10)["gap_max_ms"] - 10.0) < 1e-9,
      "a Windows timeline (game-frame events) keeps the game's marker, not the content-time's: %s" % f)
# The tablet's step figures are the [pace] windows' own: the median of the windows' average step work and of their 99th
# percentile (the first window after frame 0 is left out, as for every [pace] figure).
(tablet / "run.log").write_text("harness: frame 0 is game frame 10\n" + (PACE % (0, 1000, " step_work_avg_us=900.0 "
                                "step_work_p99_us=14000.0")) + (PACE % (0, 1000, " step_work_avg_us=2400.0 "
                                "step_work_p99_us=32000.0")) + (PACE % (0, 1000, " step_work_avg_us=1000.0 "
                                                                       "step_work_p99_us=20000.0")) +
                                (PACE % (0, 1000, " step_work_avg_us=1200.0 step_work_p99_us=22000.0")), encoding="utf-8")
f = m.figures(tablet)
check(abs(f["step_ms"] - 1.2) < 1e-9 and abs(f["step_p99_ms"] - 22.0) < 1e-9 and abs(f["gap_max_ms"] - 400.0) < 1e-9,
      "the tablet's run gives step and gap figures: %s" % f)
# A [pace] window without those fields (an older game): no step figures, nothing invented.
(tablet / "run.log").write_text("harness: frame 0 is game frame 10\n" + PACE % (0, 1000, "") * 2, encoding="utf-8")
check("step_ms" not in m.figures(tablet) and "step_p99_ms" not in m.figures(tablet), "no fields, no step figures")

if failures:
    print("second_part_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("second_part_test.py: pass (%d checks)" % checks[0])
