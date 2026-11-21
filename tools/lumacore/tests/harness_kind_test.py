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

if failures:
    print("harness_kind_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("harness_kind_test.py: pass")
