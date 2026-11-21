"""harness_clock_test.py: no verdict by the wall clock (docs/plan/04-GATE-AND-MAIN.md, "Time limits in the test runner"),
without the real game.

python -X utf8 -I tools/lumacore/tests/harness_clock_test.py [--work <folder>]

The runner sees a running game's progress in the host's step lines in run.log ("[  12.345] host: frame 721, ...", one a
second while the game steps). A stand-in game writes the same lines: a small Python process (the real-process cases) or a
scripted process on a fake clock (the 120-second and 30-second cases, which no test waits for). Each case is named:

  a hang in a test with a 30 s limit is fail, not env   the limit passes in a pause: nothing is decided, the hang is waited for
  a pause at the limit that ends leads to the speed rule  steps come again after the limit: judged then, not before
  moving at full speed past the limit is fail            "its end was not reached in N s although the game ran at full speed"
  moving at half speed past the limit is too slow        env for now; the gate runs it again alone (kinds.Settler)
  too slow in the crowd, passes alone                    the final kind of a fail that passes alone: flake
  too slow in the crowd, too slow again alone            fail, "too slow even alone: the game ran at N% of full speed"
  no step for 120 s is fail, and the dump step is called  a scripted game on the fake clock; and a real one, hang_after set by
                                                          this test (never by the environment)
  a game that never stepped is fail at its limit          nothing to call slow
  a 30 s pause is a stall line, and the test passes       "stall 30 s at frame 300" in result.json and on the console
  the stall count is in the summary                       "stalls: N" in the line the runner ends with
  a 5 s pause is nothing                                  no line, no count
  a restart that is slow but moving is not fail           the game starting again: its start-up is nobody's pause
  a missing tablet exit record is tool                    run_on_device on a scripted tablet, fake clock
  a late tablet exit record (20 s) passes
  a tablet that takes no step for 120 s is hung; one still stepping at its limit is too slow
  a picture that appears after 10 s passes                the wait for what the game writes at its exit
  a picture that never appears is tool
Exit 0 when every case holds.
"""
import argparse
import io
import json
import re
import pathlib
import subprocess
import sys
import tarfile
import tempfile
import types
from contextlib import redirect_stdout

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parents[1] / "gate"))
import importlib.util  # noqa: E402
import kinds  # noqa: E402
import luma_test  # noqa: E402

spec = importlib.util.spec_from_file_location("luma_test_android", HERE.parent / "luma_test_android.py")
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)

failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


class FakeClock:
    """The clock of a case: moves only when something waits."""

    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


def step_line(stamp, frame):
    return "[%9.3f] host: frame %d, file reads 70, save ops 6, sound blocks 1, sound subframes 1, renderer frames %d\n" % (
        stamp, frame, frame * 2)


LOSS_LINE = "[loss] draws-no-material=0 draws-no-pipeline=0 textures-blank=0 sound-reads-missing=0\n"


class Game:
    """The stand-in game on a fake clock: a process object (wait, kill) that writes run.log as the host does. `plan` is a
    list of segments in seconds since the start: ("steps", a, b[, n]) a step line every second from a to b, n steps a line
    (60 by default: the game's full speed); ("restart", t) the
    game starting again (a "boot: main at" line, the host's clock starting from 0); ("end", t) the loss report, the end
    fact and exit 0. The host's time in a line is the time since the last start."""

    def __init__(self, clock, run_folder, plan):
        self.clock, self.run_folder, self.killed = clock, run_folder, False
        self.began = clock.now
        self.events = [(0.0, "boot", None)]  # the host's start line, as in every run
        origin, frame = 0.0, 0
        for seg in plan:
            if seg[0] == "steps":
                t = float(seg[1])
                per_line = seg[3] if len(seg) > 3 else 60  # steps a line: 60 a second is full speed
                while t <= seg[2] + 1e-9:
                    frame += per_line
                    self.events.append((t, "step", (t - origin, frame)))
                    t += 1.0
            elif seg[0] == "restart":
                origin, frame = float(seg[1]), 0
                self.events.append((seg[1], "boot", None))
            elif seg[0] == "end":
                self.events.append((seg[1], "end", None))
        self.events.sort(key=lambda e: e[0])
        self.log = open(run_folder / "run.log", "a", encoding="utf-8", newline="\n")

    def wait(self, timeout=None):
        if self.killed:
            return 1
        self.clock.sleep(timeout if timeout is not None else 0.0)
        elapsed = self.clock.now - self.began
        code = None
        while self.events and self.events[0][0] <= elapsed + 1e-9:
            _, kind, data = self.events.pop(0)
            if kind == "step":
                self.log.write(step_line(*data))
            elif kind == "boot":
                self.log.write("[    0.050] boot: main at 0.050\n")
            else:
                self.log.write(LOSS_LINE)
                (self.run_folder / "facts.jsonl").write_text(
                    json.dumps({"frame": 10, "key": "harness.end", "value": {"frame": 10}}) + "\n", encoding="utf-8")
                code = 0
            self.log.flush()
        if code is None:
            raise subprocess.TimeoutExpired("stand-in game", timeout)
        return code

    def kill(self):
        self.killed = True

    def close(self):
        self.log.close()


def bare_test(work, name, limit=180, steps=None):
    folder = work / name
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "test.json").write_text(json.dumps({
        "harness": 1, "name": name, "about": "the clock", "bare": True, "limit_seconds": limit,
        "steps": steps or [{"at": 10, "end": True}], "expect": []}), encoding="utf-8")
    return luma_test.read_test(folder)


def fresh(work, name):
    run = work / name
    run.mkdir(parents=True)
    return run


def play(work, name, plan, limit=180, hang_after=luma_test.HANG_SECONDS, dump=None, shim_linux=False):
    """One run of the stand-in game through the runner's own watch_run and evaluate, on the fake clock: (verdict, reason,
    kind, ran, run_folder, clock). The watch's limit is the test's limit plus a minute, as run_one gives it."""
    clock = FakeClock()
    run = fresh(work, name)
    test = bare_test(work, name + "-test", limit)
    game = Game(clock, run, plan)
    real_platform = luma_test.platform
    if shim_linux:
        luma_test.platform = types.SimpleNamespace(system=lambda: "Linux")
    try:
        code, starts, unwatched, steps = luma_test.watch_run(
            game, run, "game.exe", test["limit"], clock=clock, hang_after=hang_after,
            dump=dump or (lambda folder, process: []))
    finally:
        luma_test.platform = real_platform
        game.close()
    ran = {"code": code, "seconds": clock.now - game.began, "frozen": {}, "mods": [], "exe": "game.exe", "starts": starts,
           "unwatched": unwatched, **steps}
    verdict, reason, checks, facts = luma_test.evaluate(test, "d3d12", run, ran)
    return verdict, reason, luma_test.kind_of(verdict), ran, run, clock


# A game that steps every second from 1 s to 20 s and ends at 21 s: the plan of an ordinary run.
ORDINARY = [("steps", 1, 20), ("end", 21)]


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)

    # The ordinary run, so that every case below differs from it in one thing only.
    verdict, reason, kind, ran, run, clock = play(work, "ordinary", ORDINARY)
    check(verdict == "pass" and kind == "pass" and not ran["hung"] and ran["stepped"],
          "an ordinary run on the fake clock passes: %s %s" % (verdict, reason))
    check(luma_test.run_stalls(run) == [], "an ordinary run has no stall line")

    # --- slow but moving past its limit is env, not fail (the stand-in game is a real process) ----------------------------
    stand_in = work / "stand_in.py"
    stand_in.write_text(STAND_IN, encoding="utf-8")

    def real_game(name, mode, limit, hang_after):
        run = fresh(work, name)
        process = subprocess.Popen([sys.executable, "-I", str(stand_in), str(run), mode])
        try:
            code, starts, unwatched, steps = luma_test.watch_run(process, run, "python.exe", limit, hang_after=hang_after)
        finally:
            process.kill()
            process.wait()
        test = bare_test(work, name + "-test", limit)
        ran = {"code": code, "seconds": limit, "frozen": {}, "mods": [], "exe": "python.exe", "starts": starts,
               "unwatched": unwatched, **steps}
        verdict, reason, _, _ = luma_test.evaluate(test, "d3d12", run, ran)
        return verdict, reason, ran, process

    # The limit passes at 1.5 s while the game still takes a step every 0.1 s. hang_after is shortened by this call.
    verdict, reason, ran, process = real_game("slow-moving", "moving", 1.5, hang_after=1.0)
    said = re.fullmatch(r"too slow on this machine now: still moving when its time limit of 1.5 s passed "
                        r"\(the game ran at (\d+)% of full speed\)", reason)
    check(verdict == "too-slow" and luma_test.kind_of(verdict) == "env" and said and 45 <= int(said.group(1)) <= 70,
          "a real stand-in game at half speed past its limit is too slow (env): %s: %s" % (verdict, reason))
    check(ran["code"] is None and ran["hung"] is None and ran["stepped"] and ran["limit_hit"] and process.poll() is not None,
          "the game is ended at its limit, and is not called hung: %s" % ran)
    verdict, reason, ran, process = real_game("fast-moving", "fast", 1.5, hang_after=1.0)
    check(verdict == "not-reached" and reason == "its end was not reached in 1.5 s although the game ran at full speed",
          "a real stand-in game at full speed past its limit is fail: %s: %s" % (verdict, reason))

    # --- no step for 120 s is fail, and the dump step is called -----------------------------------------------------------
    dumped = []

    def dump(folder, process):
        dumped.append((folder, process))
        return ["nebula-hang-20261121.dmp"]

    # Steps to 10 s, then nothing, the process alive: the 120 s default, on the fake clock.
    verdict, reason, kind, ran, run, clock = play(work, "hang", [("steps", 1, 10)], dump=dump)
    waited = clock.now - 1000.0
    check(verdict == "hung" and kind == "fail", "no step for 120 s is fail: %s (%s)" % (verdict, kind))
    check(ran["hung"] is not None and 120 <= ran["hung"]["seconds"] <= 121 and ran["hung"]["frame"] == 600
          and 130 <= waited <= 131.5 and ran["code"] is None,
          "it is ended at 120 s after the last step (10 s + 120 s), at frame 600: %s after %.1f s" % (ran["hung"], waited))
    check(len(dumped) == 1 and dumped[0][0] == run, "the dump step is called once, with the run's folder: %s" % dumped)
    check("no step for 120 s" in reason and "frame 600" in reason and "nebula-hang-20261121.dmp" in reason,
          "the reason names the time, the frame and the dump: %s" % reason)
    # 119 s of silence is a stall, not a hang.
    verdict, reason, kind, ran, run, clock = play(work, "119", [("steps", 1, 10), ("steps", 129, 140), ("end", 141)], dump=dump)
    check(verdict == "pass" and ran["hung"] is None and luma_test.run_stalls(run) == ["stall 119 s at frame 600"],
          "119 s with no step is a stall line, not a hang: %s %s %s" % (verdict, reason, luma_test.run_stalls(run)))
    # The same with a real process, hang_after shortened by the test: the process is ended and the dump step called.
    before = len(dumped)
    run = fresh(work, "real-hang")
    process = subprocess.Popen([sys.executable, "-I", str(stand_in), str(run), "freezes"])
    try:
        code, starts, unwatched, steps = luma_test.watch_run(process, run, "python.exe", 30, hang_after=1.0, dump=dump)
    finally:
        process.kill()
        process.wait()
    check(code is None and steps["hung"] is not None and steps["hung"]["frame"] == 18 and len(dumped) == before + 1,
          "a real game that stops stepping is ended as hung, dump step called: %s" % steps)

    # --- a game that never stepped is fail at its limit -------------------------------------------------------------------
    verdict, reason, kind, ran, run, clock = play(work, "never", [], limit=100)
    check(verdict == "timeout" and kind == "fail" and not ran["stepped"] and ran["hung"] is None,
          "a game that never took a step is fail at its limit, not slow: %s (%s)" % (verdict, kind))
    # A limit that passes with the last step 130 s before it is fail too (the game is at least hung, whatever ended it).
    verdict, reason, kind, ran, run, clock = play(work, "quiet-end", [("steps", 1, 5)], limit=400)
    check(verdict == "hung" and kind == "fail", "a game quiet for 120 s is hung before any limit: %s" % verdict)

    test_limit_rule(work, dump)

    # --- a 30 s pause is a stall line and the test passes; the stall count; a 5 s pause is nothing -----------------------------
    verdict, reason, kind, ran, run, clock = play(work, "pause30", [("steps", 1, 5), ("steps", 35, 40), ("end", 41)])
    check(verdict == "pass" and kind == "pass" and luma_test.run_stalls(run) == ["stall 30 s at frame 300"],
          "a 30 s pause is a stall line and the test passes: %s %s" % (verdict, luma_test.run_stalls(run)))
    verdict, reason, kind, ran, run, clock = play(work, "pause5", [("steps", 1, 5), ("steps", 10, 15), ("end", 16)])
    check(verdict == "pass" and luma_test.run_stalls(run) == [], "a 5 s pause is nothing: %s" % luma_test.run_stalls(run))
    verdict, reason, kind, ran, run, clock = play(work, "pause10", [("steps", 1, 5), ("steps", 15, 20), ("end", 21)])
    check(luma_test.run_stalls(run) == [], "a pause of 10 s is not more than 10 s: %s" % luma_test.run_stalls(run))
    verdict, reason, kind, ran, run, clock = play(work, "pause11", [("steps", 1, 5), ("steps", 16, 20), ("end", 21)])
    check(luma_test.run_stalls(run) == ["stall 11 s at frame 300"], "a pause of 11 s is a stall: %s" % luma_test.run_stalls(run))

    # A whole suite through run_tests: result.json carries the lines, the console prints them, the summary counts them.
    suite = work / "suite"
    plans = {"a-pause-30": [("steps", 1, 5), ("steps", 35, 40), ("end", 41)],
             "b-pause-5": [("steps", 1, 5), ("steps", 10, 15), ("end", 16)],
             "c-two-pauses": [("steps", 1, 5), ("steps", 20, 25), ("steps", 66, 70), ("end", 71)]}
    for n in plans:
        bare_test(suite, n)
    (suite / "suite.json").write_text(json.dumps({"harness": 1, "tests": list(plans)}), encoding="utf-8")
    bin_folder = work / "bin"
    bin_folder.mkdir(exist_ok=True)

    def fake_run_one(test, renderer, a, run_folder, previous):
        clock = FakeClock()
        run_folder.mkdir(parents=True, exist_ok=True)
        game = Game(clock, run_folder, plans[test["name"]])
        try:
            code, starts, unwatched, steps = luma_test.watch_run(game, run_folder, "game.exe", test["limit"], clock=clock)
        finally:
            game.close()
        return {"code": code, "seconds": 5.0, "frozen": {}, "mods": [], "exe": "game.exe", "starts": starts,
                "unwatched": unwatched, **steps}

    out = run_suite(suite, work, bin_folder, fake_run_one)
    result = {n: json.loads((work / "suite-work" / "runs" / "suite" / n / "d3d12" / "result.json").read_text(encoding="utf-8"))
              for n in plans}
    check(all(r["kind"] == "pass" for r in result.values()), "the paused runs pass: %s" % {n: r["verdict"] for n, r in result.items()})
    check(result["a-pause-30"]["stalls"] == ["stall 30 s at frame 300"] and result["b-pause-5"]["stalls"] == []
          and result["c-two-pauses"]["stalls"] == ["stall 15 s at frame 300", "stall 41 s at frame 660"],
          "result.json holds each test's stall lines: %s" % {n: r["stalls"] for n, r in result.items()})
    check("stall 30 s at frame 300" in out and "stall 41 s at frame 660" in out, "the console prints the stall lines: %s" % out)
    check(out.strip().splitlines()[-1] == "summary: 3 results (pass 3, fail 0, tool 0, env 0); stalls: 3",
          "the stall count is in the summary: %r" % out.strip().splitlines()[-1])

    # --- a restart that is slow but moving is not fail ---------------------------------------------------------------------
    # The game steps, starts again at 30 s (its own clock from 0; the start-up has no step for 150 s, which is nobody's pause),
    # then steps on, every second, past the limit. The limit is the whole time a process may take, from its own start.
    plan = [("steps", 1, 29), ("restart", 30), ("steps", 180, 700)]
    verdict, reason, kind, ran, run, clock = play(work, "restart-slow", plan, limit=300, shim_linux=True)
    check(verdict == "too-slow" and kind == "env" and ran["starts"] == 2 and ran["hung"] is None,
          "a restart that is slow but moving is not fail: %s %s starts %s hung %s" % (verdict, kind, ran["starts"], ran["hung"]))
    check(luma_test.run_stalls(run) == [], "the start-up between two starts is no stall: %s" % luma_test.run_stalls(run))
    # The same restart whose second start never steps again is hung at 120 s after its last step only once it has stepped:
    # a start-up that never ends is the limit's, and is fail (no step in the last 120 s).
    verdict, reason, kind, ran, run, clock = play(work, "restart-dead", [("steps", 1, 29), ("restart", 30)], limit=300,
                                                  shim_linux=True)
    check(verdict == "timeout" and kind == "fail" and not ran["stepped"],
          "a restart that never steps again is fail at its limit: %s %s" % (verdict, kind))

    # --- the tablet ---------------------------------------------------------------------------------------------------------
    test_android(work)

    # --- what the game writes at its exit ----------------------------------------------------------------------------------
    test_outputs(work)

    # The verdicts' kinds.
    for verdict, kind in (("hung", "fail"), ("too-slow", "env"), ("output-missing", "tool"), ("no-exit-record", "tool"),
                          ("timeout", "fail")):
        check(luma_test.kind_of(verdict) == kind, "verdict %s is kind %s" % (verdict, kind))

    print("\n".join(failed) if failed else "harness clock: every case holds")
    return 1 if failed else 0


def test_limit_rule(work, dump):
    """What a test that reaches its time limit is judged by: the game's pause, then its speed (docs section 16)."""
    # A hang in a test with a 30 s limit: the limit passes in the pause (steps stopped at 5 s), nothing is decided, and the
    # hang comes 120 s after the last step. Never env.
    dumped = []
    verdict, reason, kind, ran, run, clock = play(work, "hang30", [("steps", 1, 5)], limit=30,
                                                  dump=lambda folder, process: dumped.append(folder) or [])
    waited = clock.now - 1000.0
    check(verdict == "hung" and kind == "fail" and ran["hung"] and not ran["limit_hit"] and 124 <= waited <= 126.5 and len(dumped) == 1,
          "a hang in a test with a 30 s limit is fail, not env, with the dump step: %s %s after %.1f s, dump %s"
          % (verdict, kind, waited, dumped))

    # A pause that is on at the limit (steps from 1 to 5 s, again from 40 s; the limit is 30 s): not decided at 30 s, judged
    # when the steps come again, by the speed over the whole run (360 steps by 40 s: 15% of full speed).
    verdict, reason, kind, ran, run, clock = play(work, "pause-at-limit", [("steps", 1, 5), ("steps", 40, 60)], limit=30)
    waited = clock.now - 1000.0
    check(ran["limit_hit"] and 39.9 <= waited <= 41 and verdict == "too-slow" and "ran at 15% of full speed" in reason,
          "a pause at the limit that ends leads to the speed rule, judged when steps come again: %s %s after %.1f s: %s"
          % (verdict, kind, waited, reason))
    # Moving at full speed past the limit: fail, and the machine was not the reason.
    verdict, reason, kind, ran, run, clock = play(work, "full-speed", [("steps", 1, 100)], limit=50)
    check(verdict == "not-reached" and kind == "fail"
          and reason == "its end was not reached in 50 s although the game ran at full speed",
          "moving at full speed past the limit is fail, with its line: %s %s %s" % (verdict, kind, reason))
    # The edge: 80% of 60 is 48 steps a second.
    verdict, reason, kind, ran, run, clock = play(work, "speed-48", [("steps", 1, 100, 48)], limit=50)
    check(verdict == "not-reached", "48 steps a second is full speed enough: %s" % verdict)
    verdict, reason, kind, ran, run, clock = play(work, "speed-47", [("steps", 1, 100, 47)], limit=50)
    check(verdict == "too-slow" and kind == "env" and "ran at 78% of full speed" in reason,
          "47 steps a second is too slow: %s %s" % (verdict, reason))

    # Half speed past the limit: too slow, env for now.
    verdict, reason, kind, ran, run, clock = play(work, "half-speed", [("steps", 1, 100, 30)], limit=50)
    check(verdict == "too-slow" and kind == "env" and ran["limit_hit"]
          and reason == ("too slow on this machine now: still moving when its time limit of 50 s passed "
                         "(the game ran at 50% of full speed)"), "moving at half speed past the limit is too slow (env): %s: %s"
          % (verdict, reason))

    # The gate then runs it again alone (kinds.Settler): the message the runner wrote is the one it reads.
    def crowd_result(name, plan, limit=50):
        folder = work / ("crowd-" + name)
        folder.mkdir()
        verdict, reason, kind, ran, run, clock = play(work, "slowrun-" + name, plan, limit=limit)
        report_args = argparse.Namespace(commit="0123456789ab", module="hooks")
        with redirect_stdout(io.StringIO()):
            luma_test.report(folder, "slow-test", [], "d3d12", verdict, reason, [], report_args, {})
        return kinds.read_result(folder / "result.json", luma_test.kind_of)

    class Source:
        def __init__(self, again):
            self.again, self.adopted = again, []

        def alone(self, names):
            return {n: self.again for n in names}

        def adopt(self, name):
            self.adopted.append(name)

        def on_main(self, names):
            raise AssertionError("a test that was too slow is not run on main's build")

    def settle(again):
        first = crowd_result("first" + str(len(failed)) + again[0] + again[1][:4], [("steps", 1, 100, 30)])
        source = Source(again)
        cases = kinds.Cases({"slow-test": first}, 1, None, "d3d12", source)
        records, _ = kinds.judge_cases(cases)
        said = []
        flakes = work / ("flakes-%s.tsv" % (again[0] + str(len(failed))))
        settler = kinds.Settler(flakes, work / "aside.tsv", "abc", say=said.append)
        return first, settler.settle("tour/d3d12", cases, records), source, flakes

    first, records, source, flakes = settle(("env", "too-slow: too slow on this machine now: still moving when its time limit of 50 s "
                                             "passed (the game ran at 31% of full speed)"))
    r = records[0]
    check(first[0] == "env" and first[1].startswith("too-slow:"), "the runner's too-slow result is env, and says so: %s" % (first,))
    check(r["kind"] == "fail" and r["counts"] and r["message"] == "too slow even alone: the game ran at 31% of full speed",
          "alone it is slow again: fail with the percentage: %s" % r)
    first, records, source, flakes = settle(("pass", ""))
    r = records[0]
    check(r["kind"] == "flake" and not r["counts"] and source.adopted == ["slow-test"] and kinds.worst([r["kind"]]) == "flake"
          and len(kinds.read_lines(flakes)) == 1,
          "alone it passes: the final kind of a crowd fail that passes alone (flake), its second run adopted: %s" % r)


def run_suite(suite, work, bin_folder, fake_run_one):
    """luma_test.run_tests on a suite whose games are the stand-in's; what it printed."""
    real = luma_test.run_one
    args = argparse.Namespace(target=str(suite / "suite.json"), test=[], work=str(work / "suite-work"),
                              lock=str(work / "suite.lock"), limit_scale=1.0, device=None, device_lock=None, gpu=None,
                              bin=str(bin_folder), renderer="d3d12", commit="0123456789ab", module="hooks", who="test")
    luma_test.run_one = fake_run_one
    out = io.StringIO()
    try:
        with redirect_stdout(out):
            luma_test.run_tests(args)
    finally:
        luma_test.run_one = real
    return out.getvalue()


# The stand-in game as a real process: it writes the host's step lines into the run's log.
STAND_IN = '''import pathlib
import sys
import time

run = pathlib.Path(sys.argv[1])
mode = sys.argv[2]
log = open(run / "run.log", "a", buffering=1, encoding="utf-8", newline="\\n")
began = time.time()


def step(n, per):
    log.write("[%9.3f] host: frame %d, file reads 70, save ops 6, sound blocks 1, sound subframes 1, renderer frames %d\\n"
              % (time.time() - began, n * per, n * per * 2))
    log.flush()


# A step every 0.1 s: 3 steps a step line is 30 a second (half of full speed), 6 is 60 (full speed).
if mode in ("moving", "fast"):
    per = 3 if mode == "moving" else 6
    n = 0
    while True:
        n += 1
        step(n, per)
        time.sleep(0.1)
elif mode == "freezes":
    for n in range(1, 4):
        step(n, 6)
        time.sleep(0.1)
    time.sleep(60)
'''


# --- the tablet --------------------------------------------------------------------------------------------------------------

RECORD = ("ApplicationExitInfo #0:\n"
          "  timestamp=2026-11-21 10:00:00.000 pid=4321 realUid=10345 packageUid=10345 definingUid=10345 user=0\n"
          "    process=dev.nebula.galaxy.test:game reason=1 (EXIT_SELF) subreason=0 (UNKNOWN) status=0 importance=100\n")


class Tablet(android.Device):
    """A tablet on the fake clock. The game's process is alive from 1 s to `ends` seconds after am start (None: for good);
    it writes a step line a second while `steps_until` allows; Android's exit record appears `record_after` seconds after the
    process is gone (None: never). Every adb call the runner makes on it is answered as the device would."""

    def __init__(self, clock, steps_until, ends, record_after, per_step=60):
        super().__init__("fake")
        self.clock, self.steps_until, self.ends, self.record_after = clock, steps_until, ends, record_after
        self.per_step = per_step  # steps a line, a line a second: 60 is full speed
        self.started = None
        self.folder = None
        self.calls = []

    def since(self):
        return None if self.started is None else self.clock.now - self.started

    def alive(self):
        t = self.since()
        return t is not None and t >= 1.0 and (self.ends is None or t < self.ends)

    def log_lines(self, upto):
        lines = ["[%9.3f] host: frame %d, file reads 70\n" % (n + 0.5, n * self.per_step)
                 for n in range(1, int(min(upto, self.steps_until)) + 1)]
        if self.ends is not None and upto >= self.ends:
            lines.append(LOSS_LINE)
        return lines

    def answers(self):
        return True

    def installed(self):
        return True

    def files(self):
        return "/data/data/dev.nebula.galaxy.test/files"

    def run_as(self, command, timeout=600):
        self.calls.append(command)
        return 0, ""

    def shell(self, command, timeout=600):
        self.calls.append(command)
        if command.startswith("am start"):
            self.started = self.clock.now
            return 0, "Status: ok\n"
        if command.startswith("pidof"):
            return (0, "4321\n") if self.alive() else (1, "")
        if command.startswith("dumpsys activity exit-info"):
            t = self.since()
            if self.ends is not None and self.record_after is not None and t is not None and t >= self.ends + self.record_after:
                return 0, RECORD
            return 0, "ApplicationExitInfo for nothing yet\n"
        return 0, ""

    def adb(self, *args, timeout=600, stdin=None):
        self.calls.append(" ".join(args))

        class Result:
            returncode, stdout, stderr = 0, b"", b""
        result = Result()
        if args and args[0] == "logcat" and "--pid=4321" in args:
            result.stdout = "".join(self.log_lines(self.since() or 0)).encode("utf-8")
        elif args and args[0] == "logcat" and "-d" in args:
            result.stdout = "".join(self.log_lines(self.ends or self.since() or 0)).encode("utf-8")
        elif args and args[0] == "exec-out":
            name = args[1].rstrip("'").split()[-1]
            buffer = io.BytesIO()
            with tarfile.open(fileobj=buffer, mode="w") as tar:
                facts = (json.dumps({"frame": 10, "key": "harness.end", "value": {"frame": 10}}) + "\n").encode("utf-8")
                for member, body in (((name + "/facts.jsonl", facts),) if self.ends is not None else ()):
                    info = tarfile.TarInfo(member)
                    info.size = len(body)
                    tar.addfile(info, io.BytesIO(body))
            result.stdout = buffer.getvalue()
        return result


def tablet_run(work, name, tablet, clock, limit=180):
    """luma_test_android.run_one on the scripted tablet, then evaluate: (verdict, kind, reason, ran)."""
    bin_folder = work / (name + "-bin")
    bin_folder.mkdir()
    host = android.host_for("arm64-v8a")
    for file in (host["game"], host["extension"], host["half"]):
        (bin_folder / file).write_bytes(b"x")
    test = bare_test(work, name + "-test", limit)
    run = work / (name + "-run")
    args = argparse.Namespace(bin=str(bin_folder), diagnostics=None, module="hooks")
    real_host = luma_test.HOST
    luma_test.HOST = host
    try:
        with redirect_stdout(io.StringIO()):
            ran = android.run_one(luma_test, tablet, test, args, run, None, clock, clock.sleep)
        verdict, reason, checks, facts = luma_test.evaluate(test, "vulkan", run, ran)
    finally:
        luma_test.HOST = real_host
    return verdict, luma_test.kind_of(verdict), reason, ran


def test_android(work):
    # A tablet game that runs 40 s and ends at once, its exit record there 0.2 s after: passes.
    clock = FakeClock()
    verdict, kind, reason, ran = tablet_run(work, "tablet-ok", Tablet(clock, 38, 40, 0.2), clock)
    check(verdict == "pass" and ran["code"] == 0 and not ran["no_exit_record"], "a tablet run with its record passes: %s %s" % (verdict, reason))

    # The record arrives 20 s after the game ended: waited for, the run passes (it was 5 s before this change).
    clock = FakeClock()
    verdict, kind, reason, ran = tablet_run(work, "tablet-late", Tablet(clock, 38, 40, 20.0), clock)
    check(verdict == "pass" and ran["code"] == 0 and not ran["no_exit_record"] and 60 <= clock.now - 1000.0 < 75,
          "a late exit record (20 s) passes: %s %s after %.1f s" % (verdict, reason, clock.now - 1000.0))

    # The record never arrives: the wait is 60 s, then the kind is tool, not fail.
    clock = FakeClock()
    verdict, kind, reason, ran = tablet_run(work, "tablet-none", Tablet(clock, 38, 40, None), clock)
    waited = clock.now - 1000.0
    check(verdict == "no-exit-record" and kind == "tool" and ran["no_exit_record"] and 100 <= waited <= 105
          and "the tablet gave no exit record" in reason,
          "a missing tablet exit record is tool: %s %s %s after %.1f s" % (verdict, kind, reason, waited))

    # The wait itself, on the device: 60 s, then not found.
    clock = FakeClock()
    tablet = Tablet(clock, 0, 1, None)
    tablet.started = clock.now - 100
    found, code = tablet.exit_record("4321", clock, clock.sleep)
    check(not found and code is None and 60 <= clock.now - 1000.0 < 62 and android.EXIT_RECORD_SECONDS == 60.0,
          "the wait for the exit record is up to 60 s: found %s after %.1f s" % (found, clock.now - 1000.0))

    # A tablet game that takes no step for 120 s while its process lives is hung (fail); one still stepping at its limit is
    # too slow (env). Neither has an exit record: the game was stopped by the runner.
    clock = FakeClock()
    verdict, kind, reason, ran = tablet_run(work, "tablet-hung", Tablet(clock, 20, None, None), clock, limit=600)
    check(verdict == "hung" and kind == "fail" and ran["hung"] is not None and 120 <= ran["hung"]["seconds"] <= 130,
          "a tablet game with no step for 120 s is hung: %s %s %s" % (verdict, kind, ran.get("hung")))
    clock = FakeClock()
    verdict, kind, reason, ran = tablet_run(work, "tablet-slow", Tablet(clock, 10 ** 6, None, None, 30), clock, limit=100)
    check(verdict == "too-slow" and kind == "env" and "time limit of 100 s" in reason and "ran at 50% of full speed" in reason,
          "a tablet game at half speed at its limit is too slow (env): %s %s %s" % (verdict, kind, reason))
    clock = FakeClock()
    verdict, kind, reason, ran = tablet_run(work, "tablet-full", Tablet(clock, 10 ** 6, None, None), clock, limit=100)
    check(verdict == "not-reached" and kind == "fail", "a tablet game at full speed at its limit is fail: %s %s" % (verdict, kind))
    # A hang in a test with a short limit on the tablet is hung too: the limit passes in a pause, the runner waits on.
    clock = FakeClock()
    verdict, kind, reason, ran = tablet_run(work, "tablet-hung-short", Tablet(clock, 10, None, None), clock, limit=30)
    check(verdict == "hung" and kind == "fail", "a tablet hang in a test with a 30 s limit is fail: %s %s" % (verdict, kind))


# --- what the game writes at its exit ----------------------------------------------------------------------------------------


def ppm_bytes(w=2, h=2):
    return b"P6\n%d %d\n255\n" % (w, h) + bytes(range(w * h * 3))


def test_outputs(work):
    steps = [{"at": 4, "picture": "title"}, {"at": 10, "end": True}]

    def finished_run(name, with_loss=True):
        folder = work / name
        run = folder / "run"
        (run / "pictures").mkdir(parents=True)
        test = bare_test(work, name + "-test", 180, steps)
        (run / "facts.jsonl").write_text(json.dumps({"frame": 10, "key": "harness.end", "value": {"frame": 10}}) + "\n",
                                         encoding="utf-8")
        (run / "run.log").write_text("host: started\n" + (LOSS_LINE if with_loss else ""), encoding="utf-8")
        return test, run

    # The picture appears 10 s after the game ended; before that a part of it is there.
    test, run = finished_run("late-picture")
    clock = FakeClock()
    whole = ppm_bytes()
    (run / "pictures" / "title.ppm").write_bytes(whole[:15])

    def sleep(seconds):
        clock.sleep(seconds)
        if clock.now - 1000.0 >= 10.0:
            (run / "pictures" / "title.ppm").write_bytes(whole)

    ran = {"code": 0, "seconds": 5.0, "mods": []}
    missing = luma_test.settle_outputs(test, run, ran, clock=clock, sleep=sleep)
    check(missing == [] and 10.0 <= clock.now - 1000.0 < 11.0,
          "a picture that appears after 10 s passes: missing %s after %.1f s" % (missing, clock.now - 1000.0))
    ran["missing_outputs"] = missing
    luma_test.finish_pictures(run)
    verdict, reason, checks, facts = luma_test.evaluate(test, "d3d12", run, ran)
    check(verdict == "pass", "and the run passes: %s %s" % (verdict, reason))

    # A picture that is there at once is not waited for.
    test, run = finished_run("there-at-once")
    (run / "pictures" / "title.ppm").write_bytes(whole)
    clock = FakeClock()
    check(luma_test.settle_outputs(test, run, {"code": 0}, clock=clock, sleep=clock.sleep) == [] and clock.now == 1000.0,
          "a picture that is there is not waited for")

    # A picture that never appears: waited for 60 s, then tool.
    test, run = finished_run("no-picture")
    clock = FakeClock()
    ran = {"code": 0, "seconds": 5.0, "mods": []}
    missing = luma_test.settle_outputs(test, run, ran, clock=clock, sleep=clock.sleep)
    check(missing == ["title"] and 60.0 <= clock.now - 1000.0 < 61.0,
          "a picture that never appears is waited for 60 s: %s after %.1f s" % (missing, clock.now - 1000.0))
    ran["missing_outputs"] = missing
    verdict, reason, checks, facts = luma_test.evaluate(test, "d3d12", run, ran)
    check(verdict == "output-missing" and luma_test.kind_of(verdict) == "tool" and "title" in reason,
          "a picture that never appears is tool: %s %s" % (verdict, reason))
    # Without the wait's word for it, the check is the old one: a picture not written fails and names it.
    verdict, reason, checks, facts = luma_test.evaluate(test, "d3d12", run, {"code": 0, "seconds": 5.0, "mods": []})
    check(verdict == "fail" and "every picture asked was written" in reason, "evaluate alone still fails the check: %s" % verdict)
    # A real fault beside the missing picture stays fail: the game is not shown right by a tool fault elsewhere.
    (run / "facts.jsonl").write_text(json.dumps({"frame": 10, "key": "harness.end", "value": {"frame": 10}}) + "\n",
                                     encoding="utf-8")
    (run / "run.log").write_text("host: started\n[loss] draws-no-material=3 draws-no-pipeline=0 textures-blank=0 sound-reads-missing=0\n",
                                 encoding="utf-8")
    verdict, reason, checks, facts = luma_test.evaluate(test, "d3d12", run, {"code": 0, "seconds": 5.0, "mods": [],
                                                                            "missing_outputs": ["title"]})
    check(verdict == "fail", "a lost draw beside a missing picture is fail: %s" % verdict)

    # The loss report that comes late is waited for; one that never comes is tool.
    test, run = finished_run("late-report", with_loss=False)
    (run / "pictures" / "title.ppm").write_bytes(whole)
    clock = FakeClock()

    def sleep_report(seconds):
        clock.sleep(seconds)
        if clock.now - 1000.0 >= 3.0:
            (run / "run.log").write_text("host: started\n" + LOSS_LINE, encoding="utf-8")

    check(luma_test.settle_outputs(test, run, {"code": 0}, clock=clock, sleep=sleep_report) == [], "a report that comes late is waited for")
    test, run = finished_run("no-report", with_loss=False)
    (run / "pictures" / "title.ppm").write_bytes(whole)
    clock = FakeClock()
    ran = {"code": 0, "seconds": 5.0, "mods": []}
    ran["missing_outputs"] = luma_test.settle_outputs(test, run, ran, clock=clock, sleep=clock.sleep)
    luma_test.finish_pictures(run)
    verdict, reason, checks, facts = luma_test.evaluate(test, "d3d12", run, ran)
    check(verdict == "output-missing" and "[loss] report" in reason, "a report that never comes is tool: %s %s" % (verdict, reason))

    # A game that was ended by the runner (no code) or did not reach its end step is not waited for.
    test, run = finished_run("ended-by-runner")
    clock = FakeClock()
    check(luma_test.settle_outputs(test, run, {"code": None}, clock=clock, sleep=clock.sleep) == [] and clock.now == 1000.0,
          "a game the runner ended is not waited for")


if __name__ == "__main__":
    sys.exit(main())
