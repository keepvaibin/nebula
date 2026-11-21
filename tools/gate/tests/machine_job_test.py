"""machine_job_test.py: the gate's job object (tools/gate/machine.py, package T5), with real small processes and no game.

    python -X utf8 -I tools/gate/tests/machine_job_test.py

  job_object_ends_grandchild_keeps_leaver   a stand-in for the gate puts itself in the run's job object and starts a child
                                  that starts a grandchild and exits, and a program with the leave-the-job flag; when the job
                                  closes the grandchild is gone, the leaver is still there (the test then ends it)
  job_nested_leaver_outlives_both  the same inside another job (a landing's gate under the queue runner's): the program that
                                  left survives the outer job's close
  job_failure_is_one_tool_line     where the job cannot be made or joined the run goes on and one `tool` line says so; where
                                  the leave flag is refused the program is started without it, with one line
  nothing_off_windows              not on Windows nothing is done and nothing is said
  detached_starts_leave_the_job    the nightly run's detached start carries the flag, and no detached
                                  start in the tools is made without start_outliving
  adb_server_started_by_the_gate   no server running: the gate starts it with the flag (an adb program is never run here: a stand-in
                                  is); a server running: nothing is started
  sccache_server_started_by_the_gate   the compiler cache's server is started by the gate with the flag and the build's settings
                                  (a stand-in for sccache), only when the cache is in use and no server answers; the settings equal
                                  build_windows.ps1's; the gate's Windows build asks for it first
Exit 0 when every case holds.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

HERE = pathlib.Path(__file__).resolve().parent
GATE_DIR = HERE.parent
sys.path.insert(0, str(GATE_DIR))
import gate  # noqa: E402
import machine  # noqa: E402

# The stand-in for the gate: joins a job, starts a child that starts a grandchild and exits, starts a leaver, says their ids, and on
# a line of input closes the job (which ends the stand-in too).
STAND_IN = r"""
import subprocess, sys, time
sys.path.insert(0, sys.argv[1])
import machine
job = machine.join_new_job(lambda text: print("WARN " + text, flush=True))
if job is None:
    print("NOJOB", flush=True)
    sys.exit(3)
quiet = dict(stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
sleeper = [sys.executable, "-I", "-c", "import time; time.sleep(120)"]
child = ("import subprocess, sys\n"
         "g = subprocess.Popen([sys.executable, '-I', '-c', 'import time; time.sleep(120)'], stdin=subprocess.DEVNULL, "
         "stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)\n"
         "print(g.pid)\n")
grand = int(subprocess.run([sys.executable, "-I", "-c", child], capture_output=True, text=True).stdout.strip())
leaver = machine.start_outliving(subprocess.Popen, sleeper, lambda text: print("WARN " + text, flush=True), **quiet)
print("READY %d %d" % (grand, leaver.pid), flush=True)
sys.stdin.readline()
job.close()
time.sleep(60)
"""

# The same under another stand-in: the outer one makes its job and starts the inner, which makes its own (nested) job and starts
# a leaver; the outer closes its job on a line of input.
OUTER = r"""
import subprocess, sys, time
sys.path.insert(0, sys.argv[1])
import machine
job = machine.join_new_job(lambda text: print("WARN " + text, flush=True))
if job is None:
    print("NOJOB", flush=True)
    sys.exit(3)
inner = (
    "import subprocess, sys, time\n"
    "sys.path.insert(0, sys.argv[1])\n"
    "import machine\n"
    "job = machine.join_new_job(lambda text: print('WARN ' + text, flush=True))\n"
    "quiet = dict(stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)\n"
    "leaver = machine.start_outliving(subprocess.Popen, [sys.executable, '-I', '-c', 'import time; time.sleep(120)'], "
    "lambda text: print('WARN ' + text, flush=True), **quiet)\n"
    "print('INNER %d' % leaver.pid, flush=True)\n"
    "time.sleep(120)\n")
process = subprocess.Popen([sys.executable, "-I", "-c", inner, sys.argv[1]], stdout=subprocess.PIPE, text=True)
lines = []
while True:
    line = process.stdout.readline()
    lines.append(line.strip())
    if line.startswith("INNER") or not line:
        break
print("READY " + " ".join(l for l in lines if l), flush=True)
sys.stdin.readline()
job.close()
time.sleep(60)
"""


def alive(pid):
    return gate.process_alive(pid)


def end(pid):
    subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)


def wait_gone(pid, seconds=20.0):
    """True when the process is gone within `seconds` (polled: the job's end is the system's, not instant)."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        if not alive(pid):
            return True
        time.sleep(0.1)
    return not alive(pid)


def stand_in(code):
    return subprocess.Popen([sys.executable, "-I", "-c", code, str(GATE_DIR)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)


class JobTests(unittest.TestCase):
    def setUp(self):
        if not machine.NT:
            self.skipTest("Windows' job objects")
        self.cleanup = []
        self.processes = []

    def tearDown(self):
        for pid in self.cleanup:
            end(pid)
        for process in self.processes:
            for pipe in (process.stdin, process.stdout, process.stderr):
                pipe.close()
            process.wait()

    def ready(self, process):
        line = process.stdout.readline().strip()
        self.assertTrue(line.startswith("READY"), "the stand-in said: %r %s" % (line, process.stderr.read() if process.poll() else ""))
        return [int(x) for x in re.findall(r"\d+", line)]

    def test_job_object_ends_grandchild_keeps_leaver(self):
        process = stand_in(STAND_IN)
        self.processes.append(process)
        grand, leaver = self.ready(process)
        self.cleanup += [grand, leaver, process.pid]
        self.assertTrue(alive(grand) and alive(leaver), "the grandchild and the leaver run before the job closes")
        process.stdin.write("close\n")
        process.stdin.flush()
        self.assertTrue(wait_gone(process.pid), "the stand-in, which is in the job, ended with it")
        self.assertTrue(wait_gone(grand), "the grandchild of a child that has exited is gone when the job closes")
        self.assertTrue(alive(leaver), "the program started with the leave-the-job flag is still there")
        end(leaver)
        self.assertTrue(wait_gone(leaver), "the test ends the leaver itself")

    def test_job_nested_leaver_outlives_both(self):
        process = stand_in(OUTER)
        self.processes.append(process)
        ids = self.ready(process)
        leaver = ids[-1]
        self.cleanup += [leaver, process.pid]
        self.assertTrue(alive(leaver))
        process.stdin.write("close\n")
        process.stdin.flush()
        self.assertTrue(wait_gone(process.pid), "the outer stand-in ended with its job")
        self.assertTrue(alive(leaver), "a program that left the inner job (inside the outer one) outlives the outer job's close")
        end(leaver)

    def test_job_failure_is_one_tool_line(self):
        said = []
        real = machine.Job.make
        try:
            def broken():
                raise OSError("access denied")
            machine.Job.make = staticmethod(broken)
            self.assertIsNone(machine.join_new_job(said.append))
        finally:
            machine.Job.make = real
        self.assertEqual(len(said), 1, "one line: %s" % said)
        self.assertTrue(said[0].startswith("gate.py: tool:") and "job object" in said[0], said[0])
        # The leave flag refused (a job around the gate that does not allow it): started without it, one line.
        calls, lines = [], []

        def popen(command, **kw):
            calls.append(kw.get("creationflags", 0))
            if kw.get("creationflags", 0) & machine.CREATE_BREAKAWAY_FROM_JOB:
                error = OSError(13, "access denied")
                error.winerror = 5
                raise error
            return "started"
        self.assertEqual(machine.start_outliving(popen, ["x"], lines.append, creationflags=0x8), "started")
        self.assertEqual(calls, [0x8 | machine.CREATE_BREAKAWAY_FROM_JOB, 0x8], "the flag first, then none")
        self.assertEqual(len(lines), 1)
        self.assertTrue(lines[0].startswith("gate.py: tool:") and "ends with the run" in lines[0], lines[0])

        def other(command, **kw):
            raise OSError(2, "no such file")
        with self.assertRaises(OSError):
            machine.start_outliving(other, ["x"], lines.append)

    def test_nothing_off_windows(self):
        saved = machine.NT
        said = []
        try:
            machine.NT = False
            self.assertIsNone(machine.join_new_job(said.append))
            self.assertEqual(machine.outliving_flags(0x8), 0x8)
            seen = []
            machine.start_outliving(lambda command, **kw: seen.append(kw), ["x"], said.append, start_new_session=True)
            self.assertEqual(seen, [{"start_new_session": True}], "the arguments go through unchanged")
        finally:
            machine.NT = saved
        self.assertEqual(said, [], "nothing is said")

    def test_detached_starts_leave_the_job(self):
        seen = []

        def popen(command, **kw):
            seen.append((command, kw))

            class P:
                pid = 1
            return P()
        with tempfile.TemporaryDirectory() as tmp:
            t = pathlib.Path(tmp)
            args = gate.parser().parse_args(["--main", "--tablet", "T", "--work", str(t), "--gate-lock", str(t / "gate.lock"),
                                             "--long-lock", str(t / "long.lock"), "--board", "", "--status", ""])
            tree = t / "Nebula-long"
            tree.mkdir()
            sha = "b" * 40
            saved = gate.long_due, gate.long_tree_at
            gate.long_due = lambda work, now=None: True
            gate.long_tree_at = lambda *a, **k: None
            try:
                gate.start_long(sha, args, tree=tree, popen=popen)
            finally:
                gate.long_due, gate.long_tree_at = saved
        flagged = [kw.get("creationflags", 0) & machine.CREATE_BREAKAWAY_FROM_JOB for c, kw in seen]
        if machine.NT:
            self.assertEqual(len(seen), 1, "the long run was started")
            self.assertTrue(all(flagged), "each carries the leave-the-job flag: %s" % [hex(kw.get("creationflags", 0)) for c, kw in seen])
        # No detached start (DETACHED_PROCESS | NEW_PROCESS_GROUP) in the tools goes without start_outliving.
        bad = []
        for path in (GATE_DIR.parent).rglob("*.py"):
            if "tests" in path.parts or path.name == "machine.py":
                continue
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
            for i, line in enumerate(lines):
                if re.search(r"0x8 \| 0x200|DETACHED_PROCESS", line) and not line.lstrip().startswith("#"):
                    if "start_outliving" not in "\n".join(lines[i:i + 8]):
                        bad.append("%s:%d" % (path.relative_to(GATE_DIR.parent.parent), i + 1))
        self.assertEqual(bad, [], "a detached start with no start_outliving")

    def test_adb_server_started_by_the_gate(self):
        machine._ADB_READY[0] = False
        started, said = [], []

        class Process:
            def wait(self, timeout=None):
                return 0
        state = {"up": False}

        def popen(command, **kw):
            started.append((command, kw))
            state["up"] = True
            return Process()
        up = lambda: state["up"]   # noqa: E731
        self.assertTrue(machine.ensure_adb_server("adb-here", said.append, up=up, popen=popen, wait=lambda s: None))
        self.assertEqual(len(started), 1)
        self.assertEqual(started[0][0], ["adb-here", "start-server"])
        if machine.NT:
            self.assertTrue(started[0][1]["creationflags"] & machine.CREATE_BREAKAWAY_FROM_JOB, "with the leave-the-job flag")
        # A second ask in the same process, and a server already running: nothing is started.
        started.clear()
        self.assertTrue(machine.ensure_adb_server("adb-here", said.append, up=up, popen=popen))
        machine._ADB_READY[0] = False
        self.assertTrue(machine.ensure_adb_server("adb-here", said.append, up=lambda: True, popen=popen))
        self.assertEqual(started, [])
        # A server that does not come up is False, not a hang.
        machine._ADB_READY[0] = False
        self.assertFalse(machine.ensure_adb_server("adb-here", said.append, up=lambda: False, popen=lambda *a, **k: Process(),
                                                   wait=lambda s: None))
        machine._ADB_READY[0] = False

    def test_sccache_server_started_by_the_gate(self):
        machine._SCCACHE_READY[0] = False
        started, said = [], []

        class Process:
            def wait(self, timeout=None):
                return 0
        state = {"up": False}

        def popen(command, **kw):
            started.append((command, kw))
            state["up"] = True
            return Process()
        env = {"PATH": "x", "SCCACHE_BASEDIRS": "C:/other"}
        up = lambda: state["up"]   # noqa: E731
        self.assertTrue(machine.ensure_sccache_server("sccache-here", said.append, exists=lambda p: True, up=up, popen=popen,
                                                      wait=lambda s: None, env=env))
        self.assertEqual(len(started), 1)
        command, kw = started[0]
        self.assertEqual(command, ["sccache-here", "--start-server"])
        self.assertEqual(kw["env"]["SCCACHE_SERVER_PORT"], "4227")
        self.assertEqual(kw["env"]["SCCACHE_CACHE_SIZE"], "60G")
        self.assertNotIn("SCCACHE_BASEDIRS", kw["env"], "the build takes no path out of what it hashes")
        if machine.NT:
            self.assertTrue(kw["creationflags"] & machine.CREATE_BREAKAWAY_FROM_JOB, "with the leave-the-job flag")
        # Once a process; a server already running; the cache not in use: nothing is started.
        started.clear()
        self.assertTrue(machine.ensure_sccache_server("sccache-here", said.append, exists=lambda p: True, up=up, popen=popen))
        machine._SCCACHE_READY[0] = False
        self.assertTrue(machine.ensure_sccache_server("sccache-here", said.append, exists=lambda p: True, up=lambda: True, popen=popen))
        machine._SCCACHE_READY[0] = False
        self.assertTrue(machine.ensure_sccache_server("none", said.append, exists=lambda p: False, up=lambda: False, popen=popen))
        self.assertEqual(started, [])
        # A server that does not come up is False, one line when it cannot be started.
        machine._SCCACHE_READY[0] = False
        self.assertFalse(machine.ensure_sccache_server("sccache-here", said.append, exists=lambda p: True, up=lambda: False,
                                                       popen=lambda *a, **k: Process(), wait=lambda s: None))
        def broken(*a, **k):
            raise OSError(2, "no such file")
        self.assertFalse(machine.ensure_sccache_server("sccache-here", said.append, exists=lambda p: True, up=lambda: False, popen=broken))
        self.assertTrue(said and said[-1].startswith("gate.py: tool:"))
        machine._SCCACHE_READY[0] = False
        # What the gate starts it with is what the build script would: the same program, folder, size and port.
        text = (GATE_DIR / "build_windows.ps1").read_text(encoding="utf-8")
        self.assertIn('[string]$Sccache = "%s"' % machine.SCCACHE_PROGRAM, text)
        self.assertIn('[string]$SccacheDir = "%s"' % machine.SCCACHE_ENV["SCCACHE_DIR"], text)
        self.assertIn('$env:SCCACHE_CACHE_SIZE = "%s"' % machine.SCCACHE_ENV["SCCACHE_CACHE_SIZE"], text)
        self.assertIn('$env:SCCACHE_SERVER_PORT = "%d"' % machine.SCCACHE_PORT, text)
        # The gate's Windows build asks for the server before the build starts.
        order = []
        saved = machine.ensure_sccache_server, gate.run
        machine.ensure_sccache_server = lambda *a, **k: order.append("server") or True
        gate.run = lambda cmd, log, **k: order.append("build") or 0
        try:
            with tempfile.TemporaryDirectory() as tmp:
                t = pathlib.Path(tmp)
                args = gate.parser().parse_args(["--quick", "--work", str(t), "--build-root", str(t / "b"), "--status", "", "--board", ""])
                g = gate.Gate(args, dry=True)
                log = t / "build.log"
                log.write_text("", encoding="utf-8")
                gate.build_windows(g)(log)
                shutil.rmtree(g.work, ignore_errors=True)
        finally:
            machine.ensure_sccache_server, gate.run = saved
        self.assertEqual(order, ["server", "build"])


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("machine_job_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
