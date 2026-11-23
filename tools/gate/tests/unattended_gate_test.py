"""unattended_gate_test.py: what the gate and its tools do so that a run nobody watches does not stop (brief U, sections B, E and 6).

    python -X utf8 -I tools/gate/tests/unattended_gate_test.py [--work FOLDER]

Each test was seen to fail with the thing it guards broken (named in a phrase after the test).
  plant_branches_never_land             `gate.py land plant/...` queued the branch; the push guard let a plant out; Landing.land took it
  no_prompt_and_no_crash_box            the environment of git lacked GIT_TERMINAL_PROMPT=0 / GCM_INTERACTIVE=never; the error mode was
                                        not set (a child that crashes must end with its exception code, at once, with no box)
  measure_wait_stop                     a measure that still runs after the minutes: exit 3 and nothing started with --stop; 0 without it
  a_device_lock_of_an_ended_process_is_taken_over    the gate's and the runner's tablet lock held for ever by a process that ended;
                                        a lock taken by hand was taken
  the_gates_tablet_lock_names_its_holder   the holder line carries the pid of the process that took it
  a_flake_opens_a_repair                the third flake did not open a repair; the repair's check; the run folder kept whole
  tablet_owed_is_recorded_and_cleared   a landing that owed the tablet left no line; one that played it left the file
  the_agents_commands_wait_for_a_measure   build_windows.ps1 and luma_test.py went on after the minutes
"""
import contextlib
import datetime
import io
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(TREE / "tools" / "gate"))
sys.path.insert(0, str(TREE / "tools" / "lumacore"))
sys.path.insert(0, str(TREE / "tools" / "plan"))
import gate  # noqa: E402
import kinds  # noqa: E402
import landing  # noqa: E402
import lockholder  # noqa: E402
import main_push_guard  # noqa: E402
import measure_wait  # noqa: E402
import noprompt  # noqa: E402
import stress  # noqa: E402

ZERO = "0" * 40


def ended_pid():
    """The id of a process that has ended."""
    p = subprocess.Popen([sys.executable, "-X", "utf8", "-I", "-c", "pass"])
    p.wait()
    return p.pid


class PlantTest(unittest.TestCase):
    def test_plant_branches_never_land(self):
        self.assertEqual(landing.plant_branches(["plant/M-001-1", "native/S0.1.1", "mend/M-001", "plant/7"]), ["plant/M-001-1", "plant/7"])
        with tempfile.TemporaryDirectory() as tmp:
            queue = pathlib.Path(tmp) / "queue.tsv"
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                code = gate.land_main(["plant/M-001-1", "--queue-file", str(queue), "--runner-lock", str(pathlib.Path(tmp) / "runner.lock"),
                                       "--live", tmp, "--failures", str(pathlib.Path(tmp) / "f.tsv")])
            self.assertEqual(code, landing.EXIT_NOT_JUDGED)
            self.assertIn("a planted fault and never lands", err.getvalue())
            self.assertIn("VERDICT not-judged", out.getvalue())
            self.assertFalse(queue.exists(), "nothing was queued")
            q = landing.Queue(queue)
            said = []
            runner = landing.Landing(q, mock.Mock(), repo=mock.Mock(), say=said.append)
            self.assertEqual(runner.land(["plant/x"]), landing.EXIT_NOT_JUDGED)
            self.assertFalse(queue.exists(), "the runner's own entry refuses it too")

    def test_the_push_guard_keeps_a_plant_on_the_pc(self):
        line = "refs/heads/plant/M-001-1 %s refs/heads/plant/M-001-1 %s" % ("a" * 40, ZERO)
        self.assertIn("planted fault never leaves", main_push_guard.judge([line], ""))
        self.assertIsNone(main_push_guard.judge(["refs/heads/mend/M-001 %s refs/heads/mend/M-001 %s" % ("a" * 40, ZERO)], ""))
        self.assertIsNotNone(main_push_guard.judge(["refs/heads/main %s refs/heads/main %s" % ("a" * 40, ZERO)], ""), "main stays guarded")


class NoPromptTest(unittest.TestCase):
    def test_no_prompt_and_no_crash_box(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            for key in noprompt.ENV:
                os.environ.pop(key, None)
            mode = noprompt.install()
            self.assertEqual(os.environ["GIT_TERMINAL_PROMPT"], "0")
            self.assertEqual(os.environ["GCM_INTERACTIVE"], "never")
        if os.name != "nt":
            return
        self.assertEqual(mode & noprompt.MODE, noprompt.MODE, "the error mode is set")
        began = time.time()
        child = subprocess.run([sys.executable, "-X", "utf8", "-I", "-c", "import os; os.abort()"], capture_output=True, timeout=60)
        self.assertEqual(child.returncode & 0xFFFFFFFF, 0xC0000409, "the child that crashes ends with its exception code")
        self.assertLess(time.time() - began, 30, "in seconds: no box waits for a person")

    def test_every_git_call_of_the_tools_inherits_it(self):
        env = dict(os.environ)
        env.pop("GIT_TERMINAL_PROMPT", None)
        env.pop("GCM_INTERACTIVE", None)
        code = ("import sys; sys.path.insert(0, %r); import noprompt, os; noprompt.install(); "
                "print(os.environ['GIT_TERMINAL_PROMPT'], os.environ['GCM_INTERACTIVE'])" % str(TREE / "tools" / "gate"))
        r = subprocess.run([sys.executable, "-X", "utf8", "-I", "-c", code], capture_output=True, text=True, env=env)
        self.assertEqual(r.stdout.split(), ["0", "never"])
        for name in ("tools/plan/plan.py", "tools/gate/gate.py"):
            self.assertIn("noprompt.install()", (TREE / name).read_text(encoding="utf-8"), "%s calls it first" % name)


class MeasureWaitTest(unittest.TestCase):
    def lock(self, tmp, text):
        folder = pathlib.Path(tmp) / "measure.lock"
        folder.mkdir()
        (folder / "holder.txt").write_text(text + "\n", encoding="utf-8")
        return folder

    def run_wait(self, lock, *extra):
        return subprocess.run([sys.executable, "-X", "utf8", "-I", str(TREE / "tools" / "gate" / "measure_wait.py"), "--lock", str(lock),
                               "--minutes", "0.01", "--every", "0.2", *extra], capture_output=True, text=True, timeout=60)

    def test_measure_wait_stop(self):
        with tempfile.TemporaryDirectory() as tmp:
            lock = self.lock(tmp, "measure pid %d" % os.getpid())
            r = self.run_wait(lock, "--stop")
            self.assertEqual(r.returncode, 3, r.stdout)
            self.assertIn("nothing is started", r.stdout)
            self.assertEqual(self.run_wait(lock).returncode, 0, "without --stop it goes on, as before")
            free = pathlib.Path(tmp) / "none.lock"
            self.assertEqual(self.run_wait(free, "--stop").returncode, 0, "a free lock: nothing to wait for")

    def test_the_agents_commands_wait_for_a_measure(self):
        self.assertEqual(measure_wait.AGENT_MINUTES, 40.0)
        ps = (TREE / "tools" / "gate" / "build_windows.ps1").read_text(encoding="utf-8")
        self.assertIn("[double]$MeasureWaitMinutes = 40", ps)
        self.assertIn("--stop", ps)
        self.assertIn("exit 3", ps)
        luma = (TREE / "tools" / "lumacore" / "luma_test.py").read_text(encoding="utf-8")
        self.assertIn("else measure_wait.AGENT_MINUTES) == \"went on\"", luma)
        self.assertIn("nothing is started", luma)


class LockTest(unittest.TestCase):
    def test_a_device_lock_of_an_ended_process_is_taken_over(self):
        import luma_test_android
        for take in (gate.take_device_lock, luma_test_android.device_lock):
            with tempfile.TemporaryDirectory() as tmp:
                lock = pathlib.Path(tmp) / "tablet.lock"
                lock.mkdir()
                (lock / "holder.txt").write_text("gate main x 2026-11-22 10:00:00 since 1 pid %d\n" % ended_pid(), encoding="utf-8")
                said = io.StringIO()
                with contextlib.redirect_stdout(said):
                    self.assertIsNone(take(str(lock), "a new run"), take.__module__)
                self.assertIn("has ended: taken over", said.getvalue())
                text = (lock / "holder.txt").read_text(encoding="utf-8")
                self.assertIn("a new run", text)
                self.assertEqual(lockholder.parse(text)[0], os.getpid(), "the new holder names its own process")
                self.assertIsNotNone(take(str(lock), "another"), "a lock whose holder runs is held")

    def test_a_lock_taken_by_hand_is_never_taken(self):
        import luma_test_android
        for take in (gate.take_device_lock, luma_test_android.device_lock):
            with tempfile.TemporaryDirectory() as tmp:
                lock = pathlib.Path(tmp) / "tablet.lock"
                lock.mkdir()
                (lock / "holder.txt").write_text("by hand 2026-11-22 10:00:00\n", encoding="utf-8")
                self.assertEqual(take(str(lock), "a run"), "by hand 2026-11-22 10:00:00")
                self.assertTrue(lock.is_dir())

    def test_the_gates_tablet_lock_names_its_holder(self):
        with tempfile.TemporaryDirectory() as tmp:
            lock = pathlib.Path(tmp) / "tablet.lock"
            self.assertIsNone(gate.take_device_lock(str(lock), "gate main"))
            self.assertEqual(lockholder.parse((lock / "holder.txt").read_text(encoding="utf-8"))[0], os.getpid())


class FlakeTest(unittest.TestCase):
    def test_a_flake_opens_a_repair(self):
        found = {"check": "engine/d3d11", "test": "menu-key-icons", "renderer": "d3d11", "message": "picture differs", "count": 3}
        cmd = stress.flake_command("L", found, "abc123", "D:/runs/one")
        self.assertEqual(cmd[cmd.index("--source") + 1], "flake")
        self.assertEqual(cmd[cmd.index("--kind") + 1], "fail", "`next` gives it before a step")
        self.assertEqual(cmd[cmd.index("--check") + 1], "flake:menu-key-icons")
        self.assertEqual(cmd[cmd.index("--renderer") + 1], "d3d11")
        self.assertIn("set aside", cmd[cmd.index("--note") + 1])
        calls, said = [], []

        def run(command, **kw):
            calls.append(command)
            return mock.Mock(returncode=0, stdout="R-007\n", stderr="")

        with tempfile.TemporaryDirectory() as tmp:
            ids = stress.open_flake_repairs("L", tmp, "abc123", [found], run=run, say=said.append)
            self.assertEqual(ids, ["R-007"])
            self.assertTrue((pathlib.Path(tmp) / "keep.txt").is_file(), "the run folder is kept whole: the evidence")
        self.assertIn("R-007", said[0])

    def test_the_gate_opens_one_for_a_test_it_set_aside_only_where_it_has_a_live_folder(self):
        text = (TREE / "tools" / "gate" / "gate.py").read_text(encoding="utf-8")
        self.assertIn('live = getattr(self.args, "live", "")', text)
        self.assertIn("if settler.flake_results and live:", text, "a gate with no live folder (a self-test) writes nothing there")

    def test_the_third_flake_is_a_result_of_the_settler(self):
        self.assertTrue(hasattr(kinds.Settler(pathlib.Path("f"), pathlib.Path("a"), "abc"), "flake_results"))


class TabletOwedTest(unittest.TestCase):
    def test_tablet_owed_is_recorded_and_cleared(self):
        with tempfile.TemporaryDirectory() as tmp:
            owed = [{"check": "android/tablet", "state": "owed"}, {"check": "measure/step/tablet", "state": "owed"},
                    {"check": "measure/step/pc", "state": "owed"}, {"check": "build/windows", "state": "pass"}]
            gate.record_tablet_owed(tmp, "0123456789abc", owed, now=datetime.datetime(2026, 11, 22, 10, 0, 0))
            gate.record_tablet_owed(tmp, "fedcba9876", [{"check": "tour/tablet", "state": "owed"}], now=datetime.datetime(2026, 11, 22, 11, 0, 0))
            lines = (pathlib.Path(tmp) / gate.TABLET_OWED).read_text(encoding="utf-8").splitlines()
            self.assertEqual(lines, ["2026-11-22T10:00:00\t012345678\tandroid/tablet,measure/step/tablet",
                                     "2026-11-22T11:00:00\tfedcba987\ttour/tablet"], "the PC's own measure is not the tablet's")
            gate.record_tablet_owed(tmp, "aaaa", [{"check": "android/tablet", "state": "pass"}])
            self.assertFalse((pathlib.Path(tmp) / gate.TABLET_OWED).exists(), "a landing that played the tablet: it is back")
            gate.record_tablet_owed("", "aaaa", owed)      # no live folder: nothing, no error


class SignInTest(unittest.TestCase):
    def test_a_refused_sign_in_is_the_environment_and_the_page_says_so(self):
        for text in ("fatal: Authentication failed for 'https://github.com/x/y.git/'", "fatal: could not read Username for 'https://github.com': terminal prompts disabled",
                     "remote: Permission denied", "fatal: unable to access 'https://x/': The requested URL returned error: 403"):
            if "Permission denied" in text and "publickey" not in text:
                continue
            self.assertTrue(gate.needs_sign_in(text), text)
        self.assertFalse(gate.needs_sign_in("! [rejected] main -> main (fetch first)"))
        self.assertEqual(gate.not_moved_kind("%s: the push of main was refused (x)" % gate.SIGN_IN), "env", "judged nothing: the branch waits")
        import plan
        pushed = mock.Mock(returncode=1, stdout="", stderr="fatal: Authentication failed for 'https://github.com/x/y.git/'\n")
        with mock.patch.object(plan, "git", return_value=pushed):
            verdict = plan.land_with_gate(pathlib.Path("."), "native/S0.1.1")
        self.assertEqual(verdict.failures[0][:2], ("github/sign-in", "ENV"))
        self.assertIn("GitHub sign-in needed", verdict.failures[0][2])
        with mock.patch.object(plan, "git", return_value=mock.Mock(returncode=1, stdout="", stderr="! [rejected] native/x (stale info)\n")):
            self.assertEqual(plan.land_with_gate(pathlib.Path("."), "native/x").failures[0][:2], ("git/push", "FAIL"), "any other refusal is as before")
        import page
        live = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(live, ignore_errors=True))
        import live as lv
        lv.append_fault(live, 1, "S0.1.1", "ENV", "github/sign-in", "GitHub sign-in needed: fatal: Authentication failed", now=datetime.datetime(2026, 11, 22, 10, 0, 0))
        tree = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(tree, ignore_errors=True))
        (tree / "docs" / "plan" / "native").mkdir(parents=True)
        (tree / "docs" / "plan" / "native" / "steps.tsv").write_text("\t".join(plan.STEP_COLUMNS) + "\n1\tA.1\tA\tt\t\t03-X.md\topen\t\n", encoding="utf-8")
        static, data, landings, faults = plan.page_data(tree, live)
        self.assertEqual(data["signin"], "2026-11-22T10:00:00")
        self.assertIn("GitHub sign-in needed since", page.render(static, data))
        lv.add_landing(live, "A.1", "abc", 5, now=datetime.datetime(2026, 11, 22, 11, 0, 0))
        self.assertNotIn("signin", plan.page_data(tree, live)[1], "a landing after it: signed in again")


def test_names(args):
    """The test names asked for on the command line; the harness's own option --work FOLDER is not one."""
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
        elif a == "--work":
            skip = True
        elif not a.startswith("--work="):
            out.append(a)
    return out


def main():
    result = unittest.main(argv=[sys.argv[0]] + test_names(sys.argv[1:]), exit=False, verbosity=1).result
    print("unattended_gate_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
