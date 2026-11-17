"""step_hash_test.py: tools/verify/step_hash.py on facts whose answer is known: the same runs, a run that differs from a
step on (named with its first actor), a run with no hashes, and runs paired by their place in two work folders."""
import contextlib
import io
import json
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "verify"))
import step_hash  # noqa: E402


def write_run(folder, hashes, actors=None):
    folder.mkdir(parents=True)
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

    if failures:
        print("step_hash_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("step_hash_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
