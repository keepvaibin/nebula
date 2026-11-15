# SPDX-License-Identifier: GPL-3.0-only
"""python -m unittest tools/telemetry/test_diag_reports.py. The round trip through the game's own sealing and opening
runs when NEBULA_DIAG_EXE names a Nebula executable; the rest needs nothing."""
import csv
import json
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import diag_reports  # noqa: E402

FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "testdata", "benchmark.json")


def bench(gpu, vendor, shared, budget, backend, low, passes, limiter, stutter_cause=None, render_parts=None):
    """The fixture as one device's result: both parts' scenes get `low` as their 1% low (the second scene twice it),
    the GPU passes `passes` (name: ms) with their shares, and `limiter`; with `stutter_cause`, each scene's first slow
    frame becomes a 40 ms stutter with that cause; with `render_parts`, each scene's first slow frame has those."""
    with open(FIXTURE, encoding="ascii") as handle:
        doc = json.load(handle)
    doc["device"].update(gpu=gpu, vendor_id=vendor, shared_memory=shared, gpu_budget_mb=budget)
    doc["backend"] = backend
    total = sum(passes.values())
    for run in doc["runs"]:
        for index, scene in enumerate(run["scenes"]):
            scene["low_1_fps"] = low * (index + 1)
            scene["average_fps"] = low * (index + 1) * 2
            scene["limiter"] = limiter
            scene["gpu_ms"] = {"average": total, "p99": total * 1.5, "worst": total * 2}
            scene["gpu_passes"] = {name: {"average": ms, "p99": ms * 1.5, "share": ms / total}
                                   for name, ms in passes.items()}
            if stutter_cause:
                scene["slowest_frames"][0].update(ms=40.0, cause=stutter_cause)
            if render_parts:
                scene["slowest_frames"][0]["render_parts"] = dict(render_parts)
    return doc


class Summary(unittest.TestCase):
    def setUp(self):
        self.reports = [
            ("a", bench("Intel UHD 600", 0x8086, True, 1024, "d3d12", 40.0, {"scene": 30.0, "bloom": 10.0}, "gpu",
                        "pipeline built")),
            ("b", bench("RTX 3060", 0x10DE, False, 12288, "vulkan", 300.0, {"scene": 3.0, "post": 1.0}, "game thread",
                        render_parts={"texture create": 21.0, "submit": 0.4})),
            ("c", bench("Intel HD 500", 0x8086, True, 1536, "d3d12", 20.0, {"scene": 40.0, "bloom": 10.0}, "gpu")),
        ]

    def test_groups_and_slowest_first(self):
        text, rows = diag_reports.summarise(self.reports)
        self.assertEqual(len(rows), 12)  # three reports, two parts, two scenes each
        self.assertTrue(text.startswith("3 reports, 12 scenes (6 at the standard settings)"), text)
        lines = text.splitlines()
        by = lines.index(next(l for l in lines if l.startswith("By GPU class")))
        groups = lines[by + 1:by + 3]
        self.assertTrue(groups[0].startswith("  Intel integrated <2GB / d3d12: 2 devices, median 1% low 40.0 FPS"),
                        groups[0])
        self.assertIn("limited by gpu in 4 of 4 scenes", groups[0])
        self.assertIn("worst scene CosmosGardenGalaxy, 1% low 20.0 FPS", groups[0])
        self.assertTrue(groups[1].startswith("  NVIDIA discrete 8GB+ / vulkan: 1 devices"), groups[1])
        self.assertIn("limited by game thread", groups[1])
        costs = lines[lines.index(next(l for l in lines if l.startswith("Top costs"))) + 1:]
        self.assertTrue(costs[0].startswith("  Intel integrated <2GB / d3d12: passes scene 78%, bloom 22%"), costs[0])
        self.assertIn("stutters pipeline built 4", costs[0])
        self.assertIn("slowest frames not timed", costs[0])
        self.assertIn("stutters none", costs[1])
        self.assertIn("slowest frames texture create 4", costs[1])
        slowest = lines[lines.index(next(l for l in lines if l.startswith("Slowest"))) + 1:]
        self.assertEqual(len(slowest), 3)
        self.assertIn("Intel HD 500", slowest[0])
        self.assertIn("Intel UHD 600", slowest[1])
        self.assertIn("RTX 3060", slowest[2])
        self.assertIn("(top pass scene), limited by gpu", slowest[0])

    def test_csv(self):
        _, rows = diag_reports.summarise(self.reports)
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "out.csv")
            diag_reports.write_csv(rows, path)
            with open(path, encoding="utf-8") as handle:
                read = list(csv.DictReader(handle))
        self.assertEqual(len(read), 12)
        self.assertNotIn("passes", read[0])
        first = read[0]
        self.assertEqual(first["gpu_class"], "Intel integrated <2GB")
        self.assertEqual(first["part"], "yours")
        self.assertEqual(first["top_pass"], "scene")
        self.assertEqual(first["top_pass_share"], "0.75")
        self.assertEqual(first["stutter_causes"], "pipeline built=1")
        self.assertEqual(first["slow_frame_parts"], "")
        self.assertEqual(first["verdict"], "fits")

    def test_csv_hostile_strings(self):
        reports = [("h", bench('=HYPERLINK("http://x","y")', 0x8086, True, 1024, "d3d12", 30.0, {"scene": 9.0}, "gpu")),
                   ("i", bench("+1\r\n-2\t@3", 0x10DE, False, 8192, "vulkan", 90.0, {"scene": 3.0}, "gpu"))]
        _, rows = diag_reports.summarise(reports)
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "out.csv")
            diag_reports.write_csv(rows, path)
            with open(path, encoding="utf-8", newline="") as handle:
                raw = handle.read()
                handle.seek(0)
                read = list(csv.DictReader(handle))
        gpus = {row["gpu"] for row in read}
        self.assertEqual(gpus, {"'=HYPERLINK(\"http://x\",\"y\")", "'+1-2@3"})
        self.assertEqual(raw.count("\n"), len(read) + 1)  # no line break inside a cell
        for row in read:
            for value in row.values():
                self.assertFalse(value.startswith(diag_reports.FORMULA_LEADS), value)
            self.assertNotEqual(row["vendor_id"][:1], "'")  # numbers stay numbers


class Schema(unittest.TestCase):
    def test_other_schema_left_out(self):
        with open(FIXTURE, encoding="ascii") as handle:
            current = json.load(handle)
        old = dict(current, schema=1)
        with tempfile.TemporaryDirectory() as folder:
            for name, bench_doc in (("a.json", current), ("b.json", old), ("c.json", "text")):
                with open(os.path.join(folder, name), "w", encoding="ascii") as handle:
                    json.dump({"report": 1, "benchmark": bench_doc}, handle)
            with open(os.path.join(folder, "d.json"), "w", encoding="ascii") as handle:
                handle.write("{")
            reports, other = diag_reports.load_reports(folder)
        self.assertEqual([report_id for report_id, _ in reports], ["a"])
        self.assertEqual(other, 2)  # b (schema 1) and c (no result object); d is not a report at all


class Opening(unittest.TestCase):
    def test_junk_and_duplicates(self):
        report = {"report": 1, "benchmark": {"schema": diag_reports.SCHEMA}}
        answers = {
            b"NBDR\x01" + b"a" * 100: ("ok", json.dumps(report)),
            b"NBDR\x01" + b"b" * 100: ("ok", json.dumps(report)),  # the same report sealed again
            b"NBDR\x01" + b"c" * 100: ("junk", "changed after sealing, or a forged report"),
            b"NBDR\x01" + b"d" * 100: ("ok", "not json"),
        }
        with tempfile.TemporaryDirectory() as folder:
            files = {"1.nbdr": b"NBDR\x01" + b"a" * 100, "2.nbdr": b"NBDR\x01" + b"a" * 100,  # replayed
                     "3.nbdr": b"NBDR\x01" + b"b" * 100, "4.nbdr": b"NBDR\x01" + b"c" * 100,
                     "5.nbdr": b"NBDR\x01" + b"d" * 100, "6.nbdr": b"GIF89a" + b"x" * 100,
                     "7.nbdr": b"NBDR\x01", "8.nbdr": b"NBDR\x01" + b"e" * (256 * 1024)}
            os.makedirs(os.path.join(folder, "in", "2026-11-13"))
            for name, data in files.items():
                with open(os.path.join(folder, "in", "2026-11-13", name), "wb") as handle:
                    handle.write(data)

            def opener(path):
                with open(path, "rb") as handle:
                    return answers[handle.read()]

            out = os.path.join(folder, "out")
            counts = diag_reports.open_folder(os.path.join(folder, "in"), out, opener)
            self.assertEqual(counts, {"opened": 1, "junk": 5, "duplicate": 2})
            self.assertEqual(len(os.listdir(out)), 1)
            self.assertEqual(diag_reports.load_reports(out), ([(os.listdir(out)[0][:-5], {"schema": diag_reports.SCHEMA})], 0))


@unittest.skipUnless(os.environ.get("NEBULA_DIAG_EXE"), "NEBULA_DIAG_EXE names no executable")
class RoundTrip(unittest.TestCase):
    """keygen, --save-sealed with the generated public key, open: the game's own code end to end."""

    def test_round_trip(self):
        exe = os.environ["NEBULA_DIAG_EXE"]
        with tempfile.TemporaryDirectory() as folder:
            keys = os.path.join(folder, "keys")
            self.assertEqual(diag_reports.main(["keygen", "--exe", exe, "--out", keys]), 0)
            key = os.path.join(keys, "nebula-diag-private.key")
            self.assertTrue(os.path.isfile(key))
            again = subprocess.run([exe, "--report-keygen", keys], capture_output=True)
            self.assertEqual(again.returncode, 6, "an existing private key is never replaced")
            other_key = key
            ours = subprocess.run([exe, "--report-keygen", os.path.join(folder, "ours")], capture_output=True, text=True)
            self.assertEqual(ours.returncode, 0)
            public = ours.stdout.split("public key: ")[1].split()[0]
            key = os.path.join(folder, "ours", "nebula-diag-private.key")
            inbox = os.path.join(folder, "inbox")
            os.makedirs(inbox)
            for name in ("a.nbdr", "b.nbdr"):
                sealed = subprocess.run([exe, "--send-report", FIXTURE, "--save-sealed", os.path.join(inbox, name),
                                         "--public-key", public], capture_output=True, text=True)
                self.assertEqual(sealed.returncode, 0, sealed.stderr)
            with open(os.path.join(inbox, "a.nbdr"), "rb") as handle:
                data = handle.read()
            with open(os.path.join(inbox, "replayed.nbdr"), "wb") as handle:
                handle.write(data)
            bent = bytearray(data)
            bent[-3] ^= 1
            with open(os.path.join(inbox, "bent.nbdr"), "wb") as handle:
                handle.write(bytes(bent))
            refused = subprocess.run([exe, "--open-report", os.path.join(inbox, "bent.nbdr"), "--key", key],
                                     capture_output=True)
            self.assertEqual(refused.returncode, 7)
            wrong = subprocess.run([exe, "--open-report", os.path.join(inbox, "a.nbdr"), "--key", other_key],
                                   capture_output=True)
            self.assertEqual(wrong.returncode, 7)
            out = os.path.join(folder, "opened")
            counts = diag_reports.open_folder(inbox, out, diag_reports.exe_opener(exe, key))
            # a and b are two seals of one report: one opened, b and the replay are duplicates, bent is junk.
            self.assertEqual(counts, {"opened": 1, "junk": 1, "duplicate": 2})
            with open(FIXTURE, encoding="ascii") as handle:
                self.assertEqual(diag_reports.load_reports(out)[0][0][1], json.load(handle))


if __name__ == "__main__":
    unittest.main()
