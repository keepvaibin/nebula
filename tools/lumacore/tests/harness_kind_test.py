"""harness_kind_test.py: the runner's kind of result (docs/plan/04-GATE-AND-MAIN.md section 9), without a game.

python -X utf8 -I tools/lumacore/tests/harness_kind_test.py

Every result.json carries "kind": pass, fail (the game or the change is wrong), tool (the runner or the test is) or env (the
machine was not fit). luma_test.py kind_of is the one place it is decided. Here:
  1. each verdict has its kind: crashed, timeout, hung (no step for 120 s) and a fail are fail; harness-error, setup-failed,
     output-missing (a picture or report the game writes at its exit never came) and no-exit-record (the tablet's) are tool;
     not-run (a disc's game not given), device-lost and too-slow (still stepping when the limit passed) are env; a verdict
     the runner does not know is fail;
  2. report() writes the kind, and the gate's reader (tools/gate/kinds.py read_result) reads the same kind back;
  3. an exception of the runner's own, while it reads a test or runs one, is a result of its own: tool, with the
     exception's name, and its traceback in runner-error.txt; the suite's other tests still run;
  4. a drive with no room (ENOSPC, Windows' disk-full codes) is env, not tool;
  5. the runner that cannot start because the machine is not fit (the run lock held, the device not answering) ends with
     exit code ENV_EXIT and writes no result, and the gate expects that same number;
  6. a game that cannot be started at all is tool ("the game could not be started").
  7. a fault of the tool's that one more play can clear (RETRIED: output-missing, no-exit-record; a setup that failed; an
     exception of the runner's own) is played once more at once: the result is the second play's and names the first in
     "retried", whose folder is kept as <renderer>-first; a pass then is a pass the gate names in played_again; a malformed test, a
     continue_from that did not pass, a disc not given, a full drive and a device that left are played once.
Exit 0 when every case holds.
"""
import argparse
import errno
import io
import json
import pathlib
import subprocess
import sys
import tempfile
from contextlib import redirect_stderr, redirect_stdout

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parents[1] / "gate"))
import luma_test  # noqa: E402
import kinds  # noqa: E402

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


# 1. The mapping.
WANT = {"pass": "pass", "fail": "fail", "crashed": "fail", "timeout": "fail", "hung": "fail", "harness-error": "tool",
        "setup-failed": "tool", "output-missing": "tool", "no-exit-record": "tool", "not-run": "env", "device-lost": "env",
        "too-slow": "env", "something-else": "fail"}
for verdict, kind in WANT.items():
    check(luma_test.kind_of(verdict) == kind, "verdict %s is kind %s, not %s" % (verdict, kind, luma_test.kind_of(verdict)))

with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
    tmp = pathlib.Path(tmp)
    args = argparse.Namespace(commit="0123456789ab", module="hooks")

    # 2. report() and the gate's reader.
    for verdict, kind in WANT.items():
        run = tmp / "report" / verdict
        with redirect_stdout(io.StringIO()):
            luma_test.report(run, "t", "proves", "d3d12", verdict, "why it ended", [], args, {})
        result = json.loads((run / "result.json").read_text(encoding="utf-8"))
        check(result.get("kind") == kind, "result.json of %s says kind %s: %s" % (verdict, kind, result.get("kind")))
        read = kinds.read_result(run / "result.json", lambda v: "unused")
        check(read[0] == kind, "the gate reads kind %s back for %s: %s" % (kind, verdict, read))

    # 3, 4. Exceptions of the runner's own, while a suite runs.
    suite = tmp / "suite"
    body = {"harness": 1, "name": "t", "about": "a", "proves": ["services.net.local"], "steps": [{"at": 1, "end": True}],
            "expect": [{"fact": "f", "is": True}]}
    names = ["fine", "raises", "full", "refused", "unreadable"]
    for n in names:
        (suite / n).mkdir(parents=True)
        (suite / n / "test.json").write_text(json.dumps(dict(body, name=n)), encoding="utf-8")
    (suite / "suite.json").write_text(json.dumps({"harness": 1, "tests": names}), encoding="utf-8")
    bin_folder = tmp / "bin"
    bin_folder.mkdir()

    def run_args(lock, work, renderer="d3d12"):
        return argparse.Namespace(target=str(suite / "suite.json"), test=[], work=str(work), lock=str(lock), limit_scale=1.0,
                                  device=None, device_lock=None, gpu=None, bin=str(bin_folder), renderer=renderer,
                                  commit="0123456789ab", module="hooks", who="test")

    real = {k: getattr(luma_test, k) for k in ("run_one", "evaluate", "read_test")}

    def fake_run_one(test, renderer, a, run_folder, previous):
        if test["name"] == "raises":
            raise ZeroDivisionError("the runner's own bug")
        if test["name"] == "full":
            raise OSError(errno.ENOSPC, "No space left on device")
        if test["name"] == "refused":
            raise luma_test.NotRun("needs disc RMGP01: this run's game is RMGE01")
        return {"code": 0, "seconds": 1.0, "frozen": {}, "mods": [], "exe": "x", "starts": 1, "unwatched": None}

    def fake_evaluate(test, renderer, run_folder, ran):
        return "pass", "", [], []

    def fake_read_test(folder, types=None):
        if pathlib.Path(folder).name == "unreadable":
            raise KeyError("steps")
        return real["read_test"](folder, types)

    luma_test.run_one, luma_test.evaluate, luma_test.read_test = fake_run_one, fake_evaluate, fake_read_test
    try:
        with redirect_stdout(io.StringIO()):
            code = luma_test.run_tests(run_args(tmp / "run.lock", tmp / "work"))
    finally:
        luma_test.run_one, luma_test.evaluate, luma_test.read_test = real["run_one"], real["evaluate"], real["read_test"]
    check(code == 1, "a suite with a test that did not pass exits 1: %s" % code)
    got = {}
    for n in names:
        for place in ("d3d12", "-"):
            f = tmp / "work" / "runs" / "suite" / n / place / "result.json"
            if f.is_file():
                got[n] = json.loads(f.read_text(encoding="utf-8"))
    check(all(n in got for n in names), "every test left a result.json, whatever raised: %s" % sorted(got))
    check(got["fine"]["kind"] == "pass", "the test that ran: pass")
    check(got["raises"]["kind"] == "tool" and got["raises"]["verdict"] == "harness-error"
          and "ZeroDivisionError" in got["raises"]["reason"], "an exception of the runner's own: tool, named: %s" % got["raises"])
    check((tmp / "work" / "runs" / "suite" / "raises" / "d3d12" / "runner-error.txt").is_file(), "its traceback is kept")
    check(got["full"]["kind"] == "env" and got["full"]["verdict"] == "not-run" and "drive is full" in got["full"]["reason"],
          "a full drive: env: %s" % got["full"])
    check(got["refused"]["kind"] == "env", "a disc's game not given: env")
    check(got["unreadable"]["kind"] == "tool" and "KeyError" in got["unreadable"]["reason"],
          "a test the runner raised on while reading it: tool: %s" % got["unreadable"])
    check(luma_test.drive_full(OSError(errno.ENOSPC, "x")) and not luma_test.drive_full(OSError(errno.ENOENT, "x"))
          and not luma_test.drive_full(ValueError("x")), "drive_full tells a full drive from another error")
    check(not (tmp / "run.lock").exists(), "the run lock is freed after a suite whose tests raised")

    # 5. The machine not fit: exit ENV_EXIT, no result.
    lock = tmp / "held.lock"
    lock.write_text("another runner\n", encoding="utf-8")
    old_argv = sys.argv
    sys.argv = ["luma_test.py", "run", str(suite / "suite.json"), "--bin", str(bin_folder), "--work", str(tmp / "work2"),
                "--lock", str(lock), "--renderer", "d3d12"]
    err = io.TextIOWrapper(io.BytesIO(), encoding="utf-8")
    try:
        with redirect_stderr(err), redirect_stdout(io.TextIOWrapper(io.BytesIO(), encoding="utf-8")):
            code = luma_test.main()
            err.flush()
            said = err.buffer.getvalue().decode("utf-8")
    finally:
        sys.argv = old_argv
    check(code == luma_test.ENV_EXIT == 3 and "env: run.lock held" in said,
          "a held run lock: exit %s and its words on stderr: %r" % (code, said))
    check(not (tmp / "work2").exists(), "and no result is written")
    import gate
    check(gate.ENV_EXIT == luma_test.ENV_EXIT, "the gate expects the runner's number")

    # 6. A game that cannot be started.
    bins = tmp / "full-bin"
    bins.mkdir()
    for name in (luma_test.HOST["exes"]["d3d12"], luma_test.HOST["game"], luma_test.HOST["importer"], luma_test.HOST["extension"]):
        (bins / name).write_bytes(b"x")
    version = int((luma_test.TREE / "runtime" / "include" / "galaxy" / "store_version.inc").read_text(encoding="utf-8").strip())
    pak = tmp / "game.pak"
    pak.write_bytes(b"NBSTORE\0" + version.to_bytes(4, "little") + bytes(200))
    test = real["read_test"](suite / "fine")
    test["bare"] = True
    run_a = argparse.Namespace(bin=str(bins), pak=str(pak), pak_for=[], extension=None, half=None, warm=None, diagnostics=None,
                               module="hooks", gpu=None, commit="x", who="t")

    real_popen = subprocess.Popen

    def no_start(command, *a, **k):
        if pathlib.Path(str(command[0])).name != luma_test.HOST["exes"]["d3d12"]:
            return real_popen(command, *a, **k)  # the runner's own helpers start as they do
        raise FileNotFoundError(errno.ENOENT, "the program is not there")

    subprocess.Popen = no_start
    try:
        try:
            luma_test.run_one(test, "d3d12", run_a, tmp / "start-run", None)
            check(False, "a game that could not be started raised nothing")
        except luma_test.HarnessError as error:
            check("the game could not be started" in str(error) and luma_test.kind_of("harness-error") == "tool",
                  "a game that could not be started: harness-error, tool: %s" % error)
        except Exception as error:  # noqa: BLE001
            import traceback
            check(False, "run_one raised %s: %s (not a HarnessError) %s" % (type(error).__name__, error, traceback.format_exc()))
    finally:
        subprocess.Popen = real_popen

    # 7. Played once more after a fault of the tool's.
    suite7 = tmp / "suite7"
    names7 = ["once", "always", "setup-once", "raises-once", "malformed", "refused", "full", "follows", "lost"]  # lost last: none plays after it
    for n in names7:
        (suite7 / n).mkdir(parents=True)
        (suite7 / n / "test.json").write_text(json.dumps(dict(body, name=n)), encoding="utf-8")
    (suite7 / "follows" / "test.json").write_text(json.dumps(dict(body, name="follows", continue_from="malformed")), encoding="utf-8")
    (suite7 / "suite.json").write_text(json.dumps({"harness": 1, "tests": names7}), encoding="utf-8")
    plays = {}

    def fake_run_one7(test, renderer, a, run_folder, previous):
        n = test["name"]
        plays[n] = plays.get(n, 0) + 1
        run_folder.mkdir(parents=True, exist_ok=True)
        (run_folder / "play.txt").write_text(str(plays[n]), encoding="utf-8")
        if n == "setup-once" and plays[n] == 1:
            raise luma_test.SetupFailed("adb push: no answer")
        if n == "raises-once" and plays[n] == 1:
            raise RuntimeError("run-as dev.nebula.galaxy.test gave no folder")
        if n == "malformed":
            raise luma_test.HarnessError("the test names an object the types file lacks")
        if n == "refused":
            raise luma_test.NotRun("needs disc RMGP01: this run's game is RMGE01")
        if n == "lost":
            raise luma_test.DeviceLost("T left adb during the test")
        if n == "full":
            raise OSError(errno.ENOSPC, "No space left on device")
        return {"code": 0, "seconds": 1.0, "frozen": {}, "mods": [], "exe": "x", "starts": 1, "unwatched": None}

    def fake_evaluate7(test, renderer, run_folder, ran):
        n = test["name"]
        if n == "always" or (n == "once" and plays[n] == 1):
            return "output-missing", "the game ended, and 60 s later this was still not there: after", [], []
        return "pass", "", [], []

    luma_test.run_one, luma_test.evaluate = fake_run_one7, fake_evaluate7
    try:
        with redirect_stdout(io.StringIO()):
            luma_test.run_tests(argparse.Namespace(**dict(vars(run_args(tmp / "run7.lock", tmp / "work7")), target=str(suite7 / "suite.json"))))
    finally:
        luma_test.run_one, luma_test.evaluate = real["run_one"], real["evaluate"]
    got7 = {}
    for n in names7:
        f = tmp / "work7" / "runs" / "suite7" / n / "d3d12" / "result.json"
        got7[n] = json.loads(f.read_text(encoding="utf-8")) if f.is_file() else {}
    first = tmp / "work7" / "runs" / "suite7" / "once" / "d3d12-first"
    check(got7["once"].get("kind") == "pass" and got7["once"].get("retried", {}).get("verdict") == "output-missing"
          and got7["once"]["retried"].get("folder") == "d3d12-first" and plays["once"] == 2,
          "a picture that did not come back, then a pass: the pass, naming the first play: %s" % got7["once"])
    check((first / "play.txt").read_text(encoding="utf-8") == "1" if (first / "play.txt").is_file() else False,
          "the first play's folder is kept beside it as d3d12-first")
    check((tmp / "work7" / "runs" / "suite7" / "once" / "d3d12" / "play.txt").read_text(encoding="utf-8") == "2",
          "the result's folder holds the second play")
    check(kinds.read_result(tmp / "work7" / "runs" / "suite7" / "once" / "d3d12" / "result.json", lambda v: "fail")[0] == "pass"
          and "once" in kinds.played_again(tmp / "work7", "suite7", "d3d12"),
          "the gate reads it as a pass that was played again (its judges look at it, then it is flake)")
    check(got7["always"].get("kind") == "tool" and got7["always"].get("verdict") == "output-missing" and plays["always"] == 2
          and got7["always"].get("retried", {}).get("verdict") == "output-missing",
          "the same fault twice: tool, played twice, naming the first: %s" % got7["always"])
    check(got7["setup-once"].get("kind") == "pass" and got7["setup-once"].get("retried", {}).get("verdict") == "setup-failed",
          "a setup that failed once: played once more: %s" % got7["setup-once"])
    check(got7["raises-once"].get("kind") == "pass" and got7["raises-once"].get("retried", {}).get("verdict") == "harness-error"
          and "gave no folder" in got7["raises-once"]["retried"].get("reason", ""),
          "the runner's own exception once (run-as gave no folder): played once more: %s" % got7["raises-once"])
    for n, verdict in (("malformed", "harness-error"), ("refused", "not-run"), ("lost", "device-lost"), ("full", "not-run")):
        check(plays.get(n) == 1 and got7[n].get("verdict") == verdict and "retried" not in got7[n],
              "%s is played once (%s plays): %s" % (n, plays.get(n), got7[n]))
    check("follows" not in plays and got7["follows"].get("verdict") == "setup-failed" and "retried" not in got7["follows"],
          "a continue_from that did not pass is not played: %s" % got7["follows"])

if failures:
    print("harness_kind_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("harness_kind_test.py: pass")
