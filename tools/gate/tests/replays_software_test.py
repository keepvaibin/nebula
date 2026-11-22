"""replays_software_test.py: the recorded plays on a software graphics device (package T9: tools/gate/replays_software.py, the runner's
and the replay tool's `--software-device`, and what the game's sources say of the option), without a game, a build or a graphics card.

  the_option_in_the_game   the launch option is declared, parsed and listed as known in host_main.cpp, written in the document, and
                           every renderer takes only a software device by it (adapter_allowed in Direct3D 12 and 11 and Vulkan, the
                           refusal line in Metal), none of them falling back to another renderer
  the_runner               luma_test.py run takes --software-device 0|1 and hands it to the game; it is refused with --device
  the_replay_tool          step_hash.py replay hands it to both builds' runner
  copies                   the plays run as many at once as memory and processors allow, at least one; the option's number as given
  window                   WxH from 64 to 16384
  probe                    the game's start on the software device says ok, none (the game's line) or fault; the layer is read
  judging                  a play at its limit with the game still moving is tool, a step hash that differs is a fail naming the first
                           step, a play that wrote no hashes is a fail, a crash stays a fail
  the_cause                a play that did not pass names the first line of its log that says why a renderer stopped (device lost, a layer's error)
  the_gate_part            gate.py's check `replays/software` (--also by name): the script's command, and its result.json read into the
                           check's tests (pass, fail, env for a renderer with no software device, tool for no result.json)
  the_whole_run            fakes for the game: the plays pass, one differs (VERDICT fail and its KIND line), no software device for
                           Direct3D 12 (env: not-judged), Vulkan with none (one plain line, the verdict unchanged), a layer that is not on
                           (one plain line), the files of the run folder
"""
import contextlib
import io
import json
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import judged  # noqa: E402,F401
import replays_software as rs  # noqa: E402
import step_hash  # noqa: E402

TWO_STOPS = ["HoneyBeeKingdomGalaxy 1", "OceanRingGalaxy 1"]
STEPS = 8


def source(path):
    return (TREE / path).read_text(encoding="utf-8").replace("\r\n", "\n")


def the_option_in_the_game(check):
    args = source("runtime/include/galaxy/launch_args.h")
    check("bool software_device = false;" in args, "launch_args.h declares software_device")
    host = source("native/host/host_main.cpp")
    check('key == "--software-device") {\n            args.software_device = argv[i + 1][0] == \'1\';' in host,
          "host_main.cpp parses --software-device 1 beside the other options")
    check('key == "--start-check" ||\n                   key == "--software-device") {' in host,
          "host_main.cpp lists it among the options read early, so the second loop does not die on it")
    doc = source("docs/using-nebula/command-line-arguments.md")
    check("| `--software-device 1` |" in doc and "[gpu] --software-device: no software device for RENDERER" in doc,
          "the document names the option and the line a start ends with")
    for path, what in (("runtime/src/render/renderer_d3d12.cpp", "Direct3D 12"), ("runtime/src/render/d3d11/d3d11_context.cpp", "Direct3D 11")):
        text = source(path)
        check("!adapter_allowed((" in text and "launch_args().software_device" in text, "%s takes an adapter by adapter_allowed" % what)
        check("software_device_missing_line(RendererKind::k%s)" % ("D3D12" if "d3d12" in path else "D3D11") in text,
              "%s ends with the missing line when there is none" % what)
    vk = source("runtime/src/render/vk/vk_context.cpp")
    check("adapter_allowed(candidate_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU" in vk
          and "software_device_missing_line(RendererKind::kVulkan)" in vk, "Vulkan takes only the processor type with the option")
    check("note_no_device();\n        return fail(refused" in vk and vk.index("software_device_missing_line") < vk.index("note_no_device();\n        return fail(refused"),
          "Vulkan says the missing line before the generic failure (which asks another renderer to take over), and not after it")
    metal = source("runtime/src/render/mtl/mtl_context.mm")
    check("software_device_missing_line(RendererKind::kMetal)" in metal, "Metal refuses the option with the line")
    d3d12 = source("runtime/src/render/renderer_d3d12.cpp")
    check(d3d12.index("software_device_missing_line") < d3d12.index("if (!same_card_only_) note_no_device();"),
          "Direct3D 12 says the missing line before note_no_device, so no other renderer is started in its place")
    d3d11 = source("runtime/src/render/d3d11/d3d11_context.cpp")
    check(d3d11.index("software_device_missing_line") < d3d11.index("        note_no_device();\n        failure_ = refused"),
          "Direct3D 11 says the missing line before note_no_device")


def the_runner(check):
    runner = TREE / "tools" / "lumacore" / "luma_test.py"
    done = subprocess.run([sys.executable, "-X", "utf8", "-I", str(runner), "run", "--help"], capture_output=True, text=True)
    check(done.returncode == 0 and "--software-device" in done.stdout, "luma_test.py run takes --software-device")
    done = subprocess.run([sys.executable, "-X", "utf8", "-I", str(runner), "run", "x", "--bin", "y", "--software-device", "2"],
                          capture_output=True, text=True)
    check(done.returncode == 2 and "invalid choice" in done.stderr, "0 or 1 only: %s" % done.stderr[-120:])
    with tempfile.TemporaryDirectory() as tmp:
        done = subprocess.run([sys.executable, "-X", "utf8", "-I", str(runner), "run", str(pathlib.Path(tmp) / "x"), "--bin", "y", "--work",
                               str(pathlib.Path(tmp) / "w"), "--lock", str(pathlib.Path(tmp) / "l"), "--software-device", "1", "--device", "serial"],
                              capture_output=True, text=True, env={"NEBULA_SELF_TEST": "1", "SYSTEMROOT": __import__("os").environ.get("SYSTEMROOT", "")})
        check(done.returncode != 0 and "--software-device" in done.stderr + done.stdout, "a device cannot take --software-device: %s" % (
            (done.stderr + done.stdout)[-160:],))
    text = source("tools/lumacore/luma_test.py")
    check('    if getattr(args, "software_device", 0):\n' in text and '        command += ["--software-device", "1"]\n' in text,
          "run_one hands the option to the game when it is asked for")


def the_replay_tool(check):
    seen = []
    real = subprocess.run
    subprocess.run = lambda command, **kw: seen.append(list(command))
    try:
        with tempfile.TemporaryDirectory() as tmp:
            work = pathlib.Path(tmp) / "a"
            step_hash.play(pathlib.Path(tmp) / "s" / "suite.json", pathlib.Path(tmp) / "bin", work, "d3d12", "", 1)
            step_hash.play(pathlib.Path(tmp) / "s" / "suite.json", pathlib.Path(tmp) / "bin", work, "d3d12", "")
    finally:
        subprocess.run = real
    check(len(seen) == 2 and seen[0][-2:] == ["--software-device", "1"], "play hands --software-device 1 to the runner: %s" % (seen[:1],))
    check(len(seen) == 2 and "--software-device" not in seen[1], "and nothing without it")
    text = source("tools/verify/step_hash.py")
    check("args.software_device" in text and '"--software-device", type=int, choices=(0, 1)' in text, "replay takes --software-device and hands it on")


def copies(check):
    gb = 2 ** 30
    check(rs.copies_for(64 * gb, 32, 40)[0] == 8, "32 processors, memory for many: 8 copies (4 processors each): %s" % (rs.copies_for(64 * gb, 32, 40),))
    check(rs.copies_for(10 * gb, 32, 40)[0] == 3, "10 GB free holds 3 (70%% of it at 2 GB each): %s" % (rs.copies_for(10 * gb, 32, 40),))
    check(rs.copies_for(1 * gb, 32, 40)[0] == 1, "less than one copy of room still asks for one (the wait for room says no)")
    check(rs.copies_for(64 * gb, 32, 3)[0] == 3, "never more than the plays")
    check(rs.copies_for(None, 2, 40)[0] == 1, "no memory reading sets no limit, 2 processors: one copy")
    check(rs.copies_for(64 * gb, 32, 40, asked=5)[0] == 5 and rs.copies_for(64 * gb, 32, 4, asked=9)[0] == 4, "the option's number, at most the plays")
    check("no graphics-card slot" in rs.copies_for(64 * gb, 32, 40)[1], "the note says no card slot is taken")


def gate_tour_default_window():
    """The window of a tour test that names none (the runner's default, luma_test.py read_test): what the recordings were made in."""
    import re
    text = source("tools/lumacore/luma_test.py")
    m = re.search(r"window = \[(\d+), (\d+)\]\n    if \"window\" in t:", text)
    return (int(m.group(1)), int(m.group(2))) if m else None


def window(check):
    check(rs.parse_window("640x360") == (640, 360) and rs.parse_window(" 1280x720 ") == (1280, 720), "WxH read")
    for bad in ("640", "63x360", "640x20000", "ax360", "640x360x1", ""):
        try:
            rs.parse_window(bad)
            check(False, "%r is refused" % bad)
        except ValueError:
            pass
    check(rs.WINDOW == (1280, 720) and rs.WINDOW == gate_tour_default_window(), "the default window is the size the plays were recorded in")


def probe(check):
    ok = rs.read_probe("d3d12", 0, "[gpu] d3d12 debug-layer=1\n[gpu] adapter=\"Microsoft Basic Render Driver\" software=1\nstart check: 30 frames drawn, 30 pictures shown\n")
    check(ok[0] == "ok" and ok[2] is True, "exit 0 is ok, with the layer on: %s" % (ok[0], ))
    off = rs.read_probe("d3d11", 0, "[gpu] d3d11 debug-layer=0\n")
    check(off[0] == "ok" and off[2] is False, "layer off is read")
    silent = rs.read_probe("d3d12", 0, "")
    check(silent[2] is None, "no layer line: unknown")
    none = rs.read_probe("vulkan", 3, "[gpu] --software-device: no software device for vulkan\nstart check: the renderer did not start\n")
    check(none[0] == "none", "the game's own line is none: %s" % (none[0],))
    fault = rs.read_probe("d3d12", 3, "start check: the renderer did not start: No hardware D3D12 adapter can run the renderer\n")
    check(fault[0] == "fault", "another failure is a fault, not none: %s" % (fault[0],))
    check(rs.read_probe("d3d12", -1073741819, "")[0] == "fault", "a crash is a fault")


def plant_recordings(folder, stops):
    """The recordings of two tests: STEPS steps each, as step_hash.py record writes them. Returns {test: [hash per step]}."""
    out = {}
    folder.mkdir(parents=True, exist_ok=True)
    for stop in stops:
        for index, stage, scenario in step_hash.gate_tour.stops():
            if "%s %d" % (stage, scenario) == stop:
                name = step_hash.gate_tour.test_for(index, stage, scenario)[0]
                hashes = ["%016x" % (index * 1000 + s) for s in range(STEPS)]
                folder.joinpath(name + ".tsv").write_text("# planted\n" + "".join("%d\t%s\t5\n" % (s, h) for s, h in enumerate(hashes)), encoding="utf-8")
                out[name] = hashes
    return out


def plant_run(work, test, renderer, hashes, verdict="pass", kind="pass", reason="", seconds=77.0, log=None, size=None):
    """What the runner leaves for a play: runs/tour/<test>/<renderer>/result.json, facts.jsonl and run.log."""
    folder = pathlib.Path(work) / "runs" / rs.SUITE / test / renderer
    folder.mkdir(parents=True, exist_ok=True)
    folder.joinpath("result.json").write_text(json.dumps({"test": test, "verdict": verdict, "kind": kind, "reason": reason, "seconds": seconds}), encoding="utf-8")
    lines = [{"frame": s, "key": "hash.step", "value": {"h": h, "actors": 5}} for s, h in enumerate(hashes)]
    folder.joinpath("facts.jsonl").write_text("\n".join(json.dumps(l) for l in lines) + "\n", encoding="utf-8")
    if size is not None:   # the window the runner gave the game (settings/config.ini)
        (folder / "settings").mkdir(exist_ok=True)
        (folder / "settings" / "config.ini").write_text("vsync_interval=0\nwindow_width=%d\nwindow_height=%d\n" % size, encoding="utf-8")
    folder.joinpath("run.log").write_text(log if log is not None else "[D3D12-LIVE-ERR] id 5: planted\n[D3D12-LIVE-WARN] id 6: planted\n", encoding="utf-8")


def judging(check):
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        recs = plant_recordings(t / "rec", TWO_STOPS[:1])
        name, hashes = next(iter(recs.items()))
        real = step_hash.RECORDINGS
        step_hash.RECORDINGS = t / "rec"
        try:
            plant_run(t / "same", name, "d3d12", hashes)
            check(rs.judge_play("pass", "", t / "same", name) == ("pass", ""), "the same hashes pass")
            wrong = list(hashes)
            wrong[3] = "deadbeef00000000"
            plant_run(t / "wrong", name, "d3d12", wrong)
            kind, message = rs.judge_play("pass", "", t / "wrong", name)
            check(kind == "fail" and "differs from step 3" in message, "a hash that differs from the recording is a fail naming step 3: %s %s" % (kind, message))
            plant_run(t / "size", name, "d3d12", hashes, size=(640, 360))
            check(rs.judge_play("pass", "", t / "size", name) == ("tool", "recorded at 1280x720, run at 640x360"),
                  "a play at another window than the recordings' is a tool fault by its size, not a hash fail: %s" % (rs.judge_play("pass", "", t / "size", name),))
            plant_run(t / "size-wrong", name, "d3d12", wrong, size=(640, 360))
            check(rs.judge_play("pass", "", t / "size-wrong", name)[0] == "tool", "at another window, other hashes are not compared either")
            plant_run(t / "size-same", name, "d3d12", hashes, size=(1280, 720))
            check(rs.judge_play("pass", "", t / "size-same", name) == ("pass", ""), "at the recordings' window the same hashes pass")
            plant_run(t / "none", name, "d3d12", [])
            kind, message = rs.judge_play("pass", "", t / "none", name)
            check(kind == "fail" and "no hashes" in message, "a pass that wrote no hashes is a fail: %s %s" % (kind, message))
            kind, message = rs.judge_play("env", "too-slow: still moving when its time limit of 1920 s passed (the game ran at 11% of full speed)", t / "same", name)
            check(kind == "tool" and "time limit of this check is too short" in message, "at the limit with the game moving (too-slow) is tool: %s %s" % (kind, message))
            kind, message = rs.judge_play("fail", "not-reached: its end was not reached in 1920 s although the game ran at full speed", t / "same", name)
            check(kind == "tool", "not-reached is tool too: %s" % kind)
            kind, message = rs.judge_play("fail", "hung: the game completed no step for 120 s", t / "same", name)
            check(kind == "fail" and message.startswith("hung"), "a hang (T6's rule) stays a fail: %s %s" % (kind, message))
            kind, message = rs.judge_play("fail", "crashed: dump x", t / "same", name)
            check(kind == "fail", "a crash stays a fail")
            kind, message = rs.judge_play("env", "not-run: the drive is full", t / "same", name)
            check(kind == "env", "another env stays env")
        finally:
            step_hash.RECORDINGS = real


class Args:
    def __init__(self, work, bin_dir, **kw):
        self.work, self.bin, self.pak = str(work), str(bin_dir), None
        self.renderers, self.limit_scale, self.copies = "d3d12,d3d11,vulkan", 8.0, 2
        self.who, self.commit, self.window, self.stops = "test", "abc", "640x360", list(TWO_STOPS)
        for k, v in kw.items():
            setattr(self, k, v)


def whole_run(check, label, probes, wrong=(), crash=(), crash_once=(), hang_once=(), logs=None, **kw):
    """run_all with fakes for the game: `probes` {renderer: (state, layer)}; `wrong` the (renderer, test index) whose hash differs at step 3."""
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        recs = plant_recordings(t / "rec", TWO_STOPS)
        names = sorted(recs)
        bin_dir = t / "bin"
        bin_dir.mkdir()
        for exe in rs.EXES.values():
            (bin_dir / exe).write_bytes(b"")
        started = []

        def fake_probe(binary, renderer, out, layer=True):
            state, on = probes[renderer]
            text = "[gpu] --software-device: no software device for %s\n" % renderer if state == "none" else ""
            return state, text, on

        def fake_play(command, log, limit, popen=None):
            started.append(list(command))
            work = pathlib.Path(command[command.index("--work") + 1])
            renderer, test = command[command.index("--renderer") + 1], command[command.index("--test") + 1]
            hashes = list(recs[test])
            if (renderer, names.index(test)) in wrong:
                hashes[3] = "0badf00d00000000"
            again = work.name.endswith("-alone")
            key = (renderer, names.index(test))
            if key in hang_once and not again:
                plant_run(work, test, renderer, hashes[:3], verdict="hung", kind="fail", reason="the game completed no step for 120 s")
            elif key in crash_once and not again:
                plant_run(work, test, renderer, hashes[:3], verdict="crashed", kind="fail", reason="dump x; last lines: crash: minidump failed")
            elif (renderer, names.index(test)) in crash:
                log = "[ 69.031] exception: thrown: C++ exception std::runtime_error: device lost: a GPU fence never completed\n[ 69.038] crash: unhandled exception 0x0000087D\n"
                plant_run(work, test, renderer, hashes[:3], verdict="crashed", kind="fail", reason="dump x; last lines: crash: minidump failed", log=log)
            else:
                plant_run(work, test, renderer, hashes, log=(logs or {}).get((renderer, names.index(test))))
            return 0, 1.0

        real = (rs.start_probe, rs.play_one, step_hash.RECORDINGS)
        rs.start_probe, rs.play_one, step_hash.RECORDINGS = fake_probe, fake_play, t / "rec"
        said = []
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                code = rs.run_all(Args(t / "work", bin_dir, **kw), say=said.append)
        finally:
            rs.start_probe, rs.play_one, step_hash.RECORDINGS = real
        files = sorted(p.name for p in (t / "work").iterdir() if p.is_file())
        verdict = json.loads((t / "work" / "result.json").read_text(encoding="utf-8")) if (t / "work" / "result.json").is_file() else {}
        flat = json.loads((t / "work" / "verdict.json").read_text(encoding="utf-8")) if (t / "work" / "verdict.json").is_file() else {}
        return code, said, started, files, verdict, flat


def the_whole_run(check):
    on = {"d3d12": ("ok", True), "d3d11": ("ok", True), "vulkan": ("none", None)}
    code, said, started, files, verdict, flat = whole_run(check, "pass", on)
    text = "\n".join(said)
    check(code == 0 and said[-1] == "VERDICT pass", "every play the same: VERDICT pass, exit 0: %s %s" % (code, said[-3:]))
    check(len(started) == 4 and sorted(set(c[c.index("--renderer") + 1] for c in started)) == ["d3d11", "d3d12"],
          "two plays on each of the two Direct3D renderers and none on Vulkan: %d started" % len(started))
    check(all("--software-device" in c and c[c.index("--software-device") + 1] == "1" and "--gpu-validation" in c for c in started),
          "each play is started with --software-device 1 and the debug layer")
    check(rs.NO_VULKAN in text, "Vulkan with none: the plain line")
    check(not any(l.startswith("KIND ") for l in said), "no KIND line for a run that passed")
    check({"result.json", "verdict.json", "result.txt"} <= set(files), "the run folder has result.json, verdict.json, result.txt: %s" % files)
    check(flat.get("verdict") == "pass" and flat.get("exit") == 0 and verdict.get("check") == "replays/software", "the files say pass")
    check("d3d12 debug layer: 2 errors and 2 warnings" in text, "the layer's messages are counted and named, not judged: %s" % text[-400:])

    code, said, started, files, verdict, flat = whole_run(check, "differs", on, wrong={("d3d11", 1)})
    kinds_lines = [l for l in said if l.startswith("KIND ")]
    check(code == 1 and said[-1] == "VERDICT fail", "one play that differs: VERDICT fail, exit 1: %s" % said[-2:])
    check(len(kinds_lines) == 1 and kinds_lines[0].startswith("KIND fail replays/software/d3d11 ") and "differs from step 3" in kinds_lines[0],
          "one KIND line, the renderer and the first step that differs: %s" % kinds_lines)

    code, said, _, _, _, _ = whole_run(check, "crash", on, crash={("d3d12", 1)})
    crashed = [l for l in said if l.startswith("KIND ")]
    check(code == 1 and len(crashed) == 1 and crashed[0].startswith("KIND fail replays/software/d3d12 ") and "crashed: dump x" in crashed[0]
          and "(the game's log: [ 69.031] exception: thrown: C++ exception std::runtime_error: device lost: a GPU fence never completed)" in crashed[0],
          "a crash is a fail whose KIND line names the game's own cause line: %s" % crashed)

    off = {"d3d12": ("ok", False), "d3d11": ("ok", False), "vulkan": ("none", None)}
    code, said, _, _, _, _ = whole_run(check, "layer off", off)
    check(code == 0 and any(l == "replays_software.py: " + rs.LAYER_LINE % "Direct3D" for l in said) and sum(rs.LAYER_LINE % "Direct3D" in l for l in said) == 1,
          "no layer: one plain line, the plays run: %s" % said[:4])
    check(not any("--gpu-validation" in l for l in said), "no layer in the text")

    gone = {"d3d12": ("none", None), "d3d11": ("ok", True), "vulkan": ("none", None)}
    code, said, started, _, _, _ = whole_run(check, "no d3d12", gone)
    check(code == 2 and said[-1] == "VERDICT not-judged" and any(l.startswith("KIND env replays/software/d3d12") for l in said),
          "no software device for Direct3D 12: env, not-judged, exit 2: %s" % said[-3:])
    check(len(started) == 2 and all("d3d11" in c for c in started), "the renderer that has one still plays")

    broken = {"d3d12": ("fault", None), "d3d11": ("ok", True), "vulkan": ("none", None)}
    code, said, _, _, _, _ = whole_run(check, "fault", broken)
    check(code == 2 and any(l.startswith("KIND tool replays/software/d3d12") for l in said), "a start that failed in another way is tool: %s" % said[-3:])

    code, said, _, _, _, _ = whole_run(check, "vulkan has one", {"d3d12": ("ok", True), "d3d11": ("ok", True), "vulkan": ("ok", False)})
    check(code == 0 and rs.NO_VULKAN not in "\n".join(said), "a software Vulkan device would be played, with no 'not run' line")


def the_play_again(check):
    """A play that crashed or hung is played once more alone after the others: a pass is flake (the first cause in its message), a second
    crash is fail; a step hash that differs is never played again."""
    on = {"d3d12": ("ok", True), "d3d11": ("ok", True), "vulkan": ("none", None)}
    code, said, started, _, verdict, _ = whole_run(check, "crash once", on, crash_once={("d3d12", 1)})
    lines = [l for l in said if l.startswith("KIND ")]
    check(code == 0 and said[-1] == "VERDICT pass", "a crash that passes alone: the run passes: %s" % said[-3:])
    check(len(lines) == 1 and lines[0].startswith("KIND flake replays/software/d3d12 ") and "failed once, passed alone; the first run: crashed: dump x" in lines[0],
          "the play is a flake and says the first run's cause: %s" % lines)
    check(len(started) == 5 and started[-1][started[-1].index("--work") + 1].endswith("-alone") and "d3d12" in started[-1],
          "five plays started, the fifth alone and last: %s" % [c[c.index("--work") + 1][-40:] for c in started])
    check(any("played again alone" in l for l in said), "it says that it plays the one again")
    code, said, started, _, _, _ = whole_run(check, "hang once", on, hang_once={("d3d11", 0)})
    lines = [l for l in said if l.startswith("KIND ")]
    check(code == 0 and len(lines) == 1 and lines[0].startswith("KIND flake replays/software/d3d11 ") and "hung" in lines[0], "a hang that passes alone is a flake: %s" % lines)
    code, said, started, _, _, _ = whole_run(check, "crash twice", on, crash={("d3d12", 1)})
    lines = [l for l in said if l.startswith("KIND ")]
    check(code == 1 and len(started) == 5 and len(lines) == 1 and lines[0].startswith("KIND fail replays/software/d3d12 ")
          and "failed again alone: crashed: dump x" in lines[0] and "the first run: crashed: dump x" in lines[0],
          "a crash that crashes again alone is a fail with both causes: %s" % lines)
    code, said, started, _, _, _ = whole_run(check, "hash differs", on, wrong={("d3d11", 1)})
    check(code == 1 and len(started) == 4, "a step hash that differs is not played again: %d plays started" % len(started))
    results = {("d3d12", "a"): ("fail", "crashed: x", 1.0, ""), ("d3d12", "b"): ("fail", "the step hashes differ from the recording: differs from step 3", 1.0, ""),
               ("d3d12", "c"): ("env", "too-slow: x", 1.0, ""), ("d3d12", "d"): ("tool", "no result.json", 1.0, ""), ("d3d11", "e"): ("fail", "hung: the game completed no step for 120 s", 1.0, "")}
    again = []
    out = rs.play_again(Args("w", "b"), "suite", results, {}, lambda text: None, play=lambda a, suite, renderer, test, layers, folder: again.append((renderer, test, folder)) or ("pass", "", 2.0, ""))
    check(sorted(again) == [("d3d11", "e", "e-alone"), ("d3d12", "a", "a-alone")], "only crashed and hung plays are played again: %s" % again)
    check(out[("d3d12", "b")] == results[("d3d12", "b")] and out[("d3d12", "c")] == results[("d3d12", "c")] and out[("d3d12", "d")] == results[("d3d12", "d")],
          "the others are left as they were")


def the_removal(check):
    """A play whose log says the graphics device was removed is a fail that names the step, though it ends and its hashes are the recorded ones."""
    quiet = "[ 1.0] host: frame 40, file reads 1\n[ 2.0] host: frame 80, file reads 1\n"
    check(rs.removal_note(quiet) == "", "a log with no removal: nothing said")
    log = ("[ 13.0] host: frame 510, file reads 1\n[ 14.0] host: frame 548, file reads 1\n[Renderer] DEVICE REMOVED reason=0x887a0005\n"
           "[ 22.4] extension: harness: frame 0 is game frame 994\n")
    note = rs.removal_note(log)
    check("removed after game frame 548, before the play's step 0 (game frame 994)" in note and "DEVICE REMOVED reason=0x887a0005" in note, "before the first step: said so: %s" % note)
    later = log.replace("frame 548", "frame 1100").replace("frame 510", "frame 1000")
    check("after game frame 1100, at step 106 of the play" in rs.removal_note(later), "after the first step: the step is named: %s" % rs.removal_note(later))
    check("graphics device was removed" in rs.removal_note("[render] the graphics card was reset: device removed while polling a GPU fence\n"), "the game's own reset line counts too")
    results = {("d3d12", "a"): ("pass", "", 5.0, log), ("d3d12", "b"): ("pass", "", 5.0, quiet), ("d3d12", "c"): ("fail", "crashed: x", 5.0, log)}
    out, removed = rs.judge_removals(results, say=lambda text: None)
    check(removed == 2, "two plays lost the device: %s" % removed)
    check(out[("d3d12", "a")][0] == "fail" and "the graphics device was removed after game frame 548" in out[("d3d12", "a")][1], "a pass with a removed device is a fail that names the step: %s" % (out[("d3d12", "a")],))
    check(out[("d3d12", "b")] == results[("d3d12", "b")] and out[("d3d12", "c")] == results[("d3d12", "c")], "a play with no removal, and one that failed, are left as they were")
    on = {"d3d12": ("ok", True), "d3d11": ("ok", True), "vulkan": ("none", None)}
    code, said, _, _, verdict, _ = whole_run(check, "removed", on, logs={("d3d12", 1): log})
    kinds_lines = [l for l in said if l.startswith("KIND ")]
    check(code == 1 and said[-1] == "VERDICT fail" and len(kinds_lines) == 1 and kinds_lines[0].startswith("KIND fail replays/software/d3d12 ")
          and "the graphics device was removed after game frame 548" in kinds_lines[0], "a play that passed with a removed device is a fail, its KIND line names the step: %s" % kinds_lines)
    check(any("the graphics device was removed in 1 play" in l for l in said), "the count is said")
    code, said, _, _, _, _ = whole_run(check, "not removed", on, logs={("d3d12", 1): quiet})
    check(code == 0 and said[-1] == "VERDICT pass", "the same play without the removal passes")


def the_history_after_a_remake(check):
    """The three renderers that make an upload ring again at a frame boundary (Direct3D 12, Vulkan, Metal) leave the vertex and matrix rings' segment
    counter and segment fences as they are (P-339): a ring that was not made again keeps the segments the frames in flight use. There is no
    device-free test of the counter itself (it is renderer state in begin_frame); this reads the sources."""
    for path, what in (("runtime/src/render/renderer_d3d12.cpp", "Direct3D 12"), ("runtime/src/render/vk/renderer_vk.cpp", "Vulkan"),
                       ("runtime/src/render/mtl/renderer_mtl.mm", "Metal")):
        text = source(path)
        a = text.index("if (remake_upload_rings(")
        block = text[a:text.index("}", text.index("history_used_ = true;", a))]
        check("history_used_ = true;" in block, "%s: a remake makes the next frame take its own segment" % what)
        check("history_segment_ = 0u" not in block and "history_segment_fence_.fill(0u)" not in block,
              "%s: a remake leaves history_segment_ and history_segment_fence_ as they are" % what)


def the_cause(check):
    check(rs.cause_line("a\n  [  1.0] exception: device lost:   a GPU fence never completed \nb\n[D3D12-LIVE-ERR] id 1: x") ==
          "[ 1.0] exception: device lost: a GPU fence never completed", "the first line that names a stopped renderer, blanks squeezed")
    check(rs.cause_line("[D3D12-LIVE-ERR] id 921: ID3D12PipelineState::<final-release>: CORRUPTION") .startswith("[D3D12-LIVE-ERR] id 921"), "a layer's error")
    check(rs.cause_line("[D3D12-LIVE-WARN] id 6: planted\nall fine\n") == "" and rs.cause_line("") == "", "a warning and a quiet log name none")
    check(len(rs.cause_line("device lost " + "x" * 500)) == 200, "cut at 200 characters")


def the_gate_part(check):
    """gate.py's `replays/software` (--also by name): the script's command, and its result.json read into the check's tests."""
    import types
    import gate
    import kinds
    names = ["048-HoneyBeeKingdomGalaxy-1", "068-OceanRingGalaxy-1"]
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        g = types.SimpleNamespace(work=t, bin=str(t / "bin"), commit="abc123", paks_text=lambda: "RMGE01=X.pak",
                                  args=types.SimpleNamespace(build_root=str(t)))
        seen, plant = [], {}

        def fake_run(command, log, env=None, cwd=None, timeout=None):
            seen.append((list(command), dict(env or {}), cwd))
            pathlib.Path(log).write_text("the script said this", encoding="utf-8")
            if plant.get("results") is not None:
                out = pathlib.Path(command[command.index("--work") + 1])
                rows = rs.rows_of(plant["plan"], plant["results"], names, [])
                rs.write_run(out, rows, [], 0.0, "abc123", rs.plays_of(plant["results"], names))
            return plant["code"]

        real = gate.run
        gate.run = fake_run
        try:
            good = {(r, n): ("pass", "", 5.0, "") for r in ("d3d12", "d3d11") for n in names}
            plant.update(plan={"d3d12": None, "d3d11": None}, results=good, code=0)
            passed, detail, cases = gate.replays_software_check(g)(t / "replays-software.log")
            check(passed and isinstance(cases, kinds.Cases) and len(cases.results) == 4 and all(v[0] == "pass" for v in cases.results.values()),
                  "a run that passed: four plays, each a test of the check: %s %s" % (passed, cases.results if cases else None))
            command, env, cwd = seen[-1]
            check(command[command.index("--bin") + 1] == g.bin and str(command[command.index("run") - 1]).endswith("replays_software.py")
                  and env.get("LUMA_TEST_PAKS") == "RMGE01=X.pak" and "--commit" in command and cwd == str(t),
                  "the script's command: the gate's bin, the paks in the environment: %s %s" % (command, env))
            check(set(cases.results) == {"d3d12/" + n for n in names} | {"d3d11/" + n for n in names}, "the tests are <renderer>/<play>: %s" % sorted(cases.results))

            bad = dict(good)
            bad[("d3d11", names[1])] = ("fail", "the step hashes differ from the recording: differs from step 3", 5.0, "")
            plant.update(results=bad, code=1)
            passed, detail, cases = gate.replays_software_check(g)(t / "replays-software.log")
            check(not passed and cases.results["d3d11/" + names[1]][0] == "fail", "a play that differs is a fail test: %s" % (cases.results,))
            records, state = kinds.judge_cases(cases)
            check(state == "fail" and len(records) == 1 and "differs from step 3" in records[0]["message"], "the check's state is fail with that play: %s" % (records,))

            plant.update(plan={"d3d12": ("env", "no software device for d3d12 on this PC"), "d3d11": None},
                         results={k: v for k, v in good.items() if k[0] == "d3d11"}, code=2)
            passed, detail, cases = gate.replays_software_check(g)(t / "replays-software.log")
            records, state = kinds.judge_cases(cases, passed=passed)
            check(not passed and cases.results["d3d12"][0] == "env" and state == "env", "a renderer with no software device is env: %s %s" % (state, cases.results))

            plant.update(results=None, code=1)
            out = gate.replays_software_check(g)(t / "replays-software.log")
            check(out[0] is False and out[3] == "tool" and out[1].startswith("no result.json"), "a script that left no result.json is tool: %s" % (out,))
        finally:
            gate.run = real


CASES = [the_option_in_the_game, the_runner, the_replay_tool, copies, window, probe, judging, the_whole_run, the_play_again, the_removal, the_history_after_a_remake, the_cause, the_gate_part]


def main():
    failures = []
    for case in CASES:
        found = []
        try:
            case(lambda ok, what, found=found: ok or found.append(what))
        except Exception as error:  # noqa: BLE001 - a case that raised is a failed case, said with its name
            import traceback
            found.append("raised %s: %s\n%s" % (type(error).__name__, error, traceback.format_exc()))
        print("%-26s %s" % (case.__name__, "pass" if not found else "FAIL"))
        for what in found:
            print("    " + what)
            failures.append("%s: %s" % (case.__name__, what))
    print("replays_software_test.py: %s (%d cases)" % ("pass" if not failures else "FAILED", len(CASES)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
