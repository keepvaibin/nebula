"""harness_ends_test.py: a test's "ends", "crash-at-stop" above all, without the game.

python -X utf8 -I tools/lumacore/tests/harness_ends_test.py [--work <folder>]

Judges run folders written here (run.log, facts.jsonl, a dump) with luma_test.evaluate:
- crash-at-stop: the end step reached, then a crash line, a dump and a code that is not 0: every check passes and the
  verdict is a pass;
- crash-at-stop with a crash before the end step: the verdict is "crashed";
- crash-at-stop that stopped cleanly: its crash checks fail;
- crash-at-stop without a "crash: " log expectation, or an unknown ends: refused when the test is read;
- ends "end" with a crash after the end step: still "crashed";
- ends "crash": a crash before the end step passes its checks, as before.
Exit 0 when every case holds.
"""
import json
import pathlib
import platform
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import luma_test  # noqa: E402

CRASH = "[   1.250] crash: access violation at 0x0000000000000010"
CRASH_CODE = -11 if platform.system() != "Windows" else 0xC0000005


def write_test(folder, ends, expect_crash=True):
    folder.mkdir()
    expect = [{"log": "crash: "}] if expect_crash else []
    (folder / "test.json").write_text(json.dumps({"harness": 1, "name": folder.name, "about": "ends", "bare": True,
                                                  "ends": ends, "steps": [{"at": 10, "end": True}],
                                                  "expect": expect}), encoding="utf-8")


def judge(work, name, ends, log_lines, end_frame, code, dump):
    folder = work / name
    write_test(folder, ends)
    test = luma_test.read_test(folder)
    run = folder / "run"
    run.mkdir()
    (run / "run.log").write_text("\n".join(log_lines) + "\n", encoding="utf-8")
    facts = [{"frame": end_frame, "key": "harness.end", "value": {"frame": end_frame}}] if end_frame is not None else []
    (run / "facts.jsonl").write_text("".join(json.dumps(f) + "\n" for f in facts), encoding="utf-8")
    if dump:
        (run / "nebula-crash.dmp").write_bytes(b"MDMP")
    verdict, _, checks, _ = luma_test.evaluate(test, "d3d12", run, {"code": code, "seconds": 1.0, "mods": []})
    return verdict, {c["check"]: c["result"] for c in checks}


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    verdict, checks = judge(work, "at_stop", "crash-at-stop", ["host: started", CRASH], 10, CRASH_CODE, True)
    check(verdict == "pass", "crash-at-stop after the end step: a pass: %s" % verdict)
    check(checks.get("the run reached its end step") == "pass", "crash-at-stop: end step reached: %s" % checks)
    check(checks.get("the game crashed while it stopped, after its end step") == "pass", "crash-at-stop: crash: %s" % checks)
    check(all(r == "pass" for r in checks.values()), "crash-at-stop: every check passes: %s" % checks)

    verdict, checks = judge(work, "at_stop_early", "crash-at-stop", ["host: started", CRASH], None, CRASH_CODE, True)
    check(verdict == "crashed", "crash-at-stop before the end step: crashed: %s" % verdict)

    verdict, checks = judge(work, "at_stop_clean", "crash-at-stop", ["host: started"], 10, 0, False)
    check(checks.get("the game crashed while it stopped, after its end step") == "fail",
          "crash-at-stop that stopped cleanly: the crash check fails: %s %s" % (verdict, checks))

    verdict, checks = judge(work, "end_then_crash", "end", ["host: started", CRASH], 10, CRASH_CODE, True)
    check(verdict == "crashed", "ends end with a crash after the end step: crashed: %s" % verdict)

    verdict, checks = judge(work, "crash", "crash", ["host: started", CRASH], None, CRASH_CODE, True)
    check(verdict == "pass" and checks.get("the game ended in a crash before its end step") == "pass",
          "ends crash: passes as before: %s %s" % (verdict, checks))

    for name, ends, expect_crash in (("no_line", "crash-at-stop", False), ("unknown", "crash-later", True)):
        folder = work / name
        write_test(folder, ends, expect_crash)
        try:
            luma_test.read_test(folder)
            check(False, "read_test took ends %s (crash expectation %s)" % (ends, expect_crash))
        except luma_test.HarnessError:
            pass

    print("\n".join(failed) if failed else "harness ends: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
