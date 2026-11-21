"""band_test.py: the speed measure's band, history, run-again-once and accept (T7a), with fakes: no game, no machine.

python -X utf8 -I tools/measure/tests/band_test.py
"""
import argparse
import io
import contextlib
import json
import pathlib
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


# ---- The band's arithmetic on fixed numbers ----
# Spread zero: the 5 % floor decides. Median 100, so the band is 5.
flat = [100.0] * 10
check(band.band_of(flat) == (100.0, 5.0), "the floor: median 100, no spread: %s" % (band.band_of(flat),))
check(band.judge(105.0, flat)["verdict"] == "holds", "exactly on the floor's edge holds")
check(band.judge(105.01, flat)["verdict"] == "worse", "just past the floor on the bad side is worse")
check(band.judge(95.0, flat)["verdict"] == "holds", "exactly on the good edge holds")
check(band.judge(94.99, flat)["verdict"] == "better", "past the band on the good side is better, not worse")
check(band.judge(1.0, flat)["verdict"] == "better", "far on the good side is better, never an error")
# The spread wins: 90, 95, 100, 105, 110: median 100, MAD 5, 3 x 1.4826 x 5 = 22.239 against the floor's 5.
spread = [90.0, 95.0, 100.0, 105.0, 110.0]
median, width = band.band_of(spread)
check(median == 100.0 and abs(width - 22.239) < 1e-9, "the spread wins: %s %s" % (median, width))
check(band.judge(122.2, spread)["verdict"] == "holds", "inside the spread's band holds (122.2)")
check(band.judge(122.3, spread)["verdict"] == "worse", "past the spread's band is worse (122.3)")
check(band.judge(77.7, spread)["verdict"] == "better" and band.judge(77.8, spread)["verdict"] == "holds",
      "the spread's good side")
# One wild earlier value does not move the median or the spread much (that is why it is the median).
check(band.judge(104.0, [100.0, 100.0, 100.0, 100.0, 5000.0])["verdict"] == "holds", "a wild earlier value does not widen it")
# Fewer than five trusted earlier runs: no baseline yet, never worse.
for n in range(0, 5):
    r = band.judge(1e9, [100.0] * n)
    check(r["verdict"] == "no baseline yet" and r["median"] is None, "%d earlier runs: no baseline yet (%s)" % (n, r))
check(band.judge(1e9, [100.0] * 5)["verdict"] == "worse", "five earlier runs make a baseline")
# Only the last ten count: five old 1000s, then ten 100s.
check(band.judge(100.0, [1000.0] * 5 + [100.0] * 10)["median"] == 100.0, "only the last ten earlier runs count")
# A figure where lower is bad: the sides turn round.
check(band.judge(94.0, flat, bad="lower")["verdict"] == "worse" and band.judge(106.0, flat, bad="lower")["verdict"] ==
      "better", "lower is bad: frames a second below the band are worse")
# Not measured is compared with nothing (never 0).
r = band.judge(None, flat)
check(r["verdict"] == "not measured" and r["median"] is None and r["band"] is None, "not measured: %s" % r)
# Without a floor a median of 0 gives a band of 0: any rise is outside it (second_part_test.py gives each figure its floor).
check(band.judge(0.0, [0.0] * 6)["verdict"] == "holds" and band.judge(0.1, [0.0] * 6)["verdict"] == "worse",
      "median 0, no floor: the band is 0")
check(band.FIGURES["gap_max_ms"] == "higher" and set(band.FIGURES) == set(m.COMPARED),
      "every compared figure has a bad side")

# ---- Marks and keys in the history ----
check(band.parse_marks("a=trusted;b=worse;c=bogus;=trusted") == {"a": "trusted", "b": "worse"}, "marks are read")
check(band.parse_marks(band.format_marks({"b": "outlier", "a": "trusted"})) == {"a": "trusted", "b": "outlier"},
      "marks round-trip")
base = {"computer": "PC1", "gpu": "Card A", "device": "pc", "renderer": "d3d12", "scene": "S:1", "size": "100x100",
        "rate": "120"}


def row(commit="c1", value="10.00", marks="step_max_ms=trusted", **over):
    r = {c: "" for c in band.COLUMNS}
    r.update(base)
    r.update({"commit": commit, "time": "2026-11-21 00:00", "step_max_ms": value, "marks": marks, "run": "R"})
    r.update(over)
    return r


key = band.key_of(row())
rows = [row("a", "10"), row("b", "11"), row("c", "12", "step_max_ms=outlier"), row("d", "13", "step_max_ms=worse"),
        row("e", "14", "step_max_ms=retired"), row("f", "-", "step_max_ms=trusted"), row("g", "15", ""),
        row("h", "16")]
check(band.trusted_values(rows, key, "step_max_ms") == [10.0, 11.0, 16.0],
      "only trusted lines with a number count: %s" % band.trusted_values(rows, key, "step_max_ms"))
for part, other in (("computer", "PC2"), ("gpu", "Card B"), ("device", "pc-4cores"), ("renderer", "vulkan"),
                    ("scene", "S:2"), ("size", "200x100"), ("rate", "60")):
    mixed = rows + [row("z", "999", **{part: other})]
    check(band.trusted_values(mixed, key, "step_max_ms") == [10.0, 11.0, 16.0],
          "a different %s does not mix in" % part)
    check(band.trusted_values(mixed, band.key_of(row(**{part: other})), "step_max_ms") == [999.0],
          "a different %s has its own baseline" % part)

# ---- Fakes for the runs ----
pace = "[pace] seconds=10 steps=600 refresh_ms=8.333 missed=0 step_work_max_us=5000 render_avg_us=%d\n"


class Machine:
    """A machine with scripted answers: waits for quiet (True, ""), the card's use as (percent, text), and the
    processors' use by other programs over a run as (percent, text)."""
    def __init__(self, quiet=None, card=None, cpu=None):
        self.quiet, self.card, self.cpu, self.asked = list(quiet or []), list(card or []), list(cpu or []), 0

    def wait_quiet(self):
        self.asked += 1
        return self.quiet.pop(0) if self.quiet else (True, "")

    def card_use(self):
        return self.card.pop(0) if self.card else (0.0, "other programs use the card 0.0%")

    def cpu_watch(self):
        machine = self

        class Watch:
            def begin(self):
                pass

            def end(self):
                return machine.cpu.pop(0) if machine.cpu else (0.0, "other programs use the processors 0.0%")
        return Watch()


def make_runner(steps, calls, extra=None):
    """A harness that writes a run folder: the step work (microseconds) for the renderer, one value a call (the last
    one stays)."""
    def runner(command, env=None, check=False):
        renderer = command[command.index("--renderer") + 1]
        work = pathlib.Path(command[command.index("--work") + 1])
        folder = work / "runs" / "measure-route" / renderer
        folder.mkdir(parents=True)
        values = steps[renderer]
        value = values.pop(0) if len(values) > 1 else values[0]
        calls.append((renderer, work.name))
        (folder / "run.log").write_text('[gpu] adapter="Card A" vram-mb=1\n'
                                        '[Renderer] internal resolution 1280x720 -> 3840x2160\n'
                                        "harness: frame 0 is game frame 10\n" + pace % value * 3, encoding="utf-8")
        (folder / "test.json").write_text(json.dumps({"name": "measure-route", "stage": {"name": "S", "scenario": 1}}),
                                          encoding="utf-8")
        if extra:
            extra(folder)
        return type("D", (), {"returncode": 0})()
    return runner


def go(history, runs, steps, commit, machine=None, extra=None):
    args = argparse.Namespace(runs=runs, bin="B", android_bin="", apk="", pak="", device="", device_lock="", paks="",
                              commit=commit, history=str(history))
    calls, work = [], pathlib.Path(tempfile.mkdtemp())
    with contextlib.redirect_stdout(io.StringIO()) as out:
        code, said = m.measure_runs(args, work, make_runner(steps, calls, extra), machine=machine or Machine())
    return code, said, calls, work, out.getvalue()


def figure_rows(history, renderer="d3d12"):
    return [r for r in m.read_history(history) if r["renderer"] == renderer]


def verdict_of(work):
    return json.loads((work / m.VERDICT_FILE).read_text(encoding="utf-8"))


def entry(verdict, figure, attempt=1):
    return next(e for e in verdict["figures"] if e["key"]["figure"] == figure and e["attempt"] == attempt)


# ---- No baseline yet: the first runs are trusted, compared with nothing, exit 0 ----
history = pathlib.Path(tempfile.mkdtemp()) / "measure-history.tsv"
for n in range(1, 6):
    code, said, calls, work, out = go(history, "d3d12", {"d3d12": [10000]}, "%09d" % n)
    check(code == 0 and said == ["d3d12 holds"], "run %d of a new history: exit 0, %s %s" % (n, code, said))
    if n == 1:
        check("no baseline yet" in out, "the first run says no baseline yet: %s" % out)
check(len(figure_rows(history)) == 5, "five lines written")
first = figure_rows(history)[0]
check(first["computer"] and first["gpu"] == "Card A" and first["scene"] == "S:1" and first["size"] == "3840x2160" and
      first["device"] == "pc" and first["rate"] == "120", "the line carries the key: %s" % first)
marks = band.parse_marks(first["marks"])
check(marks.get("step_max_ms") == "trusted" and "gpu_ms" not in marks and first["gpu_ms"] == "-",
      "a figure not measured has no mark and is written '-': %s" % first["marks"])
v = verdict_of(work)
g = entry(v, "gpu_ms")
check(g["verdict"] == "not measured" and g["value"] is None and g["median"] is None and g["mark"] == "-" and
      v["exit"] == 0 and v["verdict"] == "ok", "the verdict file: a figure not measured is compared with nothing: %s" % g)
s = entry(v, "render_ms")
check(s["verdict"] == "no baseline yet" or s["verdict"] == "holds", "the verdict file has each figure: %s" % s)
# From the sixth run there is a baseline (five trusted earlier).
code, said, calls, work, out = go(history, "d3d12", {"d3d12": [10000]}, "%09d" % 6)
s = entry(verdict_of(work), "render_ms")
check(s["verdict"] == "holds" and s["median"] == 10.0 and abs(s["band"] - 0.5) < 1e-9 and s["mark"] == "trusted",
      "with five trusted lines the figure is compared: %s" % s)

# ---- Better is reported, never an error ----
code, said, calls, work, out = go(history, "d3d12", {"d3d12": [8000]}, "%09d" % 7)
check(code == 0 and said == ["d3d12 better: render_ms"] and len(calls) == 1, "better: exit 0, no second run: %s %s" % (code, said))
check(band.parse_marks(figure_rows(history)[-1]["marks"])["render_ms"] == "trusted", "a better figure is trusted")

# ---- Worse once, fine the second time: ok, outlier then trusted ----
n_before = len(figure_rows(history))
code, said, calls, work, out = go(history, "d3d12", {"d3d12": [12000, 10000]}, "%09d" % 8)
rows = figure_rows(history)
check(code == 0 and len(calls) == 2 and calls[1][1].endswith("-again"), "run again once, in its own folder: %s %s" % (code, calls))
check(len(rows) == n_before + 2, "both measures are kept")
r1, r2 = rows[-2], rows[-1]
check(band.parse_marks(r1["marks"])["render_ms"] == "outlier" and band.parse_marks(r2["marks"]) == {"render_ms": "trusted"},
      "the first figure is an outlier, the second trusted, and the second's other figures carry no mark: %s | %s" % (
          r1["marks"], r2["marks"]))
check(r1["render_ms"] == "12.000" and r2["render_ms"] == "10.000", "the figures are the two measures'")
check(said == ["d3d12 ok (worse in render_ms once, fine when measured again)"], "the verdict is ok: %s" % said)
v = verdict_of(work)
check(entry(v, "render_ms", 1)["verdict"] == "worse" and entry(v, "render_ms", 1)["mark"] == "outlier" and
      entry(v, "render_ms", 2)["verdict"] == "holds" and entry(v, "render_ms", 2)["mark"] == "trusted" and
      v["exit"] == 0, "the verdict file keeps both: %s" % v["figures"][:3])
check(12.0 not in band.trusted_values(m.read_history(history), band.key_of(r1), "render_ms"),
      "an outlier is never in a baseline")

# ---- Worse twice: WORSE, both kept as worse, exit 1 ----
code, said, calls, work, out = go(history, "d3d12", {"d3d12": [13000, 13500]}, "%09d" % 9)
rows = figure_rows(history)
check(code == 1 and said == ["d3d12 WORSE: render_ms"] and len(calls) == 2, "worse twice: exit 1, %s %s" % (code, said))
check(all(band.parse_marks(r["marks"])["render_ms"] == "worse" for r in rows[-2:]), "both lines are marked worse")
check(verdict_of(work)["exit"] == 1 and verdict_of(work)["verdict"] == "WORSE", "the verdict file says WORSE")
check(not {13.0, 13.5} & set(band.trusted_values(m.read_history(history), band.key_of(rows[-1]), "render_ms")),
      "worse lines are not in the baseline")

# ---- accept: the baseline restarts from a commit, its worse lines become trusted ----
worse_commit = "%09d" % 9
code, say = 0, ""
f_out = io.StringIO()
with contextlib.redirect_stdout(f_out):
    code = m.main(["accept", "--commit", worse_commit, "--history", str(history), "--renderer", "d3d12"])
rows = figure_rows(history)
trusted_now = band.trusted_values(m.read_history(history), band.key_of(rows[-1]), "render_ms")
check(code == 0 and trusted_now == [13.0, 13.5], "accept: the older lines retire, the worse lines from the commit on are trusted: %s %s" % (
    code, trusted_now))
check("accepted from" in f_out.getvalue(), "accept says what it did: %s" % f_out.getvalue())
code, said, calls, work, out = go(history, "d3d12", {"d3d12": [13000]}, "%09d" % 10)
check(code == 0 and entry(verdict_of(work), "render_ms")["verdict"] == "no baseline yet",
      "after accept, the new level starts a baseline (only two trusted lines): %s %s" % (code, said))
# A commit that is not in the history is refused (exit 2), changing nothing.
size_before = history.read_text(encoding="utf-8")
with contextlib.redirect_stdout(io.StringIO()):
    check(m.main(["accept", "--commit", "deadbeef0", "--history", str(history)]) == 2, "accept of an unknown commit: exit 2")
check(history.read_text(encoding="utf-8") == size_before, "a refused accept changes nothing")

# accept for one renderer and scene only: the others keep their baseline.
mixed_history = pathlib.Path(tempfile.mkdtemp()) / "h.tsv"
for i, (rend, scene) in enumerate([("d3d12", "S:1"), ("vulkan", "S:1"), ("d3d12", "S:2")] * 4):
    band.add_row(mixed_history, row("c%02d" % (i // 3), "10", renderer=rend, scene=scene))
band.add_row(mixed_history, row("c9", "20", "step_max_ms=worse", renderer="d3d12", scene="S:1"))
keys_changed, lines_changed = band.accept(mixed_history, "c9", renderer="d3d12", scene="S:1")
after = band.read_history(mixed_history)
mine = band.key_of(row(renderer="d3d12", scene="S:1"))
check(keys_changed == 1 and band.trusted_values(after, mine, "step_max_ms") == [20.0], "accept for one renderer and scene")
check(len(band.trusted_values(after, band.key_of(row(renderer="vulkan", scene="S:1")), "step_max_ms")) == 4 and
      len(band.trusted_values(after, band.key_of(row(renderer="d3d12", scene="S:2")), "step_max_ms")) == 4,
      "the other renderer and scene keep theirs")
try:
    band.accept(mixed_history, "c9", renderer="d3d11")
    check(False, "accept for a renderer with no line is refused")
except ValueError:
    check(True, "")

# ---- Only the scenes and renderers that were worse are measured again ----
h2 = pathlib.Path(tempfile.mkdtemp()) / "h.tsv"
for n in range(1, 7):
    go(h2, "d3d12,vulkan", {"d3d12": [10000], "vulkan": [10000]}, "%09d" % n)
code, said, calls, work, out = go(h2, "d3d12,vulkan", {"d3d12": [14000, 10000], "vulkan": [10000]}, "%09d" % 7)
check([c[0] for c in calls] == ["d3d12", "vulkan", "d3d12"] and code == 0, "only the worse renderer again: %s %s" % (calls, code))
check(len(figure_rows(h2, "vulkan")) == 7 and len(figure_rows(h2, "d3d12")) == 8, "the lines: vulkan 7, d3d12 8")

# ---- Not trusted: no line, no comparison, exit 3 ----
h3 = pathlib.Path(tempfile.mkdtemp()) / "h.tsv"
code, said, calls, work, out = go(h3, "d3d12", {"d3d12": [10000]}, "%09d" % 1, Machine(quiet=[(False, "still busy after 30 minutes: cl.exe (pid 4)")]))
check(code == 3 and calls == [] and not h3.exists() and said == ["d3d12 not trusted (the machine was not quiet: still busy after 30 minutes: cl.exe (pid 4))"],
      "a machine that never goes quiet: not trusted, nothing run, nothing written, exit 3: %s %s" % (code, said))
check("not trusted" in out and "cl.exe" in out, "the reason is printed")
check(verdict_of(work)["exit"] == 3 and verdict_of(work)["runs_without_figures"][0]["status"] == "not trusted",
      "the verdict file says not trusted")
code, said, calls, work, out = go(h3, "d3d12", {"d3d12": [10000]}, "%09d" % 1, Machine(card=[(25.0, "other programs use the card 25.0% (chrome.exe 25.0%)"), (0.0, "x")]))
check(code == 3 and said == ["d3d12 not trusted (card in use by other programs)"] and not h3.exists(),
      "the card busy before: not trusted: %s" % said)
code, said, calls, work, out = go(h3, "d3d12", {"d3d12": [10000]}, "%09d" % 1, Machine(card=[(0.0, "x"), (10.5, "other programs use the card 10.5%")]))
check(code == 3 and said == ["d3d12 not trusted (card in use by other programs)"] and not h3.exists(),
      "the card busy after: not trusted: %s" % said)
code, said, calls, work, out = go(h3, "d3d12", {"d3d12": [10000]}, "%09d" % 1, Machine(card=[(10.0, "x"), (10.0, "x")]))
check(code == 0 and h3.exists(), "exactly 10 % is not more than 10 %: trusted")
h4 = pathlib.Path(tempfile.mkdtemp()) / "h.tsv"
code, said, calls, work, out = go(h4, "d3d12", {"d3d12": [10000]}, "%09d" % 1, Machine(card=[(None, "card use not read")] * 2))
check(code == 0 and h4.exists() and "card use not read" in out, "counters that cannot be read: trusted, said so: %s" % out)
# The second measure not trusted: the first (worse) is not written; exit 3.
h5 = pathlib.Path(tempfile.mkdtemp()) / "h.tsv"
for n in range(1, 7):
    go(h5, "d3d12", {"d3d12": [10000]}, "%09d" % n)
size = len(figure_rows(h5))
code, said, calls, work, out = go(h5, "d3d12", {"d3d12": [14000, 10000]}, "%09d" % 7, Machine(quiet=[(True, ""), (False, "busy")]))
check(code == 3 and len(calls) == 1 and len(figure_rows(h5)) == size, "the second measure not trusted: exit 3, nothing written: %s %s" % (code, said))

# ---- Exit 2: a run that gives nothing, or results that cannot be read ----
h6 = pathlib.Path(tempfile.mkdtemp()) / "h.tsv"


def broken(folder):
    (folder / "timeline.bin").write_bytes(b"not a timeline at all, long enough to read a header " * 3)


code, said, calls, work, out = go(h6, "d3d12", {"d3d12": [10000]}, "%09d" % 1, extra=broken)
check(code == 2 and said[0].startswith("d3d12 no run (its results could not be read: ") and not h6.exists(),
      "results that cannot be read: exit 2: %s %s" % (code, said))
# A run's harness exit other than 0 is a tool fault, not a figure.
args = argparse.Namespace(runs="d3d12", bin="B", android_bin="", apk="", pak="", device="", device_lock="", paks="",
                          commit="0" * 9, history=str(h6))
with contextlib.redirect_stdout(io.StringIO()):
    code, said = m.measure_runs(args, pathlib.Path(tempfile.mkdtemp()), lambda *a, **k: type("D", (), {"returncode": 1})(),
                                machine=Machine())
check(code == 2, "a run with no folder: exit 2: %s" % said)
# The exit codes win in the order tool fault (2), WORSE (1), not trusted (3).
h7 = pathlib.Path(tempfile.mkdtemp()) / "h.tsv"
for n in range(1, 7):
    go(h7, "d3d12", {"d3d12": [10000]}, "%09d" % n)
code, said, calls, work, out = go(h7, "d3d12,vulkan", {"d3d12": [14000, 14000], "vulkan": [10000]}, "%09d" % 7,
                                  Machine(quiet=[(True, ""), (False, "busy")]))
check(code == 1 and verdict_of(work)["exit"] == 1 and any("not trusted" in s for s in said),
      "WORSE beside a run that was not trusted: exit 1: %s %s" % (code, said))

# ---- run(): the exit code is what measure_runs gave; a stop inside is 2, and the lock is freed ----
lock = pathlib.Path(tempfile.mkdtemp()) / "measure.lock"
args = argparse.Namespace(work=str(pathlib.Path(tempfile.mkdtemp())), commit="0123456789ab", gate_lock="", lock=str(lock),
                          board="", who="t", wait=1.0, runs="d3d12", history="h", quiet_minutes=0.0)
real = m.measure_runs
try:
    for given in (0, 1, 3):
        m.measure_runs = lambda a, w, given=given: (given, ["d3d12 fake"])
        with contextlib.redirect_stdout(io.StringIO()) as out:
            code = m.run(args)
        check(code == given and out.getvalue().strip().endswith("measure: d3d12 fake") and not lock.exists(),
              "run() gives exit %d and its last line starts 'measure: ': %s" % (given, out.getvalue()))

    def stop(a, w):
        raise KeyError("boom")
    m.measure_runs = stop
    with contextlib.redirect_stdout(io.StringIO()) as out, contextlib.redirect_stderr(io.StringIO()):
        code = m.run(args)
    check(code == 2 and "the measure stopped: KeyError" in out.getvalue() and not lock.exists(),
          "a stop inside is exit 2, the lock freed: %s" % out.getvalue())
    lock.mkdir()
    (lock / "holder.txt").write_text("someone 2026-11-21 00:00:00\n", encoding="utf-8")
    m.measure_runs = lambda a, w: (0, [])
    with contextlib.redirect_stdout(io.StringIO()) as out:
        code = m.run(args)
    check(code == 2 and "held" in out.getvalue() and lock.exists(), "a held measure lock: exit 2, the lock left: %s" % out.getvalue())
finally:
    m.measure_runs = real

if failures:
    print("band_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("band_test.py: pass (%d checks)" % checks[0])
