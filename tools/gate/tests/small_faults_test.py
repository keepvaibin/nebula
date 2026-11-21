"""small_faults_test.py: two small faults of the test and build tools that package T4 fixes (P-329, P-308), and the
lock rule, without the game, a build or a graphics card.

    python -X utf8 -I tools/gate/tests/small_faults_test.py [--work FOLDER]

  generated_types_read_only_named_sources   P-329: tools/lumacore/gen_types.py reads the instrumented copies' lists of the
                                            units the build names (its compile commands) and not a copy that a source the build
                                            no longer has left in a reused build folder
  site_pipelines_keeps_an_existing_list     P-308: tools/dev/site_pipelines.py reads the kept list inside its `with` block: a merge
                                            into a list that exists no longer stops on a closed file, and keeps what the list holds
  no_lock_for_own_work                      the rule "own work takes the gate lock" is gone from the code: nothing in tools/ asks
                                            for lockrun.py, and a run of the gate's own checks takes only gate.lock
Exit 0 when every case holds.
"""
import importlib
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent.parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402


class GeneratedTypes(unittest.TestCase):
    def load(self):
        sys.path.insert(0, str(TREE / "tools" / "lumacore"))
        saved = sys.argv
        sys.argv = ["gen_types.py"]
        try:
            return importlib.import_module("gen_types")
        finally:
            sys.argv = saved

    def test_only_the_units_the_build_names_are_read(self):
        gen = self.load()
        with tempfile.TemporaryDirectory() as t:
            t = pathlib.Path(t)
            luma = t / "build" / "luma"
            for name in ("System/Kept.cpp", "System/Gone.cpp", "Game/Other.cpp"):
                for suffix in (".types", ".rows", ".numbers", ".globals", ".functions"):
                    p = luma / (name + suffix)
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.write_text("" if suffix == ".globals" else "{}", encoding="utf-8")
            commands = [{"directory": str(t / "build"), "file": str(luma / "System" / "Kept.cpp"), "command": "clang"},
                        {"directory": str(t / "build"), "file": "luma/Game/Other.cpp", "command": "clang"},
                        {"directory": str(t), "file": str(t / "elsewhere.cpp"), "command": "clang"}]
            saved = gen.NAMED
            try:
                gen.name_units(commands, luma)
                for suffix in (".types", ".rows", ".numbers", ".globals"):
                    got = sorted(p.relative_to(luma).as_posix() for p in gen.unit_files(luma, "*" + suffix))
                    self.assertEqual(got, ["Game/Other.cpp" + suffix, "System/Kept.cpp" + suffix], suffix)
                self.assertEqual(sorted(p.name for p in gen.unit_files(luma, "*.types")), ["Kept.cpp.types", "Other.cpp.types"],
                                 "a copy of a source the build no longer has (Gone.cpp) is not read")
                # the merges and the global list read only the named ones
                (luma / "System" / "Gone.cpp.globals").write_text("_stale_symbol\n", encoding="utf-8")
                (luma / "System" / "Kept.cpp.globals").write_text("_kept_symbol\n", encoding="utf-8")
                self.assertEqual(gen.defined_symbols(luma), {"_kept_symbol"})
                # commands that name no copy of this folder (a fixture's): no unit is left out
                gen.name_units([{"directory": str(t), "file": str(t / "elsewhere.cpp"), "command": "clang"}], luma)
                self.assertEqual(len(gen.unit_files(luma, "*.types")), 3)
            finally:
                gen.NAMED = saved


class SitePipelines(unittest.TestCase):
    def test_keeps_an_existing_list(self):
        sys.path.insert(0, str(TREE / "tools" / "dev"))
        site = importlib.import_module("site_pipelines")
        with tempfile.TemporaryDirectory() as t:
            t = pathlib.Path(t)
            written = (site.SITE | 2, site.SITE | 4)           # two written shaders (ids below WRITTEN_LIMIT)
            record = struct.pack("<QQ", *written) + bytes(28 - 16)
            self.assertEqual(len(record), site.RECORD_HEADER)
            kept = t / "site_pipelines.txt"
            kept.write_text(record.hex() + "\n", encoding="ascii")
            cooked = t / "Pipelines.bin"                       # the cook's own list: no record
            cooked.write_bytes(site.MAGIC + struct.pack("<I", 0) + bytes(site.HEADER - 12))
            table = t / "table.tsv"
            table.write_text("", encoding="utf-8")
            with mock.patch.object(site, "OUT", str(kept)), mock.patch.object(site, "TABLE", str(table)), \
                    mock.patch.object(sys, "argv", ["site_pipelines.py", str(cooked)]):
                import contextlib
                import io
                out = io.StringIO()
                with contextlib.redirect_stdout(out):
                    site.main()
            self.assertEqual(kept.read_text(encoding="ascii").split(), [record.hex()], "the list that was there is kept")
            self.assertIn("others 1", out.getvalue())


class NoLockForOwnWork(unittest.TestCase):
    def test_nothing_asks_for_lockrun_and_a_run_takes_only_the_gate_lock(self):
        for path in (TREE / "tools").rglob("*"):
            if path.suffix in (".py", ".ps1", ".sh", ".md") and path.is_file() and path.name != "small_faults_test.py":
                text = path.read_text(encoding="utf-8", errors="replace")
                self.assertNotIn("lockrun.py", text, "%s still names lockrun.py (the rule 'own work takes the gate lock' is gone)" % path)
        self.assertEqual(gate.locks_for("land", [], "G", "L"), ["G"])
        self.assertEqual(gate.locks_for("quick", [], "G", "L"), ["G"])
        self.assertEqual(gate.locks_for("main", [], "G", "L"), ["G"], "one gate run at a time: gate.lock is the runner's")
        self.assertEqual(gate.locks_for("long", [], "G", "L"), ["L"])
        doc = (TREE / "docs" / "plan" / "04-GATE-AND-MAIN.md").read_text(encoding="utf-8")
        self.assertIn("An agent's own build and unit tests in its own work folder take no lock", " ".join(doc.split()))


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("small_faults_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
