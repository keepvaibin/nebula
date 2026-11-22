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
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import judged  # noqa: E402
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
        # A file whose hash lines are the same is left untouched whatever build the run names; one that differs is written.
        before = {p.name: p.read_bytes() for p in (t / "rec").iterdir()}
        again = tour("source3", {one: (good, "pass"), two: (good, "pass")})
        for test in (one, two):
            r = again / "runs" / "tour" / test / "d3d12" / "result.json"
            r.write_text(json.dumps({"verdict": "pass", "commit": "deadbee"}), encoding="utf-8")
        said = io.StringIO()
        with contextlib.redirect_stdout(said):
            step_hash.record(again, t / "rec")
        check({p.name: p.read_bytes() for p in (t / "rec").iterdir()} == before, "the same hashes from another build: no file touched")
        check("2 files left as they were, 0 written" in said.getvalue(), "it says how many it left and wrote: %s" % said.getvalue())
        changed = tour("source4", {one: (good, "pass"), two: (["r1", "r2", "z3"], "pass")})
        for test in (one, two):
            r = changed / "runs" / "tour" / test / "d3d12" / "result.json"
            r.write_text(json.dumps({"verdict": "pass", "commit": "deadbee"}), encoding="utf-8")
        said = io.StringIO()
        with contextlib.redirect_stdout(said):
            step_hash.record(changed, t / "rec")
        check("1 files left as they were, 1 written" in said.getvalue(), "one hash changed: one file written: %s" % said.getvalue())
        check((t / "rec" / (one + ".tsv")).read_bytes() == before[one + ".tsv"], "the file that did not change is as it was")
        check("a build of deadbee" in (t / "rec" / (two + ".tsv")).read_text(encoding="utf-8")
              and "z3" in (t / "rec" / (two + ".tsv")).read_text(encoding="utf-8"), "the file that changed names its new build")
        step_hash.record(source, t / "rec")            # back to the first recording for the checks below
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

        # The window of a run is the recordings' (P-340): a run at another size is named, not compared.
        def sized(name, runs, size, steps=None):
            folder = tour(name, runs)
            for test in runs:
                run = folder / "runs" / "tour" / test / "d3d12"
                (run / "settings").mkdir(parents=True)
                (run / "settings" / "config.ini").write_text("vsync_interval=0\nwindow_width=%d\nwindow_height=%d\n" % size, encoding="utf-8")
                (run / "test.json").write_text(json.dumps({"steps": steps or [{"at": 0, "hash": "on"}]}), encoding="utf-8")
            return folder

        ok, lines, kind = step_hash.check_kind(sized("w1280", {one: (good, "pass"), two: (good, "pass")}, (1280, 720)), recordings=t / "rec")
        check(ok and kind == "pass" and lines[-1].startswith("2 of 2 the same"), "runs at the recordings' size: compared, pass")
        ok, lines, kind = step_hash.check_kind(sized("w640", {one: (good, "pass"), two: (good, "pass")}, (640, 360)), recordings=t / "rec")
        check(not ok and kind == "tool" and "recorded at 1280x720, run at 640x360" in lines[-1],
              "runs at another size: named by size, kind tool: %s" % lines[-1])
        ok, lines, kind = step_hash.check_kind(sized("w640b", {one: (good, "pass"), two: (["r1", "x2", "x3"], "pass")}, (640, 360)),
                                               recordings=t / "rec")
        check(not ok and kind == "tool" and "differs" not in lines[-1] and "recorded at 1280x720, run at 640x360" in lines[-1],
              "another size with other hashes: the size is said, the hashes are not compared: %s" % lines[-1])
        mixed = sized("wmix", {one: (good, "pass"), two: (["r1", "x2", "x3"], "pass")}, (1280, 720))
        (mixed / "runs" / "tour" / one / "d3d12" / "settings" / "config.ini").write_text("window_width=640\nwindow_height=360\n", encoding="utf-8")
        ok, lines, kind = step_hash.check_kind(mixed, recordings=t / "rec")
        check(not ok and kind == "fail" and "differs from step 1" in lines[-1] and "recorded at 1280x720, run at 640x360" in lines[-1],
              "one run at another size and another with other hashes: a fail, both named: %s" % lines[-1])
        stepped = sized("wstep", {one: (good, "pass"), two: (good, "pass")}, (1280, 720), [{"at": 0, "hash": "on"}, {"at": 5, "window": [800, 450]}])
        ok, lines, kind = step_hash.check_kind(stepped, recordings=t / "rec")
        check(not ok and kind == "tool" and "run at 800x450" in lines[-1], "a window step to another size: named: %s" % lines[-1])
        check(step_hash.window_note(t / "same" / "runs" / "tour" / one / "d3d12") == "", "a run folder that names no size: no note")
        try:
            step_hash.record(sized("w640c", {one: (good, "pass"), two: (good, "pass")}, (640, 360)), t / "rec-640")
            check(False, "record refuses runs at another size")
        except SystemExit as stop:
            check("recorded at 1280x720, run at 640x360" in str(stop) and not list((t / "rec-640").glob("*.tsv")),
                  "record names the size it refuses and writes nothing: %s" % stop)
        results = {one: ("pass", ""), two: ("pass", "")}
        out = judged.hash_results(t / "w640", results, recordings=t / "rec")
        check(all(v[0] == "tool" and "recorded at 1280x720, run at 640x360" in v[1] for v in out.values()),
              "the lanes' judge: a play at another size is a tool fault: %s" % out)
        out = judged.hash_results(t / "w1280", results, recordings=t / "rec")
        check(all(v[0] == "pass" for v in out.values()), "the lanes' judge: a play at the recordings' size passes")

    # The gate's replays: a run at another size is kind tool, a hash that differs is kind fail (P-340).
    import types
    import gate
    real_check = step_hash.check_kind
    try:
        with tempfile.TemporaryDirectory() as tmp:
            g = types.SimpleNamespace(work=pathlib.Path(tmp), level="main")
            for answer, kind in (((False, ["a", "1 of 2 the same; not compared, the run's window is not the recordings': x (recorded at 1280x720, run at 640x360)"], "tool"), "tool"),
                                 ((False, ["a", "1 of 2 the same; not: x (differs from step 3)"], "fail"), "fail"),
                                 ((False, ["0 of 0 the same"], "fail"), "tool"),
                                 ((True, ["2 of 2 the same"], "pass"), None)):
                step_hash.check_kind = lambda *a, _answer=answer, **k: _answer
                out = gate.replays(g, "tour", "d3d12")(pathlib.Path(tmp) / "log.txt")
                if kind is None:
                    check(out[0] is True, "the gate's replays pass when the check passes")
                else:
                    check(out[0] is False and out[3] == kind, "the gate's replays: a check of kind %s is %s: %s" % (answer[2], kind, out[3]))
    finally:
        step_hash.check_kind = real_check

    # Values the same to 1/1000: the bits name the float that differs, with both values in full (N-1064).
    import struct
    def bits(*floats):
        return "".join("%08x" % struct.unpack("<I", struct.pack("<f", f))[0] for f in floats)
    rest = [0.0] * 3 + [1.0] * 3 + [0.0] * 3 + [0.0, -1.0, 0.0]
    a = dict(actor(5, "Mario", 1.0), h="aa", bits=bits(1.0, 2.0, 3.0, *rest))
    b = dict(actor(5, "Mario", 1.0), h="bb", bits=bits(1.0, 2.0000002, 3.0, *rest))
    said = step_hash.first_actor([a], [b])
    check(said is not None and "(bits)" in said and "pos.y 2.0 / 2.000000" in said and "pos.x" not in said,
          "a difference below 1/1000 is named from the bits: %s" % said)
    check("below 1/1000" in step_hash.first_actor([dict(a, bits=None)], [dict(b, bits=None)]),
          "without bits it is still said to be below 1/1000")

    if failures:
        print("step_hash_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("step_hash_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
