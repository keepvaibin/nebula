"""replays_rates.py: the same play at a world rate of 2 and at a rate of 1 gives the same step hashes (the check `replays/rates`).

The game's steps do not depend on how many pictures are drawn. At a world rate of 2 the game runs two world steps for each frame it draws
(`GameSystem::frameLoop`: the extra step is the controls, the scene's update and the animation, without `GameSystem::update`), so anything
that a draw does to the game's state (a random number, a decision made in `draw()`) shows as a step hash that is not the one of a run at
a rate of 1. This check plays each stop of tests/replays/rate-stops.txt (stops of the tour list) twice with the runner's test key `rate`
(1000 and 2000: tools/lumacore/luma_test.py), and compares the hash of every world step from 0 to LAST_STEP of the two plays, step by step.

A play is the tour's stop with its input given in world steps, moved to the steps where a frame begins at the rate of 2 (world steps 0, 1,
3, 5 ...: the controls are read once a frame), so both plays get the same input at the same world step. Its pictures are left out, and it
ends at the frame that holds the hash of step LAST_STEP (a frame that runs one step, because the game holds the scene, makes a play at a
rate of 2 longer in frames, not different in steps). The harness's game half writes the step hash after every world step once the plan has
set a rate (tools/test_driver/luma_test/luma_test.c, the `rate` line), numbered from the first; step_hash.py reads them by that number.

The stops of the file are the ones whose steps do not depend on the draw count today; a stop is added to the file when the game no longer
depends on it (a line is never taken out: tools/gate/strictness.tsv). A stop of the tour that is not in the file can be played by hand with
`--stops`.

A stop is
  pass  both plays reached their end and every world step from 0 to LAST_STEP has the same hash in both;
  fail  a hash differs (the first step that differs is named), or a play has no hash for a step, or a play failed (the runner's own kind:
        crashed, hung ...);
  tool  the runner broke, or a play left no result;
  env   the PC was not fit.

    python -X utf8 -I tools/gate/replays_rates.py run --bin BIN --work WORK [--pak PAK] [--renderer d3d12] [--half DLL]
                                                      [--stops "Stage N" ...] [--who NAME] [--commit SHA]
    python -X utf8 -I tools/gate/replays_rates.py write OUT [--stops "Stage N" ...]    the suite only

`run` ends with one `KIND <kind> <check> <test> :: <message>` line for each stop that is not a pass and then `VERDICT pass`, `VERDICT fail`
or `VERDICT not-judged` (exit 0, 1, 2; tools/gate/kinds.py), and writes result.json, verdict.json and result.txt in WORK.
"""
import argparse
import copy
import datetime
import json
import pathlib
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import gate_tour  # noqa: E402
import kinds  # noqa: E402
import step_hash  # noqa: E402

CHECK = "replays/rates"
SUITE = "rates"
RENDERERS = ("d3d12", "vulkan", "d3d11")
RUNNER = TREE / "tools" / "lumacore" / "luma_test.py"
GIT = "C:/Program Files/Git/cmd/git.exe"
# The two rates a stop is played at, thousandths of real time: the plan's reference and the rate under test.
RATES = (1000, 2000)
# Frames the end step waits for the hash of the last step (a play that holds the scene a long time is not cut short).
END_TIMEOUT = 1500
ENV_EXIT = 3  # luma_test.py ENV_EXIT: the runner could not start because the machine was not fit
# The last world step whose hash is compared: both plays end at the frame that holds it or the first after it, so the steps after it
# are not (a play's frames that run one step, because the game holds the scene or a scene change is asked, make it more frames at a rate of 2).
LAST_STEP = 600
# The stops the check requires: tests/replays/rate-stops.txt, one "Stage N" a line (a stop of the tour list), "#" lines are notes.
STOPS_FILE = TREE / "tests" / "replays" / "rate-stops.txt"


def rate_stops(path=STOPS_FILE):
    """The stops the check requires, in the file's order. The file is the stops whose steps do not depend on the draw count (a stop is
    added when its steps no longer do); a stop that is not in the tour list is refused by hashed_tour."""
    lines = (l.split("#", 1)[0].strip() for l in pathlib.Path(path).read_text(encoding="utf-8").splitlines())
    return [l for l in lines if l]


def suffix(rate):
    return "x%d" % (rate // 1000)


def on_frame_edge(step, rate):
    """The nearest world step at or after `step` that begins a frame at `rate` (a whole number of steps a frame)."""
    k = rate // 1000
    return step if step == 0 else step + (-(step - 1)) % k


def world_steps(test, rate):
    """The tour test's steps as a plan in world steps that both rates can play (luma_test.py: `rate`): the input where it begins, moved
    to the next step that begins a frame at `rate`, its length to a whole number of frames; no pictures; the hash on from step 0; the end
    when the hash of step LAST_STEP is written (the fact hash.steps counts them)."""
    k = rate // 1000
    out = []
    for s in test["steps"]:
        if "picture" in s or "end" in s:
            continue
        s = dict(s)
        s["at"] = on_frame_edge(s["at"], rate)
        for key in ("frames", "shake"):
            if key in s:
                s[key] += (-s[key]) % k
        out.append(s)
    out.append({"at": 0, "hash": "on"})
    out.sort(key=lambda s: s["at"])
    out.append({"after": {"fact": "hash.steps", "at_least": LAST_STEP + 1}, "timeout": END_TIMEOUT, "end": True})
    return out


def rate_test(test, rate, steps):
    """One play of a stop: the tour test with `steps` and the world rate `rate`, named for it."""
    name = "%s-%s" % (test["name"], suffix(rate))
    out = copy.deepcopy(test)
    out.update({"name": name, "about": "%s, at a world rate of %d" % (test["about"], rate), "steps": steps, "rate": rate})
    return name, out


def hashed_tour(out, stops=None):
    """The stops of the file STOPS_FILE (or `stops`) written as a suite under `out`: each at every rate of RATES, in the same plan in world
    steps. Returns suite.json's path; the plays of a stop are named `<stop's test>-x1`, `-x2`."""
    out = pathlib.Path(out)
    want = {tuple(s.split()) for s in (stops or rate_stops())}
    found = [(i, stage, scenario) for i, stage, scenario in gate_tour.stops() if (stage, str(scenario)) in want]
    if len(found) != len(want):
        raise ValueError("not in the tour list: %s" % sorted(" ".join(w) for w in want - {(s, str(n)) for _, s, n in found}))
    names = []
    for index, stage, scenario in found:
        test = gate_tour.test_for(index, stage, scenario)[1]
        steps = world_steps(test, max(RATES))
        for rate in RATES:
            name, played = rate_test(test, rate, steps)
            (out / name).mkdir(parents=True, exist_ok=True)
            (out / name / "test.json").write_text(json.dumps(played, indent=2) + "\n", encoding="utf-8")
            names.append(name)
    suite = out / "suite.json"
    suite.write_text(json.dumps({"harness": 1, "tests": names}, indent=2) + "\n", encoding="utf-8")
    return suite


def stop_names(names):
    """The stops of a suite's play names, in order: `<test>-x1` and `<test>-x2` are the plays of `<test>`."""
    out = []
    for n in names:
        stop = n.rsplit("-", 1)[0]
        if stop not in out:
            out.append(stop)
    return out


# --- Judging ----------------------------------------------------------------------------------------------------------------------


def run_folder(work, name, renderer):
    return pathlib.Path(work) / "runs" / SUITE / name / renderer


def same_through(reference, tested, last):
    """(same, text): the hash of every step from 0 to `last` is in both {step: (hash, actors)} and the same."""
    for step in range(last + 1):
        for rate, steps in zip(RATES, (reference, tested)):
            if step not in steps:
                return False, "the play at a rate of %d wrote no hash for step %d" % (rate, step)
        if reference[step] != tested[step]:
            return False, "differs from step %d (hash %s, %d actors / %s, %d actors)" % ((step,) + reference[step] + tested[step])
    return True, "the same over %d steps" % (last + 1)


def judge_stop(stop, results, work, renderer, last=LAST_STEP):
    """(kind, message) of one stop from its plays' results {play: (kind, message)} and their hashes."""
    reference, tested = ["%s-%s" % (stop, suffix(r)) for r in RATES]
    bad = [(r, results[n]) for r, n in zip(RATES, (reference, tested)) if results[n][0] != "pass"]
    if bad:
        return kinds.worst([b[1][0] for b in bad]), "; ".join("at a rate of %d: %s" % (r, b[1] or b[0]) for r, b in bad)
    steps = [step_hash.plain_steps(step_hash.read_facts(run_folder(work, n, renderer))[0]) for n in (reference, tested)]
    same, text = same_through(steps[0], steps[1], last)
    if same:
        return "pass", ""
    return "fail", "the step hashes at a rate of %d differ from those at a rate of %d: %s" % (RATES[1], RATES[0], text)


def judge(work, names, renderer, last=LAST_STEP):
    """({stop: (kind, message)}, problem): the verdict of every stop from the runner's results of its plays. `problem` is a count of
    plays that is not the declared one (tool)."""
    plays, _, problem = kinds.read_cases(work, SUITE, names, renderer, lambda verdict: "tool")
    return {stop: judge_stop(stop, plays, work, renderer, last) for stop in stop_names(names)}, problem


def rows_of(results, problem, whole=None):
    """[row]: the check's one row, its `records` the stops that are not a pass (a stop is a test of the check). `whole`: (kind, message)
    when no stop could be played at all."""
    if whole is not None:
        records = [kinds.record("-", whole[0], whole[1])]
        return [{"check": CHECK, "state": kinds.state_of(records), "detail": whole[1], "records": records}]
    cases = kinds.Cases(results, declared=len(results))
    cases.problem = problem
    records, state = kinds.judge_cases(cases)
    return [{"check": CHECK, "state": state, "detail": kinds.describe(cases, records), "records": records}]


def write_run(work, rows, notes, started, commit="", plays=()):
    """result.json, verdict.json and result.txt in the run's folder; returns (verdict, the summary lines)."""
    verdict = kinds.verdict_of([r["records"] for r in rows])
    lines = kinds.kind_lines(rows)
    seconds = round(time.time() - started, 1)
    now = datetime.datetime.fromtimestamp(started).isoformat(timespec="seconds")
    body = {"check": CHECK, "verdict": verdict, "passed": verdict == "pass", "started": now, "seconds": seconds, "commit": commit,
            "notes": notes, "checks": rows, "plays": list(plays)}
    work = pathlib.Path(work)
    work.mkdir(parents=True, exist_ok=True)
    (work / "result.json").write_text(json.dumps(body, indent=2) + "\n", encoding="utf-8")
    flat = [dict(check=r["check"], **rec) for r in rows for rec in r["records"]]
    (work / "verdict.json").write_text(json.dumps({"verdict": verdict, "exit": kinds.EXIT_OF[verdict], "lines": lines, "results": flat,
                                                   "notes": notes, "checks": [{"check": r["check"], "state": r["state"], "detail": r["detail"]}
                                                                             for r in rows]}, indent=2) + "\n", encoding="utf-8")
    head = ["%s: %s (%.0f s)" % (CHECK, verdict.upper(), seconds)] + ["  %-20s %-7s %s" % (r["check"], r["state"], r["detail"]) for r in rows]
    (work / "result.txt").write_text("\n".join(head[:1] + ["  " + n for n in notes] + head[1:] + lines + [kinds.verdict_line(verdict)]) + "\n",
                                    encoding="utf-8")
    return verdict, head + lines


def cases_of(work):
    """(results, detail) of a finished run, for the gate's check: {stop's test: (kind, message)} and the run's detail line; None when the
    run left no result.json."""
    try:
        body = json.loads((pathlib.Path(work) / "result.json").read_text(encoding="utf-8"))
        plays = body["plays"]
        rows = body["checks"]
    except (OSError, ValueError, KeyError, TypeError):
        return None
    results = {p["test"]: (p["kind"], p["message"]) for p in plays}
    for row in rows:
        whole = [r for r in row["records"] if r["test"] == "-"]
        if whole:
            results["-"] = (whole[0]["kind"], whole[0]["message"])
    return results, "; ".join(r["detail"] for r in rows)


# --- run --------------------------------------------------------------------------------------------------------------------------


def command(suite, binary, pak, work, who, renderer, commit, half=None):
    """The runner's command line for the whole suite (the gate's harness source builds it so: gate.py HarnessSource.command)."""
    out = [sys.executable, "-X", "utf8", "-I", str(RUNNER), "run", str(suite), "--bin", str(binary), "--work", str(work), "--who", who,
           "--lock", str(pathlib.Path(work) / "run.lock"), "--renderer", renderer]
    if pak:
        out += ["--pak", str(pak)]
    if commit:
        out += ["--commit", commit]
    if half:
        out += ["--half", str(half)]
    return out


def head_commit(tree=TREE):
    r = subprocess.run([GIT, "-C", str(tree), "rev-parse", "--short=9", "HEAD"], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 and r.stdout.strip() else ""


def run_all(a, say=print, play=subprocess.run):
    started = time.time()
    work = pathlib.Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    suite = hashed_tour(work / SUITE, a.stops)
    names = json.loads(suite.read_text(encoding="utf-8"))["tests"]
    say("replays_rates.py: %d stops at the rates %s on %s, run in %s" % (len(stop_names(names)), ", ".join(map(str, RATES)), a.renderer, work))
    done = play(command(suite, a.bin, a.pak, work, a.who, a.renderer, a.commit or head_commit(), a.half))
    results, problem = judge(work, names, a.renderer)
    whole = None
    if done.returncode == ENV_EXIT and not any(kinds.result_path(work, SUITE, n, a.renderer).is_file() for n in names):
        whole = ("env", "the runner could not start: the machine was not fit (exit %d)" % ENV_EXIT)
    rows = rows_of(results, problem, whole)
    shown = [{"test": s, "kind": k, "message": m} for s, (k, m) in results.items()]
    verdict, lines = write_run(work, rows, [], started, a.commit, shown)
    for line in lines:
        say(line)
    say(kinds.verdict_line(verdict))
    return kinds.EXIT_OF[verdict]


def add_stops(s):
    s.add_argument("--stops", nargs="*", default=[], help='only these recorded stops, "Stage N" each (default: all of them)')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run")
    r.add_argument("--bin", required=True)
    r.add_argument("--work", required=True)
    r.add_argument("--pak", default=None, help="the game.pak (default: the bin folder's pak.path, or LUMA_TEST_PAKS)")
    r.add_argument("--renderer", default="d3d12", choices=RENDERERS)
    r.add_argument("--half", default=None, help="the game half, a built luma_test.c (default: the bin folder's luma-test library)")
    r.add_argument("--who", default="replays-rates")
    r.add_argument("--commit", default="")
    add_stops(r)
    w = sub.add_parser("write")
    w.add_argument("out")
    add_stops(w)
    a = p.parse_args(argv)
    if a.command == "write":
        print(hashed_tour(a.out, a.stops))
        return 0
    return run_all(a)


if __name__ == "__main__":
    sys.exit(main())
