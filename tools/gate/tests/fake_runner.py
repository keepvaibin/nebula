"""fake_runner.py: stands in for tools/lumacore/luma_test.py in the gate's own tests (result_kinds_test.py). It starts no
game: it writes the result.json the real runner would, from a scenario.

    python -I fake_runner.py run <suite.json | folder> --bin B --work W --who W --lock L --commit C --renderer R
                             [--test T ...] [--device S]

The scenario is the JSON file FAKE_RUNNER_SCENARIO names: {"tests": {name: {"first": o, "again": o, "main": o}},
"extra": [names that leave a result though the suite does not list them], "exit": code, "touch": name}. `o` is what the
run of that test leaves, by the phase it is in: the first run (any other work folder), the run again alone (a work folder under rerun) or the run on main's kept build (bin folder under base-bins):
  "pass"            a pass
  "fail:MESSAGE"    a fail with that reason (verdict fail)
  "tool"            verdict harness-error (kind tool); "env": verdict not-run (kind env)
  "slow:PCT"        verdict too-slow (kind env): the time limit passed with the game at PCT% of full speed
  "retried:VERDICT" a pass whose result names a first play that ended VERDICT (the real runner's "retried": it played the test
                    once more at once after a fault of the tool's)
  "none"            no result at all, as if the runner had raised before it
  "garbage"         a result.json that is not JSON;  "no-verdict": one that names no verdict
A test the scenario does not name passes. "exit" overrides the exit code (the real one: 0 when all passed, else 1);
"refuse": the runner ends with exit 3 and no result (the machine was not fit). Each run also leaves phase.txt in the
test's run folder, so a test can see which run its folder holds, and env.txt (the LUMA_TEST_GPU_VALIDATION and
LUMA_TEST_LIMIT_SCALE the run was started with). "logs": {name: {phase: text}} (phase "all": every run) is the text of the
test's run.log, which a lane reads for the graphics layer's messages.
"""
import json
import os
import pathlib
import sys


def main():
    argv = sys.argv[1:]
    assert argv[0] == "run"
    target = pathlib.Path(argv[1])
    opts = {}
    only = []
    i = 2
    while i < len(argv):
        if argv[i] == "--test":
            only.append(argv[i + 1])
        else:
            opts[argv[i]] = argv[i + 1]
        i += 2
    scenario = json.loads(pathlib.Path(os.environ["FAKE_RUNNER_SCENARIO"]).read_text(encoding="utf-8"))
    if scenario.get("refuse"):
        print("luma_test.py: env: the device does not answer adb", file=sys.stderr)
        return 3
    if target.name == "suite.json":
        names = json.loads(target.read_text(encoding="utf-8"))["tests"]
        if only:
            names = [n for n in names if n in only]
        suite = target.parent.name
    else:
        names, suite = [target.name], target.name
    work = pathlib.Path(opts["--work"])
    renderer = opts["--renderer"]
    phase = "main" if "base-bins" in opts["--bin"].replace("\\", "/") else ("again" if "rerun" in work.parts else "first")
    bad = 0
    for name in list(names) + list(scenario.get("extra", [])):
        outcome = scenario.get("tests", {}).get(name, {}).get(phase, "pass")
        run = work / "runs" / suite / name / renderer
        run.mkdir(parents=True, exist_ok=True)
        (run / "phase.txt").write_text(phase, encoding="utf-8")
        (run / "env.txt").write_text("validation=%s scale=%s" % (os.environ.get("LUMA_TEST_GPU_VALIDATION", ""),
                                                                 os.environ.get("LUMA_TEST_LIMIT_SCALE", "")), encoding="utf-8")
        logs = scenario.get("logs", {}).get(name, {})
        if logs.get(phase, logs.get("all")) is not None:
            (run / "run.log").write_text(logs.get(phase, logs.get("all")), encoding="utf-8")
        verdict, kind, reason = "pass", "pass", ""
        if outcome.startswith("fail"):
            verdict, kind, reason = "fail", "fail", outcome.partition(":")[2]
        elif outcome == "tool":
            verdict, kind, reason = "harness-error", "tool", "the test could not be read"
        elif outcome == "env":
            verdict, kind, reason = "not-run", "env", "needs disc RMGP01"
        elif outcome.startswith("slow"):
            verdict, kind = "too-slow", "env"
            reason = ("too slow on this machine now: still moving when its time limit of 240 s passed "
                      "(the game ran at %s%% of full speed)" % outcome.partition(":")[2])
        retried = None
        if outcome.startswith("retried:"):
            retried = {"verdict": outcome.partition(":")[2], "reason": "the game ended, and 60 s later this was still not there: after",
                       "folder": renderer + "-first"}
        if outcome == "none":
            bad += 1
            continue
        if outcome == "garbage":
            (run / "result.json").write_text("{not json", encoding="utf-8")
            bad += 1
            continue
        result = {"harness": 1, "test": name, "renderer": renderer, "verdict": verdict, "kind": kind, "reason": reason}
        if outcome == "no-verdict":
            result.pop("verdict")
        if retried:
            result["retried"] = retried
        (run / "result.json").write_text(json.dumps(result), encoding="utf-8")
        bad += kind != "pass"
        print("%-28s %-7s %-14s %s" % (name, renderer, verdict, reason))
    return scenario.get("exit", 1 if bad else 0)


if __name__ == "__main__":
    sys.exit(main())
