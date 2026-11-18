"""measure_short.py: the short measurement set at the long run's end (docs/plan/04-GATE-AND-MAIN.md section 10, N-1043).

  measure_short.py run --bin BIN --pak PAK --work WORK --commit SHA [--renderer R] [--history FILE] [--lock PATH]
      One renderer a run, in turn (d3d12, vulkan, d3d11 by the history's count unless --renderer names one), under the
      measurement lock: the pacing run at 120 Hz (tests/pace/short.json through the harness, real time, as pace.py
      runs it) and the built-in benchmark (--benchmark 1: its stages and probes, hidden, on a blank save in memory).
      Their figures become one line of the history; a figure worse than the spread of the same renderer's last ten
      lines fails the run (with fewer than three earlier lines there is no spread yet). Exit 0 when every figure holds
      and both parts measured.
  measure_short.py compare --history FILE
      The newest line against the ten before it of its renderer: the same verdict, without running anything.

The figures, each with the direction that is better: the game's steps a second (nearest 60), the share of pictures
whose animation error is within 1 ms (higher), the delay's p99 and the render thread's p99 (lower), the longest step
(lower), missed and doubled refreshes (lower), the benchmark's average and 1 % low frame rates (higher). Pacing figures
come from the game's [pace] lines (tools/gate/pace.py), the benchmark's from its result (summary).
"""
import argparse
import datetime
import json
import os
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[1]
sys.path.insert(0, str(HERE))
import pace  # noqa: E402

RENDERERS = ("d3d12", "vulkan", "d3d11")
EXES = {"d3d12": "Nebula.exe", "vulkan": "NebulaVk.exe", "d3d11": "NebulaD11.exe"}
HISTORY = "D:/NebulaWork/build/gate-runs/measure-history.tsv"
SUITE = TREE / "tests" / "pace" / "short.json"
# (name, which way is better: +1 higher, -1 lower, 0 nearest the target), the target for 0.
FIGURES = (("steps_per_second", 0, 60.0), ("animation_within_1ms", 1, None), ("delay_p99_ms", -1, None),
           ("render_p99_ms", -1, None), ("step_work_max_ms", -1, None), ("missed", -1, None), ("doubled", -1, None),
           ("bench_average_fps", 1, None), ("bench_low_1_fps", 1, None))
COLUMNS = ("time", "commit", "renderer") + tuple(f[0] for f in FIGURES) + ("notes",)
SPREAD_RUNS = 10
LEAST_RUNS = 3
BENCH_LIMIT_SECONDS = 900


def read_history(path):
    path = pathlib.Path(path)
    if not path.exists():
        return []
    lines = path.read_text(encoding="utf-8").splitlines()
    rows = []
    for line in lines[1:]:
        cells = line.split("\t")
        if len(cells) != len(COLUMNS):
            continue
        row = dict(zip(COLUMNS, cells))
        for name, _, _ in FIGURES:
            row[name] = float(row[name]) if row[name] not in ("", "-") else None
        rows.append(row)
    return rows


def append_history(path, row):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    new = not path.exists()
    with open(path, "a", encoding="utf-8") as f:
        if new:
            f.write("\t".join(COLUMNS) + "\n")
        f.write("\t".join("-" if row.get(c) is None else ("%.6g" % row[c] if isinstance(row[c], float) else str(row[c]))
                          for c in COLUMNS) + "\n")


def renderer_for(rows):
    """The renderer this run measures: each in turn, by how many runs the history has."""
    return RENDERERS[len(rows) % len(RENDERERS)]


def compare(row, earlier):
    """[(figure, value, spread text, verdict)] of `row` against the last SPREAD_RUNS lines of `earlier` of its
    renderer: worse than every one of them in the better direction is "worse"; "no spread" below LEAST_RUNS."""
    same = [r for r in earlier if r["renderer"] == row["renderer"]][-SPREAD_RUNS:]
    out = []
    for name, better, target in FIGURES:
        value = row.get(name)
        values = [r[name] for r in same if r.get(name) is not None]
        if value is None:
            out.append((name, "-", "", "not measured"))
            continue
        if len(values) < LEAST_RUNS:
            out.append((name, value, "%d earlier" % len(values), "no spread"))
            continue
        lo, hi = min(values), max(values)
        if better > 0:
            worse = value < lo
        elif better < 0:
            worse = value > hi
        else:
            worse = abs(value - target) > max(abs(v - target) for v in values)
        out.append((name, value, "%.6g to %.6g over %d" % (lo, hi, len(values)), "worse" if worse else "holds"))
    return out


def pace_figures(work):
    """The pacing run's figures from its run.log ([pace] windows, as pace.py reads them); None when none."""
    logs = sorted(pathlib.Path(work).rglob("run.log"))
    figures = pace.combine(pace.windows_of(logs[0].read_text(encoding="utf-8", errors="replace"))) if logs else None
    if figures is None:
        return None
    return {"steps_per_second": figures["steps_per_second"], "animation_within_1ms": figures["animation_within_1ms"],
            "delay_p99_ms": figures["delay_p99_us"] / 1000.0, "render_p99_ms": figures["render_p99_us"] / 1000.0,
            "step_work_max_ms": figures["step_work_max_us"] / 1000.0, "missed": float(figures["missed"]),
            "doubled": float(figures["doubled"])}


def bench_figures(result_file):
    """The benchmark's figures from its result; None when it did not complete."""
    path = pathlib.Path(result_file)
    if not path.exists():
        return None
    result = json.loads(path.read_text(encoding="utf-8"))
    summary = result.get("summary") or {}
    if result.get("status") != "complete" or "average_fps" not in summary:
        return None
    return {"bench_average_fps": float(summary["average_fps"]), "bench_low_1_fps": float(summary["low_1_fps"])}


def pace_command(args, renderer, work):
    return [sys.executable, "-X", "utf8", "-I", str(TREE / "tools/lumacore/luma_test.py"), "run", str(SUITE), "--bin",
            args.bin, "--work", str(work), "--who", "measure", "--renderer", renderer]


def bench_command(args, renderer, work):
    return [str(pathlib.Path(args.bin) / EXES[renderer]), "--pak", args.pak, "--benchmark", "1", "--benchmark-out",
            str(work / "benchmark.json"), "--settings", str(work / "settings"), "--crash-dumps", str(work)]


def run_parts(args, renderer, work, runner=subprocess.run):
    """Both parts, one after the other; their figures and notes."""
    notes, row = [], {}
    env = dict(os.environ)
    if args.paks:
        env["LUMA_TEST_PAKS"] = args.paks
    runner(pace_command(args, renderer, work / "pace"), env=env, check=False)
    got = pace_figures(work / "pace")
    if got is None:
        notes.append("pacing: no measured [pace] window")
    else:
        row.update(got)
    bench = work / "bench"
    (bench / "settings").mkdir(parents=True, exist_ok=True)
    # Its own data folder (LOCALAPPDATA), a hidden window, no sound device asked for beyond the game's own.
    benv = dict(os.environ, LOCALAPPDATA=str(bench / "appdata"))
    hidden = {}
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        hidden["startupinfo"] = startup
    with open(bench / "run.log", "wb") as log:
        try:
            runner(bench_command(args, renderer, bench), env=benv, stdout=log, stderr=subprocess.STDOUT,
                   timeout=BENCH_LIMIT_SECONDS, check=False, **hidden)
        except subprocess.TimeoutExpired:
            notes.append("benchmark: no end in %d s" % BENCH_LIMIT_SECONDS)
    got = bench_figures(bench / "benchmark.json")
    if got is None:
        notes.append("benchmark: no complete result")
    else:
        row.update(got)
    return row, notes


def run(args, runner=subprocess.run, now=None):
    rows = read_history(args.history)
    renderer = args.renderer or renderer_for(rows)
    work = pathlib.Path(args.work) / renderer
    # A result left by an earlier run in this folder is never this run's figures.
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)
    if args.lock:
        pace.take_lock(args.lock, "measure_short.py %s" % work)
    try:
        figures, notes = run_parts(args, renderer, work, runner)
    finally:
        if args.lock:
            shutil.rmtree(args.lock, ignore_errors=True)
    row = {"time": (now or datetime.datetime.now()).isoformat(timespec="seconds"), "commit": args.commit[:9],
           "renderer": renderer, "notes": "; ".join(notes) or "-"}
    row.update({name: figures.get(name) for name, _, _ in FIGURES})
    verdicts = compare(row, rows)
    append_history(args.history, row)
    return report(row, verdicts, notes)


def report(row, verdicts, notes):
    print("measure/short %s on %s:" % (row["commit"], row["renderer"]))
    for name, value, spread, verdict in verdicts:
        print("  %-22s %-12s %-28s %s" % (name, value if isinstance(value, str) else "%.6g" % value, spread, verdict))
    for n in notes:
        print("  " + n)
    worse = [v[0] for v in verdicts if v[3] == "worse"]
    summary = "%s: %s" % (row["renderer"], ("worse than the last runs: " + ", ".join(worse)) if worse else
                          ("parts missing: " + "; ".join(notes)) if notes else "every figure holds")
    print("measure: " + summary)
    return 1 if worse or notes else 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run")
    r.add_argument("--bin", required=True)
    r.add_argument("--pak", required=True)
    r.add_argument("--paks", default=os.environ.get("LUMA_TEST_PAKS", ""))
    r.add_argument("--work", required=True)
    r.add_argument("--commit", required=True)
    r.add_argument("--renderer", choices=RENDERERS)
    r.add_argument("--history", default=HISTORY)
    r.add_argument("--lock", default="D:/NebulaWork/plan/measure.lock")
    c = sub.add_parser("compare")
    c.add_argument("--history", default=HISTORY)
    args = p.parse_args(argv)
    if args.command == "run":
        return run(args)
    rows = read_history(args.history)
    if not rows:
        print("measure: no history at %s" % args.history)
        return 1
    return report(rows[-1], compare(rows[-1], rows[:-1]), [])


if __name__ == "__main__":
    sys.exit(main())
