"""harness_memory_test.py: the harness's memory fact (a test's "memory"), what a soak run (tools/gate/soak.py) watches,
without the game.

python -X utf8 -I tools/test_driver/tests/harness_memory_test.py [--work <folder>]

1. The runner (tools/lumacore/luma_test.py): a test without "memory" writes no memory line; "memory": 10 writes
   `memory 10`; 0, 3601, a fraction, a string and true are harness errors that name the key.
2. The host half (tools/test_driver/extension/harness.cpp with process_memory.cpp, built as harness_wait_test.py builds
   it): `memory 1` writes process.memory at frame 0 with the seconds since frame 0 and the process's own bytes (not
   zero); no memory line writes none; `memory 0`, `memory 3601` and a bare `memory`
   are refused (the run does not start).
Exit 0 only when every check holds.
"""
import json
import pathlib
import subprocess
import sys
import tempfile

TREE = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(TREE / "tools" / "lumacore"))
sys.path.insert(0, str(TREE / "tools" / "lumacore" / "tests"))
import harness_wait_test  # noqa: E402
import luma_test  # noqa: E402

BASE = {"harness": 1, "name": "t", "about": "a", "bare": True, "steps": [{"at": 10, "end": True}],
        "expect": [{"no_log": "never"}]}


def read(work, name, extra):
    folder = work / name
    folder.mkdir()
    (folder / "test.json").write_text(json.dumps(dict(BASE, **extra)), encoding="utf-8")
    return luma_test.read_test(folder)


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    # 1. The runner.
    plain = read(work, "plain", {})
    check(plain.get("memory", "absent") is None, "a test without the key has no memory: %r" % plain.get("memory", "absent"))
    check(not any(l.startswith("memory") for l in luma_test.plan_text(plain, work / "out").splitlines()),
          "a plan without the key has no memory line")
    every = read(work, "every", {"memory": 10})
    check(every.get("memory") == 10 and "memory 10" in luma_test.plan_text(every, work / "out").splitlines(),
          "\"memory\": 10 writes `memory 10`")
    for n, bad in enumerate((0, 3601, 1.5, "10", True)):
        try:
            read(work, "bad%d" % n, {"memory": bad})
            failed.append("no harness error for memory %r" % (bad,))
        except luma_test.HarnessError as error:
            check("memory" in str(error), "the error for memory %r names the key: %s" % (bad, error))

    # 2. The host half.
    exe = harness_wait_test.build(work)
    for name, line, want in (("none", None, 0), ("one", "memory 1", 1), ("zero", "memory 0", None),
                             ("long", "memory 3601", None), ("empty", "memory", None)):
        out = work / ("half-" + name)
        out.mkdir()
        plan = out / "plan.txt"
        plan.write_text("harness 1\nout %s\nbare 1\n%sstep 5 end\n" % (out.as_posix(), line + "\n" if line else ""),
                        encoding="utf-8")
        result = subprocess.run([str(exe), str(plan), "10"], capture_output=True, text=True)
        facts_file = out / "facts.jsonl"
        facts = [json.loads(l) for l in facts_file.read_text(encoding="utf-8").splitlines() if l] if facts_file.is_file() else []
        memory = [f for f in facts if f["key"] == "process.memory"]
        if want is None:
            check(result.returncode != 0 and not memory, "`%s` is refused: exit %d, %d facts" % (line, result.returncode, len(memory)))
            continue
        check(result.returncode == 0, "%s: exit %d (%s)" % (line, result.returncode, result.stderr.strip()[-200:]))
        check(len(memory) >= want and (want or not memory), "%s: %d process.memory facts" % (line, len(memory)))
        if want and memory:
            first = memory[0]
            v = first["value"]
            check(first["frame"] == 0, "the first is at frame 0: %r" % first)
            check(isinstance(v.get("seconds"), (int, float)) and 0 <= v["seconds"] < 1, "seconds from frame 0: %r" % v)
            check(isinstance(v.get("bytes"), int) and v["bytes"] > 0, "the process's own bytes: %r" % v)

    for f in failed:
        print("FAIL", f)
    print("harness_memory_test: %s" % ("pass" if not failed else "%d failed" % len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
