"""build_steps_test.py: a build step that failed in the compiler cache alone is run again (tools/gate/build_steps.ps1, build_windows.ps1,
gate.py build_windows), and a landing shows each line when it is printed. No game, no compiler, no cache: a fake step is a small
Python process that prints a planted log and exits with a planted code.

    python -X utf8 -I tools/gate/tests/build_steps_test.py [--work FOLDER]

Seen on 2026-11-21: one job of about 4,700, in a build from nothing with the cache's server running throughout, ended with a line of the
cache's own client (`sccache: error: Only one usage of each socket address ... (os error 10048)`) and no compiler message, and the
whole build and the landing were lost.

  cache_error_only_is_built_again       a log whose every FAILED block has an `sccache: error` line and no compiler or linker diagnostic:
                                        the step runs once more, one line says so, the second result is the step's (passes)
  cache_error_twice_is_not_judged       the second run fails the same way: counted as cache-only (the script ends exit 2), not as a failure
  compiler_error_is_never_run_again     a real diagnostic (error C, error LNK, `: error:`, fatal error): one run, a failure
  mix_is_never_run_again                one cache block and one compiler block: one run, a failure
  second_run_with_a_compiler_error      the second run fails with a compiler error: a failure
  block_rule                            a diagnostic line outside a FAILED block (after the next [n/m] line) is not part of the block; no
                                        FAILED block at all is never run again
  script_wiring                         build_windows.ps1 dot-sources build_steps.ps1, passes the commands to both Finish-Step calls and
                                        ends exit 2 with the line the gate reads
  gate_reads_exit_2_as_tool             gate.py build_windows: the not-judged line and exit 2 are kind `tool` (never `fail`); a real failure is
                                        still `fail`; each build run again by itself is one line TOOL in tool-faults.tsv with the could-not text
  vcvars_is_in_front_of_every_start     the step runs in the build environment (a fake vcvars .bat sets a variable the fake step prints), the
                                        first time and the second (the run-again goes through the same Start-Step)
  failure_is_named                      gate.py build_windows: the count is the script's (the sum of the steps' N FAILED) and the detail is
                                        `<step>: <first compiler or linker message>` (MSVC, clang, linker), else the first FAILED line; two
                                        different compile errors are two different texts (the signature of three failures in a row)
  output_is_line_buffered               gate.unbuffer_output: a line printed to a pipe arrives at once (a landing showed nothing for 543 s)
  game_folder_is_configured_again       build_windows.ps1: a game folder whose CMakeCache.txt holds other NEBULA_SANITIZE or NEBULA_COVERAGE values than asked
                                        is configured again (Game-Config-Differs in build_steps.ps1); one that holds them is not; a folder that after
                                        configuring still differs is refused with exit 1 (M-003: a sanitizer long run was refused, never built)
Exit 0 when every case holds.
"""
import json
import os
import pathlib
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
sys.path.insert(0, str(TREE / "tools" / "plan"))
import gate  # noqa: E402
import kinds  # noqa: E402

STEPS = GATE / "build_steps.ps1"
SCCACHE = "sccache: error: Only one usage of each socket address (protocol/network address/port) is normally permitted. (os error 10048)"
CACHE_BLOCK = "FAILED: CMakeFiles/a.dir/a.cpp.obj \n\"D:/sccache.exe\" cl /nologo /c a.cpp\n" + SCCACHE + "\n"
CL_BLOCK = "FAILED: CMakeFiles/b.dir/b.cpp.obj \n\"D:/sccache.exe\" cl /nologo /c b.cpp\nb.cpp(12): error C2065: 'x': undeclared identifier\n"
LNK_BLOCK = "FAILED: nebula.exe \nlink /nologo a.obj\na.obj : error LNK2019: unresolved external symbol y\n"
COLON_BLOCK = "FAILED: c.o \nclang++ -c c.cpp\nc.cpp:3:1: error: expected ';'\n"
FATAL_BLOCK = "FAILED: d.o \nclang++ -c d.cpp\nd.cpp:1:10: fatal error: 'q.h' file not found\n"
STOP = "ninja: build stopped: cannot make progress due to previous errors.\n"


def log(*blocks):
    return "[1/9] Building CXX x\n" + "".join(blocks) + "[7/9] Building CXX y\n" + STOP


FAKE = """import json, os, pathlib, sys
state = pathlib.Path(sys.argv[1])
spec = json.loads((state / "spec.json").read_text(encoding="utf-8"))
counter = state / "runs.txt"
n = len(counter.read_text(encoding="utf-8").split()) if counter.exists() else 0
counter.write_text(counter.read_text(encoding="utf-8") + "x " if counter.exists() else "x ", encoding="utf-8")
text, code = spec[min(n, len(spec) - 1)]
sys.stdout.write("VC=%s\\n" % os.environ.get("FAKE_VC", "unset") + text)
sys.exit(code)
"""


def powershell():
    return shutil.which("powershell.exe") if os.name == "nt" else None


@unittest.skipUnless(powershell(), "the build steps are PowerShell on Windows")
class Steps(unittest.TestCase):
    def step(self, *runs, vcvars=""):
        """Runs `Step` of build_steps.ps1 over a fake command that prints each run's planted log and exits with its code.
        (output lines, runs made, the two counts)."""
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="build-steps-"))
        self.addCleanup(lambda: shutil.rmtree(tmp, ignore_errors=True))
        (tmp / "spec.json").write_text(json.dumps(runs), encoding="utf-8")
        (tmp / "fake.py").write_text(FAKE, encoding="utf-8")
        wrapper = tmp / "wrapper.ps1"
        wrapper.write_text('$ErrorActionPreference = "Stop"\n$vcvars = "%s"\n. "%s"\n' % (vcvars, STEPS.as_posix())
                           + 'Step "msvc" \'"%s" -I "%s" "%s"\' "%s"\n' % (sys.executable, (tmp / "fake.py").as_posix(), tmp.as_posix(),
                                                                       (tmp / "msvc.build.log").as_posix())
                           + '"RESULT failed=$($script:failed) cacheOnly=$($script:cacheOnly)"\n', encoding="utf-8")
        r = subprocess.run([powershell(), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(wrapper)], capture_output=True,
                           text=True, timeout=120)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        lines = r.stdout.splitlines()
        runs_made = len((tmp / "runs.txt").read_text(encoding="utf-8").split()) if (tmp / "runs.txt").exists() else 0
        result = next(l for l in lines if l.startswith("RESULT "))
        counts = dict(kv.split("=") for kv in result[len("RESULT "):].split())
        self.tmp = tmp
        return lines, runs_made, (int(counts["failed"]), int(counts["cacheOnly"]))

    def test_vcvars_is_in_front_of_every_start(self):
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="build-vcvars-"))
        self.addCleanup(lambda: shutil.rmtree(tmp, ignore_errors=True))
        bat = tmp / "vcvars64.bat"
        bat.write_text("@echo off\r\nset FAKE_VC=on\r\n", encoding="ascii")
        lines, runs, counts = self.step((log(CACHE_BLOCK), 1), ("ok\n", 0), vcvars=str(bat))
        self.assertEqual((runs, counts), (2, (0, 0)), lines)
        first = (self.tmp / "msvc.build.log.cache-error").read_text(encoding="utf-8")
        second = (self.tmp / "msvc.build.log").read_text(encoding="utf-8")
        self.assertIn("VC=on", first, "the first start has the build environment")
        self.assertIn("VC=on", second, "and so does the run again")
        lines, runs, counts = self.step(("ok\n", 0))
        self.assertIn("VC=unset", (self.tmp / "msvc.build.log").read_text(encoding="utf-8"), "with no vcvars the variable is not there")

    def test_cache_error_only_is_built_again(self):
        lines, runs, counts = self.step((log(CACHE_BLOCK), 1), ("[1/1] Linking\n", 0))
        self.assertEqual(runs, 2, lines)
        self.assertEqual(counts, (0, 0), lines)
        again = [l for l in lines if "job(s) failed in the compiler cache, not in the compiler; built again" in l]
        self.assertEqual(len(again), 1, lines)
        self.assertTrue(again[0].startswith("msvc: 1 job(s) failed in the compiler cache, not in the compiler; built again"), again)
        self.assertIn("os error 10048", again[0], "the cache's line is in it (the tool-fault record's first error)")
        self.assertIn("msvc: exit 0, 0 FAILED", lines[-2], "the second result is the step's: %s" % lines)
        self.assertTrue((self.tmp / "msvc.build.log.cache-error").is_file(), "the first log is kept")

    def test_two_cache_blocks_are_counted(self):
        lines, runs, counts = self.step((log(CACHE_BLOCK, CACHE_BLOCK), 1), ("ok\n", 0))
        self.assertEqual((runs, counts), (2, (0, 0)))
        self.assertTrue(any(l.startswith("msvc: 2 job(s) failed in the compiler cache") for l in lines), lines)

    def test_cache_error_twice_is_not_judged(self):
        lines, runs, counts = self.step((log(CACHE_BLOCK), 1), (log(CACHE_BLOCK), 1))
        self.assertEqual(runs, 2, lines)
        self.assertEqual(counts, (0, 1), "counted as the cache's, not as a failure: %s" % lines)

    def test_compiler_errors_are_never_run_again(self):
        for block in (CL_BLOCK, LNK_BLOCK, COLON_BLOCK, FATAL_BLOCK):
            lines, runs, counts = self.step((log(block), 1), ("ok\n", 0))
            self.assertEqual((runs, counts), (1, (1, 0)), "%s: %s" % (block.splitlines()[0], lines))
            self.assertFalse(any("built again" in l for l in lines), lines)

    def test_a_mix_is_never_run_again(self):
        for blocks in ((CACHE_BLOCK, CL_BLOCK), (CL_BLOCK, CACHE_BLOCK), (CACHE_BLOCK, LNK_BLOCK)):
            lines, runs, counts = self.step((log(*blocks), 1), ("ok\n", 0))
            self.assertEqual((runs, counts), (1, (1, 0)), lines)

    def test_a_cache_error_beside_a_diagnostic_in_the_same_block_is_a_real_failure(self):
        mixed = "FAILED: e.obj \ncl /c e.cpp\ne.cpp(1): error C2143: syntax error\n" + SCCACHE + "\n"
        lines, runs, counts = self.step((log(mixed), 1))
        self.assertEqual((runs, counts), (1, (1, 0)), lines)

    def test_the_second_run_with_a_compiler_error_is_a_failure(self):
        lines, runs, counts = self.step((log(CACHE_BLOCK), 1), (log(CL_BLOCK), 1))
        self.assertEqual((runs, counts), (2, (1, 0)), lines)

    def test_the_second_run_with_a_cache_error_and_a_compiler_error_is_a_failure(self):
        lines, runs, counts = self.step((log(CACHE_BLOCK), 1), (log(CACHE_BLOCK, CL_BLOCK), 1))
        self.assertEqual((runs, counts), (2, (1, 0)), lines)

    def test_block_rule(self):
        # The diagnostic after the next [n/m] line is some other job's output, not part of the failed job's block.
        outside = "[3/9] Building CXX z\nz.cpp(9): error C2065: 'q': undeclared identifier\n"
        lines, runs, counts = self.step(("[1/9] x\n" + CACHE_BLOCK + outside + STOP, 1), ("ok\n", 0))
        self.assertEqual((runs, counts), (2, (0, 0)), lines)
        lines, runs, counts = self.step(("no FAILED block here\n", 1), ("ok\n", 0))
        self.assertEqual((runs, counts), (1, (1, 0)), "an exit 1 with no failed job is never run again: %s" % lines)
        lines, runs, counts = self.step(("fine\n", 0))
        self.assertEqual((runs, counts), (1, (0, 0)), lines)

    def test_the_script_is_wired(self):
        script = (GATE / "build_windows.ps1").read_text(encoding="utf-8")
        self.assertIn('build_steps.ps1', script)
        self.assertNotIn("function Finish-Step", script, "one definition, in build_steps.ps1")
        for name in ("game", "msvc"):
            line = next(l for l in script.splitlines() if l.startswith('Finish-Step "%s"' % name))
            self.assertIn("--build", line, "the step's command is passed, so it can be run again: " + line)
        self.assertIn('exit 2', script)
        self.assertIn(gate.NOT_JUDGED_BUILD, script, "the line the gate reads")


def vswhere():
    return shutil.which("vswhere.exe") or next(iter(pathlib.Path(os.environ.get("ProgramFiles(x86)", "")).glob("Microsoft Visual Studio/Installer/vswhere.exe")), None)


@unittest.skipUnless(powershell(), "the build script is PowerShell on Windows")
class GameConfig(unittest.TestCase):
    """The game folder's configure: when its CMakeCache.txt holds other sanitizers or another coverage setting than asked, it is
    configured again; after that a folder that still differs is refused (a build never runs with other settings than asked)."""

    def cache(self, tmp, sanitize=None, coverage=None):
        tmp.mkdir(parents=True, exist_ok=True)
        lines = []
        if sanitize is not None:
            lines.append("NEBULA_SANITIZE:STRING=%s" % sanitize)
        if coverage is not None:
            lines.append("NEBULA_COVERAGE:BOOL=%s" % coverage)
        (tmp / "CMakeCache.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    def differs(self, cache, sanitize, coverage):
        """Game-Config-Differs over a planted cache (cache: (sanitize, coverage) cache lines, or None for no CMakeCache.txt)."""
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="build-config-"))
        self.addCleanup(lambda: shutil.rmtree(tmp, ignore_errors=True))
        if cache is not None:
            self.cache(tmp, *cache)
        wrapper = tmp / "wrapper.ps1"
        wrapper.write_text('$ErrorActionPreference = "Stop"\n$vcvars = ""\n. "%s"\n' % STEPS.as_posix()
                           + "$differs = Game-Config-Differs '%s' '%s' %s\n" % (tmp.as_posix(), sanitize, "$true" if coverage else "$false")
                           + '"RESULT $differs"\n',
                           encoding="utf-8")
        r = subprocess.run([powershell(), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(wrapper)], capture_output=True,
                           text=True, timeout=120)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return next(l for l in r.stdout.splitlines() if l.startswith("RESULT ")) == "RESULT True"

    def test_the_settings_asked_for_against_the_cache(self):
        cases = [  # (cache sanitize, cache coverage), asked sanitize, asked coverage, differs
            (("address", "OFF"), "address,undefined", False, True),
            (("address,undefined", "OFF"), "address,undefined", False, False),
            (("address,undefined", "OFF"), "", False, True),
            (("", "OFF"), "address", False, True),
            (("", "OFF"), "", False, False),
            (("", "ON"), "", False, True),
            (("", "OFF"), "", True, True),
            (("", "ON"), "", True, False),
            (("address", "ON"), "address", True, False),
            (("address", "ON"), "address,undefined", True, True),
            ((None, None), "", False, False),
            ((None, None), "address", False, True),
            ((None, None), "", True, True),
            (None, "", False, False),
            (None, "address", False, True),
        ]
        for cache, sanitize, coverage, want in cases:
            self.assertEqual(self.differs(cache, sanitize, coverage), want, "cache %r, asked %r coverage %r" % (cache, sanitize, coverage))

    def run_script(self, cache, sanitize="", coverage=False):
        """build_windows.ps1 over a planted tree and a game folder with that cache, up to the game configure (the real cmake fails
        on the empty native folder at once; nothing is compiled). (output, exit code, the game folder's configure log is there)."""
        if not (vswhere() and shutil.which("ninja")):
            self.skipTest("no Visual Studio or ninja here")
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="build-config-run-"))
        self.addCleanup(lambda: shutil.rmtree(tmp, ignore_errors=True))
        (tmp / "tree" / "native").mkdir(parents=True)
        self.cache(tmp / "root" / "game", *cache)
        (tmp / "root" / "game" / "build.ninja").write_text("\n", encoding="utf-8")
        env = dict(os.environ, NEBULA_NO_MEASURE_WAIT="1")
        cmd = [powershell(), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(GATE / "build_windows.ps1"), "-Tree", str(tmp / "tree"),
               "-Root", str(tmp / "root"), "-Bin", str(tmp / "bin"), "-Sanitize", sanitize, "-Sccache", ""]
        if coverage:
            cmd.append("-Coverage")
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300, env=env)
        return r.stdout + r.stderr, r.returncode, (tmp / "root" / "game.configure.log").exists()

    def test_a_folder_with_other_sanitizers_is_configured_again(self):
        out, code, configured = self.run_script(("address", "OFF"), sanitize="address,undefined")
        self.assertTrue(configured, "the game folder is configured again with the sanitizers asked for: " + out)
        self.assertIn("game configure: exit", out)

    def test_a_folder_with_another_coverage_setting_is_configured_again(self):
        out, code, configured = self.run_script(("", "OFF"), coverage=True)
        self.assertTrue(configured, out)
        out, code, configured = self.run_script(("", "ON"))
        self.assertTrue(configured, out)

    def test_a_plain_build_in_a_sanitizer_folder_is_configured_again(self):
        out, code, configured = self.run_script(("address,undefined", "OFF"))
        self.assertTrue(configured, out)

    def test_a_folder_that_still_differs_after_configuring_is_refused(self):
        # the configure fails here (no sources), so the cache stays as it was: refused with exit 1, nothing built
        out, code, configured = self.run_script(("address", "OFF"), sanitize="address,undefined")
        self.assertEqual(code, 1, out)
        self.assertIn("was configured with NEBULA_SANITIZE='address', asked for 'address,undefined'", out)
        self.assertNotIn("bins ok", out)
        out, code, configured = self.run_script(("", "OFF"), coverage=True)
        self.assertEqual(code, 1, out)
        self.assertIn("was configured with NEBULA_COVERAGE=False, asked for True", out)


class GateSide(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="build-gate-"))
        self.addCleanup(lambda: shutil.rmtree(self.tmp, ignore_errors=True))
        self.args = gate.parser().parse_args(["--main", "--work", str(self.tmp / "work"), "--live", str(self.tmp / "live"),
                                              "--board", "", "--status", "", "--build-root", str(self.tmp / "build")])
        self.args.agent = "7"
        self.saved = gate.run, gate.machine.ensure_sccache_server
        gate.machine.ensure_sccache_server = lambda: None
        self.addCleanup(self.restore)

    def restore(self):
        gate.run, gate.machine.ensure_sccache_server = self.saved

    def check(self, code, text):
        def fake_run(cmd, log, **kw):
            pathlib.Path(log).parent.mkdir(parents=True, exist_ok=True)
            pathlib.Path(log).write_text(text, encoding="utf-8")
            return code
        gate.run = fake_run
        g = gate.Gate(self.args)
        g.bin = str(self.tmp / "bin")
        g.check("build/windows", gate.build_windows(g))
        return g

    def faults(self):
        path = self.tmp / "live" / "tool-faults.tsv"
        if not path.exists():
            return []
        import live
        return live.read_faults(self.tmp / "live")

    def test_not_judged_is_kind_tool_and_never_fail(self):
        text = ("msvc: 1 job(s) failed in the compiler cache, not in the compiler; built again (%s)\nmsvc: exit 1, 2 FAILED, 5 s\n"
                "  FAILED: a.obj\n%s\n" % (SCCACHE, gate.NOT_JUDGED_BUILD))
        g = self.check(2, text)
        row = g.results[-1]
        self.assertEqual(row["state"], "tool", row)
        self.assertEqual([r["kind"] for r in row["records"]], ["tool"], "never a fail: nothing counts toward the three failures")
        self.assertIn("compiler cache", row["detail"])
        verdict = kinds.verdict_of([r["records"] for r in g.results])
        self.assertEqual((verdict, kinds.EXIT_OF[verdict]), ("not-judged", 2))
        faults = self.faults()
        self.assertEqual(len(faults), 1, "one line for the build run again: %s" % faults)
        f = faults[0]
        self.assertEqual((f["kind"], f["check"], f["agent"], f["run_folder"]), ("TOOL", "build/windows", "7", str(g.work)))
        self.assertIn("os error 10048", f["first_error"])
        self.assertEqual(f["could_not"], "nothing: the build was run again by itself")

    def test_built_again_and_passed_is_one_fault_line_and_a_pass(self):
        text = "msvc: 1 job(s) failed in the compiler cache, not in the compiler; built again (%s)\nmsvc: exit 0, 0 FAILED, 5 s\nbins ok abc\n" % SCCACHE
        g = self.check(0, text)
        self.assertEqual(g.results[-1]["state"], "pass", g.results[-1])
        faults = self.faults()
        self.assertEqual([(f["kind"], f["check"]) for f in faults], [("TOOL", "build/windows")])

    def test_a_real_failure_is_still_fail_and_writes_nothing(self):
        text = "msvc: exit 1, 1 FAILED, 5 s\nFAILED: b.obj\nb.cpp(12): error C2065\nbuild failed\n"
        g = self.check(1, text)
        self.assertEqual(g.results[-1]["state"], "fail")
        self.assertEqual(self.faults(), [])
        # exit 2 without the line the script prints is not the cache's: an ordinary failure
        g2 = self.check(2, "something else broke\n")
        self.assertEqual(g2.results[-1]["state"], "fail")

    def test_a_pass_with_no_cache_trouble_writes_nothing(self):
        g = self.check(0, "game: exit 0, 0 FAILED, 5 s\nbins ok abc\n")
        self.assertEqual((g.results[-1]["state"], self.faults()), ("pass", []))


def script_output(*steps):
    """What build_windows.ps1 prints for steps ((name, exit, [indented lines])): one result line each, then the indented lines."""
    out = []
    for name, code, lines in steps:
        n = sum(1 for l in lines if l.startswith("FAILED:"))
        out.append("%s: exit %d, %d FAILED, 5 s" % (name, code, n))
        out += ["  " + l for l in lines]
    return "\n".join(out) + "\nbuild failed\n"


class FailureNamed(unittest.TestCase):
    def named(self, *steps):
        return gate.build_failure(script_output(*steps))

    def test_an_msvc_error(self):
        n, why = self.named(("game", 0, []), ("msvc", 1, ["FAILED: CMakeFiles/b.dir/b.cpp.obj", "b.cpp(12): error C2065: 'x': undeclared identifier"]))
        self.assertEqual((n, why), (1, "msvc: b.cpp(12): error C2065: 'x': undeclared identifier"))

    def test_a_clang_error(self):
        n, why = self.named(("game", 1, ["FAILED: native/c.o", "C:/t/c.cpp:3:1: error: expected ';' after expression"]))
        self.assertEqual((n, why), (1, "game: C:/t/c.cpp:3:1: error: expected ';' after expression"))

    def test_a_linker_error_and_a_fatal_error(self):
        n, why = self.named(("msvc", 1, ["FAILED: nebula.exe", "a.obj : error LNK2019: unresolved external symbol y"]))
        self.assertEqual(why, "msvc: a.obj : error LNK2019: unresolved external symbol y")
        n, why = self.named(("msvc", 1, ["FAILED: d.obj", "d.cpp(1): fatal error C1083: Cannot open include file: 'q.h'"]))
        self.assertTrue(why.startswith("msvc: d.cpp(1): fatal error C1083"), why)

    def test_a_failure_with_no_diagnostic_line_names_the_failed_job(self):
        n, why = self.named(("msvc", 1, ["FAILED: CMakeFiles/z.dir/z.cpp.obj"]))
        self.assertEqual((n, why), (1, "msvc: FAILED: CMakeFiles/z.dir/z.cpp.obj"))
        n, why = gate.build_failure("a tool broke\nbuild failed\n")
        self.assertEqual((n, why), (None, "build failed"), "no step line at all: the old reading, the last line (last_line)")

    def test_two_failed_steps_the_first_ones_line_is_taken_and_the_count_is_the_sum(self):
        n, why = self.named(("game", 1, ["FAILED: g.o", "g.cpp:1:1: error: first"]),
                            ("msvc", 1, ["FAILED: a.obj", "FAILED: b.obj", "a.cpp(1): error C2143: second"]))
        self.assertEqual((n, why), (3, "game: g.cpp:1:1: error: first"))

    def test_different_compile_errors_are_different_texts(self):
        a = self.named(("msvc", 1, ["FAILED: a.obj", "a.cpp(1): error C2143: one"]))[1]
        b = self.named(("msvc", 1, ["FAILED: b.obj", "b.cpp(2): error C2065: two"]))[1]
        self.assertNotEqual(a, b)

    def test_the_text_is_cut_to_200_characters_with_the_objects_path_kept(self):
        why = self.named(("msvc", 1, ["FAILED: x.obj", "D:/very/long/path/" + "p/" * 80 + "x.cpp(1): error C2143: " + "m" * 100]))[1]
        self.assertEqual(len(why), 200)
        self.assertTrue(why.startswith("msvc: D:/very/long/path/"))

    def test_a_cache_line_is_not_taken_as_the_diagnostic(self):
        n, why = self.named(("msvc", 1, ["FAILED: a.obj", SCCACHE, "FAILED: b.obj", "b.cpp(2): error C2065: two"]))
        self.assertEqual(why, "msvc: b.cpp(2): error C2065: two")

    def test_the_gates_line_has_the_count_and_the_text(self):
        case = GateSide("check")
        case.setUp()
        self.addCleanup(case.tearDown)
        g = case.check(1, script_output(("msvc", 1, ["FAILED: a.obj", "a.cpp(1): error C2143: one"])))
        self.assertEqual(g.results[-1]["detail"], "exit 1, 1 FAILED: msvc: a.cpp(1): error C2143: one")
        self.assertEqual(g.results[-1]["state"], "fail")


class Output(unittest.TestCase):
    def test_output_is_line_buffered(self):
        code = ("import sys, time\nsys.path.insert(0, %r)\nimport gate\ngate.unbuffer_output()\nprint('one line', end='\\n')\n"
                "time.sleep(20)\n" % str(GATE))
        p = subprocess.Popen([sys.executable, "-X", "utf8", "-I", "-c", code], stdout=subprocess.PIPE, text=True)
        try:
            import threading
            got = []
            t = threading.Thread(target=lambda: got.append(p.stdout.readline()), daemon=True)
            t.start()
            t.join(10)
            self.assertEqual(got, ["one line\n"], "the line arrived while the process was still running")
        finally:
            p.kill()
            p.wait()

    def test_the_landing_runner_asks_for_unbuffered_children(self):
        env = {}

        class Proc:
            stdout = iter(())

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

            def wait(self):
                return 0

        def popen(cmd, **kw):
            env.update(kw["env"])
            return Proc()
        args = SimpleNamespace(who="w", work="W", board="", status="", gate_lock="g", long_lock="l", tablet_lock="t", tablet_wait=1,
                               paks="", discs="d", jobs=1, wait=1, github_wait=1, pak="", tablet="", mods="", github_again=False,
                               live="", agent="1")
        gate.GateChecker(args, say=lambda text: None, popen=popen).run(TREE, "tip", ["b"], "repo")
        self.assertEqual(env.get("PYTHONUNBUFFERED"), "1")


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result       # --work FOLDER is the gate's, and not needed
    print("build_steps_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    sys.exit(0 if result.wasSuccessful() else 1)
