"""speed_base_test.py: the kept builds of the speed check (tools/gate/speedbase.py) and the gate's reading of a pair, on made-up folders and
made-up result.json and --pending files; no game, no build, no graphics card.

  The kept builds
    make_keeps_the_build        bins copied, game.pak a hard link (a copy where none can be made), pak.path pointing at the folder's own pak,
                                kept.json with commit, time and the data's key, current.txt naming the folder
    make_replaces_the_old       the old base's folder is removed after the new one is whole; a make that fails (no game data) leaves the old one
    a_broken_base_is_said       current.txt naming a folder that is gone, or a folder with no kept.json: a KeptError, never "no base"
    data_key                    the registry's key when the pak is in it, else the folder's name
    show_lines                  commit, time, key, sizes of the base and of the anchor; `none` where there is none
    anchor_history              the first anchor's row has no previous one; a replaced anchor's row names the one before it and the ratios last
                                judged against that one; the file of last ratios is cleared; an anchor that was not judged has `-`
    command_line                gate.py speed-base make, anchor (refused where one exists, until --replace) and show
  Reading a pair (read_pair)
    every_state                 ok, better, accepted: pass, the renderers named, the ratios of judged figures kept; worse: the figures named
                                `renderer.figure` joined by +, the pair's own lines; no-baseline in the table or in a state: uncalibrated; a renderer
                                that did not run: tool, its reason kept; the time limit, a stopped run, no result.json, a state the pair does not have,
                                an exit that is not the states': could-not
    base_side_only              only where every renderer that did not run says `the base side`
    store_difference            a base side's reason that says the game data is of another store version is named, so that it is never
                                taken for a build that cannot run the route; base_side_ended gives one line of what the pair said, cut short
    probe_ran                   the probe's output shows a run that reached the stage only with a number for each figure asked for
Exit 0 when every case holds. The history of the base: remember_base writes made, moved, kept and rebased rows.
"""
import contextlib
import io
import json
import os
import pathlib
import shutil
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import speedbase  # noqa: E402


class World:
    """A temporary folder with a built bin folder, game data and a registry."""

    def __init__(self, test):
        self.t = pathlib.Path(tempfile.mkdtemp())
        test.addCleanup(lambda: shutil.rmtree(self.t, ignore_errors=True))
        self.bins = self.t / "build" / "bins"
        (self.bins / "D3D12").mkdir(parents=True)
        (self.bins / "Nebula.exe").write_bytes(b"exe-bytes")
        (self.bins / "D3D12" / "d.dll").write_bytes(b"dll")
        self.paks = self.t / "paks" / "us-pak-37"
        self.paks.mkdir(parents=True)
        self.pak = self.paks / "game.pak"
        self.pak.write_bytes(b"P" * 5000)
        (self.bins / "pak.path").write_text("%s\n" % self.pak, encoding="ascii")
        self.root = self.t / "speed-base"
        self.registry = self.t / "registry.tsv"
        self.registry.write_text("key\tpath\thow\tmade\tlast used\nRMGE01-37-0f3a9c1d2b4e\t%s\tmade\tx\ty\n" % self.pak, encoding="utf-8")

    def make(self, kind="base", commit="c" * 40, **kw):
        return speedbase.make(kind, self.bins, commit, root=self.root, registry=self.registry, **kw)


class Kept(unittest.TestCase):
    def test_make_keeps_the_build(self):
        w = World(self)
        kept = w.make()
        self.assertEqual(kept.commit, "c" * 40)
        self.assertTrue((kept.bins / "Nebula.exe").is_file() and (kept.bins / "D3D12" / "d.dll").is_file())
        self.assertTrue(kept.pak.is_file())
        self.assertEqual(os.stat(kept.pak).st_ino, os.stat(w.pak).st_ino, "a hard link: the same file")
        w.pak.unlink()
        self.assertEqual(kept.pak.read_bytes(), b"P" * 5000, "the pak folder's clean-up cannot take it away")
        self.assertEqual((kept.bins / "pak.path").read_text(encoding="ascii").strip(), str(kept.pak))
        info = json.loads((kept.folder / "kept.json").read_text(encoding="utf-8"))
        self.assertEqual((info["commit"], info["key"], info["linked"]), ("c" * 40, "RMGE01-37-0f3a9c1d2b4e", True))
        self.assertRegex(info["time"], r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d$")
        self.assertEqual((w.root / "current.txt").read_text(encoding="utf-8").strip(), kept.folder.name)
        again = speedbase.read("base", w.root)
        self.assertEqual((again.commit, again.folder), (kept.commit, kept.folder))
        self.assertIsNone(speedbase.read("anchor", w.root), "no anchor was made")

    def test_a_copy_where_a_link_cannot_be_made(self):
        w = World(self)
        real = os.link

        def refuse(*a, **k):
            raise OSError("no links here")
        os.link = refuse
        try:
            kept = w.make()
        finally:
            os.link = real
        self.assertEqual(kept.pak.read_bytes(), b"P" * 5000)
        self.assertNotEqual(os.stat(kept.pak).st_ino, os.stat(w.pak).st_ino)
        self.assertFalse(kept.info["linked"])
        self.assertIn("a copy", "\n".join(speedbase.show(w.root)))

    def test_make_replaces_the_old(self):
        w = World(self)
        first = w.make(commit="1" * 40)
        second = w.make(commit="2" * 40)
        self.assertFalse(first.folder.exists(), "the old base's folder is removed")
        self.assertEqual(speedbase.read("base", w.root).commit, "2" * 40)
        self.assertEqual([p.name for p in w.root.iterdir() if p.is_dir()], [second.folder.name])
        w.pak.unlink()
        with self.assertRaises(speedbase.KeptError):
            w.make(commit="3" * 40, pak=w.pak)
        self.assertEqual(speedbase.read("base", w.root).commit, "2" * 40, "a make that fails leaves the old base")
        self.assertEqual([p.name for p in w.root.iterdir() if p.is_dir()], [second.folder.name], "and no half-made folder")

    def test_a_broken_base_is_said(self):
        w = World(self)
        kept = w.make()
        shutil.rmtree(kept.folder)
        with self.assertRaises(speedbase.KeptError):
            speedbase.read("base", w.root)
        kept = w.make()
        (kept.folder / "kept.json").unlink()
        with self.assertRaises(speedbase.KeptError):
            speedbase.read("base", w.root)
        self.assertIn("broken", speedbase.show(w.root)[0])

    def test_data_key(self):
        w = World(self)
        self.assertEqual(speedbase.data_key(w.pak, w.registry), "RMGE01-37-0f3a9c1d2b4e")
        other = w.t / "paks" / "a1-pak-pal-40" / "game.pak"
        self.assertEqual(speedbase.data_key(other, w.registry), "a1-pak-pal-40", "not in the registry: the folder's name")
        self.assertEqual(speedbase.data_key(w.pak, w.t / "none.tsv"), "us-pak-37")

    def test_show_lines(self):
        w = World(self)
        self.assertEqual(speedbase.show(w.root), ["base: none", "anchor: none"])
        w.make(commit="1" * 40)
        speedbase.make_anchor(w.bins, "a" * 40, root=w.root, registry=w.registry)
        base, anchor = speedbase.show(w.root)
        self.assertTrue(base.startswith("base: commit 111111111, made 20"), base)
        self.assertIn("game data RMGE01-37-0f3a9c1d2b4e", base)
        self.assertIn("bins 0 MB", base)
        self.assertIn("hard link", base)
        self.assertTrue(anchor.startswith("anchor: commit aaaaaaaaa"), anchor)

    def test_anchor_history(self):
        w = World(self)
        first = speedbase.make_anchor(w.bins, "a" * 40, root=w.root, registry=w.registry)
        rows = speedbase.read_rows(w.root / speedbase.ANCHOR_HISTORY, speedbase.ANCHOR_COLUMNS)
        self.assertEqual([(r["commit"], r["previous"], r["ratios"]) for r in rows], [("a" * 9, "-", "-")])
        speedbase.note_anchor_ratios({"d3d12": {"cpu_work_ms": 1.0123, "gpu_ms": 0.99}, "vulkan": {"render_ms": 1.2}}, "a" * 40, "d" * 40, w.root)
        second = speedbase.make_anchor(w.bins, "b" * 40, root=w.root, registry=w.registry)
        self.assertFalse(first.folder.exists())
        self.assertEqual(speedbase.read("anchor", w.root).commit, "b" * 40)
        rows = speedbase.read_rows(w.root / speedbase.ANCHOR_HISTORY, speedbase.ANCHOR_COLUMNS)
        self.assertEqual((rows[1]["commit"], rows[1]["previous"], rows[1]["judged at"]), ("b" * 9, "a" * 9, "d" * 40))
        self.assertEqual(rows[1]["ratios"], "d3d12.cpu_work_ms=1.0123 d3d12.gpu_ms=0.9900 vulkan.render_ms=1.2000")
        self.assertFalse((w.root / speedbase.ANCHOR_LAST).exists(), "the last ratios belong to the anchor before; the file is cleared")
        third = speedbase.make_anchor(w.bins, "c" * 40, root=w.root, registry=w.registry)
        rows = speedbase.read_rows(w.root / speedbase.ANCHOR_HISTORY, speedbase.ANCHOR_COLUMNS)
        self.assertEqual((rows[2]["previous"], rows[2]["ratios"]), ("b" * 9, "-"), "an anchor that was not judged has no ratios")
        # ratios kept for another anchor are not the new one's
        speedbase.note_anchor_ratios({"d3d12": {"a": 2.0}}, "z" * 40, "d" * 40, w.root)
        speedbase.make_anchor(w.bins, "e" * 40, root=w.root, registry=w.registry)
        rows = speedbase.read_rows(w.root / speedbase.ANCHOR_HISTORY, speedbase.ANCHOR_COLUMNS)
        self.assertEqual(rows[3]["ratios"], "-")
        self.assertTrue(second.folder.parent == third.folder.parent)

    def test_base_history(self):
        w = World(self)
        speedbase.remember_base("made", "1" * 40, None, "by hand", w.root)
        speedbase.remember_base("rebased", "2" * 40, "1" * 40, "the base could not run the route", w.root)
        rows = speedbase.read_rows(w.root / speedbase.BASE_HISTORY, speedbase.BASE_COLUMNS)
        self.assertEqual([(r["event"], r["commit"], r["previous"]) for r in rows], [("made", "1" * 9, "-"), ("rebased", "2" * 9, "1" * 9)])

    def test_command_line(self):
        w = World(self)
        base = ["--bin", str(w.bins), "--commit", "f" * 40, "--root", str(w.root)]
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.assertEqual(speedbase.main(["make"] + base), 0)
            self.assertEqual(speedbase.main(["anchor"] + base), 0)
            self.assertEqual(speedbase.main(["anchor"] + base), 2, "the anchor is made once")
            self.assertEqual(speedbase.main(["anchor"] + base + ["--replace"]), 0)
            self.assertEqual(speedbase.main(["show", "--root", str(w.root)]), 0)
            self.assertEqual(speedbase.main(["make", "--bin", str(w.t / "nothing"), "--commit", "f" * 40, "--root", str(w.root)]), 2)
        self.assertIn("it is made once", err.getvalue())
        self.assertIn("base: commit fffffffff", out.getvalue())
        self.assertIn("anchor: commit fffffffff", out.getvalue())
        rows = speedbase.read_rows(w.root / speedbase.BASE_HISTORY, speedbase.BASE_COLUMNS)
        self.assertEqual([r["event"] for r in rows], ["made"])
        self.assertEqual(len(speedbase.read_rows(w.root / speedbase.ANCHOR_HISTORY, speedbase.ANCHOR_COLUMNS)), 2)


def figure(ratios, judged=True, verdict="ok"):
    return {"value": 1.0, "median": 1.0, "band": 0.05, "judged": judged, "verdict": verdict, "anchor": None, "ratios": ratios}


class Pair(unittest.TestCase):
    """Reading what `measure.py pair` wrote."""

    def put(self, runs, rows=(), exit_code=0, result_exit=None, check="measure/step/pc"):
        t = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(lambda: shutil.rmtree(t, ignore_errors=True))
        if runs is not None:
            (t / "result.json").write_text(json.dumps({"exit": exit_code if result_exit is None else result_exit, "runs": runs}), encoding="utf-8")
        lines = ["check\trenderer\tfigure\tstate\tline"] + ["\t".join([check] + list(r)) for r in rows]
        (t / "pending.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
        return t, t / "pending.tsv"

    def run_of(self, state, reason="", figs=None):
        return {"state": state, "reason": reason, "figures": figs if figs is not None else {"cpu_work_ms": figure([1.01, 1.03, 1.02])}}

    def test_every_state(self):
        for state in ("ok", "better", "accepted"):
            t, pending = self.put({"d3d12": self.run_of(state), "vulkan": self.run_of("ok")},
                                  [("d3d12", "cpu_work_ms", state, "measure/step/pc d3d12 cpu_work_ms %s: x" % state)])
            got = speedbase.read_pair(t, pending, 0, ["d3d12", "vulkan"])
            self.assertEqual(got.kind, "pass", state)
            self.assertIn("d3d12 %s" % state, got.text)
            self.assertIn("vulkan ok", got.text)
            self.assertEqual(got.ratios, {"d3d12": {"cpu_work_ms": 1.02}, "vulkan": {"cpu_work_ms": 1.02}}, "the median of the rounds, judged figures only")
        t, pending = self.put({"d3d12": self.run_of("ok", figs={"step_ms": figure([5.0], judged=False), "gpu_ms": figure([1.0])})})
        self.assertEqual(speedbase.read_pair(t, pending, 0, ["d3d12"]).ratios, {"d3d12": {"gpu_ms": 1.0}}, "a figure that is not judged is not kept")
        t, pending = self.put({"d3d12": self.run_of("accepted")}, [("d3d12", "cpu_work_ms", "accepted", "measure/step/pc d3d12 cpu_work_ms accepted: x; accepted: the owner said")])
        self.assertIn("accepted: the owner said", speedbase.read_pair(t, pending, 0, ["d3d12"]).text)

    def test_worse(self):
        rows = [("d3d12", "gpu_ms", "worse", "measure/step/pc d3d12 gpu_ms worse: base 1.000, candidate 1.5"),
                ("d3d12", "cpu_work_ms", "ok", "measure/step/pc d3d12 cpu_work_ms ok: x"),
                ("vulkan", "render_ms", "worse", "measure/step/pc vulkan render_ms worse: base 2.000, candidate 3.0")]
        t, pending = self.put({"d3d12": self.run_of("worse"), "vulkan": self.run_of("worse")}, rows, exit_code=1)
        got = speedbase.read_pair(t, pending, 1, ["d3d12", "vulkan"])
        self.assertEqual(got.kind, "worse")
        self.assertTrue(got.text.startswith("worse: d3d12.gpu_ms+vulkan.render_ms: measure/step/pc d3d12 gpu_ms worse"), got.text)
        self.assertIn("measure/step/pc vulkan render_ms worse: base 2.000", got.text)
        self.assertNotIn("cpu_work_ms", got.text, "only what is worse")
        # worse and a renderer that did not run: the worse figure is the answer
        t, pending = self.put({"d3d12": self.run_of("worse"), "vulkan": self.run_of("tool", "the cand side ended with exit 1")}, rows[:1], exit_code=2)
        self.assertEqual(speedbase.read_pair(t, pending, 2, ["d3d12", "vulkan"]).kind, "worse")

    def test_uncalibrated(self):
        t, pending = self.put({"d3d12": self.run_of("no-baseline")}, [("d3d12", "gpu_ms", "no-baseline", "measure/step/pc d3d12 gpu_ms no-baseline: x; not calibrated on this PC")])
        got = speedbase.read_pair(t, pending, 0, ["d3d12"])
        self.assertEqual((got.kind, got.text), ("uncalibrated", "not calibrated on this PC: d3d12"))
        t, pending = self.put({"d3d12": self.run_of("ok"), "vulkan": self.run_of("ok")}, [("vulkan", "gpu_ms", "no-baseline", "x")])
        got = speedbase.read_pair(t, pending, 0, ["d3d12", "vulkan"])
        self.assertEqual((got.kind, got.text), ("uncalibrated", "not calibrated on this PC: vulkan"), "a state ok with one figure that has no band")

    def test_tool_and_the_base_side(self):
        base = "the base side ended with exit 1: nothing"
        cand = "the cand side ended with exit 3"
        t, pending = self.put({"d3d12": self.run_of("tool", base, {}), "vulkan": self.run_of("ok")}, exit_code=2)
        got = speedbase.read_pair(t, pending, 2, ["d3d12", "vulkan"])
        self.assertEqual((got.kind, got.tool), ("tool", {"d3d12": base}))
        self.assertTrue(speedbase.base_side_only(got))
        t, pending = self.put({"d3d12": self.run_of("tool", base, {}), "vulkan": self.run_of("tool", cand, {})}, exit_code=2)
        got = speedbase.read_pair(t, pending, 2, ["d3d12", "vulkan"])
        self.assertEqual(sorted(got.tool), ["d3d12", "vulkan"])
        self.assertFalse(speedbase.base_side_only(got), "one renderer's candidate side did not run either")
        t, pending = self.put({"d3d12": self.run_of("tool", "the cand side ended with exit 1", {})}, exit_code=2)
        self.assertFalse(speedbase.base_side_only(speedbase.read_pair(t, pending, 2, ["d3d12"])))
        self.assertFalse(speedbase.base_side_only(speedbase.Reading("pass", "x")), "nothing failed")

    def test_a_store_version_is_no_start_that_failed(self):
        base = "the base side ended with exit 1: measure-pair d3d12 harness-error the game x.pak is of store version 37; this tree's is 39"
        t, pending = self.put({"d3d12": self.run_of("tool", base, {})}, exit_code=2)
        got = speedbase.read_pair(t, pending, 2, ["d3d12"])
        self.assertTrue(speedbase.base_side_only(got) and speedbase.store_difference(got))
        t, pending = self.put({"d3d12": self.run_of("tool", "the base side ended with exit 3: crashed", {})}, exit_code=2)
        got = speedbase.read_pair(t, pending, 2, ["d3d12"])
        self.assertFalse(speedbase.store_difference(got), "a build that crashed is a build that does not start")
        self.assertEqual(speedbase.base_side_ended(got), "d3d12: the base side ended with exit 3: crashed")
        long = speedbase.Reading("tool", "x", {"d3d12": "the base side ended " + "word " * 200})
        self.assertLessEqual(len(speedbase.base_side_ended(long)), speedbase.ENDED_CHARS)
        self.assertTrue(speedbase.base_side_ended(long).endswith("..."))

    def test_could_not(self):
        t, pending = self.put({"d3d12": self.run_of("ok")})
        self.assertEqual(speedbase.read_pair(t, pending, 124, ["d3d12"]).kind, "could-not")
        self.assertEqual(speedbase.read_pair(t, pending, 124, ["d3d12"]).text, "its time limit")
        self.assertEqual(speedbase.read_pair(t, pending, 125, ["d3d12"]).kind, "stopped")
        t, pending = self.put(None)
        got = speedbase.read_pair(t, pending, 2, ["d3d12"])
        self.assertEqual(got.kind, "could-not")
        self.assertIn("no readable result.json for d3d12 (exit 2)", got.text)
        t, pending = self.put({"vulkan": self.run_of("ok")})
        self.assertIn("could-not", speedbase.read_pair(t, pending, 0, ["d3d12"]).kind, "no entry for a renderer asked for")
        t, pending = self.put({"d3d12": self.run_of("great")})
        got = speedbase.read_pair(t, pending, 0, ["d3d12"])
        self.assertEqual((got.kind, got.text), ("could-not", "result.json names the state 'great' for d3d12"))
        t, pending = self.put({"d3d12": self.run_of("ok")})
        self.assertEqual(speedbase.read_pair(t, pending, 1, ["d3d12"]).kind, "could-not", "state ok, exit 1")
        t, pending = self.put({"d3d12": self.run_of("ok")}, exit_code=0, result_exit=3)
        self.assertEqual(speedbase.read_pair(t, pending, 0, ["d3d12"]).kind, "could-not", "result.json's exit is not the program's")
        t, pending = self.put({"d3d12": self.run_of("worse")}, exit_code=1)
        got = speedbase.read_pair(t, pending, 1, ["d3d12"])
        self.assertEqual((got.kind, got.text), ("could-not", "state worse and no figure of the table is worse"))

    def test_probe_ran(self):
        yes = "  steps_over_18  3\n  render_ms      1.5\nmeasure.py pair-probe: d3d12 form, d3d12: missing: gpu_ms\n"
        self.assertTrue(speedbase.probe_ran(yes))
        self.assertFalse(speedbase.probe_ran("measure.py pair-probe: the run ended with exit 1: x\n"))
        self.assertFalse(speedbase.probe_ran("  steps_over_18  3\n  render_ms      -\n"), "a figure the pair requires is missing")
        self.assertFalse(speedbase.probe_ran(""))


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("speed_base_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    sys.exit(0 if result.wasSuccessful() else 1)
