"""replays_software.py: the recorded plays on a software graphics device (package T9; the check `replays/software`), without a graphics card.

The game's steps do not depend on how fast the pictures are drawn. This check plays the recorded plays of tests/replays (the 20 stops
of tools/verify/step_hash.py RECORDED_STOPS, 600 steps each) on Microsoft's software device, Direct3D 12 and Direct3D 11, with the game's
`--software-device 1` (docs/using-nebula/command-line-arguments.md) at the window size the plays were recorded in, and compares the hash
of every step with the recorded one (tests/replays/*.tsv): the same recordings Direct3D 12 on a graphics card is compared with. Pictures
are not compared. It takes no graphics-card slot: the plays run as many copies at once as the PC's memory and processors allow
(copies_for).

The window is 1280x720, the size of the recordings. Every play runs at smaller 16:9 sizes down to 128x72, but the step hashes of three
plays (EggStarGalaxy 1, PhantomGalaxy 1, StarDustGalaxy 1) then differ from the recordings, the same on the software device and on a
graphics card (measured 2026-11-21 at 128x72, 160x90, 320x180, 640x360 and 960x540): the steps of those plays depend on the window's
size. So the smallest window at which all the recorded plays match is the recorded one.

Vulkan is played only where the PC has a software Vulkan device (the game's start with the option says so); else one plain line says
"not run: this PC has no software Vulkan device". The Direct3D debug layer (`--gpu-validation 1`) is on where it is installed; where it
is not, one plain line says so and the plays run without it. Its messages are counted and named, not judged here (the validate lane
judges them).

A play is
  pass  it reached its end and every step hash is the recorded one;
  fail  it crashed, the game completed no step for 120 s (the runner's `hung`, T6's rule), a step hash differs from the recorded one
        (the first step that differs is named), or the game's graphics device was removed during it (its log says so and the game makes a
        new device, so the play can still end with the recorded hashes; the message names the step; no play is excused);
  flake a play that crashed or hung (the runner's `crashed` or `hung`) was played once more alone, after all the others, and passed
        then: its message holds the first run's cause. One that fails again alone is `fail`. A step hash that differs, a play at its
        time limit and a tool or environment fault are never played again.
  tool  the runner or this script broke (no result file, a play that ran past its limit with the game still moving: the check's time limit
        is too short, never a fault of the game), or no step hash was written;
  env   the PC was not fit: no software device for Direct3D 12 or 11, no room for one copy.
The time limit of a play is the usual one (240 s) times LIMIT_SCALE: the seconds of one play measured on the software device against the
same play on the card (25 s and 5.7 s: 4.4), times two.

    python -X utf8 -I tools/gate/replays_software.py run --bin BIN --work WORK [--pak PAK] [--renderers d3d12,d3d11,vulkan]
                                  [--window 1280x720] [--limit-scale N] [--copies N] [--stops "Stage N" ...] [--who NAME]
    python -X utf8 -I tools/gate/replays_software.py write OUT [--window WxH] [--stops "Stage N" ...]    the suite only, hashed

`run` ends with one `KIND <kind> <check> <test> :: <message>` line for each play that is not a pass and then `VERDICT pass`, `VERDICT
fail` or `VERDICT not-judged` (exit 0, 1, 2; tools/gate/kinds.py), and writes result.json, verdict.json and result.txt in WORK.
"""
import argparse
import datetime
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import gate_tour  # noqa: E402
import judged  # noqa: E402
import kinds  # noqa: E402
import machine  # noqa: E402
import step_hash  # noqa: E402
import validate  # noqa: E402

RENDERERS = ("d3d12", "d3d11", "vulkan")
EXES = {"d3d12": "Nebula.exe", "vulkan": "NebulaVk.exe", "d3d11": "NebulaD11.exe"}
RUNNER = TREE / "tools" / "lumacore" / "luma_test.py"
SUITE = "tour"
# The window of every play: the size the plays were recorded in (the steps of three plays depend on the window's size; see above).
WINDOW = step_hash.RECORDED_WINDOW
# The usual limit of a tour play (gate_tour.test_for limit_seconds) times this is the limit of a play here: the measured factor of the
# software device (seconds of one play on it over the same play on the card: 25.0 over 5.7 for HoneyBeeKingdomGalaxy 1) times two, up.
LIMIT_SCALE = 9.0
# The processors one copy takes: the software device draws on its own threads, so a copy is more than one processor's work.
CORES_PER_COPY = 4
START_FRAMES = 30
# The runner's words for a play that reached its time limit while the game still moved (luma_test.py evaluate).
LIMIT_MESSAGES = ("too-slow", "not-reached")
NO_DEVICE = re.compile(r"\[gpu\] --software-device: no software device for (\w+)")
LAYER_LINE = "the %s debug layer is not installed on this PC: the plays run without it"
NO_VULKAN = "not run: this PC has no software Vulkan device"
CHECK = "replays/software"


def parse_window(text):
    """(width, height) of "WxH", whole numbers from 64 to 16384 (the runner's rule)."""
    m = re.fullmatch(r"(\d+)x(\d+)", str(text).strip())
    if m is None or not all(64 <= int(g) <= 16384 for g in m.groups()):
        raise ValueError("a window is WxH, each from 64 to 16384, not %r" % (text,))
    return int(m.group(1)), int(m.group(2))


def hashed_tour(out, stops=None, window=WINDOW):
    """The recorded plays written as a suite under `out`, each hashed from its first step. Returns suite.json's path."""
    suite = gate_tour.write(out, list(stops) if stops else list(step_hash.RECORDED_STOPS), window=window)
    step_hash.with_hash(suite.parent)
    return suite


def test_names(suite):
    return json.loads(pathlib.Path(suite).read_text(encoding="utf-8"))["tests"]


# --- What the machine has -------------------------------------------------------------------------------------------------------------


def copies_for(memory_free, cpus, plays, asked=None):
    """(copies, note): the plays run at once: as many as 70% of the PC's free memory holds (2 GB each), and as the processors allow
    (CORES_PER_COPY each), at most `plays` and at least one. `asked`: the option's number (used as given, at most `plays`)."""
    if asked is not None:
        copies = max(1, min(int(asked), plays))
        return copies, "%d plays at once, as asked" % copies
    by_memory = machine.copies_at_once(None, memory_free, machine.DEFAULT_NEED, plays)[2]
    by_cpus = max(1, int(cpus or 1) // CORES_PER_COPY)
    copies = max(1, min(by_memory, by_cpus, plays))
    return copies, "%d plays at once (memory holds %d, the %s processors %d, %d plays); no graphics-card slot" % (
        copies, by_memory, cpus, by_cpus, plays)


class NoCardProbes(machine.Probes):
    """machine.Probes with no card to read: the software device uses none."""

    def card(self):
        raise OSError("no graphics card is used")


def start_probe(binary, renderer, out, layer=True):
    """(state, text, layer): the game's own start on the software device (--start-check, no game data): state `ok` (a picture was
    shown), `none` (the game said there is no software device for the renderer) or `fault` (anything else); `text` its output; `layer`
    whether the debug layer is on (True, False, or None when the start did not say)."""
    for sub in ("data", "settings"):
        (out / sub).mkdir(parents=True, exist_ok=True)
    command = [str(binary), "--start-check", str(START_FRAMES), "--software-device", "1", "--data", str(out / "data"),
               "--settings", str(out / "settings"), "--crash-dumps", str(out)]
    if layer:
        command += ["--gpu-validation", "1"]
    startup = {}
    if os.name == "nt":
        info = subprocess.STARTUPINFO()
        info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        info.wShowWindow = 0  # SW_HIDE: a test's start
        startup = {"startupinfo": info}
    try:
        done = subprocess.run(command, capture_output=True, text=True, errors="replace", timeout=300, **startup)
        code, text = done.returncode, (done.stdout or "") + (done.stderr or "")
    except (OSError, subprocess.TimeoutExpired) as error:
        return "fault", "the start could not be run: %s" % error, None
    return read_probe(renderer, code, text)


def read_probe(renderer, code, text):
    """(state, text, layer) of a probe's exit code and output."""
    state = "ok" if code == 0 else ("none" if NO_DEVICE.search(text) else "fault")
    return state, text, validate.layer_state(text, renderer)


# --- Playing ----------------------------------------------------------------------------------------------------------------------


def task_command(suite, binary, pak, work, who, renderer, test, scale, layer, lock):
    """The runner's command for one play."""
    command = [sys.executable, "-X", "utf8", "-I", str(RUNNER), "run", str(suite), "--bin", str(binary), "--work", str(work),
               "--who", who, "--lock", str(lock), "--renderer", renderer, "--test", test, "--software-device", "1",
               "--limit-scale", "%g" % scale]
    if pak:
        command += ["--pak", str(pak)]
    if layer:
        command += ["--gpu-validation", "1"]
    return command


def end_tree(process):
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True)
    else:
        process.kill()


def play_one(command, log, limit_seconds, popen=subprocess.Popen):
    """Runs one play's command with its output in `log`; returns (exit code, seconds). The wall limit of the runner's own (the play's limit
    and the runner's wait for the game's output) is past before this one: a process still there then is ended (exit 124)."""
    began = time.time()
    with open(log, "wb") as f:
        process = popen(command, stdout=f, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=limit_seconds)
        except subprocess.TimeoutExpired:
            end_tree(process)
            process.wait()
            code = 124
    return code, time.time() - began


def result_of(work, test, renderer):
    """(kind, message, seconds) of one play from its runner's result.json, before the hashes are read."""
    path = kinds.result_path(work, SUITE, test, renderer)
    kind, message = kinds.read_result(path, lambda verdict: "tool")
    seconds = None
    try:
        seconds = json.loads(pathlib.Path(path).read_text(encoding="utf-8")).get("seconds")
    except (OSError, ValueError, AttributeError):
        pass
    return kind, message, seconds


def judge_play(kind, message, work, test):
    """(kind, message) of one play after the step hashes are read: a play at its time limit with the game still moving is tool (the
    limit of this check is too short), a pass whose hashes differ from the recording is fail, one that wrote none is fail."""
    if kind in ("env", "fail") and message.split(":", 1)[0] in LIMIT_MESSAGES:
        return "tool", "the time limit of this check is too short for this play: %s" % message
    if kind == "pass":
        return judged.hash_results(work, {test: (kind, message)})[test]
    return kind, message


def play_task(a, suite, renderer, test, layers, folder):
    """One play in the folder `folder` of the run's work folder: (kind, message, seconds, log text)."""
    limit = 240 * a.limit_scale + 900  # the runner's limit with its own grace and the wait for the game's output
    work = pathlib.Path(a.work) / renderer / folder
    work.mkdir(parents=True, exist_ok=True)
    command = task_command(suite, a.bin, a.pak, work, a.who, renderer, test, a.limit_scale, layers.get(renderer), work / "run.lock")
    code, seconds = play_one(command, work / "harness.out", limit)
    kind, message, game_seconds = result_of(work, test, renderer)
    if code == 124:
        kind, message = "tool", "the runner did not end within %d s and was ended" % limit
    elif kind == "tool" and message.startswith("no result.json"):
        said = last_line(work / "harness.out")
        message = "%s (the runner said: %s)" % (message, said)
    kind, message = judge_play(kind, message, work, test)
    logs = read_logs(work, test, renderer)
    if kind != "pass" and cause_line(logs):
        message = "%s (the game's log: %s)" % (message, cause_line(logs))
    return kind, message, game_seconds if game_seconds is not None else seconds, logs


def say_play(say, renderer, test, result):
    kind, message, seconds, _ = result
    say("%-6s %-34s %-5s %s" % (renderer, test, kind, ("%.0f s" % seconds) + (": " + message if message else "")))


def play_all(a, suite, tasks, layers, copies, say):
    """Plays every (renderer, test) of `tasks` with `copies` at once, each in a folder of its own. Returns {(renderer, test):
    (kind, message, seconds, log text)}. Idle copies take the next play: the order of the tasks is the order they start in."""
    results, lock, queue = {}, threading.Lock(), list(tasks)

    def worker():
        while True:
            with lock:
                if not queue:
                    return
                renderer, test = queue.pop(0)
            result = play_task(a, suite, renderer, test, layers, test)
            with lock:
                results[(renderer, test)] = result
            say_play(say, renderer, test, result)

    threads = [threading.Thread(target=worker) for _ in range(max(1, copies))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return results


def crashed_or_hung(result):
    """Whether a play's result is the runner's `crashed` or `hung` (the game ended on its own fault or stopped stepping), the only plays
    that are played again: a step hash that differs, a time limit, a tool or an environment fault never is."""
    kind, message = result[0], result[1]
    return kind == "fail" and message.split(":", 1)[0] in ("crashed", "hung")


def play_again(a, suite, results, layers, say, play=play_task):
    """The plays that crashed or hung are played once more, one at a time, alone, after all the others, each in a folder of its own
    (<test>-alone). One that passes alone is kind `flake` and its message holds the first run's cause; one that fails again is `fail`
    and holds both. Returns {(renderer, test): result} with the plays played again replaced. No line goes to flakes.tsv: the gate's own
    settling (kinds.py Settler) does not read this check's plays."""
    out = dict(results)
    for (renderer, test), first in sorted(results.items(), key=lambda item: item[0][1] + item[0][0]):
        if not crashed_or_hung(first):
            continue
        say("%-6s %-34s played again alone: it %s" % (renderer, test, first[1][:160]))
        second = play(a, suite, renderer, test, layers, test + "-alone")
        say_play(say, renderer, test, second)
        if second[0] == "pass":
            out[(renderer, test)] = ("flake", "failed once, passed alone; the first run: %s" % first[1], second[2], second[3])
        else:
            out[(renderer, test)] = ("fail", "failed again alone: %s; the first run: %s" % (second[1], first[1]), second[2], second[3])
    return out


# What a play's log says when the game's graphics device was removed and the game made a new one (the play still ends, and its hashes can
# still be the recorded ones): the removal's own line, and the line the game says when it starts a new device.
REMOVED_MARKERS = ("[Renderer] DEVICE REMOVED", "the graphics card was reset")
HOST_FRAME = re.compile(r"host: frame (\d+),")
HARNESS_START = re.compile(r"harness: frame 0 is game frame (\d+)")


def removal_note(text):
    """'' when the log has no device removal; else where it was: the last game frame the host counted before the removal's line (the host
    counts one line a second), as a step of the play (the play's step 0 is the game frame the harness says) or, when that frame is before
    it, as before the first step."""
    lines = text.splitlines()
    for at, line in enumerate(lines):
        if not any(marker in line for marker in REMOVED_MARKERS):
            continue
        frame = None
        for earlier in reversed(lines[:at]):
            m = HOST_FRAME.search(earlier)
            if m:
                frame = int(m.group(1))
                break
        start = HARNESS_START.search(text)
        where = "at the start of the log"
        if frame is not None:
            begin = int(start.group(1)) if start else None
            where = ("after game frame %d, at step %d of the play" % (frame, frame - begin) if begin is not None and frame >= begin else
                     "after game frame %d, before the play's step 0%s" % (frame, " (game frame %d)" % begin if begin is not None else ""))
        return "the graphics device was removed %s: %s" % (where, " ".join(line.split())[:120])
    return ""


def judge_removals(results, say=print):
    """A play that passed with a log that says the device was removed is a `fail` whose message names the step (no play is excused). A play
    that did not pass is left as it is. Returns (results, how many plays lost their device)."""
    out, removed = dict(results), 0
    for (renderer, test), (kind, message, seconds, logs) in sorted(results.items()):
        note = removal_note(logs)
        if not note:
            continue
        removed += 1
        if kind == "pass":
            out[(renderer, test)] = ("fail", note, seconds, logs)
            say("%-6s %-34s %s" % (renderer, test, note))
    return out, removed


def last_line(path):
    try:
        lines = [l.strip() for l in pathlib.Path(path).read_text(encoding="utf-8", errors="replace").splitlines() if l.strip()]
    except OSError:
        return ""
    return lines[-1][:200] if lines else ""


# The lines of a play's log that say why a renderer stopped; the first one is named in the result of a play that did not pass, so the
# KIND line carries the cause (the runner's own text for a crash is the end of the log).
CAUSE_MARKERS = ("device lost", "device removed", "the renderer stopped", "the graphics card was reset", "[D3D12-LIVE-ERR]", "[D3D11-LIVE-ERR]")


def cause_line(text):
    """The first line of a log that names a stopped renderer or a debug layer's error, cut at 200 characters, or ''."""
    for line in text.splitlines():
        if any(marker in line for marker in CAUSE_MARKERS):
            return " ".join(line.split())[:200]
    return ""


def read_logs(work, test, renderer):
    path = pathlib.Path(work) / "runs" / SUITE / test / renderer / "run.log"
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def layer_counts(results, renderer):
    """(errors, warnings) of the debug layer's messages in the logs of a renderer's plays (counted, not judged)."""
    total = validate.Messages()
    for (r, _), (_, _, _, text) in results.items():
        if r == renderer:
            total.add(validate.read_messages(text, renderer))
    return sum(c for c, _ in total.errors.values()), total.warnings


# --- The verdict --------------------------------------------------------------------------------------------------------------------


def rows_of(plan, results, tests, notes):
    """[row]: one for each renderer of the plan, `check` replays/software/<renderer>, its `records` the plays that are not a pass.
    `plan` {renderer: None, or (kind, message) when the whole renderer could not run}."""
    rows = []
    for renderer, whole in plan.items():
        if whole is not None:
            records = [kinds.record("-", whole[0], whole[1])]
            rows.append({"check": "%s/%s" % (CHECK, renderer), "state": kinds.state_of(records), "detail": whole[1], "records": records,
                         "seconds": 0.0})
            continue
        mine = {t: results[(renderer, t)][:2] for t in tests if (renderer, t) in results}
        cases = kinds.Cases(mine, declared=len(tests), renderer=renderer)
        if len(mine) != len(tests):
            cases.problem = "ran %d plays, %d declared" % (len(mine), len(tests))
        records, state = kinds.judge_cases(cases)
        seconds = sorted(results[(renderer, t)][2] for t in tests if (renderer, t) in results and results[(renderer, t)][2] is not None)
        detail = kinds.describe(cases, records)
        if seconds:
            detail += "; a play %.0f s at the least, %.0f s at the most" % (seconds[0], seconds[-1])
        rows.append({"check": "%s/%s" % (CHECK, renderer), "state": state, "detail": detail, "records": records, "seconds": sum(seconds)})
    return rows


def plays_of(results, tests):
    """[{renderer, test, kind, message, seconds}]: every play that ran, in the order of the tests (result.json's `plays`)."""
    return [{"renderer": r, "test": t, "kind": v[0], "message": v[1], "seconds": v[2]}
            for t in tests for r in RENDERERS if (r, t) in results for v in [results[(r, t)]]]


def write_run(work, rows, notes, started, commit="", plays=()):
    """result.json, verdict.json and result.txt in the run's folder; returns (verdict, the summary lines)."""
    verdict = kinds.verdict_of([r["records"] for r in rows])
    lines = kinds.kind_lines(rows)
    seconds = round(time.time() - started, 1)
    now = datetime.datetime.fromtimestamp(started).isoformat(timespec="seconds")
    body = {"check": CHECK, "verdict": verdict, "passed": verdict == "pass", "started": now, "seconds": seconds, "commit": commit,
            "notes": notes, "checks": rows, "plays": list(plays)}
    work = pathlib.Path(work)
    work.mkdir(parents=True, exist_ok=True)
    (work / "result.json").write_text(json.dumps(body, indent=2) + "\n", encoding="utf-8")
    flat = [dict(check=r["check"], **rec) for r in rows for rec in r["records"]]
    (work / "verdict.json").write_text(json.dumps({"verdict": verdict, "exit": kinds.EXIT_OF[verdict], "lines": lines, "results": flat,
                                                   "notes": notes, "checks": [{"check": r["check"], "state": r["state"], "detail": r["detail"]}
                                                                             for r in rows]}, indent=2) + "\n", encoding="utf-8")
    head = ["%s: %s (%.0f s)" % (CHECK, verdict.upper(), seconds)]
    head += ["  %-28s %-7s %s" % (r["check"], r["state"], r["detail"]) for r in rows]
    # The notes were said as they came; the file keeps them with the result.
    (work / "result.txt").write_text("\n".join(head[:1] + ["  " + n for n in notes] + head[1:] + lines + [kinds.verdict_line(verdict)]) + "\n",
                                    encoding="utf-8")
    return verdict, head + lines


def cases_of(work):
    """(results, detail) of a finished run, for the gate's check: {name: (kind, message)} for every play ("<renderer>/<test>") and for each
    renderer that could not run at all ("<renderer>"), and the run's detail line; None when the run left no result.json."""
    try:
        body = json.loads((pathlib.Path(work) / "result.json").read_text(encoding="utf-8"))
        plays = body["plays"]
        rows = body["checks"]
    except (OSError, ValueError, KeyError, TypeError):
        return None
    results = {"%s/%s" % (p["renderer"], p["test"]): (p["kind"], p["message"]) for p in plays}
    for row in rows:
        whole = [r for r in row["records"] if r["test"] == "-"]
        if whole:
            results[row["check"].rsplit("/", 1)[-1]] = (whole[0]["kind"], whole[0]["message"])
    detail = "; ".join(r["detail"] for r in rows) if rows else ""
    return results, detail


# --- run ---------------------------------------------------------------------------------------------------------------------------------


def run_all(a, say=print):
    started = time.time()
    work = pathlib.Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    binary = pathlib.Path(a.bin)
    window = parse_window(a.window)
    renderers = [r for r in a.renderers.split(",") if r]
    for r in renderers:
        if r not in RENDERERS:
            say("replays_software.py: %s is no renderer (%s)" % (r, ", ".join(RENDERERS)))
            return 2
    notes, plan, layers = [], {}, {}

    def note(text):
        if text not in notes:
            notes.append(text)
            say("replays_software.py: " + text)
    # Each renderer's own start on the software device, no game data: it says whether there is one, and whether the debug layer is on.
    for renderer in renderers:
        exe = binary / EXES[renderer]
        if not exe.is_file():
            plan[renderer] = ("tool", "%s is not in %s" % (EXES[renderer], binary))
            continue
        state, text, layer = start_probe(exe, renderer, work / ("start-" + renderer))
        (work / ("start-%s.log" % renderer)).write_text(text, encoding="utf-8")
        if state == "none" and renderer == "vulkan":
            note(NO_VULKAN)
            continue
        if state == "none":
            plan[renderer] = ("env", "no software device for %s on this PC" % renderer)
        elif state == "fault":
            plan[renderer] = ("tool", "the game's start on the software device failed: %s" % (last_line(work / ("start-%s.log" % renderer)) or "no output"))
        else:
            plan[renderer] = None
            layers[renderer] = layer is not False
            if layer is False and renderer in ("d3d12", "d3d11"):
                note(LAYER_LINE % "Direct3D")
    runnable = [r for r in renderers if plan.get(r, "x") is None]
    suite = hashed_tour(work / SUITE, a.stops, window)
    tests = test_names(suite)
    # The renderers' plays are started in turn so each renderer's first plays come early.
    tasks = [(r, t) for t in tests for r in runnable]
    copies, why = copies_for(machine.memory_free(), os.cpu_count(), len(tasks), a.copies)
    note("window %dx%d; %s" % (window[0], window[1], why))
    results = {}
    if tasks:
        room = machine.wait_for_room(NoCardProbes(), machine.DEFAULT_NEED, copies, say)
        if room.env:
            for r in runnable:
                plan[r] = ("env", room.env)
        else:
            copies = max(1, min(copies, room.copies))
            results = play_all(a, suite, tasks, layers, copies, say)
            results = play_again(a, suite, results, layers, say)
            results, removed = judge_removals(results, say)
            if removed:
                note("the graphics device was removed in %d play%s" % (removed, "" if removed == 1 else "s"))
    for r in runnable:
        if plan[r] is None and layers.get(r):
            errors, warnings = layer_counts(results, r)
            line = "%s debug layer: %d error%s and %d warning%s in the plays' logs (counted here, judged by the validate lane)" % (
                r, errors, "" if errors == 1 else "s", warnings, "" if warnings == 1 else "s")
            note(line)
    rows = rows_of(plan, results, tests, notes)
    verdict, lines = write_run(work, rows, notes, started, a.commit, plays_of(results, tests))
    for line in lines:
        say(line)
    say(kinds.verdict_line(verdict))
    return kinds.EXIT_OF[verdict]


def add_plays(s):
    s.add_argument("--window", default="%dx%d" % WINDOW, help="the window of every play, WxH (the size the plays were recorded in)")
    s.add_argument("--stops", nargs="*", default=[], help='only these recorded stops, "Stage N" each (default: all of them)')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run")
    r.add_argument("--bin", required=True)
    r.add_argument("--work", required=True)
    r.add_argument("--pak", default=None, help="the game.pak (default: the bin folder's pak.path, or LUMA_TEST_PAKS)")
    r.add_argument("--renderers", default=",".join(RENDERERS))
    r.add_argument("--limit-scale", type=float, default=LIMIT_SCALE)
    r.add_argument("--copies", type=int, default=None, help="plays at once (default: by the PC's memory and processors)")
    r.add_argument("--who", default="replays-software")
    r.add_argument("--commit", default="")
    add_plays(r)
    w = sub.add_parser("write")
    w.add_argument("out")
    add_plays(w)
    a = p.parse_args(argv)
    if a.command == "write":
        print(hashed_tour(a.out, a.stops, parse_window(a.window)))
        return 0
    if a.limit_scale < 1:
        p.error("--limit-scale is at least 1")
    return run_all(a)


if __name__ == "__main__":
    sys.exit(main())
