"""replays_rates_test.py: the check `replays/rates` (tools/gate/replays_rates.py: each recorded stop at a world rate of 1 and of 2, the
step hashes compared at equal world steps), without a game, a build or a graphics card.

  the_plans         the stops of tests/replays/rate-stops.txt: two plays of each read through the runner; the same plan in world steps for
                    both, with no pictures, the hash on from step 0 and the end after the hash of step 600; every input starts and ends where
                    a frame does at a rate of 2; the two plays give every world step the same input
  the_facts         step_hash.py reads a run's hash.step facts by the `step` they name (a frame that ran two steps wrote two), by the
                    frame when they name none
  judging           a stop passes when its two plays pass with the same hash at every step to the last; a hash that differs is a fail that
                    names the first step; a step a play has no hash for, a play with no hashes, a play that failed (the runner's kind), no
                    result at all; steps after the last are not compared
  the_whole_run     fakes for the runner: all stops pass; the 2nd play of one stop differs (VERDICT fail, its KIND line); a runner that
                    could not start (env: not-judged); the files of the run folder
  the_gate_part     gate.py's check `replays/rates` (--also by name): the script's command, its result.json read into the check's tests
  the_names         the step's extra check is in the step tool's table and in step-checks.tsv
"""
import contextlib
import io
import json
import pathlib
import sys
import tempfile
import types

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(TREE / "tools" / "verify"))
sys.path.insert(0, str(TREE / "tools" / "lumacore"))
sys.path.insert(0, str(TREE / "tools" / "lumacore" / "tests"))
sys.path.insert(0, str(TREE / "tools" / "plan"))
import harness_rate_test as rate_test  # noqa: E402
import kinds  # noqa: E402
import luma_test  # noqa: E402
import replays_rates as rr  # noqa: E402
import step_hash  # noqa: E402

TWO_STOPS = ["EggStarGalaxy 1", "HoneyBeeKingdomGalaxy 1"]
STEPS = 12
NL, TAB = chr(10), chr(9)


def the_plans(check):
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        stops = rr.rate_stops()
        check(len(stops) == len(set(stops)) and "EggStarGalaxy 1" in stops, "the file lists each stop once, EggStarGalaxy 1 among them: %s" % stops)
        tour = {"%s %s" % (stage, scenario) for _, stage, scenario in rr.gate_tour.stops()}
        check(all(s in tour and s in step_hash.RECORDED_STOPS for s in stops), "every stop of the file is a recorded stop of the tour: %s" % [
            s for s in stops if s not in tour or s not in step_hash.RECORDED_STOPS])
        suite = rr.hashed_tour(t / "all")
        names = json.loads(suite.read_text(encoding="utf-8"))["tests"]
        check(len(names) == 2 * len(stops) and rr.stop_names(names) == [n[:-3] for n in names[::2]],
              "two plays for each of the %d stops of the file: %d" % (len(stops), len(names)))
        (t / "stops.txt").write_text("# a note" + NL + "EggStarGalaxy 1   # after a stop" + NL + NL + "  OceanRingGalaxy 1" + NL, encoding="utf-8")
        check(rr.rate_stops(t / "stops.txt") == ["EggStarGalaxy 1", "OceanRingGalaxy 1"], "the file: notes and blank lines are not stops")
        guard = (TREE / "tools" / "gate" / "strictness.tsv").read_text(encoding="utf-8")
        check("tests/replays/rate-stops.txt" + TAB + "removed" + TAB + "*" in guard, "a line taken out of the file is on the strictness list")
        some = rr.hashed_tour(t / "some", ["EggStarGalaxy 1"])
        check(json.loads(some.read_text(encoding="utf-8"))["tests"] == ["022-EggStarGalaxy-1-x1", "022-EggStarGalaxy-1-x2"], "--stops names the stops played")
        check([n[-3:] for n in names[:4]] == ["-x1", "-x2", "-x1", "-x2"], "the plays of a stop are -x1 and -x2: %s" % names[:4])
        for stop in rr.stop_names(names):
            plays = [luma_test.read_test(t / "all" / (stop + "-" + s)) for s in ("x1", "x2")]
            check([p["rate"] for p in plays] == [1000, 2000], "%s: the plays' rates are 1000 and 2000" % stop)
            raws = [json.loads((t / "all" / (stop + "-" + s) / "test.json").read_text(encoding="utf-8")) for s in ("x1", "x2")]
            check(raws[0]["steps"] == raws[1]["steps"], "%s: the same plan in world steps for both" % stop)
            steps = raws[0]["steps"]
            ends = [s for s in steps if "end" in s]
            check(not any("picture" in s for s in steps) and {"at": 0, "hash": "on"} in steps and rr.LAST_STEP == 600 and
                  ends == [{"after": {"fact": "hash.steps", "at_least": 601}, "timeout": rr.END_TIMEOUT, "end": True}] and steps[-1] == ends[0],
                  "%s: no picture, the hash on, the end after the hash of step 600, last: %s" % (stop, ends))
            check(all(s["at"] % 2 == 1 for s in steps if s.get("at") and "end" not in s), "%s: every input begins at a frame's first step" % stop)
            texts = [luma_test.plan_text(p, t / "out") for p in plays]
            seen = [rate_test.input_at(text, 200, rate) for text, rate in zip(texts, (1000, 2000))]
            check(seen[0] == seen[1], "%s: the same input at every world step: %s" % (stop, next(
                (s for s in range(200) if seen[0][s] != seen[1][s]), None)))
            check(any(seen[0][s] for s in range(200)), "%s: the first 200 steps hold input" % stop)
        check(rr.on_frame_edge(0, 2000) == 0 and rr.on_frame_edge(40, 2000) == 41 and rr.on_frame_edge(41, 2000) == 41 and
              rr.on_frame_edge(2, 4000) == 5 and rr.on_frame_edge(5, 4000) == 5 and rr.on_frame_edge(7, 1000) == 7,
              "the next world step that begins a frame")
        try:
            rr.hashed_tour(t / "none", ["NoSuchGalaxy 1"])
            check(False, "a stop that is not in the tour is refused")
        except ValueError as error:
            check("NoSuchGalaxy 1" in str(error), "the refusal names the stop: %s" % error)


def write_facts(folder, hashes, by_step=True, extra=None):
    """A run's facts: the hash after each step; by_step names the step (a run with a rate), else the frame has it."""
    folder.mkdir(parents=True, exist_ok=True)
    lines = [{"frame": -1, "key": "setup.done", "value": True}]
    for step, h in enumerate(hashes):
        value = {"h": h, "actors": 5}
        if by_step:
            value["step"] = step
        lines.append({"frame": (step + 1) // 2 if by_step else step, "key": "hash.step", "value": value})
    lines += extra or []
    (folder / "facts.jsonl").write_text("\n".join(json.dumps(l) for l in lines) + "\n", encoding="utf-8")


def write_result(work, test, kind="pass", reason="", hashes=None, seconds=3.0):
    folder = pathlib.Path(work) / "runs" / rr.SUITE / test / "d3d12"
    folder.mkdir(parents=True, exist_ok=True)
    verdict = "pass" if kind == "pass" else ("fail" if kind == "fail" else "harness-error")
    (folder / "result.json").write_text(json.dumps({"test": test, "verdict": verdict, "kind": kind, "reason": reason, "seconds": seconds}),
                                        encoding="utf-8")
    if hashes is not None:
        write_facts(folder, hashes)


def the_facts(check):
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        write_facts(t / "rate", ["a", "b", "c", "d", "e"])
        steps = step_hash.read_facts(t / "rate")[0]
        check(sorted(steps) == [0, 1, 2, 3, 4] and steps[3] == ("d", 5), "a hash.step names its step: two steps of one frame are two: %s" % steps)
        write_facts(t / "plain", ["a", "b", "c"], by_step=False)
        steps = step_hash.read_facts(t / "plain")[0]
        check(sorted(steps) == [0, 1, 2] and steps[2] == ("c", 5), "a hash.step with no step is at its frame: %s" % steps)
        same, text = step_hash.compare_runs(t / "rate", t / "rate")
        check(same and "5 steps" in text, "compare_runs reads the runs by step: %s" % text)


def judging(check):
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        good = ["h%d" % i for i in range(STEPS)]
        names = []
        cases = {
            "A-1": (("pass", "", good), ("pass", "", good)),
            "B-1": (("pass", "", good), ("pass", "", good[:5] + ["X"] + good[6:])),
            "C-1": (("pass", "", good), ("pass", "", good[:-3])),
            "D-1": (("pass", "", good), ("pass", "", None)),
            "E-1": (("pass", "", good), ("fail", "crashed: exception 0xC0000005", good[:3])),
            "F-1": (("tool", "no result.json: the runner did not report this test", None), ("fail", "hung: no step", good[:3])),
            "H-1": (("pass", "", good + ["t1"]), ("pass", "", good + ["u1", "u2"])),
        }
        for stop, (one, two) in cases.items():
            for suffix, (kind, reason, hashes) in zip(("x1", "x2"), (one, two)):
                names.append("%s-%s" % (stop, suffix))
                if kind != "tool":
                    write_result(work, names[-1], kind, reason, hashes)
        results, problem = rr.judge(work, names, "d3d12", STEPS - 1)
        check(results["H-1"] == ("pass", ""), "steps after the last are not compared: %s" % (results["H-1"],))
        check(problem == "ran 13 cases, 14 declared", "a play that left no result is counted: %s" % problem)
        check(results["A-1"] == ("pass", ""), "the same hashes at every step: pass: %s" % (results["A-1"],))
        kind, message = results["B-1"]
        check(kind == "fail" and message == "the step hashes at a rate of 2000 differ from those at a rate of 1000: differs from step 5 "
              "(hash h5, 5 actors / X, 5 actors)", "a hash that differs: fail, the first step named: %s" % (results["B-1"],))
        kind, message = results["C-1"]
        check(kind == "fail" and message.endswith("the play at a rate of 2000 wrote no hash for step 9"), "a step one play has no hash for: fail, named: %s" % (results["C-1"],))
        kind, message = results["D-1"]
        check(kind == "fail" and message.endswith("the play at a rate of 2000 wrote no hash for step 0"), "a play that wrote no hash: fail: %s" % (results["D-1"],))
        kind, message = results["E-1"]
        check(kind == "fail" and message == "at a rate of 2000: crashed: exception 0xC0000005", "a failed play keeps the runner's kind and words: %s" % (results["E-1"],))
        kind, message = results["F-1"]
        check(kind == "fail" and "at a rate of 1000: no result.json" in message and "at a rate of 2000: hung" in message,
              "both plays named, the worse kind (a fail is worse than a tool fault): %s" % (results["F-1"],))
        results, problem = rr.judge(work, names + ["G-1-x1", "G-1-x2"], "d3d12", STEPS - 1)
        check(problem is not None and results["G-1"][0] == "tool", "a stop with no result at all is tool: %s %s" % (problem, results.get("G-1")))


def fake_runner(plant):
    """A `play` for run_all: writes what the runner would, and returns the exit code."""
    def play(command):
        work = pathlib.Path(command[command.index("--work") + 1])
        names = json.loads(pathlib.Path(command[command.index("run") + 1]).read_text(encoding="utf-8"))["tests"]
        plant["command"] = list(command)
        if plant.get("code") == 3:
            return types.SimpleNamespace(returncode=3)
        good = ["h%d" % i for i in range(rr.LAST_STEP + 2)]
        for name in names:
            hashes = list(good)
            if name in plant.get("differ", ()):
                hashes[7] = "Z"
            write_result(work, name, "pass", "", hashes)
        return types.SimpleNamespace(returncode=plant.get("code", 0))
    return play


def the_whole_run(check):
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)

        def args(name, **more):
            return types.SimpleNamespace(**dict(dict(bin=str(t / "bin"), work=str(t / name), pak=None, renderer="d3d12", who="gate", commit="abc123",
                                                     half=None, stops=TWO_STOPS), **more))
        said = []
        plant = {}
        code = rr.run_all(args("pass"), said.append, fake_runner(plant))
        check(code == 0 and said[-1] == "VERDICT pass" and not any(l.startswith("KIND") for l in said), "all pass: exit 0 and no KIND line: %s" % said)
        command = plant["command"]
        check(command[command.index("--renderer") + 1] == "d3d12" and command[command.index("--commit") + 1] == "abc123"
              and "--half" not in command and command[command.index("--who") + 1] == "gate", "the runner's command: %s" % command)
        body = json.loads((t / "pass" / "result.json").read_text(encoding="utf-8"))
        check(body["check"] == "replays/rates" and body["verdict"] == "pass" and len(body["plays"]) == 2 and
              (t / "pass" / "verdict.json").is_file() and (t / "pass" / "result.txt").is_file(), "result.json, verdict.json and result.txt: %s" % sorted(body))
        found = rr.cases_of(t / "pass")
        check(found is not None and sorted(found[0]) == ["022-EggStarGalaxy-1", "048-HoneyBeeKingdomGalaxy-1"] and found[1] == "2 of 2 pass",
              "cases_of: a test for each stop: %s" % (found,))

        said.clear()
        plant.update(differ=["048-HoneyBeeKingdomGalaxy-1-x2"])
        code = rr.run_all(args("diff"), said.append, fake_runner(plant))
        kind_lines = [l for l in said if l.startswith("KIND")]
        check(code == 1 and said[-1] == "VERDICT fail" and len(kind_lines) == 1 and kind_lines[0].startswith(
            "KIND fail replays/rates 048-HoneyBeeKingdomGalaxy-1 :: the step hashes at a rate of 2000 differ from those at a rate of 1000: "
            "differs from step 7"), "one stop differs: exit 1, one KIND line naming the stop and the step: %s" % said)

        said.clear()
        plant.clear()
        plant["code"] = 3
        code = rr.run_all(args("env"), said.append, fake_runner(plant))
        check(code == 2 and said[-1] == "VERDICT not-judged" and any(l.startswith("KIND env replays/rates -") for l in said),
              "a runner that could not start: env, not judged: %s" % said)

        with contextlib.redirect_stdout(io.StringIO()) as out:
            check(rr.main(["write", str(t / "written"), "--stops", "EggStarGalaxy 1"]) == 0, "write: exit 0")
        check(out.getvalue().strip().endswith("suite.json") and (t / "written" / "022-EggStarGalaxy-1-x2" / "test.json").is_file(),
              "write: the suite only, no game")


def the_gate_part(check):
    import gate
    names = ["022-EggStarGalaxy-1", "048-HoneyBeeKingdomGalaxy-1"]
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        g = types.SimpleNamespace(work=t, bin=str(t / "bin"), commit="abc123", paks_text=lambda: "RMGE01=X.pak",
                                  args=types.SimpleNamespace(build_root=str(t)))
        seen, plant = [], {}

        def fake_run(command, log, env=None, cwd=None, timeout=None):
            seen.append((list(command), dict(env or {}), cwd))
            pathlib.Path(log).write_text("the script said this", encoding="utf-8")
            if plant.get("results") is not None:
                out = pathlib.Path(command[command.index("--work") + 1])
                rows = rr.rows_of(plant["results"], None)
                rr.write_run(out, rows, [], 0.0, "abc123", [{"test": k, "kind": v[0], "message": v[1]} for k, v in plant["results"].items()])
            return plant["code"]

        real = gate.run
        gate.run = fake_run
        try:
            check("replays/rates" in gate.ALSO_PARTS and gate.ALSO_PARTS["replays/rates"].checks == ("replays/rates",)
                  and gate.ALSO_PARTS["replays/rates"].run is not None, "the part is in ALSO_PARTS and built")
            good = {n: ("pass", "") for n in names}
            plant.update(results=good, code=0)
            passed, detail, cases = gate.replays_rates_check(g)(t / "replays-rates.log")
            check(passed and isinstance(cases, kinds.Cases) and set(cases.results) == set(names), "a run that passed: a test for each stop: %s" % (cases,))
            command, env, cwd = seen[-1]
            check(command[command.index("--bin") + 1] == g.bin and str(command[command.index("run") - 1]).endswith("replays_rates.py")
                  and env.get("LUMA_TEST_PAKS") == "RMGE01=X.pak" and command[command.index("--commit") + 1] == "abc123" and cwd == str(t),
                  "the script's command: the gate's bin, the paks in the environment: %s %s" % (command, env))
            bad = dict(good)
            bad[names[1]] = ("fail", "the step hashes at a rate of 2000 differ from those at a rate of 1000: differs from step 217")
            plant.update(results=bad, code=1)
            passed, detail, cases = gate.replays_rates_check(g)(t / "replays-rates.log")
            records, state = kinds.judge_cases(cases)
            check(not passed and state == "fail" and len(records) == 1 and records[0]["test"] == names[1] and "differs from step 217" in records[0]["message"],
                  "a stop that differs is a fail test of the check: %s %s" % (state, records))
            plant.update(results=None, code=1)
            out = gate.replays_rates_check(g)(t / "replays-rates.log")
            check(out[0] is False and out[3] == "tool" and out[1].startswith("no result.json"), "a script that left no result.json is tool: %s" % (out,))
        finally:
            gate.run = real


def the_names(check):
    import plan
    check("replays/rates" in plan.ALSO_CHECKS, "the step tool takes replays/rates as an extra check of a step")
    check(plan.also_for_check("replays/rates") == ["replays/rates"], "a failed replays/rates in a repair asks for it again: %s" % plan.also_for_check("replays/rates"))
    check(plan.also_for_check("replays/software") == ["replays/software"], "replays/software is as it was")
    table = plan.read_step_checks(TREE)
    check(any(c == "replays/rates" for c, _ in table.get("S0.2.1", [])), "step S0.2.1's proof 2 asks for it (step-checks.tsv): %s" % table.get("S0.2.1"))


CASES = [the_plans, the_facts, judging, the_whole_run, the_gate_part, the_names]


def main():
    failures = []
    for case in CASES:
        found = []
        try:
            case(lambda ok, what, found=found: ok or found.append(what))
        except Exception as error:  # noqa: BLE001 - a case that raised is a failed case, said with its name
            import traceback
            found.append("raised %s: %s\n%s" % (type(error).__name__, error, traceback.format_exc()))
        print("%-26s %s" % (case.__name__, "pass" if not found else "FAIL"))
        for what in found:
            print("    " + what)
            failures.append("%s: %s" % (case.__name__, what))
    print("replays_rates_test.py: %s (%d cases)" % ("pass" if not failures else "FAILED", len(CASES)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
