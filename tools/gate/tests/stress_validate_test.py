"""stress_validate_test.py: the stress and validate lanes (package T12; tools/gate/stress.py, validate.py, judged.py and what gate.py,
kinds.py and machine.py do with them), without a game, a build, a graphics card or a tablet. The runner is tests/fake_runner.py; the
folders under D:/NebulaWork are replaced by temporary ones (result_kinds_test.Scratch).

    python -X utf8 -I tools/gate/tests/stress_validate_test.py [--work FOLDER]

Stress:
  stress_passes_alone        a test that fails with the other copies running and passes alone is kind stress: it does not fail the run,
                             no line in flakes.tsv, not counted toward a set-aside, the folder is kept whole (keep.txt) and one repair
                             is opened (source stress, kind stress) whose note is the watch's line
  stress_fails_alone         failing alone too it is a plain fail, no repair, nothing kept
  landing_flake_and_fail     the same failure in a landing run that passes alone is flake (a line in flakes.tsv), failing alone a fail
  asked_by_also_is_a_fail    a landing that asked for the lane by --also gets a fail for a stress result
  slow_under_the_load        slow with twelve copies and passing alone is only counted; slow even alone is a fail
  kept_whole                 the thinning of old runs and the pruning of passed tests leave a folder with keep.txt alone
  twelve_copies              twelve copies whatever the card says; fewer when the PC's memory is short, and the note says how many
  the_twelve_lanes           the lanes' layout and names, and what a dry run of each level registers
  the_watch_line             the repair's note names the engine watch's line in the kept run
Validate:
  layer_errors               an error whose id is not listed fails with the message as its first line; a listed one is counted and
                             reported; warnings are counted only; the formats of the three renderers
  layer_not_on               a run whose game says the layer is off is tool; a layer that is not installed leaves the check out with one
                             plain line, one line a day in tool-faults.tsv, never fail, tool or env
  four_times_the_limit       the runner gets validation on and its limits four times; a run at its limit is tool, not run again
  known_list                 validate-known.tsv: the header alone is empty; a bad row, a bad ledger row, a twice listed id are refused
  hashes                     a tour test whose step hashes differ from its recording is a fail
  the_runner_option          luma_test.py takes --gpu-validation 0|1
"""
import contextlib
import datetime
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "plan"))
import gate  # noqa: E402
import judged  # noqa: E402
import kinds  # noqa: E402
import machine  # noqa: E402
import prune_runs  # noqa: E402
import result_kinds_test as rk  # noqa: E402
import stress  # noqa: E402
import validate  # noqa: E402

TREE = HERE.parents[2]
WATCH = "[constants] watch: a compute read of the constant ring did not read what was written"


def stress_gate(s, live, *extra):
    return s.gate(extra=["--live", str(live), *extra])


def run_stress(s, g, fails=False, check="stress/tour/d3d12/1", label="stress-tour-1"):
    with contextlib.redirect_stdout(io.StringIO()) as out:
        g.check(check, gate.harness(g, s.suite, "d3d12", label, stress_lane=True, stress_fails=fails), ())
        g.settle()
        g.write()
    return out.getvalue()


def repairs(live):
    path = pathlib.Path(live) / "repairs.tsv"
    if not path.is_file():
        return []
    lines = path.read_text(encoding="utf-8").splitlines()[1:]
    return [dict(zip("id opened source kind check renderer step commit evidence row state closed seen note".split(), l.split("\t")))
            for l in lines]


def stress_passes_alone(s, check):
    s.keep_main()
    live = s.t / "live"
    # Three flakes of this test on this renderer are already on file: a stress result must not make a fourth, nor set anything aside.
    s.plan({"a": {"first": "fail:the constants watch saw every read right", "again": "pass"}},
           logs={"a": {"first": "[  1.000] boot: main at\n%s\n" % WATCH}})
    g = stress_gate(s, live)
    run_stress(s, g)
    r = rk.record_of(g, "a", "stress/tour/d3d12/1")
    row = rk.row(g, "stress/tour/d3d12/1")
    check(r["kind"] == "stress" and not r["counts"] and row["state"] == "stress", "stress: kind stress, it does not count: %s %s" % (r, row["state"]))
    check(g.verdict == "pass" and g.code == 0, "stress: the run is not failed: %s %s" % (g.verdict, g.code))
    check(not s.flake_lines() and not kinds.read_aside(s.aside), "stress: nothing in flakes.tsv, nothing set aside")
    check(rk.phase_of(s, g, "a", "runs-stress-tour-1-d3d12") == "again", "stress: the second run takes the first's place")
    check((g.work / "keep.txt").is_file(), "stress: the run folder is kept whole (keep.txt)")
    found = repairs(live)
    check(len(found) == 1 and found[0]["source"] == "stress" and found[0]["kind"] == "stress"
          and found[0]["check"] == "stress/tour/d3d12/1" and found[0]["renderer"] == "d3d12" and found[0]["state"] == "open",
          "stress: one repair, source stress, kind stress, the check and the renderer: %s" % found)
    check(found and WATCH in found[0]["note"] and found[0]["note"].startswith("a: "),
          "stress: the repair's note is the test and the watch's line: %s" % (found[0]["note"] if found else None))
    check(found and (pathlib.Path(found[0]["evidence"]) / "d3d12" / "run.log").is_file()
          and WATCH in (pathlib.Path(found[0]["evidence"]) / "d3d12" / "run.log").read_text(encoding="utf-8"),
          "stress: the evidence is the failed run's own folder: %s" % (found[0]["evidence"] if found else None))
    lines = kinds.kind_lines(g.results)
    check(any(l.startswith("KIND stress stress/tour/d3d12/1 a ::") for l in lines), "stress: a KIND stress line: %s" % lines)
    # The same lane again: the repair of the same check and renderer is counted, not written twice.
    g = stress_gate(s, live)
    run_stress(s, g)
    again = repairs(live)
    check(len(again) == 1 and again[0]["seen"] == "2", "stress: the same check again counts the open repair: %s" % again)


def stress_fails_alone(s, check):
    s.keep_main()
    live = s.t / "live"
    s.plan({"a": {"first": "fail:boom", "again": "fail:boom", "main": "pass"}})
    g = stress_gate(s, live)
    run_stress(s, g)
    r = rk.record_of(g, "a", "stress/tour/d3d12/1")
    check(r["kind"] == "fail" and r["counts"] and g.verdict == "fail" and g.code == 1, "fails alone too: a plain fail: %s %s" % (r, g.verdict))
    check(not repairs(live) and not (g.work / "keep.txt").exists(), "fails alone too: no repair, nothing kept")
    check(not (g.work / "main").exists(), "fails alone too: not run on main's build (it is no fault of the load)")


def landing_flake_and_fail(s, check):
    s.keep_main()
    s.plan({"a": {"first": "fail:" + WATCH, "again": "pass"}})
    g = s.run()
    r = rk.record_of(g, "a")
    check(r["kind"] == "flake" and len(s.flake_lines()) == 1, "landing: the same line that passes alone is flake, in flakes.tsv: %s" % r)
    s.plan({"a": {"first": "fail:" + WATCH, "again": "fail:" + WATCH, "main": "pass"}})
    g = s.run(g=s.gate())
    r = rk.record_of(g, "a")
    check(r["kind"] == "fail" and g.verdict == "fail", "landing: the watch line that fails alone too is fail: %s %s" % (r, g.verdict))


def asked_by_also_is_a_fail(s, check):
    s.keep_main()
    live = s.t / "live"
    s.plan({"a": {"first": "fail:" + WATCH, "again": "pass"}})
    g = stress_gate(s, live)
    run_stress(s, g, fails=True)
    r = rk.record_of(g, "a", "stress/tour/d3d12/1")
    check(r["kind"] == "fail" and r["counts"] and "passed alone" in r["message"] and g.verdict == "fail",
          "--also stress: a stress result counts as fail: %s %s" % (r, g.verdict))
    check(not repairs(live) and not s.flake_lines(), "--also stress: no repair and no flake line")


def slow_under_the_load(s, check):
    s.keep_main()
    live = s.t / "live"
    s.plan({"a": {"first": "slow:50", "again": "pass"}, "b": {"first": "slow:50", "again": "slow:40"}})
    g = stress_gate(s, live)
    out = run_stress(s, g)
    a, b = rk.record_of(g, "b", "stress/tour/d3d12/1"), None
    check(not any(r["test"] == "a" for r in rk.row(g, "stress/tour/d3d12/1")["records"]),
          "slow under the load, passing alone: only counted, no record of a fault")
    check("1 run too slow under the load" in rk.row(g, "stress/tour/d3d12/1")["detail"], "the row counts it: %s" % rk.row(g, "stress/tour/d3d12/1")["detail"])
    check(a["kind"] == "fail" and "too slow even alone" in a["message"] and g.verdict == "fail",
          "slow even alone: a fail: %s" % a)
    check(not s.flake_lines() and not repairs(live), "slow under the load: not a flake, not a stress, no repair")


def kept_whole(s, check):
    work = s.t / "runs"
    work.mkdir()
    names = ["20261120-100000-aaaaaaaaa-long", "20261120-110000-bbbbbbbbb-long", "20261120-120000-ccccccccc-long",
             "20261120-130000-ddddddddd-long", "20261120-140000-eeeeeeeee-long"]
    for n in names:
        d = work / n
        (d / "runs-x" / "runs").mkdir(parents=True)
        (d / "runs-x" / "runs" / "big.bin").write_bytes(b"0" * 100)
        (d / "result.json").write_text(json.dumps({"passed": True}), encoding="utf-8")
        (d / "tour.log").write_text("log\n", encoding="utf-8")
    stress.keep_whole(work / names[0], [{"check": "stress/tour/d3d11/2", "test": "a", "renderer": "d3d11", "message": "boom"}])
    whole, thin = machine.runs_to_thin(machine.run_folders(work), keep=1)
    check(names[0] in whole and names[1] in thin, "the thinning keeps the folder with keep.txt, thins the others: %s" % (sorted(whole),))
    machine.thin_runs(work, keep=1)
    check((work / names[0] / "runs-x" / "runs" / "big.bin").exists() and not (work / names[1] / "runs-x").exists(),
          "thin_runs leaves it whole and thins an old one")
    # The pruning of passed tests' working files.
    test = work / names[0] / "runs-x" / "runs" / "x" / "a" / "d3d12"
    test.mkdir(parents=True)
    (test / "result.json").write_text(json.dumps({"verdict": "pass"}), encoding="utf-8")
    (test / "audio.wav").write_bytes(b"0" * 50)
    other = work / names[2] / "runs-x" / "runs" / "x" / "a" / "d3d12"
    other.mkdir(parents=True)
    (other / "result.json").write_text(json.dumps({"verdict": "pass"}), encoding="utf-8")
    (other / "audio.wav").write_bytes(b"0" * 50)
    check(prune_runs.prune(work / names[0]) == (0, 0) and (test / "audio.wav").exists(), "prune_runs leaves a keep.txt folder alone")
    check(prune_runs.prune(work / names[2])[0] == 1 and not (other / "audio.wav").exists(), "and prunes one without it")
    text = (work / names[0] / "keep.txt").read_text(encoding="utf-8")
    check("stress/tour/d3d11/2 a on d3d11: boom" in text, "keep.txt names the result: %r" % text)


def twelve_copies(s, check):
    gb = machine.GB
    check(stress.COPIES == 12, "twelve copies")
    copies, by_memory, note = stress.copies_for(200 * gb)
    check(copies == 12 and "12 game copies at once, whatever the graphics card's rule says" in note, "plenty of memory: twelve: %s" % note)
    copies, by_memory, note = stress.copies_for(None)
    check(copies == 12, "no reading of the memory: twelve")
    # 70% of 20 GB is 14 GB: seven copies of 2 GB.
    copies, by_memory, note = stress.copies_for(20 * gb)
    check(copies == 7 and by_memory == 7 and "7 of 12 game copies at once" in note and "20.0 GB" in note, "memory short: what fits, said: %s" % note)
    copies, _, note = stress.copies_for(1 * gb)
    check(copies == 1, "less than one copy fits: one (machine_ready waits for room for one before any run)")
    # The card's rule does not limit it: a card with room for 2 copies still gives twelve; machine.copies_at_once keeps the card's rule.
    check(machine.copies_at_once(6 * gb, 200 * gb, 3 * gb, 12)[0] == 1 and stress.copies_for(200 * gb)[0] == 12,
          "the card's rule would give 1 here, the stress lane still asks twelve")


def the_twelve_lanes(s, check):
    lanes = stress.lanes()
    check(len(lanes) == 12, "twelve lanes: %d" % len(lanes))
    names = [c[0] for lane in lanes for c in lane]
    check(len(names) == 15 and len(set(names)) == 15, "fifteen checks: three engine suites and twelve tours: %d" % len(names))
    for r in ("d3d12", "vulkan", "d3d11"):
        mine = [l for l in lanes if l[0][2] == r]
        check(len(mine) == 4, "%s: four lanes" % r)
        first = [l for l in mine if len(l) == 2]
        check(len(first) == 1 and [c[0] for c in first[0]] == ["stress/engine/%s" % r, "stress/tour/%s/1" % r],
              "%s: one lane of the engine suite and then the tour: %s" % (r, first))
        check(sorted(c[0] for l in mine if len(l) == 1 for c in l) == ["stress/tour/%s/%d" % (r, n) for n in (2, 3, 4)],
              "%s: three more lanes of the tour" % r)
    # What a dry run of each level registers.
    def needs(level, *extra):
        args = gate.parser().parse_args(["--" + level, "--tablet", "T", "--work", str(s.work), "--board", "", "--status", "",
                                         "--build-root", str(s.build), "--paks", "", *extra])
        d = gate.run_checks(args, {}, {}, dry=True)
        return d.needs
    for level in ("long", "final"):
        n = needs(level)
        check(set(names) <= set(n) and all(set(n[x]) == {"build/windows", "data/RMGE01"} for x in names),
              "%s level: the stress checks, each needing the build and the data" % level)
    for level in ("land", "main", "quick"):
        n = needs("main") if level == "land" else needs(level)
        check(not any(x.startswith("stress/") for x in n), "%s level: no stress check (only the nightly run has it)" % level)
    for level, want in (("quick", False), ("main", True), ("long", True), ("final", True)):
        n = needs(level)
        has = {"validate/d3d12", "validate/vulkan", "validate/d3d11"} <= set(n)
        check(has is want, "%s level: validate checks %s" % (level, "registered" if want else "left out"))
    # A landing (the land level, which --scope gives) too.
    args = gate.parser().parse_args(["--main", "--tablet", "T", "--work", str(s.work), "--board", "", "--status", "", "--build-root",
                                     str(s.build), "--paks", ""])
    args.mode = gate.LAND_LEVEL
    d = gate.run_checks(args, {}, {}, dry=True)
    check({"validate/d3d12", "validate/vulkan", "validate/d3d11"} <= set(d.needs) and not any(x.startswith("stress/") for x in d.needs),
          "a landing: validate, no stress")
    check(gate.starts_game("validate/d3d12") and gate.starts_game("stress/tour/d3d11/2"), "both lanes start the game: the machine is checked first")


def the_watch_line(s, check):
    folder = s.t / "kept" / "a" / "d3d11"
    folder.mkdir(parents=True)
    (folder / "run.log").write_text("[ 0.1] host: frame 1\n%s\n[ 0.2] more\n" % WATCH, encoding="utf-8")
    found = {"check": "stress/tour/d3d11/3", "test": "a", "renderer": "d3d11", "message": "the constants watch saw every read right",
             "folder": str(s.t / "kept" / "a")}
    check(stress.repair_note(found) == "a: " + WATCH, "the watch's line is the note: %s" % stress.repair_note(found))
    found["folder"] = str(s.t / "nothing")
    check(stress.repair_note(found) == "a: the constants watch saw every read right", "no watch line in the run: the first failure line")
    command = stress.repair_command(s.t / "live", found, "abc", "ev")
    check(command[command.index("--source") + 1] == "stress" and command[command.index("--kind") + 1] == "stress"
          and command[command.index("--check") + 1] == "stress/tour/d3d11/3" and "--live" in command and command.index("--live") < command.index("repair"),
          "the repair command: source stress, kind stress, the check, --live before the subcommand: %s" % command)
    done = subprocess.run(command, capture_output=True, text=True)
    check(done.returncode == 0 and done.stdout.strip().splitlines()[-1] == "R-001", "plan.py takes the command: %s %s" % (done.returncode, done.stderr[-200:]))
    plain = []
    (s.t / "runfolder").mkdir()
    ids = stress.open_repairs(s.t / "live", s.t / "runfolder", "abc", [found], say=plain.append)
    check(ids == ["R-001"] and "keep.txt" in os.listdir(s.t / "runfolder"),
          "open_repairs keeps the folder and counts the same repair again: %s %s" % (ids, plain))


# --- validate ------------------------------------------------------------------------------------------------------------


D12 = "[gpu] d3d12 debug-layer=1\n"
ERR12 = "[D3D12-LIVE-ERR] id 1234: RESOURCE_BARRIER_BEFORE_AFTER_MISMATCH: before state does not match | at 0x100 0x2a0\n"
ERR11 = "[D3D11-LIVE-ERR] id=3150 DEVICE_DRAW_VIEW_DIMENSION_MISMATCH: shader reads a 2D view\n"
ERRVK = "[VK-LIVE-ERR] id VUID-vkCmdDraw-None-02699: Validation Error: [ VUID-vkCmdDraw-None-02699 ] descriptor missing\n"
WARN12 = "[D3D12-LIVE-WARN] id 9: a warning\n"


def plant_known(s, text):
    path = s.t / "validate-known.tsv"
    path.write_text("renderer\tid\tledger row\tfirst words\n" + text, encoding="utf-8")
    validate.KNOWN = path
    return path


def run_validate(s, g, log_by_phase, probe=True):
    saved = gate.layer_probe, gate.layer_fault
    gate.layer_probe = lambda g, r, log: probe
    gate.layer_fault = lambda g, r, line: s.faults.append((r, line))
    s.faults = []
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            g.check("validate/d3d12", gate.validate_check(g, "d3d12", s.suite), ())
            g.settle()
            g.write()
    finally:
        gate.layer_probe, gate.layer_fault = saved
    return g


def layer_errors(s, check):
    s.keep_main()
    original = validate.KNOWN
    try:
        plant_known(s, "d3d12\t1234\tP-77\tRESOURCE_BARRIER_BEFORE_AFTER_MISMATCH\n")
        # Listed: counted and reported, the run passes; warnings are counted only.
        s.plan({}, logs={"a": {"all": D12 + ERR12 * 50 + WARN12 * 3}, "b": {"all": D12}})
        g = run_validate(s, s.gate(), None)
        row = rk.row(g, "validate/d3d12")
        check(row["state"] == "pass" and g.verdict == "pass", "a listed error is not a fail: %s %s" % (row["state"], g.verdict))
        check("1234 x50 (P-77)" in row["detail"] and "3 layer warnings" in row["detail"], "it is counted and reported, with its row: %s" % row["detail"])
        # Not listed: a fail, the message the first line; the same on every run, so not run again alone.
        s.plan({}, logs={"a": {"all": D12 + "[D3D12-LIVE-ERR] id 777: NEW_ERROR: something else | at 0x1\n" * 2}, "b": {"all": D12}})
        g = run_validate(s, s.gate(), None)
        r = rk.record_of(g, "a", "validate/d3d12")
        check(r["kind"] == "fail" and r["counts"] and r["message"].startswith("Direct3D debug layer error 777, 2 times: NEW_ERROR: something else")
              and g.verdict == "fail", "an error whose id is not listed is a fail with the message: %s" % r)
        check(rk.phase_of(s, g, "a", "runs-validate-d3d12") == "first" and not (g.work / "rerun").exists(),
              "it is the same on every run: not run again alone")
        check(not any(r["test"] == "b" for r in rk.row(g, "validate/d3d12")["records"]), "a test with no message passes")
    finally:
        validate.KNOWN = original


def message_formats(s, check):
    m = validate.read_messages(ERR12 + WARN12 + "[D3D11-LIVE-ERR] id=5 x\n[VK-LIVE-ERR] id VUID-a-b-1: y\n", "d3d12")
    check(m.errors == {"1234": [1, "RESOURCE_BARRIER_BEFORE_AFTER_MISMATCH: before state does not match"]} and m.warnings == 1,
          "Direct3D 12's lines (the addresses cut off, the other renderers' lines not counted): %s %s" % (m.errors, m.warnings))
    m = validate.read_messages(ERR11 + "[D3D11-LIVE-WARN] id=3 w\n", "d3d11")
    check(m.errors == {"3150": [1, "DEVICE_DRAW_VIEW_DIMENSION_MISMATCH: shader reads a 2D view"]} and m.warnings == 1, "Direct3D 11's lines: %s" % m.errors)
    m = validate.read_messages(ERRVK + "[VK-LIVE-WARN] id none: w\n[vulkan] ordinary line\n", "vulkan")
    check(list(m.errors) == ["VUID-vkCmdDraw-None-02699"] and m.warnings == 1, "Vulkan's lines, the ordinary [vulkan] line not counted: %s" % m.errors)
    m = validate.read_messages("[  12.345] " + ERR12, "d3d12")
    check(list(m.errors) == ["1234"], "a line with the host's time before it")
    check(validate.layer_state("[gpu] d3d11 debug-layer=1\n", "d3d11") is True and validate.layer_state("[gpu] d3d11 debug-layer=0\n", "d3d11") is False
          and validate.layer_state("nothing\n", "d3d11") is None and validate.layer_state("[gpu] d3d12 debug-layer=1\n", "d3d11") is None,
          "the start lines of Direct3D 11 and 12")
    check(validate.layer_state('[vulkan] device="X" api=1.3 driver=0x1 validation=1\n', "vulkan") is True
          and validate.layer_state('[vulkan] device="X" api=1.3 driver=0x1 validation=0\n', "vulkan") is False, "Vulkan's start line")


def layer_not_on(s, check):
    s.keep_main()
    original = validate.KNOWN
    try:
        plant_known(s, "")
        # The game started and said the layer is off in a run: tool.
        s.plan({}, logs={"a": {"all": "[gpu] d3d12 debug-layer=0\n"}})
        g = run_validate(s, s.gate(), None)
        r = rk.record_of(g, "a", "validate/d3d12")
        check(r["kind"] == "tool" and "was not on in this run" in r["message"], "a run whose layer is off is tool: %s" % r)
        # The layer is not installed (the probe says so): the check is left out with one plain line; never fail, tool or env.
        s.plan({})
        g = run_validate(s, s.gate(), None, probe=False)
        row = rk.row(g, "validate/d3d12")
        check(row["state"] == "pass" and row["detail"] == "not run: the Direct3D debug layer is not installed on this PC"
              and g.verdict == "pass" and not row["records"], "not installed: one plain line, not a fail: %s %s" % (row["state"], row["detail"]))
        check(s.faults == [("d3d12", "not run: the Direct3D debug layer is not installed on this PC")], "and a line for the progress page: %s" % s.faults)
        check(not (g.work / "runs-validate-d3d12").exists(), "nothing was run")
        check(validate.missing_layer_line("vulkan") == "not run: the Vulkan validation layer is not installed on this PC", "Vulkan's line")
        # tool-faults.tsv: once a day for each renderer, through the real writer.
        live = s.t / "live"
        g = s.gate(extra=["--live", str(live)])
        for _ in range(3):
            gate.layer_fault(g, "d3d12", "not run: the Direct3D debug layer is not installed on this PC")
        gate.layer_fault(g, "vulkan", "not run: the Vulkan validation layer is not installed on this PC")
        rows = [l.split("\t") for l in (live / "tool-faults.tsv").read_text(encoding="utf-8").splitlines()[1:]]
        check([(r[4], r[6]) for r in rows] == [("validate/d3d12", "not run: the Direct3D debug layer is not installed on this PC")],
              "tool-faults.tsv: one line a day for Direct3D's layer, none for Vulkan's (its absence is expected, AGENT.md section 9): %s" % rows)
        now = datetime.datetime(2026, 11, 22, 9, 0, 0)
        faults = [{"check": "validate/d3d12", "time": "2026-11-21T23:59:00"}]
        check(validate.fault_due(faults, "validate/d3d12", now) and not validate.fault_due(faults, "validate/d3d12", now.replace(day=21)),
              "a line from yesterday does not hold the next day's back; one from today does")
    finally:
        validate.KNOWN = original


def four_times_the_limit(s, check):
    s.keep_main()
    original = validate.KNOWN
    try:
        plant_known(s, "")
        check(gate.VALIDATE_ENV == {"LUMA_TEST_GPU_VALIDATION": "1", "LUMA_TEST_LIMIT_SCALE": "4"} and validate.LIMIT_SCALE == 4,
              "the lane asks the runner for the layer and four times the limits: %s" % gate.VALIDATE_ENV)
        s.plan({"a": {"first": "slow:50", "again": "slow:50"}}, logs={})
        g = run_validate(s, s.gate(), None)
        check((g.work / "runs-validate-d3d12" / "runs" / "x" / "b" / "d3d12" / "env.txt").read_text(encoding="utf-8") == "validation=1 scale=4",
              "the runner is started with the layer on and the limit scale 4")
        r = rk.record_of(g, "a", "validate/d3d12")
        check(r["kind"] == "tool" and r["message"].startswith("the validate lane's time limit is too short for this test (4 times the usual): too-slow:")
              and g.verdict == "not-judged", "a run at its limit while the game still moved is tool, never fail: %s %s" % (r, g.verdict))
        check(rk.phase_of(s, g, "a", "runs-validate-d3d12") == "first" and not (g.work / "rerun").exists(), "and it is not run again")
        # The runner's own words for the second kind of limit, not-reached (the game ran at full speed): the same.
        out = validate.judge_results("d3d12", {"c": ("fail", "not-reached: its end was not reached in 240 s although the game ran at full speed"),
                                               "d": ("fail", "the game crashed"), "e": ("env", "needs disc RMGP01")}, {}, {})[0]
        check(out["c"][0] == "tool" and out["d"] == ("fail", "the game crashed") and out["e"] == ("env", "needs disc RMGP01"),
              "only a run at its limit becomes tool: %s" % out)
    finally:
        validate.KNOWN = original


def known_list(s, check):
    p = s.t / "k.tsv"
    head = "renderer\tid\tledger row\tfirst words\n"
    p.write_text(head, encoding="utf-8")
    check(validate.read_known(p) == {}, "the header alone is an empty list")
    check(validate.read_known() == {}, "the list on main holds the header only until the first real run: %s" % validate.read_known())
    p.write_text("# c\n" + head + "d3d12\t12\tP-3\tfirst words\nvulkan\tVUID-x-1\tpending\tmore\n", encoding="utf-8")
    check(validate.read_known(p) == {("d3d12", "12"): ("P-3", "first words"), ("vulkan", "VUID-x-1"): ("pending", "more")}, "two rows")
    for text, why in ((head + "d3d9\t1\tP-1\tx\n", "a renderer that is none"), (head + "d3d12\t1\tsomething\tx\n", "a ledger row that is none"),
                      (head + "d3d12\t1\tP-1\n", "a row of three cells"), (head + "d3d12\t1\tP-1\tx\nd3d12\t1\tP-2\ty\n", "an id twice"),
                      ("d3d12\t1\tP-1\tx\n", "no header"), ("", "an empty file")):
        p.write_text(text, encoding="utf-8")
        try:
            validate.read_known(p)
            check(False, "refused: %s" % why)
        except validate.KnownError as error:
            check(bool(error.problems), "refused: %s" % why)
    check(validate.not_seen({("d3d12", "1"): ("P-1", "a"), ("d3d11", "2"): ("P-2", "b"), ("vulkan", "3"): ("P-3", "c")},
                            {"d3d12": {"1"}, "d3d11": set()}) == [("d3d11", "2", "P-2", "b")],
          "a listed id a whole tour no longer shows is named (a renderer that did not run is left out)")
    # A list that cannot be read makes the check a tool fault, never a pass.
    original = validate.KNOWN
    try:
        validate.KNOWN = p
        p.write_text("not a header\n", encoding="utf-8")
        s.keep_main()
        s.plan({})
        g = run_validate(s, s.gate(), None)
        row = rk.row(g, "validate/d3d12")
        check(row["state"] == "tool" and "k.tsv" in row["detail"], "a wrong list is tool: %s %s" % (row["state"], row["detail"]))
    finally:
        validate.KNOWN = original


def hashes(s, check):
    work = s.t / "w"
    name = "001-HoneyBeeKingdomGalaxy-1"
    recordings = s.t / "rec"
    recordings.mkdir()
    (recordings / (name + ".tsv")).write_text("# x\n0\th0\t3\n1\th1\t3\n", encoding="utf-8")

    def make(test, hs):
        folder = work / "runs" / "tour" / test / "d3d12"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "result.json").write_text(json.dumps({"verdict": "pass"}), encoding="utf-8")
        lines = [{"frame": f, "key": "hash.step", "value": {"h": h, "actors": 3}} for f, h in enumerate(hs)]
        (folder / "facts.jsonl").write_text("\n".join(json.dumps(l) for l in lines) + "\n", encoding="utf-8")
    make(name, ["h0", "hX"])
    make("002-Other-1", ["q"])
    results = {name: ("pass", ""), "002-Other-1": ("pass", ""), "gone": ("pass", "")}
    out = judged.hash_results(work, results, recordings)
    check(out[name][0] == "fail" and out[name][1].startswith("the step hashes differ from the recording: differs from step 1"),
          "a hash that differs from the recording is a fail: %s" % (out[name],))
    check(out["002-Other-1"] == ("pass", "") and out["gone"] == ("pass", ""), "a test with no recording, or no run, is left alone")
    make(name, ["h0", "h1"])
    check(judged.hash_results(work, results, recordings)[name] == ("pass", ""), "the same hashes pass")
    check(judged.hash_results(work, {name: ("fail", "boom")}, recordings)[name] == ("fail", "boom"), "a test that already failed is not re-judged")
    lines = ["changed  x/%s/d3d12/arrive  3 of 100 pixels, largest difference 4" % name, "same     x/other/d3d12/after", "machine  d3d12  other card"]
    out = judged.picture_results(lines, {name: ("pass", ""), "other": ("pass", "")})
    check(out[name][0] == "fail" and "arrive differs from its approved copy: 3 of 100 pixels" in out[name][1] and out["other"] == ("pass", ""),
          "a picture that differs from its approved copy is a fail of its test: %s" % (out[name],))


def the_runner_option(s, check):
    runner = TREE / "tools" / "lumacore" / "luma_test.py"
    done = subprocess.run([sys.executable, "-X", "utf8", "-I", str(runner), "run", "--help"], capture_output=True, text=True)
    check(done.returncode == 0 and "--gpu-validation" in done.stdout and "--limit-scale" in done.stdout, "luma_test.py run takes --gpu-validation")
    done = subprocess.run([sys.executable, "-X", "utf8", "-I", str(runner), "run", "x", "--bin", "y", "--gpu-validation", "2"],
                          capture_output=True, text=True)
    check(done.returncode == 2 and "invalid choice" in done.stderr, "0 or 1 only: %s" % done.stderr[-120:])
    text = runner.read_text(encoding="utf-8").replace("\r\n", "\n")
    check('    if getattr(args, "gpu_validation", 0):\n' in text
          and '        command += ["--gpu-validation", str(int(args.gpu_validation))]\n' in text,
          "run_one hands the option to the game when it is asked for (a real run proves the game answers: its start line says 1)")


CASES = [stress_passes_alone, stress_fails_alone, landing_flake_and_fail, asked_by_also_is_a_fail, slow_under_the_load, kept_whole,
         twelve_copies, the_twelve_lanes, the_watch_line, layer_errors, message_formats, layer_not_on, four_times_the_limit, known_list,
         hashes, the_runner_option]


def main():
    failures = []
    for case in CASES:
        found = []
        with tempfile.TemporaryDirectory(prefix="stress-", ignore_cleanup_errors=True) as tmp:
            s = rk.Scratch(tmp)
            try:
                case(s, lambda ok, what: ok or found.append(what))
            except Exception as error:  # noqa: BLE001 - a case that raised is a failed case, said with its name
                import traceback
                found.append("raised %s: %s\n%s" % (type(error).__name__, error, traceback.format_exc()))
            finally:
                s.close()
        print("%-28s %s" % (case.__name__, "pass" if not found else "FAIL"))
        for what in found:
            print("    " + what)
            failures.append("%s: %s" % (case.__name__, what))
    print("stress_validate_test.py: %s (%d cases)" % ("pass" if not failures else "FAILED", len(CASES)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
