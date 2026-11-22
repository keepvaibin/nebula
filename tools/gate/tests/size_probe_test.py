"""size_probe_test.py: tools/verify/size_probe.py (P-340) on runs whose answer is known, the game replaced by a fake that writes the
facts of a run: the first step where two window sizes part is named, the actors that differ at that step are listed with the fields
that differ, runs that never part say so and play no detail runs, and the steps only one of the runs has are not compared.

    python -X utf8 -I tools/gate/tests/size_probe_test.py [--work FOLDER]
"""
import contextlib
import io
import json
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "verify"))
import size_probe  # noqa: E402

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def actor(i, name, x):
    return {"i": i, "name": name, "h": "%016x" % (i * 7 + int(x * 1000)), "pos": [x, 0.0, 0.0], "rot": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, 1.0], "vel": [0.0, 0.0, 0.0], "grav": [0.0, -1.0, 0.0], "step": 0}


class Game:
    """What a run at a size leaves: hashes by step (HASHES[size]) and, for a detail run at a step, the actors of that step."""

    def __init__(self, hashes, actors):
        self.hashes, self.actors, self.calls = hashes, actors, []

    def play(self, out, stop, size, renderer, bin_dir, software, paks, detail=None):
        self.calls.append((size, detail, paks))
        folder = out / "run" / "TEST-1" / renderer
        folder.mkdir(parents=True)
        (folder / "result.json").write_text(json.dumps({"verdict": "pass"}), encoding="utf-8")
        lines = []
        for step, h in enumerate(self.hashes[size]):
            lines.append({"frame": step, "key": "hash.step", "value": {"h": h, "actors": 2}})
            if detail and step in detail.values():
                for a in self.actors[size]:
                    lines.append({"frame": step, "key": "hash.actor", "value": a})
        (folder / "facts.jsonl").write_text("\n".join(json.dumps(l) for l in lines) + "\n", encoding="utf-8")
        return folder


def run(game, sizes, tmp):
    size_probe.play = game.play
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        size_probe.main(["--bin", "BIN", "--out", str(tmp), "--stop", "Stage 1", "--sizes"] + sizes + ["--pak", "X.pak"])
    return out.getvalue()


def main():
    big, small = (1280, 720), (640, 360)
    with tempfile.TemporaryDirectory() as t:
        t = pathlib.Path(t)
        # The runs agree to step 2 and part at step 3: one actor moved.
        game = Game({big: ["a", "b", "c", "d", "e"], small: ["a", "b", "c", "X", "Y"]},
                    {big: [actor(0, "Mario", 1.0), actor(1, "Flare", 2.0)], small: [actor(0, "Mario", 1.0), actor(1, "Flare", 2.5)]})
        text = run(game, ["1280x720", "640x360"], t / "one")
        check("640x360 against 1280x720: steps 5 / 5, first step that differs: 3" in text, "the first step that differs is named: %s" % text)
        check("actors that differ at step 3: 1 of 2" in text and "   #1 Flare (pos)" in text, "the actor that differs is listed with its field: %s" % text)
        check(len(game.calls) == 4 and game.calls[2][1] is not None and game.calls[0][2] == "RMGE01=X.pak",
              "two plays for the sizes, two with the detail at that step, the pak named: %s" % game.calls)

        # Runs that never part: said, and no detail runs.
        game = Game({big: ["a", "b", "c"], small: ["a", "b", "c"]}, {big: [], small: []})
        text = run(game, ["1280x720", "640x360"], t / "same")
        check("first step that differs: None" in text and len(game.calls) == 2, "runs that agree: no detail plays: %s" % text)

        # A step only one run has is not compared: the shorter run's steps agree, the longer one has more.
        game = Game({big: ["a", "b", "c", "d"], small: ["a", "b"]}, {big: [], small: []})
        text = run(game, ["1280x720", "640x360"], t / "short")
        check("steps 4 / 2, first step that differs: None" in text, "a step one run lacks is not a difference: %s" % text)

        # The pure parts.
        check(size_probe.first_step({0: 1, 1: 2, 5: 9}, {0: 1, 1: 3, 5: 9}) == 1, "first_step: the first shared step that differs")
        check(size_probe.first_step({0: 1}, {1: 2}) is None, "first_step: no shared step is None")
        differ = size_probe.differing_actors([actor(0, "A", 1.0), actor(1, "B", 2.0)], [actor(0, "A", 1.0), dict(actor(1, "B", 2.0), h="z")])
        check(differ == [(1, "B", ["below 1/1000"])], "an actor whose fields all agree but not its record: below 1/1000: %s" % differ)

    if failures:
        print("size_probe_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("size_probe_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
