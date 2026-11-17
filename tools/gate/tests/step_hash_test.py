"""step_hash_test.py: tools/verify/step_hash.py on facts whose answer is known: the same runs, a run that differs from a
step on (named with its first actor), a run with no hashes, runs paired by their place in two work folders, and the
gate's replays: recordings kept and checked, a renderer's runs against another's."""
import contextlib
import io
import json
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "verify"))
import step_hash  # noqa: E402


def write_run(folder, hashes, actors=None, verdict=None):
    folder.mkdir(parents=True)
    if verdict:
        (folder / "result.json").write_text(json.dumps({"verdict": verdict, "commit": "c0ffee"}), encoding="utf-8")
    lines = [{"frame": -1, "key": "setup.done", "value": True}]
    for frame, h in enumerate(hashes):
        lines.append({"frame": frame, "key": "hash.step", "value": {"h": h, "actors": 3}})
        for a in (actors or {}).get(frame, []):
            lines.append({"frame": frame, "key": "hash.actor", "value": a})
    (folder / "facts.jsonl").write_text("\n".join(json.dumps(l) for l in lines) + "\n", encoding="utf-8")


def actor(i, name, x):
    return {"i": i, "name": name, "h": "%016x" % (i * 7 + int(x * 1000)), "pos": [x, 0.0, 0.0], "rot": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, 1.0], "vel": [0.0, 0.0, 0.0], "grav": [0.0, -1.0, 0.0], "step": 4}


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
        t = pathlib.Path(tmp)
        write_run(t / "same-a", ["a1", "a2", "a3"])
        write_run(t / "same-b", ["a1", "a2", "a3"])
        same, text = step_hash.compare_runs(t / "same-a", t / "same-b")
        check(same and "3 steps" in text, "the same hashes: the same over 3 steps")

        detail_a = {1: [actor(0, "Mario", 1.0), actor(1, "Kuribo", 2.0)]}
        detail_b = {1: [actor(0, "Mario", 1.0), actor(1, "Kuribo", 2.5)]}
        write_run(t / "diff-a", ["a1", "a2", "a3"], detail_a)
        write_run(t / "diff-b", ["a1", "b2", "b3"], detail_b)
        same, text = step_hash.compare_runs(t / "diff-a", t / "diff-b")
        check(not same and "from step 1 of 3" in text, "a different hash from step 1: named")
        check("#1 Kuribo (pos)" in text, "the first actor that differs and its field: named")

        write_run(t / "short", ["a1", "a2"])
        same, text = step_hash.compare_runs(t / "same-a", t / "short")
        check(not same and "only one run" in text, "a run with fewer steps does not pass")

        (t / "none").mkdir()
        (t / "none" / "facts.jsonl").write_text('{"frame":-1,"key":"setup.done","value":true}\n', encoding="utf-8")
        same, text = step_hash.compare_runs(t / "same-a", t / "none")
        check(not same and "no hash.step" in text, "a run with no hashes does not pass")

        for side, h in (("A", "x2"), ("B", "y2")):
            write_run(t / side / "runs" / "tour" / "001-Stage-1" / "d3d12", ["x1", "x2"])
            write_run(t / side / "runs" / "tour" / "002-Stage-2" / "d3d12", ["x1", h])
        code, failed = step_hash.compare(t / "A", t / "B")
        check(code == 1 and len(failed) == 1 and "002-Stage-2" in failed[0][0], "work folders: runs paired by place")

        # The gate's replays, on two recorded stops of three steps each.
        step_hash.RECORDED_STOPS = ["BattleShipGalaxy 1", "BattleShipGalaxy 2"]
        step_hash.gate_tour.FRAMES = 3
        one, two = step_hash.recorded_tests()
        good = ["r1", "r2", "r3"]

        def tour(name, runs):
            for test, (hashes, verdict) in runs.items():
                write_run(t / name / "runs" / "tour" / test / "d3d12", hashes, verdict=verdict)
            return t / name

        source = tour("source", {one: (good, "pass"), two: (good, "fail")})
        try:
            step_hash.record(source, t / "rec")
            check(False, "record refuses a recorded stop whose run did not pass")
        except SystemExit:
            pass
        source = tour("source2", {one: (good, "pass"), two: (good, "pass")})
        step_hash.record(source, t / "rec")
        check(sorted(p.name for p in (t / "rec").iterdir()) == sorted([one + ".tsv", two + ".tsv"]),
              "record keeps one file a stop")
        check("a build of c0ffee" in (t / "rec" / (one + ".tsv")).read_text(encoding="utf-8"), "the file names its build")
        short = tour("short2", {one: (["r1", "r2"], "pass"), two: (good, "pass")})
        try:
            step_hash.record(short, t / "rec-short")
            check(False, "record refuses a run with steps missing")
        except SystemExit:
            pass

        ok, lines = step_hash.check(tour("same", {one: (good, "pass"), two: (good, "pass")}), recordings=t / "rec")
        check(ok and lines[-1].startswith("2 of 2 the same"), "the same steps as recorded: pass")
        ok, lines = step_hash.check(tour("changed", {one: (good, "pass"), two: (["r1", "x2", "x3"], "pass")}),
                                    recordings=t / "rec")
        check(not ok and "differs from step 1" in lines[-1], "a stop that changed from step 1: named, fails")
        partial = tour("partial", {one: (good, "pass")})
        ok, lines = step_hash.check(partial, recordings=t / "rec")
        check(not ok and "(not run)" in lines[-1], "a recorded stop not run fails the full gate")
        ok, lines = step_hash.check(partial, some=True, recordings=t / "rec")
        check(ok and "1 of 1" in lines[-1], "the quick gate checks the recorded stops it ran")
        failed = tour("failed", {one: (good, "pass"), two: ([], "setup-failed")})
        ok, lines = step_hash.check(failed, recordings=t / "rec")
        check(ok and "(a run did not pass)" in lines[-1], "a run that failed is named, not compared (its tour check fails)")
        ok, lines = step_hash.check(tour("none", {one: ([], "pass"), two: (good, "pass")}), recordings=t / "rec")
        check(not ok and "no hashes" in lines[-1], "a passed run with no hashes fails")

        device = tour("device", {one: (good, "pass"), two: (good, "setup-failed")})
        ok, lines = step_hash.check(device, against=tour("pc", {one: (good, "pass"), two: (good, "pass")}))
        check(ok and lines[-1].startswith("1 of 1") and "not compared" in lines[-1], "another renderer's runs: compared")
        ok, lines = step_hash.check(tour("device2", {one: (["r1", "r2", "y3"], "pass")}), against=t / "pc")
        check(not ok and "differs from step 2" in lines[-1], "a device that steps differently: fails")

    if failures:
        print("step_hash_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("step_hash_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
