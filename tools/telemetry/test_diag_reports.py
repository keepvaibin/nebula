# SPDX-License-Identifier: GPL-3.0-only
"""python -m unittest tools/telemetry/test_diag_reports.py. The round trip through the game's own sealing and opening
runs when NEBULA_DIAG_EXE names a NebulaNative executable; the rest needs nothing."""
import copy
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


def bench(gpu, vendor, shared, budget, backend, p95, passes, game, render):
    with open(FIXTURE, encoding="ascii") as handle:
        doc = json.load(handle)
    doc["device"].update(gpu=gpu, vendor_id=vendor, shared_memory=shared, gpu_budget_mb=budget)
    doc["backend"] = backend
    scene = doc["scenes"][0]
    scene["interval_ms"]["p95"] = p95
    scene["gpu_pass_ms"] = passes
    scene["gpu_ms"] = sum(passes.values())
    scene["cpu_ms"].update(game=game, render=render)
    second = copy.deepcopy(scene)
    second["stage"] = "HeavenlyBeachGalaxy"
    second["interval_ms"]["p95"] = p95 / 2
    doc["scenes"].append(second)
    return doc


class Summary(unittest.TestCase):
    def setUp(self):
        self.reports = [
            ("a", bench("Intel UHD 600", 0x8086, True, 1024, "d3d12", 48.0, {"scene": 30.0, "bloom": 9.0}, 9.0, 12.0)),
            ("b", bench("RTX 3060", 0x10DE, False, 12288, "vulkan", 8.0, {"scene": 3.0, "post": 1.0}, 2.0, 3.0)),
            ("c", bench("Intel HD 500", 0x8086, True, 1536, "d3d12", 60.0, {"scene": 40.0, "bloom": 12.0}, 14.0, 10.0)),
        ]

    def test_groups_and_slowest_first(self):
        text, rows = diag_reports.summarise(self.reports)
        self.assertEqual(len(rows), 6)
        lines = text.splitlines()
        groups = [l for l in lines if l.startswith("  ") and " / " in l]
        self.assertTrue(groups[0].startswith("  Intel integrated <2GB / d3d12: 2 devices"), groups[0])
        self.assertIn("costliest pass scene", groups[0])
        self.assertIn("costliest thread game", groups[0])
        self.assertIn("worst scene CosmosGardenGalaxy", groups[0])
        self.assertTrue(groups[1].startswith("  NVIDIA discrete 8GB+ / vulkan"), groups[1])
        slowest = lines[lines.index(next(l for l in lines if l.startswith("Slowest"))) + 1:]
        self.assertIn("Intel HD 500", slowest[0])
        self.assertIn("Intel UHD 600", slowest[1])
        self.assertIn("RTX 3060", slowest[2])

    def test_csv(self):
        _, rows = diag_reports.summarise(self.reports)
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "out.csv")
            diag_reports.write_csv(rows, path)
            with open(path, encoding="utf-8") as handle:
                read = list(csv.DictReader(handle))
        self.assertEqual(len(read), 6)
        self.assertEqual(read[0]["gpu_class"], "Intel integrated <2GB")
        self.assertEqual(read[0]["top_pass"], "scene")


class Opening(unittest.TestCase):
    def test_junk_and_duplicates(self):
        report = {"report": 1, "benchmark": {"schema": 1}}
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
            self.assertEqual(diag_reports.load_reports(out)[0][1], {"schema": 1})


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
            unsent = subprocess.run([exe, "--send-report", FIXTURE], capture_output=True)
            self.assertEqual(unsent.returncode, 2, "a build without an endpoint refuses to send")
            out = os.path.join(folder, "opened")
            counts = diag_reports.open_folder(inbox, out, diag_reports.exe_opener(exe, key))
            # a and b are two seals of one report: one opened, b and the replay are duplicates, bent is junk.
            self.assertEqual(counts, {"opened": 1, "junk": 1, "duplicate": 2})
            with open(FIXTURE, encoding="ascii") as handle:
                self.assertEqual(diag_reports.load_reports(out)[0][1], json.load(handle))


if __name__ == "__main__":
    unittest.main()
