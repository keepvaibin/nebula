"""pace.py: the pacing measures of docs/plan/00-READ-FIRST.md 4.3 and the planner's bars (brief AD), from the game's [pace] lines.

  pace.py run --bin BIN --work WORK [--renderers d3d12,vulkan,d3d11] [--suite tests/pace/suite.json]
              [--device SERIAL [--device-lock PATH] [--apk APK --pak PAK]] [--lock D:/NebulaWork/plan/measure.lock]
              [--lock-wait MINUTES] [--quiet-minutes MINUTES] [--paks ...] [--diagnostics NAMES] [--history FILE] [--again]
              [--method auto|frametimeline|latency] [--start step|vsync] [--trace-processor PATH] [--baseline FILE]
      Takes the measurement lock (waiting --lock-wait minutes for it); with --quiet-minutes, waits for a quiet machine before
      each PC run (tools/measure/quiet.py: no compiler, build tool or other copy of the game), and a machine still busy after
      that wait plays nothing more: WORK/pace-run.json names what kept it busy, as measure.py leaves a run that was not quiet unmeasured
      (pacing read beside another game is not the change's). Runs the suite through the harness (tools/lumacore/luma_test.py:
      real time, a visible window, no sound, a run lock of its own in WORK; a list such as gate.json as harness_calls says)
      on each renderer, or on an Android device,
      then digests the runs. With --apk and --pak the device's run installs the test app with the pak inside the device's
      lock and removes it after, as measure.py's tablet run does. --diagnostics: the game's --diagnostics for every run
      (`pace-fault`: the render thread sleeps 20 ms once every 2 s, the planted stutter the checks must refuse). --again: a
      renderer whose run has a FAIL row is played once more and the second run decides (the first stays in WORK/first).
      A device run also writes the game's timeline (--diagnostics timeline: its step moments) and takes the platform's records
      of the frames (tools/gate/frametimeline.py: Perfetto's FrameTimeline trace, SurfaceFlinger's --latency history).
  pace.py digest WORK [--history FILE] [--method ...] [--start ...] [--trace-processor PATH] [--baseline FILE]
      Reads every run under WORK (not WORK/first), writes WORK/pace.json and prints the table. A device run's capture is read
      to its frametimeline.json (--method, --start: frametimeline.py; the trace by --trace-processor or NEBULA_TRACE_PROCESSOR)
      and gives its rows: refreshes from a frame's start to its scan-out, the misses by cause (a miss of no named cause, Unknown,
      fails above 0.05% of the frames), the game's own split of its late presents beside them, the GPU's composition; with
      --baseline (whose method and start then hold) the run against the stored figures of its rate. Each run's header prints the
      game's [platform] lines (what the platform offers the pacing, G10.3); a device run missing one of PLATFORM_KINDS fails the
      row "what the platform offers".
  pace.py keep WORK --history FILE --commit SHA
      Adds each run's figures of WORK/pace.json to FILE, the readings of main that the load row is judged against (the
      gate keeps them after main moved).

The game writes a [pace] line every ten seconds of steps (runtime/include/galaxy/pacing_recorder.h). A run's measured
windows are those written after the harness's "frame 0" line (the test in its stage) except the first after it, which
may have begun before; the others are its load windows (the boot, the title, the stage's load). Each measure gets its
value, its target and pass, FAIL, recorded, or not measured (and why). The bars of brief AD:
  - bar 1, "even pictures" (the PC, VSync on): no missed refresh of the game's and no doubled picture in the measured
    windows. A missed refresh whose present call had returned half a refresh before the refresh it was due at is the
    system's (pacing.h Summary::missed_system): reported beside, not refused.
  - bar 2, "jumps": no two pictures shown one after the other whose (display moment - content moment) differ by more than
    1 ms, except across a seam (a closed wipe, a new swap chain, another refresh period: pacing.h Picture::seam).
  - the display lead in play (brief AD: a lead change in play is itself a jump; read from the game's `[display-lead] a->b refreshes`
    lines after frame 0, those between the wipe closing and the scene ready being a load's): until G10.5 lands (LOADS "main") one
    lead change in a run, up or down, is filed "lead moved" and refuses nothing (the line has no covered flag yet, so a step down
    under a closed wipe cannot be told from one in play), two or more fail; from G10.5 on a step up is
    filed only where its [pace] window shows LEAD_MISSES missed refreshes of the game's, every other change in play fails.
  - "animation error" (within 1 ms of the run's median) is a report only since brief AD, and so is the in-between delay's p99
    in a run whose lead moved in play (two leads in one figure: the lead row judges). With the lead still the delay is judged
    against its bound as before.
  - "loads": the game's missed refreshes and the jumps of the load windows. While LOADS is "main" (until G10.5 lands) a
    run is refused only when either count is above the most of main's last LOAD_READINGS readings of the same run in
    --history (fewer than LOAD_FLOOR = 3 readings yet: recorded, refused nothing); with LOADS "none" (from G10.5 on) any miss or jump is refused.
One row reads every window from the start, the boot's loads included: each shows at least nine tenths of the pictures its
rate asks for (a lead held high or a stalled schedule shows fewer: R-009, 36 pictures in ten seconds hid before frame 0).
Exit status: 0 when nothing failed and every run gave windows, 1 otherwise; 3 when the measurement lock stayed held, or
when a renderer was not played because the machine stayed busy and no run that was played failed.

The gate's two checks (tools/gate/gate.py):
  pace/pc      every landing that builds the game: tests/pace/gate.json (the test match cut to about 30 s of play) on
               Direct3D 12 and Vulkan, --again, the loads judged against the gate's pace-history.tsv; a FAIL row refuses.
  pace/tablet  an extra check by name (docs/plan/native/step-checks.tsv): tests/pace/tablet.json (rate-60, rate-120) on the
               tablet, under the tablet's lock and the measurement lock. Until tests/pace/tablet-base.json is stored (G10.2m,
               from frametimeline.py baseline), it records its rows and refuses nothing. With it stored, the late share,
               three-refresh share and unnamed share (the frame timeline's rows) of a run are set against the most a run of
               the baseline had at that rate (the row "against the stored baseline"); the row is a report (BASELINE_REFUSES
               False, M-011): the most of three runs is no bound for a fourth, the late share at 120 Hz varies from 8.7% to
               14.6% between clean runs (brief AD: the share figures are a report, the bars refuse). A baseline that cannot
               judge a run (another method, no frames) is still the tool's fault. The step that classifies the shares (G10.4m)
               sets BASELINE_REFUSES, or replaces the row.
               G10.3 adds a second row, "what the platform offers": a device run whose log lacks a line of what the platform
               offers (PLATFORM_KINDS) is refused by it, which pace/tablet refuses by.
"""
import argparse
import datetime
import json
import math
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

TREE = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import frametimeline  # noqa: E402

STEP_MS = 1000.0 / 60.0
PACE = re.compile(r"\[pace\] (.*)$")
FRAME_ZERO = re.compile(r"harness: frame 0 is game frame")
# The Vulkan renderer's own report of its presents (renderer_vk.cpp report_present_timing): late split by whether the picture
# reached the compositor after its time (available late) or was ready and shown later than asked (delayed late).
PRESENT_TIMING = re.compile(r"\[present-timing\] (\d+) frames: .*? (\d+) late, .*?late\(available/delayed\)=(\d+)/(\d+)")
# The display model's report of a change of its lead (runtime/src/render/frame_schedule.cpp begin_waiting): the lead in refreshes.
LEAD = re.compile(r"\[display-lead\] (-?\d+)->(-?\d+) refreshes")
# A load's lines: the wipe closes, the old scene is destroyed, the new one is ready. A lead change between them is a load's, not play's.
LOAD_OPENS = re.compile(r"held under the wipe|game: scene: \[(destroy|initialize) Scene")
LOAD_ENDS = re.compile(r"game: scene: \[scene ready\]")
# From G10.5 on a step up of the lead in play is allowed only where the window it fell in shows this many of the game's missed refreshes.
LEAD_MISSES = 2
# Display reports must cover this share of a run's pictures before its display measures are judged.
COVERAGE = 0.99
# Every window from the start shows at least this share of the pictures its rate asks for.
SHOWN = 0.9
# The load windows' rule: "main" (refused only above main's readings) until G10.5 lands; "none" (any miss or jump) from it on.
LOADS = "main"
LOAD_READINGS = 5
# The fewest readings of main the loads row is judged against: a load window's count varies from run to run (Vulkan's was 11 and
# then 24 on the same code), so one or two readings are no baseline. Below it the row records and refuses nothing.
LOAD_FLOOR = 3
HISTORY_COLUMNS = ("time", "commit", "run", "load_missed", "load_jumps", "load_windows", "missed", "missed_system", "jumps")
# A first run kept beside the one that decides (--again): WORK/first/<renderer>; digest leaves it out.
FIRST = "first"
# The exit status when the measurement lock stayed held (the machine's, not the change's: the gate's kind env).
LOCK_HELD = 3
# The exit status when a renderer was not played because the machine stayed busy (the machine's too).
NOT_QUIET = 3
# A device run's row against a stored baseline (frametimeline.judge). A share above the stored most is a FAIL of this row only when
# BASELINE_REFUSES is set (pace/tablet refuses by it); until the step that classifies the shares (G10.4m) it is a report: "recorded",
# the shares above the most named (M-011; brief AD: the shares are a report, bars 1 and 2 refuse; the planner's order of 11-26).
BASELINE_ROW = "against the stored baseline"
BASELINE_REFUSES = False
# What the platform offers the pacing (G10.3): the game's `[platform] <kind>: <value>` lines (perf_hint.cpp, display_clock.cpp,
# host_window_sdl.cpp, renderer_vk.cpp), printed in each run's header. A device run must hold one line of each PLATFORM_KINDS with a
# value ("not supported (<why>)" is one), the frame timeline's at the run's own rate; one missing is a FAIL of PLATFORM_ROW, which
# pace/tablet refuses by beside the baseline row. Other kinds (hint-calls) are printed and not required.
PLATFORM = re.compile(r"\[platform\] ([a-z-]+): ?(.*?)\s*$")
PLATFORM_KINDS = ("hint-features", "frame-timeline", "game-mode", "thermal-thresholds", "vulkan-images")
PLATFORM_ROW = "what the platform offers"
PLATFORM_RATE = re.compile(r"([0-9.]+) Hz:")


def windows_of(log_text, from_start=False):
    """The run's measured [pace] windows, each a dict of its numbers; with from_start every window, the boot's too."""
    after = False
    seen_after = 0
    out = []
    for line in log_text.splitlines():
        if FRAME_ZERO.search(line):
            after, seen_after = True, 0
            continue
        m = PACE.search(line)
        if not m or not (after or from_start):
            continue
        seen_after += 1
        if seen_after == 1 and not from_start:
            continue  # may have begun before frame 0
        out.append({k: float(v) for k, v in re.findall(r"([a-z_0-9]+)=([-0-9.]+)", m.group(1))})
    return out


def leads_of(log_text):
    """The lead changes the game told after the harness's frame 0, each {before, after (refreshes), load (between the wipe closing and
    the scene ready), misses (the game's missed refreshes in the [pace] window the line fell in, None with no window after it)}."""
    after, loading, out, waiting = False, False, [], []
    for line in log_text.splitlines():
        if FRAME_ZERO.search(line):
            after, loading, out, waiting = True, False, [], []
            continue
        if not after:
            continue
        if LOAD_OPENS.search(line):
            loading = True
        elif LOAD_ENDS.search(line):
            loading = False
        m = LEAD.search(line)
        if m:
            lead = {"before": int(m.group(1)), "after": int(m.group(2)), "load": loading, "misses": None}
            out.append(lead)
            waiting.append(lead)
            continue
        m = PACE.search(line)
        if m:
            figures = {k: float(v) for k, v in re.findall(r"([a-z_0-9]+)=([-0-9.]+)", m.group(1))}
            for lead in waiting:
                lead["misses"] = figures.get("missed", 0.0) - figures.get("missed_system", 0.0)
            waiting = []
    return out


def lead_row(leads):
    """The row of the display lead in play (brief AD: a lead change in play is itself a visible jump; one step up to stop repeated
    misses counts as one event and is reported). Until G10.5 lands (LOADS "main") ONE lead change in play per run, up or down, is
    filed "lead moved" and refused nothing; two or more fail. This is a stopgap: the game's [display-lead] line has no covered
    flag yet (the lead may step down under a closed loading wipe), and G10.5 adds it and removes this rule. From G10.5 on (LOADS "none") a step up is filed only where its
    window shows LEAD_MISSES of the game's missed refreshes, and every other change in play fails. Changes inside a load are counted
    beside and judged by the loads row's own rule."""
    play = [l for l in leads if not l["load"]]
    at_load = len(leads) - len(play)
    target = ("no change in play; one change reported" if LOADS == "main"
              else "no change in play; one step up after %d misses reported" % LEAD_MISSES)
    if not play:
        return ("display lead in play", "no change%s" % (", %d at loads" % at_load if at_load else ""), target, "pass")
    ups = [l for l in play if l["after"] > l["before"]]
    downs = [l for l in play if l["after"] < l["before"]]
    text = ", ".join("%d->%d refreshes" % (l["before"], l["after"]) for l in play)
    value = "%s in play%s" % (text, ", %d at loads" % at_load if at_load else "")
    if LOADS == "main":
        # Stopgap, ends with G10.5 (the covered flag on the [display-lead] line): one change in a run, either way, is a report.
        ok = len(play) == 1
    else:
        ok = not downs and len(ups) == 1 and ups[0]["misses"] is not None and ups[0]["misses"] >= LEAD_MISSES
    return ("display lead in play", value, target, "lead moved" if ok else "FAIL")


def pictures_asked(window, test):
    """The pictures a window's rate asks for with VSync on: the refresh, or the test's rate on whole refreshes
    (144 on 240 Hz: every second refresh, 120); None where nothing fixes it (VSync off, an unlimited rate)."""
    if setting(test, "GALAXY_VSYNC_INTERVAL", "1") == "0" or window.get("refresh_ms", 0.0) <= 0:
        return None
    refresh = 1000.0 / window["refresh_ms"]
    try:
        rate = float(setting(test, "GALAXY_MAX_FPS", "0"))
    except ValueError:
        return None
    if rate <= 0 or rate >= refresh * 0.99:
        rate = refresh
    else:
        rate = refresh / math.ceil(refresh / rate - 0.01)  # 239.97 Hz over 120: 2
    return rate * window.get("seconds", 0.0)


def shown_from_start(windows, test):
    """The pictures-shown row: every window from the start against what its rate asks for."""
    asked = [(w, pictures_asked(w, test)) for w in windows]
    asked = [(w, a) for w, a in asked if a]
    if not asked:
        return None
    shares = [w.get("pictures", 0.0) / a for w, a in asked]
    worst = min(range(len(shares)), key=lambda i: shares[i])
    missed = sum(w.get("missed", 0.0) for w in windows)
    return ("pictures shown, from the start", "fewest %.0f%% (window %d of %d, %d pictures); %d missed in all" % (
        100 * shares[worst], worst + 1, len(windows), asked[worst][0].get("pictures", 0.0), missed),
        "every window %d%% of its rate" % (100 * SHOWN), "pass" if shares[worst] >= SHOWN else "FAIL")


def combine(windows):
    """One run's windows as one: counts summed, shares and averages weighted, percentiles and longest the largest."""
    if not windows:
        return None
    s = lambda k: sum(w.get(k, 0.0) for w in windows)
    top = lambda k: max(w.get(k, 0.0) for w in windows)

    def weighted(k, weight):
        total = sum(weight(w) for w in windows)
        return sum(w.get(k, 0.0) * weight(w) for w in windows) / total if total > 0 else 0.0
    seconds = s("seconds")
    return {
        "windows": len(windows), "seconds": seconds, "steps": s("steps"),
        "steps_per_second": sum(w["steps"] - 1 for w in windows) / seconds if seconds > 0 else 0.0,
        "pictures": s("pictures"), "displayed": s("displayed"), "generated": s("generated"),
        "refresh_ms": windows[-1].get("refresh_ms", 0.0), "on_time": s("on_time"), "missed": s("missed"),
        "doubled": s("doubled"), "missed_system": s("missed_system"),
        # Lines of a build before the jump count (G10.1) carry no jumps: None, not 0.
        "jumps": s("jumps") if all("jumps" in w for w in windows) else None, "jump_max_us": top("jump_max_us"),
        "animation_within_1ms": weighted("animation_within_1ms", lambda w: w.get("displayed", 0.0)),
        "animation_p99_us": top("animation_p99_us"),
        "delay_avg_us": weighted("delay_avg_us", lambda w: w.get("displayed", 0.0)), "delay_p99_us": top("delay_p99_us"),
        "render_avg_us": weighted("render_avg_us", lambda w: w.get("pictures", 0.0) - w.get("generated", 0.0)),
        "render_p99_us": top("render_p99_us"),
        "generated_render_avg_us": weighted("generated_render_avg_us", lambda w: w.get("generated", 0.0)),
        "generated_render_p99_us": top("generated_render_p99_us"),
        "step_work_avg_us": weighted("step_work_avg_us", lambda w: w.get("steps", 0.0)),
        "step_work_p99_us": top("step_work_p99_us"), "step_work_max_us": top("step_work_max_us"),
    }


def setting(test, key, default=""):
    # A run folder's test.json holds the runner's record, the test under "test"; a test file holds it at the top.
    for a in test.get("settings", test.get("test", {}).get("settings", [])):
        k, _, v = a.partition("=")
        if k == key:
            return v
    return default


def load_figures(windows):
    """The load windows' (game's missed refreshes, jumps); jumps None where the lines carry none."""
    missed = sum(w.get("missed", 0.0) - w.get("missed_system", 0.0) for w in windows)
    jumps = sum(w.get("jumps", 0.0) for w in windows) if all("jumps" in w for w in windows) else None
    return missed, jumps


def load_row(run, history):
    """The loads row: the load windows' game misses and jumps, against main's readings (LOADS)."""
    loads = run.get("load_windows", [])
    if not loads:
        return None
    missed, jumps = load_figures(loads)
    value = "%d missed, %s jumps in %d windows" % (missed, "?" if jumps is None else "%d" % jumps, len(loads))
    if LOADS == "none":
        ok = missed == 0 and jumps == 0
        return ("loads", value, "no missed refresh, no jump", "pass" if ok else "FAIL")
    readings = history.get(run["run_key"], [])[-LOAD_READINGS:]
    if len(readings) < LOAD_FLOOR:
        return ("loads", value, "%d of main's readings, %d needed to judge: recorded" % (len(readings), LOAD_FLOOR), "recorded")
    most_missed = max(r["load_missed"] for r in readings)
    most_jumps = max(r["load_jumps"] for r in readings)
    ok = missed <= most_missed and (jumps is None or jumps <= most_jumps)
    return ("loads", value, "at most main's most of its last %d (%d missed, %d jumps)" % (len(readings), most_missed, most_jumps),
            "pass" if ok else "FAIL")


def present_timing_of(log_text):
    """The game's own split of its late presents (row 3 of the frame timeline): (frames, late, available late, delayed late) over
    the [present-timing] windows after frame 0, the first after it left out as for the [pace] windows; None where there is none."""
    after, seen, sums = False, 0, None
    for line in log_text.splitlines():
        if FRAME_ZERO.search(line):
            after, seen = True, 0
            continue
        m = PRESENT_TIMING.search(line)
        if not m or not after:
            continue
        seen += 1
        if seen == 1:
            continue
        values = [int(v) for v in m.groups()]
        sums = values if sums is None else [a + b for a, b in zip(sums, values)]
    return tuple(sums) if sums else None


def platform_lines_of(log_text):
    """The game's [platform] lines in the order written, each (kind, value), a line written again with the same value once."""
    out = []
    for line in log_text.splitlines():
        m = PLATFORM.search(line)
        if m and (m.group(1), m.group(2)) not in out:
            out.append((m.group(1), m.group(2)))
    return out


def platform_row(lines, refresh_ms):
    """PLATFORM_ROW of a device run: a line of each PLATFORM_KINDS with a value; "not supported" counts only with its reason, and a
    frame timeline's figure only at the run's own rate (within 1.5 Hz of its refresh)."""
    hz = 1000.0 / refresh_ms if refresh_ms else 0.0
    have = set()
    for kind, value in lines:
        if not value or (value.startswith("not supported") and "(" not in value):
            continue
        if kind == "frame-timeline" and not value.startswith("not supported"):
            m = PLATFORM_RATE.match(value)
            if not m or not hz or abs(float(m.group(1)) - hz) > 1.5:
                continue
        have.add(kind)
    missing = [k for k in PLATFORM_KINDS if k not in have]
    value = "%d of %d%s" % (len(PLATFORM_KINDS) - len(missing), len(PLATFORM_KINDS),
                            "; missing: " + ", ".join(missing) if missing else "")
    return (PLATFORM_ROW, value, "a line of each kind with its value (the frame timeline's at %.0f Hz)" % hz,
            "FAIL" if missing else "pass")


def share_text(n, frames):
    return "%d (%.3f%%)" % (n, 100.0 * n / frames) if frames else "%d" % n


def frametimeline_rows(run, base=None, base_name=""):
    """A device run's rows from the platform's records (frametimeline.py): refreshes from a frame's start to its scan-out, the
    misses by cause (Unknown judged), the game's own split beside them, the GPU's composition; with a stored baseline, the run
    against it."""
    report = run.get("frametimeline")
    if report is None:
        return [("frame timeline", "-", "the platform's records of the frames", "not measured: the run has no capture")]
    rows = []
    frames, judged = report["frames"], report["refresh_frames"]
    how = "%s, start %s" % (report["method"], report["start"])
    if judged:
        rows.append(("frame start to scan-out", "%s of %d (%s)" % (", ".join("%s: %.2f%%" % (b, 100.0 * report["refreshes"][b] / judged)
                                                                          for b in frametimeline.BINS), judged, how),
                     "refreshes, recorded", "recorded"))
    else:
        rows.append(("frame start to scan-out", "-", "refreshes, recorded",
                     "not measured: %s" % ("; ".join(n for n in report["notes"] if "row 1" in n) or "no frame with a start")))
    if not frames:
        rows.append(("misses by cause", "-", "every miss named", "not measured: %s" % ("; ".join(report["notes"]) or "no frames")))
    else:
        for cause in frametimeline.CAUSES:
            n = report["causes"][cause]
            if cause == "Unknown":
                rows.append(("miss: Unknown", share_text(n, frames), "at most %.2f%% of %d frames" % (
                    100 * frametimeline.UNKNOWN_SHARE, frames), "FAIL" if frametimeline.unknown_fails(report) else "pass"))
            else:
                rows.append(("miss: %s" % cause, share_text(n, frames), "named (%s)" % how, "recorded"))
        rows.append(("buffer stuffing", share_text(report["buffer_stuffing"], frames), "counted, not a miss", "recorded"))
    game = run.get("present_timing")
    if game is not None:
        rows.append(("misses by the game's lines", "%d late of %d: %d available late, %d delayed late" % (game[1], game[0], game[2], game[3]),
                     "beside the causes", "recorded"))
    if report["gpu_frames"]:
        rows.append(("composed by the GPU", "%s; latched unsignaled %s" % (
            share_text(report["gpu_composition"], report["gpu_frames"]),
            "-" if report["latched_unsignaled"] is None else "%d" % report["latched_unsignaled"]), "recorded", "recorded"))
    if base is not None:
        verdict, text = frametimeline.judge(report, base)
        target = "no share above the most of a run in %s" % base_name
        if verdict in ("pass", "FAIL") and not BASELINE_REFUSES:
            target = "report only: the shares against the most of a run in %s" % base_name
            text += "; above the stored most" if verdict == "FAIL" else ""
            verdict = "recorded"
        rows.append((BASELINE_ROW, text or "-", target, verdict))
    return rows


def judge(run, history=None, base=None, base_name=""):
    """The rows for one run: (measure, value, target, verdict)."""
    c, test, on_device = run["figures"], run["test"], run["device"]
    rows = []
    if c is None:
        return [("all", "no measured [pace] window", "", "FAIL")]
    vsync = setting(test, "GALAXY_VSYNC_INTERVAL", "1") != "0"
    refresh = c["refresh_ms"]
    covered = c["pictures"] > 0 and c["displayed"] >= COVERAGE * c["pictures"]
    uncovered = "not measured: the renderer reported %d of %d pictures shown" % (c["displayed"], c["pictures"])
    rate = c["steps_per_second"]
    rows.append(("game speed", "%.4f steps a second" % rate, "60.000 within 0.01%",
                 "pass" if abs(rate - 60.0) <= 0.006 else "FAIL"))
    if vsync and not on_device:
        game = c["missed"] - c["missed_system"]
        rows.append(("even pictures", "%d missed (%d the system's), %d doubled, %d of %d on time" % (
            c["missed"], c["missed_system"], c["doubled"], c["on_time"], c["displayed"]),
                     "no missed refresh of the game's, none doubled", ("pass" if game == 0 and c["doubled"] == 0 else "FAIL")
                     if covered else uncovered))
    if c["jumps"] is None:
        rows.append(("jumps", "-", "no change over 1 ms between two pictures shown",
                     "not measured: the [pace] lines carry no jumps"))
    else:
        rows.append(("jumps", "%d, largest change %.2f ms" % (c["jumps"], c["jump_max_us"] / 1000.0),
                     "no change over 1 ms between two pictures shown", ("pass" if c["jumps"] == 0 else "FAIL") if covered
                     else uncovered))
    leads = run.get("leads", [])
    rows.append(lead_row(leads))
    # The old figure (within 1 ms of the run's median) is a report only since brief AD: the jumps and the lead judge what it did.
    rows.append(("animation error", "%.2f%% within 1 ms (p99 %.0f us)" % (100 * c["animation_within_1ms"],
                                                                       c["animation_p99_us"]),
                 "report only (the jumps and the lead judge)", "recorded" if covered else uncovered))
    work = c["step_work_p99_us"] / 1000.0
    delay = c["delay_p99_us"] / 1000.0
    if c["generated"] > 0:
        bound = STEP_MS + refresh + work
        target = "one step + one refresh + the step's work (%.2f ms)" % bound
    else:
        bound = work + refresh
        target = "the step's work + one refresh (%.2f ms)" % bound
    moved = any(not l["load"] for l in leads)
    if moved:
        # A run whose lead moved in play has two leads in its delay: steady delay is the lead row's to judge (brief AD bar 4).
        target += ", reported: the lead moved in play, the lead row judges"
    rows.append(("delay, in-between pictures %s" % ("on" if c["generated"] > 0 else "off"),
                 "p99 %.2f ms (average %.2f)" % (delay, c["delay_avg_us"] / 1000.0), target,
                 (("recorded" if moved else "pass" if delay <= bound else "FAIL") if covered and refresh > 0 else uncovered)))
    if not on_device:
        longest = c["step_work_max_us"] / 1000.0
        rows.append(("long step", "longest %.2f ms (p99 %.2f)" % (longest, work), "none over 18 ms",
                     "pass" if longest <= 18.0 else "FAIL"))
    else:
        hz = 1000.0 / refresh if refresh > 0 else 0.0
        if 110 <= hz <= 130:
            share = c["on_time"] / c["displayed"] if c["displayed"] else 0.0
            rows.append(("tablet at 120 Hz", "%.2f%% one refresh after the last" % (100 * share), "99 of 100",
                         ("pass" if share >= 0.99 else "FAIL") if covered else uncovered))
        elif 55 <= hz <= 65:
            ok = c["missed"] == 0 and c["doubled"] == 0 and delay <= 2 * refresh
            rows.append(("tablet at 60 Hz", "%d missed, %d doubled, delay p99 %.2f ms" % (c["missed"], c["doubled"], delay),
                         "every picture at its time; two refreshes from step to display", ("pass" if ok else "FAIL")
                         if covered else uncovered))
    shown = shown_from_start(run.get("all_windows", []), test)
    if shown is not None:
        rows.append(shown)
    loads = load_row(run, history or {})
    if loads is not None:
        rows.append(loads)
    if c["generated"] > 0:
        rows.append(("in-between picture's render work", "%.3f ms average, %.3f p99" % (
            c["generated_render_avg_us"] / 1000.0, c["generated_render_p99_us"] / 1000.0),
            "does not grow with the objects drawn", "compared across stages"))
    if on_device:
        rows.append(platform_row(run.get("platform", []), refresh))
        rows += frametimeline_rows(run, base, base_name)
    return rows


def read_history(path):
    """{run: [reading]} of a history file (HISTORY_COLUMNS), oldest first; {} where there is none."""
    out = {}
    if not path or not pathlib.Path(path).is_file():
        return out
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines()[1:]:
        cells = line.split("\t")
        if len(cells) != len(HISTORY_COLUMNS):
            continue
        row = dict(zip(HISTORY_COLUMNS, cells))
        try:
            reading = {"commit": row["commit"], "load_missed": float(row["load_missed"]),
                       "load_jumps": float(row["load_jumps"]) if row["load_jumps"] != "-" else 0.0}
        except ValueError:
            continue
        out.setdefault(row["run"], []).append(reading)
    return out


def reading_of(method="auto", start="step", processor="", baseline=""):
    """How the device runs' frame timelines are read (frametimeline.read) and judged: a stored baseline (pace/tablet's
    tests/pace/tablet-base.json) names its method and start, which then hold whatever was asked."""
    base = None
    if baseline:
        base = json.loads(pathlib.Path(baseline).read_text(encoding="utf-8"))
        method, start = base["method"], base["start"]
    return {"method": method, "start": start, "processor": processor, "base": base,
            "base_name": pathlib.Path(baseline).name if baseline else ""}


def read_runs(work, history=None, reading=None):
    """Every run under WORK (not WORK/first), each with its figures and rows; a device run's capture is read to its
    frametimeline.json (`reading`, reading_of's)."""
    reading = reading or reading_of()
    work = pathlib.Path(work)
    runs = []
    for log in sorted(work.rglob("run.log")):
        folder = log.parent
        if folder.relative_to(work).parts[0] == FIRST:
            continue
        test_file = folder / "test.json"
        test = json.loads(test_file.read_text(encoding="utf-8")) if test_file.is_file() else {}
        device = (folder / "exit-info.txt").is_file() or (folder / "diagnostics.txt").is_file()
        text = log.read_text(encoding="utf-8", errors="replace")
        measured, every = windows_of(text), windows_of(text, from_start=True)
        run = {"run": str(folder.relative_to(work)), "run_key": "%s/%s" % (folder.parent.name, folder.name), "test": test,
               "renderer": folder.name, "device": device, "figures": combine(measured), "all_windows": every,
               "leads": leads_of(text), "platform": platform_lines_of(text),
               "load_windows": every[:len(every) - len(measured)]}
        missed, jumps = load_figures(run["load_windows"])
        run["loads"] = {"windows": len(run["load_windows"]), "missed": missed, "jumps": jumps}
        if device:
            figures = run["figures"] or {}
            run["frametimeline"] = frametimeline.read(folder, reading["method"], reading["start"], reading["processor"],
                                                      figures.get("refresh_ms") or None)
            run["present_timing"] = present_timing_of(text)
        run["rows"] = judge(run, history, reading["base"], reading["base_name"])
        runs.append(run)
    return runs


def failed_rows(run):
    return [r for r in run["rows"] if r[3] == "FAIL"]


def digest(work, history_path=None, reading=None):
    work = pathlib.Path(work)
    runs = read_runs(work, read_history(history_path), reading)
    failed = not runs
    for run in runs:
        settings = run["test"].get("settings", run["test"].get("test", {}).get("settings", []))
        print("%s (%s)" % (run["run"], ", ".join(settings) or "default settings"))
        for kind, value in run.get("platform", []):
            print("  platform %s: %s" % (kind, value))
        for measure, value, target, verdict in run["rows"]:
            print("  %-34s %-46s %-50s %s" % (measure, value, target, verdict))
            failed |= verdict == "FAIL"
    first = work / FIRST
    if first.is_dir():
        for run in read_runs(first, read_history(history_path), reading):
            bad = failed_rows(run)
            if bad:
                print("played again: the first run %s failed: %s" % (run["run"], "; ".join("%s %s" % (r[0], r[1]) for r in bad)))
    (work / "pace.json").write_text(json.dumps([{k: v for k, v in r.items() if k not in ("test", "all_windows", "load_windows")}
                                                for r in runs], indent=1), encoding="utf-8")
    return 1 if failed else 0


def keep(work, history_path, commit):
    """Each run's figures of WORK/pace.json added to the history (HISTORY_COLUMNS)."""
    runs = json.loads((pathlib.Path(work) / "pace.json").read_text(encoding="utf-8"))
    path = pathlib.Path(history_path)
    new = not path.is_file()
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    with open(path, "a", encoding="utf-8", newline="\n") as f:
        if new:
            f.write("\t".join(HISTORY_COLUMNS) + "\n")
        for run in runs:
            c, loads = run.get("figures") or {}, run.get("loads") or {}
            if not c or not loads.get("windows"):
                continue
            cells = (now, commit[:9], run["run_key"], "%d" % loads["missed"],
                     "-" if loads.get("jumps") is None else "%d" % loads["jumps"], "%d" % loads["windows"],
                     "%d" % c.get("missed", 0), "%d" % c.get("missed_system", 0),
                     "-" if c.get("jumps") is None else "%d" % c["jumps"])
            f.write("\t".join(cells) + "\n")
    print("pace.py: the readings of %s kept in %s" % (commit[:9], path))
    return 0


def take_lock(path, who, wait_minutes=0.0):
    lock = pathlib.Path(path)
    until = time.time() + 60.0 * wait_minutes
    while True:
        try:
            lock.mkdir()
            break
        except FileExistsError:
            if time.time() < until:
                time.sleep(10)
                continue
            holder = lock / "holder.txt"
            print("pace.py: the measurement lock %s is held: %s" % (
                lock, holder.read_text(encoding="utf-8", errors="replace").strip() if holder.exists() else "unknown"), file=sys.stderr)
            sys.exit(LOCK_HELD)
    (lock / "holder.txt").write_text("%s %s\n" % (who, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")),
                                     encoding="utf-8")


def harness_calls(suite):
    """The harness runs that play --suite, each [target, --test NAME ...]. luma_test.py reads a list of tests only from a file
    named suite.json (its tests the folders beside it) and a test from its folder. So a test folder and a suite.json go as they
    are; another list (harness 1 and its tests, beside them: gate.json, tablet.json, short.json) is one run of the suite.json
    beside it, with --test for each of its tests that suite lists (one start, one install on a device), then one run of each
    other test's own folder, in the list's order. A list that cannot be read, or a test whose test.json is not there, stops
    pace.py before it takes the measurement lock."""
    path = pathlib.Path(suite)
    if path.is_dir() or path.name == "suite.json":
        return [[str(path)]]
    try:
        listed = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise SystemExit("pace.py: --suite %s is not a test folder, a suite.json or a list of tests: %s" % (path, error))
    tests = listed.get("tests") if isinstance(listed, dict) else None
    if (not isinstance(listed, dict) or listed.get("harness") != 1 or not isinstance(tests, list) or not tests
            or not all(isinstance(t, str) and t for t in tests)):
        raise SystemExit("pace.py: --suite %s: a list of tests is {\"harness\": 1, \"tests\": [names]}" % path)
    missing = [t for t in tests if not (path.parent / t / "test.json").is_file()]
    if missing:
        raise SystemExit("pace.py: --suite %s: no test.json for %s beside it" % (path, ", ".join(missing)))
    whole = path.parent / "suite.json"
    try:
        known = json.loads(whole.read_text(encoding="utf-8")).get("tests", []) if whole.is_file() else []
    except (OSError, ValueError, AttributeError):
        known = []
    listed_there = [t for t in tests if t in known]
    calls = [[str(whole)] + [w for t in listed_there for w in ("--test", t)]] if listed_there else []
    return calls + [[str(path.parent / t)] for t in tests if t not in listed_there]


def harness_lock(work, renderer, again=False, call=0):
    """The run lock the harness is given for a run (luma_test.py refuses a run without one): beside the renderer's folder, which
    --again moves to WORK/first, one of its own for each harness run of the suite (call) and for the run played again
    (measure.py's harness-<run>[-again].lock)."""
    return pathlib.Path(work) / ("harness-%s%s%s.lock" % (renderer, "-%d" % (call + 1) if call else "", "-again" if again else ""))


def harness_command(args, renderer, work, call, index=0, again=False):
    """The command of one harness run (harness_calls' `call`) on a renderer, into work/<renderer>."""
    diagnostics = [d for d in (args.diagnostics or "").split(",") if d]
    if args.device and "timeline" not in diagnostics:
        diagnostics.append("timeline")  # a device run's step moments, the frame timeline's frame starts (frametimeline.py)
    return ([sys.executable, "-X", "utf8", "-I", str(TREE / "tools/lumacore/luma_test.py"), "run", call[0]] + call[1:] +
            ["--bin", args.bin, "--work", str(pathlib.Path(work) / renderer), "--who", "pace", "--renderer", renderer,
             "--lock", str(harness_lock(work, renderer, again, index))] +
            (["--diagnostics", ",".join(diagnostics)] if diagnostics else []))


def play(args, renderer, work, again=False):
    """The harness runs of the suite on a renderer (or the device), into work/<renderer>: 0, or the first harness exit that
    was not 0."""
    code = 0
    for index, call in enumerate(harness_calls(args.suite)):
        one = play_one(args, harness_command(args, renderer, work, call, index, again))
        code = code or one
    return code


def play_one(args, command):
    env = dict(os.environ)
    if args.paks:
        env["LUMA_TEST_PAKS"] = args.paks
    if args.device:
        env[frametimeline.CAPTURE_ENV] = "1"  # the harness takes the platform's records of the frames (frametimeline.Capture)
    if args.device and args.apk:
        # The test app installed with the pak inside the device's lock and removed after (measure.py's tablet run).
        sys.path.insert(0, str(TREE / "tools" / "measure"))
        import measure
        tablet = argparse.Namespace(device=args.device, device_lock=args.device_lock, device_wait=args.device_wait,
                                    commit="pace", apk=args.apk, pak=args.pak)
        code, why = measure.on_tablet(tablet, command, env)
        if code is None:
            print("pace.py: the device's run did not start: %s" % why, flush=True)
            return 3
        return code
    if args.device:
        command = command + ["--device", args.device] + (["--device-lock", args.device_lock] if args.device_lock else [])
    return subprocess.run(command, env=env, check=False).returncode


def run(args):
    work = pathlib.Path(args.work)
    harness_calls(args.suite)  # a suite the harness cannot read stops here, before the lock and before any wait
    work.mkdir(parents=True, exist_ok=True)
    reading = reading_of(getattr(args, "method", "auto"), getattr(args, "start", "step"), getattr(args, "trace_processor", ""),
                         getattr(args, "baseline", ""))  # a baseline that cannot be read stops pace.py before the lock
    take_lock(args.lock, "pace.py %s" % work, args.lock_wait)
    codes, busy = {}, {}
    history = read_history(args.history)

    def play_quiet(renderer, again=False):
        # With --quiet-minutes a PC run first waits for a quiet machine (tools/measure/quiet.py: no compiler, build tool or other copy
        # of the game). A machine still busy after the wait plays nothing more (pacing beside another game is not the change's: a
        # run measure.py finds not quiet is not measured either); the run's code is None and pace-run.json names what kept it busy. Once
        # one wait has run out, the renderers after it do not wait again.
        if not args.device and args.quiet_minutes is not None:
            quiet_now, why = (False, next(iter(busy.values()))) if busy else machine_quiet(args.quiet_minutes)
            if not quiet_now:
                busy[renderer] = why
                print("pace.py: %s not played: the machine was not quiet: %s" % (renderer, why), flush=True)
                return None
        return play(args, renderer, work, again)
    try:
        renderers = ["vulkan"] if args.device else args.renderers.split(",")
        for renderer in renderers:
            codes[renderer] = play_quiet(renderer)
            if args.again and codes[renderer] is not None and any(failed_rows(r) for r in read_runs(work / renderer, history, reading)):
                (work / FIRST).mkdir(exist_ok=True)
                shutil.rmtree(work / FIRST / renderer, ignore_errors=True)
                shutil.move(str(work / renderer), str(work / FIRST / renderer))
                print("pace.py: %s had a FAIL row: played once more" % renderer, flush=True)
                codes[renderer] = play_quiet(renderer, again=True)
    finally:
        shutil.rmtree(args.lock, ignore_errors=True)
        (work / "pace-run.json").write_text(json.dumps({"harness_exit": codes, "not_quiet": busy}, indent=1), encoding="utf-8")
    code = digest(work, args.history, reading)
    for renderer, why in busy.items():
        print("%s: not played, the machine was not quiet: %s" % (renderer, why))
    if busy and not any(failed_rows(r) for r in read_runs(work, history, reading)):
        return NOT_QUIET
    return code


def machine_quiet(minutes):
    """(True, "") when nothing makes the machine busy (quiet.wait_quiet, waiting up to `minutes`), else (False, what ran)."""
    sys.path.insert(0, str(TREE / "tools" / "measure"))
    import quiet
    return quiet.wait_quiet(minutes=minutes, say=lambda text: print(text.replace("measure.py", "pace.py"), flush=True))


def parser():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run")
    r.add_argument("--bin", required=True)
    r.add_argument("--work", required=True)
    r.add_argument("--renderers", default="d3d12,vulkan,d3d11")
    r.add_argument("--suite", default=str(TREE / "tests/pace/suite.json"),
                   help="a test folder, a suite.json, or another list of tests beside them (tests/pace/gate.json): harness_calls")
    r.add_argument("--device", default="")
    r.add_argument("--device-lock", default="")
    r.add_argument("--device-wait", type=float, default=20.0, help="minutes to wait for a held device lock (with --apk)")
    r.add_argument("--apk", default="", help="with --device: the test app to install for the run, with --pak")
    r.add_argument("--pak", default="")
    r.add_argument("--lock", default="D:/NebulaWork/plan/measure.lock")
    r.add_argument("--lock-wait", type=float, default=0.0, help="minutes to wait for a held measurement lock")
    r.add_argument("--quiet-minutes", type=float, default=None,
                   help="a PC run's wait for a quiet machine (no compiler, build tool or other game copy); still busy after it, the run "
                        "is not played (pace-run.json says what kept it busy). Not given: no wait, every run plays")
    r.add_argument("--paks", default=os.environ.get("LUMA_TEST_PAKS", ""))
    r.add_argument("--diagnostics", default="", help="the game's --diagnostics for every run (pace-fault: the planted stutter)")
    r.add_argument("--history", default="", help="main's readings the load row is judged against (pace.py keep writes them)")
    r.add_argument("--again", action="store_true", help="a renderer with a FAIL row is played once more; the second decides")
    d = sub.add_parser("digest")
    d.add_argument("work")
    d.add_argument("--history", default="")
    for q in (r, d):
        # A device run's frame timeline (frametimeline.py): how it is read, and the stored baseline it is judged against.
        q.add_argument("--method", choices=frametimeline.METHODS, default="auto",
                       help="the frame timeline's source: auto (the decision rule), frametimeline or latency")
        q.add_argument("--start", choices=frametimeline.STARTS, default="step",
                       help="a frame's start: the game's step moment or the Choreographer vsync of the frame")
        q.add_argument("--trace-processor", default=os.environ.get(frametimeline.PROCESSOR_ENV, ""),
                       help="Perfetto's trace_processor_shell (else %s); none: the trace is not read" % frametimeline.PROCESSOR_ENV)
        q.add_argument("--baseline", default="", help="a stored baseline (tests/pace/tablet-base.json): its method and start hold, "
                                                      "and each device run is judged against it")
    k = sub.add_parser("keep")
    k.add_argument("work")
    k.add_argument("--history", required=True)
    k.add_argument("--commit", required=True)
    return p


def main():
    p = parser()
    args = p.parse_args()
    if args.command == "run":
        if args.apk and not (args.device and args.pak):
            p.error("--apk needs --device and --pak")
        sys.exit(run(args))
    if args.command == "keep":
        sys.exit(keep(args.work, args.history, args.commit))
    sys.exit(digest(args.work, args.history, reading_of(args.method, args.start, args.trace_processor, args.baseline)))


if __name__ == "__main__":
    main()
