"""harness_rate_test.py: a test run at a world rate (the test key `rate`), without the game.

python -X utf8 -I tools/lumacore/tests/harness_rate_test.py

The runner (tools/lumacore/luma_test.py) and the game half's source (tools/test_driver/luma_test/luma_test.c):
- `rate` is 1 to 16000 as the time service takes it and a whole number of steps a frame (1000, 2000 ... 16000); another number, a
  word, a bare test: a harness error that names the key;
- a test with a rate has a first step, at 0, that is the plan line `game rate N`; without one the plan is as before;
- the plan's times are world steps: a step's `at` and an input's `frames` become frames (frame 0 runs step 0, frame f runs steps
  (f - 1) * k + 1 to f * k); one that does not begin or end where a frame does is refused, naming the step; at a rate of 1000 the
  plan is unchanged;
- a plan at 2000 gives the same input at every world step as the same plan at 1000 (the property the rate check stands on);
- the game half takes the line `rate`, sets the rate through the time service, and writes the hash after each world step.
Exit 0 when every case holds.
"""
import json
import pathlib
import re
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
import luma_test  # noqa: E402

END = {"at": 601, "end": True}
BASE = {"harness": 1, "name": "t", "about": "a", "bare": True, "steps": [END], "expect": [{"no_log": "never"}]}


def read(work, name, extra, bare=True, steps=None):
    folder = work / name
    folder.mkdir()
    test = dict(BASE, **extra)
    test["bare"] = bare
    if not bare:
        test["stage"] = {"name": "EggStarGalaxy", "scenario": 1}
    if steps is not None:
        test["steps"] = steps
    (folder / "test.json").write_text(json.dumps(test), encoding="utf-8")
    return luma_test.read_test(folder)


def refused(check, work, name, extra, words, bare=False):
    try:
        read(work, name, extra, bare)
        check(False, "no harness error for %r" % extra)
    except luma_test.HarnessError as error:
        check(all(w in str(error) for w in words), "the error for %r names %s: %s" % (extra, words, error))


def steps_of(text):
    """{step: (action, words)} of a plan's `step` lines: the frame, the words of the line."""
    out = []
    for line in text.splitlines():
        m = re.fullmatch(r"step (\d+) (.*)", line)
        if m:
            out.append((int(m.group(1)), m.group(2)))
    return out


def input_at(plan, steps, rate):
    """The input each world step 0..steps - 1 gets from a plan's lines: {step: [(kind, value)]}. A frame's input holds for the
    steps the frame runs (frame 0: step 0; frame f: (f - 1) * k + 1 to f * k)."""
    k = rate // 1000
    ran = {0: [0]}
    for f in range(1, steps):
        ran[f] = list(range((f - 1) * k + 1, f * k + 1))
    out = {s: [] for s in range(steps)}
    for frame, words in steps_of(plan):
        w = words.split()
        if w[0] == "game":
            continue
        kind = " ".join(w[:-1]) if w[0] in ("hold", "stick", "pointer") else w[0]
        length = int(w[-1]) if w[0] in ("hold", "stick", "pointer", "shake") else 1
        for f in range(frame, frame + length):
            for s in ran.get(f, []):
                if s < steps:
                    out[s].append(kind)
    return out


def cases(check, work):
    # The key.
    plain = read(work, "plain", {}, bare=False)
    check(plain.get("rate") is None and all("rate" not in line for line in luma_test.plan_text(plain, work / "out").splitlines()),
          "a test without the key: no rate and no rate line in the plan")
    steady = read(work, "steady", {"rate": 1000}, bare=False)
    check(steady["rate"] == 1000 and steady["steps"][0]["line"] == "game rate 1000" and steady["steps"][0]["at"] == 0,
          "a rate of 1000 is the first step, at 0, `game rate 1000`: %r" % steady["steps"][:1])
    fast = read(work, "fast", {"rate": 2000}, bare=False)
    check("step 0 game rate 2000" in luma_test.plan_text(fast, work / "out").splitlines(), "the plan says `step 0 game rate 2000`")
    for n, bad in enumerate((0, 16001, -1000, 1500, 500, 1, 2000.0, "2000", True, None, [2000])):
        refused(check, work, "bad%d" % n, {"rate": bad}, ["rate"])
    refused(check, work, "bare", {"rate": 2000}, ["bare", "rate"], bare=True)
    check(read(work, "most", {"rate": 16000}, bare=False, steps=[{"at": 33, "end": True}])["rate"] == 16000, "16000 is the most the time service takes")
    refused(check, work, "range", {"rate": 17000}, ["rate", "1 to 16000"])
    refused(check, work, "whole", {"rate": 1500}, ["rate", "whole number of steps a frame"])

    # The plan's times are world steps.
    plan = [{"at": 0, "hash": "on"}, {"at": 41, "stick": [0.5, 0.0], "frames": 30}, {"at": 71, "hold": ["A"], "frames": 8},
            {"at": 101, "shake": 10}, {"at": 601, "end": True}]
    at2 = read(work, "at2", {"rate": 2000}, bare=False, steps=plan)
    got = [(s["at"], s["line"]) for s in at2["steps"]]
    check(got == [(0, "game rate 2000"), (0, "game hash on"), (21, "stick 0.5 0.0 15"), (36, "hold A 4"), (51, "shake 5"), (301, "end")],
          "world steps become frames at a rate of 2000: %s" % got)
    at1 = read(work, "at1", {"rate": 1000}, bare=False, steps=plan)
    got = [(s["at"], s["line"]) for s in at1["steps"]][1:]
    check(got == [(0, "game hash on"), (41, "stick 0.5 0.0 30"), (71, "hold A 8"), (101, "shake 10"), (601, "end")],
          "at a rate of 1000 the plan is as written: %s" % got)
    at16 = read(work, "at16", {"rate": 16000}, bare=False,
                steps=[{"at": 17, "hold": ["B"], "frames": 32}, {"at": 0, "hash": "on"}, {"at": 49, "end": True}])
    got = [(s["at"], s["line"]) for s in at16["steps"]]
    check((17 // 16 + 1, "hold B 2") in got and (4, "end") in got, "16 steps a frame: step 17 is frame 2, 32 steps are 2 frames: %s" % got)

    # A step that is not where a frame begins, or a length that does not end where a frame ends, is refused with its place.
    def misaligned(name, steps, words):
        try:
            read(work, name, {"rate": 2000}, bare=False, steps=steps + [END])
            check(False, "%s: no harness error" % name)
        except luma_test.HarnessError as error:
            check(all(w in str(error) for w in words), "%s: the error names %s: %s" % (name, words, error))
    misaligned("even-at", [{"at": 40, "hold": ["A"], "frames": 4}], ["steps[0].at", "world step 40", "does not begin a frame"])
    misaligned("odd-length", [{"at": 41, "stick": [0, 1], "frames": 29}], ["steps[0].frames", "world step 70"])
    misaligned("shake", [{"at": 41, "shake": 5}], ["steps[0].shake", "world step 46"])
    misaligned("picture", [{"at": 30, "picture": "arrive"}], ["steps[0].at", "world step 30"])
    misaligned("after-odd", [{"after": {"log": "ready"}, "hold": ["A"], "frames": 3}], ["steps[0].frames", "3 world steps",
                                                                                          "whole number of frames"])
    ok = read(work, "after-even", {"rate": 2000}, bare=False, steps=[{"after": {"log": "ready"}, "hold": ["A"], "frames": 4}, END])
    check([s["line"] for s in ok["steps"]][:2] == ["game rate 2000", "hold A 2"], "a step after a condition holds a whole number of frames")
    first = read(work, "from0", {"rate": 2000}, bare=False, steps=[{"at": 0, "hold": ["A"], "frames": 9}, END])
    check(first["steps"][1]["line"] == "hold A 5", "from step 0, 9 steps are frame 0 and 4 frames: %s" % first["steps"][1])
    misaligned("from0-even", [{"at": 0, "hold": ["A"], "frames": 8}], ["steps[0].frames", "world step 8"])

    # The same input at every world step at both rates: the plan that the check plays.
    steps = [{"at": 0, "hash": "on"}, {"at": 41, "stick": [0.5, 0.0], "frames": 30}, {"at": 71, "hold": ["A", "Z"], "frames": 8},
             {"at": 81, "shake": 10}, {"at": 101, "pointer": [0.25, 0.5], "frames": 6}, END]
    steps4 = [{"at": 0, "hash": "on"}, {"at": 41, "stick": [0.5, 0.0], "frames": 28}, {"at": 69, "hold": ["A", "Z"], "frames": 8},
              {"at": 77, "shake": 8}, {"at": 85, "pointer": [0.25, 0.5], "frames": 4}, {"at": 601, "end": True}]
    per_step = {}
    for name, rate, plan_steps in (("a1", 1000, steps), ("a2", 2000, steps), ("b1", 1000, steps4), ("b4", 4000, steps4)):
        t = read(work, "same" + name, {"rate": rate}, bare=False, steps=plan_steps)
        per_step[name] = input_at(luma_test.plan_text(t, work / "out"), 120, rate)
    for fast, slow, rate in (("a2", "a1", 2000), ("b4", "b1", 4000)):
        check(per_step[fast] == per_step[slow], "a rate of %d gives every world step the input a rate of 1000 gives: first difference at %s" % (
            rate, next((s for s in range(120) if per_step[fast][s] != per_step[slow][s]), None)))
    check(all(any(per_step[n][s] for s in range(120)) for n in per_step) and per_step["a1"] != per_step["b1"], "the comparison saw input")

    # The game half (the source: the test builds nothing).
    c = (TREE / "tools" / "test_driver" / "luma_test" / "luma_test.c").read_text(encoding="utf-8").replace("\r\n", "\n")
    check("#include <luma_time.h>" in c and '{LUMA_IMPORT_SERVICE, LUMA_OPTIONAL, LUMA_TIME_NAME, "1.0", &time_service, 0, 0, 0}' in c,
          "the game half imports the time service, optional (a test with no rate runs on a game without it)")
    check('finish(0, "rate: the game has no time service 1.0");' in c, "a rate asked of a game without the service is a failed line that names it")
    check('LUMA_HOOK_ON("GameSystem::calcAnim", "void()", LUMA_BEFORE, on_step_end)' in c,
          "the hash after each world step hangs on calcAnim, which every world step calls once (the frame's, a rate's extra steps and the skip's)")
    check('same(w.word[0], "rate") && w.count == 2u' in c and "time_service->set_rate(self, (uint32_t)n)" in c
          and "n < (int64_t)LUMA_TIME_RATE_LEAST || n > (int64_t)LUMA_TIME_RATE_MOST" in c,
          "the line `rate N` sets the world's rate through the service, 1 to 16000, and says so when it did not take")
    check("if (hashing && !rate_mode && frame_now >= 0 && frame_now != hashed_frame)" in c
          and "if (hashing && rate_mode && frame_now >= 0) {\n        hash_step(0);" in c,
          "with a rate the hash is written after each world step and not once a frame; without one it is as it was")
    check('put(&o, ",\\"step\\":");\n        put_int(&o, hashed_steps++);' in c, "each hash.step fact of a run with a rate says its step")
    check('fact_int("hash.steps", hashed_steps);' in c, "and the count of steps hashed, as a number a step's `after` can wait for (the rate check ends on it)")


def main():
    failed = []
    work = pathlib.Path(tempfile.mkdtemp())

    def check(ok, what):
        if not ok:
            failed.append(what)

    try:
        cases(check, work)
    except luma_test.HarnessError as error:
        failed.append("the runner refused a test it should read: %s" % error)
    print("\n".join(failed) if failed else "harness rate: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
