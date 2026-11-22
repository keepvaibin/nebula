"""sanitizer_test.py: the sanitizer tour's reading of a run (package B5; tools/gate/sanitizer.py and what gate.py does with it), without a
game, a build or a graphics card. The logs are planted; the harness is replaced by a recorder.

    python -X utf8 -I tools/gate/tests/sanitizer_test.py [--work FOLDER]

  address_report        an AddressSanitizer report is named by the first frame of the game's code (the runtime's and the host's frames
                        above it are passed), the process id and the time are not part of its first line
  undefined_report      an undefined-behaviour report is named by its stack's first game frame, else by its own file and line; one in code
                        that is not the game's has no frame
  several_reports       every report of a log is read, in order
  no_frame              a report with no frame of the game's code has no name: it cannot be listed
  judge_unlisted        a report not in the known list makes the test a fail with the report as its message, final; a test with no report is
                        left as it is
  judge_listed          a stop that ended by a listed report passes, counted by its frame and ledger row, final; a pass with a listed
                        report stays a pass
  known_list            the header alone is empty; bad rows, a bad ledger row, a twice listed frame, a missing file are refused; the file in
                        the repository reads
  tour_check            gate.sanitizer_tour starts the tour with print_stacktrace and the judge; a known list that cannot be read is a failed
                        check with its first problem; the judge reads each stop's run.log of the first run
  listed_exit           harness(): the runner's exit code of a stop ended by a listed report is no fault; the same code with a stop that did
                        not pass is still a fail, and a code after tests that all passed on their own is still a problem
Exit 0 when every case holds.
"""
import pathlib
import sys
import tempfile
import types

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402
import kinds  # noqa: E402
import sanitizer  # noqa: E402

failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


RUN = "D:\\NebulaWork\\build\\t7b\\runs\\048-HoneyBeeKingdomGalaxy-1\\d3d12\\bin\\"
GAME_DLL = RUN + "smg_game.dll"
NATIVE_FILE = "D:/NebulaWork/MarioGalaxyRecomp/Nebula-X/native/petari/src/Game/System/NebulaModule.c"
HOST_FILE = "D:/NebulaWork/MarioGalaxyRecomp/Nebula-X/runtime/src/game_module.cpp"
# What a run.log holds when the planted fault was a write past a 4-byte block (a real run of this tour, 11-21; the paths shortened): the
# report's lines, another thread's log line between them, the frames of the host and of the system after the game's, a second stack.
ADDRESS_LOG = (
    "[thread] name=Nebula Render id=17040\n"
    "=================================================================\n"
    "==58076==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x12545f92f014 at pc 0x7ff835d84d85 bp 0x006ba06ff800 sp 0x006ba06ff848\n"
    "WRITE of size 1 at 0x12545f92f014 thread T46\n"
    "[    1.158] [power] display held on (playing)\n"
    "    #0 0x7ff835d84d84 in nebula_game_run+0xbc (" + GAME_DLL + "+0x1826f4d84)\n"
    "    #1 0x7ff666e21fe0  (" + RUN + "Nebula.exe+0x1403c1fe0)\n"
    "    #2 0x7ff94d36cd2f in wcsrchr+0x14f (C:\\Windows\\System32\\ucrtbase.dll+0x18002cd2f)\n"
    "    #3 0x7ff8993cdc0e in _asan_wrap_CreateThread+0x14e (" + RUN + "clang_rt.asan_dynamic-x86_64.dll+0x18005dc0e)\n"
    "\n"
    "0x12545f92f014 is located 0 bytes after 4-byte region [0x12545f92f010,0x12545f92f014)\n"
    "allocated by thread T46 here:\n"
    "    #0 0x7ff8993bc92f in _asan_wrap_memcpy+0x73f (" + RUN + "clang_rt.asan_dynamic-x86_64.dll+0x18004c92f)\n"
    "    #1 0x7ff835d84d03 in nebula_game_run+0x3b (" + GAME_DLL + "+0x1826f4d03)\n"
    "SUMMARY: AddressSanitizer: heap-buffer-overflow\n")
# The same for an undefined-behaviour report (the planted fault was an index past an array; UBSAN_OPTIONS=print_stacktrace=1): no time
# on its first line, a log line of another thread after it.
UNDEFINED_LOG = (
    "[thread] name=Nebula Render id=43704\n"
    + NATIVE_FILE + ":67:5: runtime error: index 4 out of bounds for type 'volatile char[4]'\n"
    "[    1.528] [power] display held on (playing)\n"
    "    #0 0x7ff835d84d54 in nebula_game_run+0x8c (" + GAME_DLL + "+0x1826f4d54)\n"
    "    #1 0x7ff704df1fe0  (" + RUN + "Nebula.exe+0x1403c1fe0)\n"
    "    #2 0x7ff94ffb9c5b in RtlUserThreadStart+0x2b (C:\\Windows\\SYSTEM32\\ntdll.dll+0x1800c9c5b)\n"
    "\n"
    "SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior " + NATIVE_FILE + ":67:5\n")
# Frames as other builds name them: a template with its parameters, no name in the plain module, a source path.
OTHER_FRAMES = (
    "==1==ERROR: AddressSanitizer: stack-use-after-return on address 0x1 at pc 0x2 bp 0x3 sp 0x4\n"
    "    #0 0x1 in Baz<int>::get(int) const+0x10 (" + GAME_DLL + "+0x5)\n")
NAMELESS_FRAME = (
    "==1==ERROR: AddressSanitizer: use-after-poison on address 0x1 at pc 0x2 bp 0x3 sp 0x4\n"
    "    #0 0x1  (" + RUN + "smg_game_plain.dll+0x1826f4d54)\n")
SOURCE_FRAME = (
    "==1==ERROR: AddressSanitizer: heap-use-after-free on address 0x1 at pc 0x2 bp 0x3 sp 0x4\n"
    "    #0 0x1 in memcpy C:\\Windows\\System32\\ntdll.dll:5\n"
    "    #1 0x2 in Foo::update(float, int) D:\\work\\native\\petari\\src\\Game\\Enemy\\Foo.cpp:120:5\n")
# Undefined behaviour in the host's code (not the game's): no frame of the game's code, whichever frames follow.
UNDEFINED_HOST = (HOST_FILE + ":12:3: runtime error: signed integer overflow\n"
                  "    #0 0x1 in host_call+0x4 (" + RUN + "Nebula.exe+0x9)\n")
# Without a stack: the report's own source and line.
UNDEFINED_PLAIN = NATIVE_FILE + ":55:9: runtime error: shift exponent 40 is too large for 32-bit type 'int'\n"
NO_FRAME = ("==9==ERROR: AddressSanitizer: stack-overflow on address 0x1 (pc 0x2)\n"
            "    #0 0x7ff1 in memset+0x9 (C:\\Windows\\System32\\ntdll.dll+0x9)\n")
FIRST_ADDRESS = ("AddressSanitizer: heap-buffer-overflow on address 0x12545f92f014 at pc 0x7ff835d84d85 bp 0x006ba06ff800 "
                 "sp 0x006ba06ff848")


def main():
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)

    # address_report
    reports = sanitizer.read_reports(ADDRESS_LOG)
    check(len(reports) == 1 and reports[0].frame == "nebula_game_run", "address: the first frame in the game module, not the host's or the system's: %s" % [r.frame for r in reports])
    check(reports and reports[0].first == FIRST_ADDRESS, "address: the first line has no process id: %s" % [r.first for r in reports])
    check(reports and reports[0].line() == FIRST_ADDRESS + " at nebula_game_run", "address: the message names the frame")

    # undefined_report
    one = sanitizer.read_reports(UNDEFINED_LOG)
    check(len(one) == 1 and one[0].frame == "nebula_game_run" and one[0].first == "runtime error: index 4 out of bounds for type 'volatile char[4]'",
          "undefined with a stack: the first frame in the game module: %s" % [(r.frame, r.first) for r in one])
    plain = sanitizer.read_reports(UNDEFINED_PLAIN)
    check(len(plain) == 1 and plain[0].frame == "NebulaModule.c:55" and plain[0].first.startswith("runtime error: shift exponent 40"),
          "undefined without a stack: its own file and line: %s" % [(r.frame, r.first) for r in plain])
    host = sanitizer.read_reports(UNDEFINED_HOST)
    check(len(host) == 1 and host[0].frame is None, "undefined in the host's code has no game frame: %s" % [r.frame for r in host])

    # frames
    check([r.frame for r in sanitizer.read_reports(OTHER_FRAMES)] == ["Baz<int>::get"], "a template's parameters are cut, its brackets are not")
    check([r.frame for r in sanitizer.read_reports(NAMELESS_FRAME)] == ["smg_game_plain.dll+0x1826f4d54"], "a frame with no name: the module and the offset")
    check([r.frame for r in sanitizer.read_reports(SOURCE_FRAME)] == ["Foo::update"], "a frame with a source: the native folder's is the game's, the system's is not")

    # several_reports
    both = sanitizer.read_reports(ADDRESS_LOG + UNDEFINED_PLAIN + NAMELESS_FRAME)
    check([r.frame for r in both] == ["nebula_game_run", "NebulaModule.c:55", "smg_game_plain.dll+0x1826f4d54"], "several: %s" % [r.frame for r in both])
    check(sanitizer.read_reports("[ 1.0] boot: main at 0\n[ 2.0] a line that says runtime error in words\n") == [], "no report in a plain log")

    # no_frame
    nf = sanitizer.read_reports(NO_FRAME)
    check(len(nf) == 1 and nf[0].frame is None and "no frame of the game's code" in nf[0].line(), "no frame: %s" % [r.line() for r in nf])

    # judge_unlisted, judge_listed
    known = {"nebula_game_run": ("N-1100", "AddressSanitizer: heap-buffer-overflow")}
    results = {"a": ("fail", "crashed"), "b": ("pass", ""), "c": ("fail", "crashed"), "d": ("fail", "crashed"), "e": ("pass", "")}
    logs = {"a": ADDRESS_LOG, "b": "", "c": UNDEFINED_PLAIN, "d": NO_FRAME, "e": ADDRESS_LOG}
    out, final, totals = sanitizer.judge_results(results, logs, known)
    check(out["a"][0] == "pass" and "N-1100" in out["a"][1] and "nebula_game_run" in out["a"][1], "listed: a stop ended by it passes: %s" % (out["a"],))
    check(out["b"] == ("pass", ""), "no report: left as it is")
    check(out["c"][0] == "fail" and out["c"][1].startswith("sanitizer report: runtime error: shift exponent"), "unlisted: fail with the report: %s" % (out["c"],))
    check(out["d"][0] == "fail" and "no frame of the game's code" in out["d"][1], "no frame: fail: %s" % (out["d"],))
    check(out["e"] == ("pass", ""), "a pass with a listed report stays a pass")
    check(final == {"a", "c", "d"}, "final: the judged ones that changed: %s" % sorted(final))
    check(totals == {"nebula_game_run": 2}, "totals count the listed reports of every log: %s" % totals)
    check(sanitizer.summary(totals, known) == "listed sanitizer reports: nebula_game_run x2 (N-1100)", "summary: %s" % sanitizer.summary(totals, known))
    check(sanitizer.summary({}, known) == "", "summary of nothing")
    mixed, final2, _ = sanitizer.judge_results({"m": ("fail", "crashed")}, {"m": ADDRESS_LOG + UNDEFINED_PLAIN}, known)
    check(mixed["m"][0] == "fail" and "NebulaModule.c:55" in mixed["m"][1], "a listed and an unlisted report in one log: fail, the unlisted named: %s" % (mixed["m"],))

    # known_list
    def tsv(name, text):
        path = work / name
        path.write_text(text, encoding="utf-8")
        return path

    header = "frame\tledger row\tfirst words\n"
    check(sanitizer.read_known(tsv("empty.tsv", "# comment\n" + header)) == {}, "the header alone is empty")
    check(sanitizer.read_known(tsv("one.tsv", header + "Foo.cpp:Foo::update\tN-1100\theap-buffer-overflow\n")) ==
          {"Foo.cpp:Foo::update": ("N-1100", "heap-buffer-overflow")}, "a row reads")
    check(sanitizer.read_known(tsv("pending.tsv", header + "Foo.cpp:55\tpending\tshift exponent\n")) == {"Foo.cpp:55": ("pending", "shift exponent")},
          "pending is a ledger row")
    for name, text, word in (("noheader.tsv", "Foo.cpp:1\tN-1\tx\n", "header"), ("cells.tsv", header + "Foo.cpp:1\tN-1\n", "three cells"),
                             ("empty-cell.tsv", header + "Foo.cpp:1\tN-1\t\n", "three cells"), ("row.tsv", header + "Foo.cpp:1\tsoon\tx\n", "ledger row"),
                             ("twice.tsv", header + "Foo.cpp:1\tN-1\tx\nFoo.cpp:1\tN-2\ty\n", "twice"), ("nothing.tsv", "# only a comment\n", "no header")):
        try:
            sanitizer.read_known(tsv(name, text))
            check(False, "%s: refused" % name)
        except sanitizer.KnownError as error:
            check(word in str(error), "%s: the problem names %r: %s" % (name, word, error))
    try:
        sanitizer.read_known(work / "no-such-file.tsv")
        check(False, "a missing file is refused")
    except sanitizer.KnownError:
        pass
    try:
        sanitizer.read_known()
    except sanitizer.KnownError as error:
        check(False, "the list in the repository does not read: %s" % error)

    # tour_check
    class Recorder:
        def __init__(self):
            self.calls = []

        def __call__(self, g, suite, renderer, label, **kwargs):
            self.calls.append((suite, renderer, label, kwargs))
            return lambda log: (True, "", None)

    class Source:
        suite_dir = "tour-asan"

        def __init__(self, folder):
            self.folder, self.final, self.counted, self.ended_listed = folder, set(), "", False

        def first_work(self):
            return self.folder

    real = gate.harness
    try:
        rec = Recorder()
        gate.harness = rec
        log = work / "tour.log"
        check(gate.sanitizer_tour(None, "SUITE", "BIN")(log) == (True, "", None), "tour_check: the harness's answer is the check's")
        suite, renderer, label, kw = rec.calls[0]
        check((suite, renderer, label, kw["bin_dir"]) == ("SUITE", "d3d12", "tour-asan", "BIN"), "tour_check: the tour on Direct3D 12 with the sanitizer build: %s" % (rec.calls[0][:3],))
        check(kw["env"] == {"UBSAN_OPTIONS": "print_stacktrace=1"}, "tour_check: the stack of an undefined-behaviour report is on: %s" % kw["env"])
        run = work / "first" / "runs" / "tour-asan"
        for test, text in (("s1", ADDRESS_LOG), ("s2", UNDEFINED_PLAIN)):
            (run / test / "d3d12").mkdir(parents=True, exist_ok=True)
            (run / test / "d3d12" / "run.log").write_text(text, encoding="utf-8")
        source = Source(work / "first")
        out = kw["judge"](source, work / "first", {"s1": ("fail", "crashed"), "s2": ("fail", "crashed"), "s3": ("pass", "")})
        check(out["s1"][0] == "fail" and out["s2"][0] == "fail" and out["s3"] == ("pass", ""), "tour_check: the judge reads each stop's run.log: %s" % out)
        check(source.final == {"s1", "s2"} and not source.ended_listed and source.counted == "", "tour_check: nothing listed: %s" % (source.final,))
        original = sanitizer.KNOWN
        sanitizer.KNOWN = tsv("live.tsv", header + "nebula_game_run\tN-1100\theap-buffer-overflow\n")
        try:
            rec2 = Recorder()
            gate.harness = rec2
            gate.sanitizer_tour(None, "SUITE", "BIN")(log)
            source = Source(work / "first")
            out = rec2.calls[0][3]["judge"](source, work / "first", {"s1": ("fail", "crashed")})
            check(out["s1"][0] == "pass" and source.ended_listed and source.counted == "listed sanitizer reports: nebula_game_run x1 (N-1100)",
                  "tour_check: a listed report: passes, counted, the exit is no fault: %s %s" % (out, source.counted))
            source = Source(work / "other")
            rec2.calls[0][3]["judge"](source, work / "other", {})
            check(source.counted == "", "tour_check: the count is the first run's alone")
        finally:
            sanitizer.KNOWN = original
        sanitizer.KNOWN = tsv("bad.tsv", "Foo.cpp:1\tN-1\tx\n")
        try:
            rec3 = Recorder()
            gate.harness = rec3
            answer = gate.sanitizer_tour(None, "SUITE", "BIN")(log)
            check(answer[0] is False and "header" in answer[1] and not rec3.calls, "tour_check: a list that cannot be read fails the check, no run: %s" % (answer,))
        finally:
            sanitizer.KNOWN = original
    finally:
        gate.harness = real

    # listed_exit
    class Fixed:
        """What harness() asks of its source, with the sanitizer's judge in read()."""

        def __init__(self, code, results, logs, known):
            self.code, self.raw, self.logs, self.known = code, results, logs, known
            self.suite_dir, self.listed, self.final, self.counted, self.ended_listed = "tour-asan", list(results), set(), "", False

        def first_work(self):
            return work / "fixed"

        def start(self, *args):
            return self.code

        def read(self, folder, names):
            out, final, totals = sanitizer.judge_results(self.raw, self.logs, self.known)
            self.final |= final
            self.ended_listed = self.ended_listed or any(out[t][0] == "pass" != self.raw[t][0] for t in final)
            return {n: (k, m) for n, (k, m) in out.items()}, list(self.raw), None

    real_source, real_cases = gate.HarnessSource, kinds.Cases
    try:
        def case(code, results, logs, known):
            holder = {}

            class Made(Fixed):
                def __init__(self, *args):
                    Fixed.__init__(self, code, results, logs, known)
                    holder["source"] = self

            gate.HarnessSource = Made
            kinds.Cases = lambda results, declared, problem, *a, **k: type("C", (), {"results": results, "problem": problem})()
            (work / "listed.log").write_text("the runner's last line\n", encoding="utf-8")
            fake_gate = types.SimpleNamespace(bin="BIN", work=work)
            passes, _, cases = gate.harness(fake_gate, "SUITE", "d3d12", "tour-asan")(work / "listed.log")
            return passes, cases.problem

        listed = {"nebula_game_run": ("N-1100", "x")}
        check(case(1, {"a": ("fail", "crashed"), "b": ("pass", "")}, {"a": ADDRESS_LOG}, listed) == (True, None),
              "listed_exit: a code that only a listed report explains is no fault")
        passes, problem = case(1, {"a": ("fail", "crashed"), "b": ("pass", "")}, {"a": UNDEFINED_PLAIN}, listed)
        check(not passes, "listed_exit: an unlisted report is a fail")
        passes, problem = case(1, {"a": ("pass", ""), "b": ("pass", "")}, {}, listed)
        check(not passes and problem and "exited 1 though every test passed" in problem, "listed_exit: a code after tests that all passed on their own is a problem: %s" % problem)
        passes, problem = case(1, {"a": ("fail", "crashed"), "b": ("fail", "crashed")}, {"a": ADDRESS_LOG}, listed)
        check(not passes, "listed_exit: a stop that failed by itself beside a listed one is a fail")
    finally:
        gate.HarnessSource, kinds.Cases = real_source, real_cases

    print("\n".join(failed) if failed else "sanitizer: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
