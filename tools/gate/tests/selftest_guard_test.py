"""selftest_guard_test.py: a self-test can never reach the live state (tools/gate/selftest_guard.py, tools/gate/tests/guarded.py, and the
gate's harness/self check that starts every script through them). No game, no build. Every probe of the live folder is refused by the
guard before anything is read or written; the hook cannot be removed in a process, so each probe runs in a child process.

    python -X utf8 -I tools/gate/tests/selftest_guard_test.py [--work FOLDER]

  live_folder_is_refused_and_logged     a read, a write, a listing, a new folder, a removal, a rename and a copy in D:/NebulaWork/plan: each is
                                        refused with RuntimeError("a self-test reached the live state: <event> <path>") and one line
                                        `<event> <path>` in the log file
  runs_folder_files_only                a file directly in D:/NebulaWork/build/gate-runs is refused, one below a run folder is not (the
                                        child's own error is FileNotFoundError, not the guard's), the folder itself is refused for listing
  paths_are_compared_normalised         upper case, back slashes, bytes and a relative path into the folder are refused; a number (a file
                                        handle), a folder that only starts with the same letters and an ordinary path are not
  nothing_without_the_variable          install_if_asked installs only for NEBULA_SELF_TEST=1
  a_child_tool_is_guarded               a repository tool started as a child process with the variable is guarded (it writes under the live
                                        folder: refused, logged, non-zero exit)
  every_tool_a_test_starts_asks         each tool a self-test starts as a child process calls install_if_asked() first in its main()
  check_fails_a_script_with_a_log       gate.scripts: a script that exits 0 but whose log is not empty fails, the detail names the script and
                                        the first path; a clean script passes; one that exits 1 fails; the variables are in its environment
  runner_lock_option                    gate.py land --runner-lock: the landing runner takes the lock it is given, not the live one
Exit 0 when every case holds.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace

HERE = pathlib.Path(__file__).resolve().parent
GATE = HERE.parent
TREE = GATE.parent.parent
sys.path.insert(0, str(GATE))
import gate  # noqa: E402
import selftest_guard  # noqa: E402

PLAN = "D:/NebulaWork/plan"
RUNS = "D:/NebulaWork/build/gate-runs"


def child(code, env_on=True, log=None):
    """Runs `code` in a child process with the guard installed (NEBULA_SELF_TEST=1 and the log) or not: (exit code, stdout, stderr)."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("NEBULA_SELF_TEST")}
    if env_on:
        env["NEBULA_SELF_TEST"] = "1"
        env["NEBULA_SELF_TEST_LOG"] = str(log)
    full = "import sys\nsys.path.insert(0, %r)\nimport selftest_guard\nselftest_guard.install_if_asked()\n%s" % (str(GATE), code)
    r = subprocess.run([sys.executable, "-X", "utf8", "-I", "-c", full], capture_output=True, text=True, env=env, timeout=60)
    return r.returncode, r.stdout, r.stderr


class Guard(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="selftest-guard-"))
        self.addCleanup(lambda: shutil.rmtree(self.tmp, ignore_errors=True))
        self.log = self.tmp / "guard.log"
        self.log.write_text("", encoding="utf-8")

    def probe(self, code):
        out = child("import os, shutil\ntry:\n    %s\nexcept RuntimeError as e:\n    print('REFUSED ' + str(e))\nexcept BaseException as e:\n"
                    "    print('OTHER ' + type(e).__name__)\n" % code, log=self.log)
        return out[1].strip(), self.log.read_text(encoding="utf-8").splitlines()

    def test_live_folder_is_refused_and_logged(self):
        probes = {
            "open": ("open(%r)" % (PLAN + "/BOARD.md"), "open"),
            "write": ("open(%r, 'w')" % (PLAN + "/native/tool-faults.tsv"), "open"),
            "listdir": ("os.listdir(%r)" % (PLAN + "/native"), "os.listdir"),
            "scandir": ("os.scandir(%r)" % PLAN, "os.scandir"),
            "mkdir": ("os.mkdir(%r)" % (PLAN + "/selftest-probe"), "os.mkdir"),
            "remove": ("os.remove(%r)" % (PLAN + "/landing-queue.tsv"), "os.remove"),
            "rmdir": ("os.rmdir(%r)" % (PLAN + "/gate.lock"), "os.rmdir"),
            "rmtree": ("shutil.rmtree(%r)" % (PLAN + "/native"), "shutil.rmtree"),
            "rename src": ("os.rename(%r, %r)" % (PLAN + "/a", str(self.tmp / "b")), "os.rename"),
            "rename dst": ("os.rename(%r, %r)" % (str(self.tmp / "a"), PLAN + "/b"), "os.rename"),
            "copyfile": ("shutil.copyfile(%r, %r)" % (str(self.tmp / "a"), PLAN + "/c"), "shutil.copyfile"),
        }
        for name, (code, event) in probes.items():
            self.log.write_text("", encoding="utf-8")
            said, logged = self.probe(code)
            self.assertTrue(said.startswith("REFUSED a self-test reached the live state: %s " % event), "%s: %s" % (name, said))
            self.assertEqual(len(logged), 1, "%s: %s" % (name, logged))
            self.assertTrue(logged[0].startswith(event + " ") and "NebulaWork" in logged[0].replace("\\", "/"), "%s: %s" % (name, logged))
            path = logged[0][len(event) + 1:]
            self.assertIn(path, said, "the message carries the path the log has")

    def test_runs_folder_files_only(self):
        said, logged = self.probe("open(%r)" % (RUNS + "/history.tsv"))
        self.assertTrue(said.startswith("REFUSED"), said)
        self.assertEqual(len(logged), 1)
        self.log.write_text("", encoding="utf-8")
        said, logged = self.probe("os.listdir(%r)" % (RUNS + "/selftest-guard-no-such-run-folder"))
        self.assertEqual((said, logged), ("OTHER FileNotFoundError", []), "a run folder is free: the child's own error, not the guard's")
        said, logged = self.probe("open(%r, 'w')" % (RUNS + "/selftest-guard-no-such-run-folder/sub/x.txt"))
        self.assertEqual((said, logged), ("OTHER FileNotFoundError", []))
        said, logged = self.probe("os.listdir(%r)" % RUNS)
        self.assertTrue(said.startswith("REFUSED"), "listing the folder itself reads its history: " + said)

    def test_paths_are_compared_normalised(self):
        r = selftest_guard.reaches_live_state
        for path in ("D:\\NEBULAWORK\\PLAN\\BOARD.md", "d:/nebulawork/plan", "D:/NebulaWork/plan/", "D:/NebulaWork/plan/native/../BOARD.md",
                     b"D:/NebulaWork/plan/x", "D:/NebulaWork/build/gate-runs/history.tsv", "D:\\NebulaWork\\build\\gate-runs\\measure-history.tsv"):
            self.assertTrue(r("open", path), path)
        for path in (3, None, "", "D:/NebulaWork/plan-other/x", "D:/NebulaWork/plan.tsv", "D:/NebulaWork/build/gate-runs/20261121-x/result.json",
                     "D:/NebulaWork/build/gate-runs-old/x", "C:/Users/x/Temp/y", str(self.tmp / "z")):
            self.assertFalse(r("open", path), path)
        os.chdir(str(self.tmp))
        self.assertFalse(r("open", "relative.txt"), "a relative path is made absolute first")
        self.assertTrue(r("os.listdir", RUNS) and not r("open", RUNS), "the runs folder itself: listing is refused, the name alone is not")

    def test_nothing_without_the_variable(self):
        code = "print(selftest_guard._STATE['installed'])"
        self.assertEqual(child(code, env_on=False)[1].strip(), "False")
        self.assertEqual(child(code, env_on=True, log=self.log)[1].strip(), "True")
        env = dict(os.environ, NEBULA_SELF_TEST="0")
        r = subprocess.run([sys.executable, "-X", "utf8", "-I", "-c", "import sys\nsys.path.insert(0, %r)\nimport selftest_guard\n"
                            "selftest_guard.install_if_asked()\nprint(selftest_guard._STATE['installed'])" % str(GATE)],
                           capture_output=True, text=True, env=env)
        self.assertEqual(r.stdout.strip(), "False", "only 1 installs it")

    def test_a_child_tool_is_guarded(self):
        tool = TREE / "tools" / "modding" / "events_list.py"
        env = dict(os.environ, NEBULA_SELF_TEST="1", NEBULA_SELF_TEST_LOG=str(self.log))
        r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(tool), str(TREE), PLAN + "/selftest-guard-probe"], capture_output=True,
                           text=True, env=env, timeout=120)
        logged = self.log.read_text(encoding="utf-8").splitlines()
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("a self-test reached the live state", r.stderr)
        self.assertEqual(len(logged), 1, logged)
        self.assertIn("selftest-guard-probe", logged[0])
        self.assertFalse(pathlib.Path(PLAN + "/selftest-guard-probe").exists(), "nothing was made in the live folder")

    def test_every_tool_a_test_starts_asks(self):
        tools = ["tools/gate/gate.py", "tools/plan/plan.py", "tools/measure/measure.py", "tools/lumacore/luma_test.py",
                 "tools/lumacore/luma_test_android.py", "tools/gate/coverage.py", "tools/gate/pictures.py", "tools/gate/parse_scripts.py",
                 "tools/verify/doc_refs.py", "tools/verify/settings_args.py", "tools/modding/events_list.py",
                 "tools/lumacore/instrument.py", "tools/lumacore/gen_types.py", "tools/lumacore/placement.py",
                 "tools/lumacore/compare_types.py"]
        for rel in tools:
            text = (TREE / rel).read_text(encoding="utf-8")
            at = re.search(r"^def main\(", text, re.M)
            self.assertIsNotNone(at, rel)
            body = text[at.start():at.start() + 1400]
            self.assertIn("selftest_guard.install_if_asked()", body, rel + ": main() asks first")
            # nothing but the optional docstring and the import of the guard comes before it
            before = body[:body.index("selftest_guard.install_if_asked()")]
            self.assertNotIn("argv =", before, rel)

    def test_check_fails_a_script_with_a_log(self):
        work = self.tmp / "work"
        work.mkdir()
        sneaky = self.tmp / "sneaky_test.py"
        sneaky.write_text("import os, sys\ntry:\n    os.listdir(%r)\nexcept RuntimeError:\n    pass\nsys.exit(0)\n" % (PLAN + "/native"), encoding="utf-8")
        clean = self.tmp / "clean_test.py"
        clean.write_text("import os, sys\nsys.exit(0 if os.environ.get('NEBULA_SELF_TEST') == '1' and os.environ.get('NEBULA_SELF_TEST_LOG') "
                         "else 3)\n", encoding="utf-8")
        failing = self.tmp / "failing_test.py"
        failing.write_text("import sys\nsys.exit(1)\n", encoding="utf-8")
        g = SimpleNamespace(work=work)
        passed, detail = gate.scripts(g, [clean], ("--work", "{out}"))(work / "a.log")[:2]
        self.assertTrue(passed, detail)
        passed, detail = gate.scripts(g, [sneaky], ("--work", "{out}"))(work / "b.log")[:2]
        self.assertFalse(passed, "a script that swallowed the guard's exception still fails")
        self.assertIn("sneaky_test", detail)
        self.assertIn("reached the live state: os.listdir", detail)
        self.assertIn("native", detail)
        passed, detail = gate.scripts(g, [failing], ("--work", "{out}"))(work / "c.log")[:2]
        self.assertFalse(passed)
        self.assertNotIn("reached the live state", detail)
        passed, detail = gate.scripts(g, [clean, sneaky, failing], ("--work", "{out}"))(work / "d.log")[:2]
        self.assertEqual(detail.split(";")[0], "1 of 3 pass")
        self.assertTrue((work / (gate.GUARD_LOG % "sneaky_test")).read_text(encoding="utf-8").strip())
        self.assertEqual((work / (gate.GUARD_LOG % "clean_test")).read_text(encoding="utf-8"), "", "each script has its own log")

    def test_runner_lock_option(self):
        args = gate.parser(land=True).parse_args(["docs/x"])
        self.assertEqual(args.runner_lock, str(gate.landing.RUNNER_LOCK), "the default is the live runner lock")
        args = gate.parser(land=True).parse_args(["docs/x", "--runner-lock", str(self.tmp / "own.lock")])
        self.assertEqual(args.runner_lock, str(self.tmp / "own.lock"))
        made = {}
        saved = gate.landing.Landing

        class Stop(Exception):
            pass

        def fake(queue, checker, repo, **kw):
            made.update(kw)
            raise Stop()
        gate.landing.Landing = fake
        try:
            with self.assertRaises(Stop):
                gate.land_main(["docs/x", "--queue-file", str(self.tmp / "q.tsv"), "--failures", str(self.tmp / "f.tsv"),
                                "--live", str(self.tmp / "live"), "--runner-lock", str(self.tmp / "own.lock"),
                                "--landing-tree", str(self.tmp / "tree")])
        finally:
            gate.landing.Landing = saved
        self.assertEqual(made["runner"].path, self.tmp / "own.lock")


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("selftest_guard_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
