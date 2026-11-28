"""frametimeline.py: why a picture on an Android device was late, from the platform's own records (G10.2; brief AB S1, brief AD).

The game's [pace] and [present-timing] lines say that a picture was late, not why, and their "pipeline" figure is the depth the
game asked for (renderer_vk.cpp, slots.depth_used()), not what the platform delivered. This module takes the platform's records
of a device run and turns them into four rows (tools/gate/pace.py prints them for each tablet run):
  1. refreshes from a frame's start to its scan-out, as a distribution: 1 or less, 2, 3, 4 or more;
  2. misses by cause, one row each (CAUSES), every miss in exactly one row (PRECEDENCE); Buffer Stuffing is counted and is no
     miss; Unknown, a miss no named cause covers, fails above UNKNOWN_SHARE of the frames;
  3. the same split from the game's own [present-timing] lines (available late, delayed late), beside it (pace.py);
  4. the share of frames SurfaceFlinger composed with the GPU (gpu_composition: pre-rotation) and the buffers it latched
     before their GPU work had ended (latched unsignaled).

THE CAPTURE (Capture; tools/lumacore/luma_test_android.py starts it when LUMA_TEST_FRAMETIMELINE=1, which pace.py sets for a
device run). After the harness's "frame 0" line: SurfaceFlinger's layer list (`dumpsys SurfaceFlinger --list`, sf-list.txt) gives
the game's layer (pick_layer); Perfetto starts in the background (`perfetto --background --txt -c - -o TRACE_REMOTE`, the config
of trace_config on stdin through Device.shell_input) with the FrameTimeline source (android.surfaceflinger.frametimeline) in a
buffer of its own and an ftrace part (power/cpu_frequency, power/gpu_frequency, sched/sched_switch; atrace gfx and view for the
test app). While the game runs, `dumpsys SurfaceFlinger --latency <layer>` (128 frames each) every second at 120 Hz, every 2 s at
60 (the game's [display] refresh-hz line), appended to sf-latency.txt. At the end: the last --latency read, `dumpsys
SurfaceFlinger --frametimeline -jank` (sf-jank.txt), Perfetto stopped (SIGTERM ends its trace), the wait until its process has
ended and the trace's size has stood still (wait_written), the trace pulled into the run folder (frametimeline.pftrace) and its
size checked against the device's. capture.json says what was done and what failed; a capture that fails is never a verdict on the
game. The shell user may not be allowed every ftrace event: Perfetto leaves out what it may not read, and G10.2m's first trace
on the tablet says which. The layer's name (pick_layer) is the bare name of the listing's line in any Android 12 to 16 format.

THE READING (read, `frametimeline.py read RUN_FOLDER`). The trace processor (Perfetto's trace_processor_shell; its path from
--trace-processor or from the environment's NEBULA_TRACE_PROCESSOR) runs the QUERIES in run_processor, the one function that
runs it; everything after reads its tables. Then, by the method (--method):
  frametimeline  the app's surface frames of the game's layer (actual_frame_timeline_slice rows of the game's process): ready at
                 the slice's end (max of GPU done and post, the perfetto page), shown at its display frame's end (the same
                 display_frame_token in SurfaceFlinger's process), the Choreographer vsync of the frame at its expected slice's
                 start (expected_frame_timeline_slice, the same surface_frame_token); its cause from its jank_type and
                 present_type.
  latency        SurfaceFlinger's --latency history of the layer (desired present, actual present, frame ready; ns,
                 CLOCK_MONOTONIC): late when shown more than a refresh after the time it asked for (the game asks its slot less
                 half a vsync, renderer_vk.cpp); its cause from the display frame shown at the same moment (the trace's
                 SurfaceFlinger rows, which exist with or without app rows) and App Deadline Missed when the frame was ready
                 after the time it asked for.
  auto           the decision rule: frametimeline when the trace holds app rows of the game's layer, else latency, with the
                 note that adding a frame-timeline token (ASurfaceTransaction_setFrameTimeline) is a later question only if that
                 is not enough. The perfetto page says "SurfaceViews are currently not supported", and the game draws into one.
The frame's start (--start): `vsync`, the Choreographer vsync of the frame (the frametimeline method only); `step`, the game's
own step moment: the latest start of work at or before the frame was ready, a start being a step's (the timeline's StepWaitEnd,
the step loop's wait returning at the vsync) or an in-between picture's walk (WalkBegin with b=1). The game's moments are
CLOCK_MONOTONIC; the trace's are its own clock, so they are moved by the trace's clock snapshot (BOOTTIME less MONOTONIC). Which
method and which start hold for this game is not decided here: G10.2m's first trace on the tablet decides it, and the stored
baseline names both.

THE BASELINE (`frametimeline.py baseline RUN_FOLDER... --out FILE`): the reports of several runs by the display's rate (60, 120):
rows 1 to 4 summed, the frames of each cause, the shares (late, three refreshes or more, unnamed) of the runs together, each run's
rows (ROWS: those shares, the mean refreshes from the step to the screen, and what the screen showed, below) and the bound of each
row, the median of the runs plus their range (max - min; G10.4m's range rule). tests/pace/tablet-base.json is such a file;
judge() says FAIL when a row of a run is beyond the bound of its rate (rows_beyond names them) and pace.py decides which rows refuse.

WHAT THE SCREEN SHOWED (G10.4m; shown_events, pair_verdict, classify_late, episodes, `frametimeline.py classify`): FrameTimeline's labels
name a cause, not what a player saw. From the present times SurfaceFlinger recorded (desired and actual, the --latency reads) each pair
of pictures shown one after the other is "repeated" (the screen held the first two refreshes or more), "skipped" (the game's moment
moved on more than the screen's), "shifted" (another change of shown minus asked over 1 ms) or "on time". At 120 Hz about nine of ten
pictures the label calls late were shown on time in this sense (one refresh after the time first asked for, in step), so the A/B row is
the pairs that were not (events a minute) and the late share is a report. The rule of the A/B (ab_wins, power_raises, ab_choice): a
variant wins only when its median beats the baseline's by more than the larger of the two ranges, and a power measure counts as
raised only when its median is above the baseline's by more than the baseline's own range.
"""
import argparse
import csv
import datetime
import io
import json
import os
import pathlib
import re
import shlex
import statistics
import struct
import subprocess
import sys
import tempfile
import time

APP = "dev.nebula.galaxy"            # the game's processes: the player's and the test app's (dev.nebula.galaxy.test:game)
PROCESSOR_ENV = "NEBULA_TRACE_PROCESSOR"
CAPTURE_ENV = "LUMA_TEST_FRAMETIMELINE"
REPORT = "frametimeline.json"
TRACE_FILE = "frametimeline.pftrace"
TRACE_REMOTE = "/data/misc/perfetto-traces/nebula-frametimeline.pftrace"
METHODS = ("auto", "frametimeline", "latency")
STARTS = ("step", "vsync")
CAUSES = ("App Deadline Missed", "SurfaceFlinger CPU Deadline Missed", "SurfaceFlinger GPU Deadline Missed", "Display HAL",
          "Prediction Error", "ModeChange in progress", "Dropped Frame", "Unknown")
STUFFING = "Buffer Stuffing"
# A miss with several flags goes to the first of these it has: a mode change explains everything about its frame; then the
# game's own lateness (a frame late by itself is the game's whatever else was late); then the platform's, from the compositor
# down to the display; a frame dropped with no other flag last. A miss with none of them is Unknown.
PRECEDENCE = ("ModeChange in progress", "App Deadline Missed", "SurfaceFlinger CPU Deadline Missed",
              "SurfaceFlinger GPU Deadline Missed", "Display HAL", "Prediction Error", "Dropped Frame")
UNKNOWN_SHARE = 0.0005               # Unknown fails above 0.05 % of the frames
BINS = ("1 or less", "2", "3", "4 or more")
SHARES = ("late_share", "three_refresh_share", "unnamed_share")
# The rows a stored baseline holds for each rate (G10.4m): the shares above and what the screen showed (shown_events).
ROWS = SHARES + ("refreshes_mean", "events_per_minute", "jumps_per_minute")
# The game's timeline (runtime/include/galaxy/frame_timeline.h): its events and header.
WALK_BEGIN, STEP_WAIT_END = 29, 32
FRAME_ZERO = re.compile(r"harness: frame 0 is game frame (\d+)")
REFRESH_HZ = re.compile(r"\[display\] refresh-hz=(\d+)")
SETTLE_SECONDS = 60                  # how long the capture waits for Perfetto to write its trace out after SIGTERM
SETTLED = 3                          # reads of the trace's size, half a second apart, that must agree
NEVER = 1 << 62                      # --latency's "pending" (INT64_MAX) and anything past it
BOOTTIME, MONOTONIC = 6, 3           # Perfetto's builtin clock ids (clock_snapshot)
FORMAT = 2         # 2 (G10.4m): the baseline holds each run's rows and their bounds; a report is read the same in both

# The trace processor's queries, one result each (run_processor). Whole rows: what a column is called is read from the table, so a
# trace processor with more or fewer columns is read the same way.
QUERIES = {
    "frames": "SELECT a.*, p.name AS process_name FROM actual_frame_timeline_slice a LEFT JOIN process p USING(upid)",
    "expected": "SELECT e.ts, e.dur, e.surface_frame_token, e.display_frame_token, p.name AS process_name "
                "FROM expected_frame_timeline_slice e LEFT JOIN process p USING(upid)",
    "clocks": "SELECT snapshot_id, clock_id, clock_value FROM clock_snapshot",
}


# ---------------------------------------------------------------------------------------------------------------------------
# The trace processor


def processor_path(given=""):
    """The trace processor's path: the option's, else the environment's NEBULA_TRACE_PROCESSOR; "" when neither names one."""
    return given or os.environ.get(PROCESSOR_ENV, "")


def cell(text):
    """One CSV cell of the trace processor: None for a null, an int or a float where it reads as one, else the text."""
    if text in ("", "[NULL]", "NULL"):
        return None
    for kind in (int, float):
        try:
            return kind(text)
        except ValueError:
            pass
    return text


def run_processor(command, trace, queries, timeout=600):
    """Runs the trace processor (`command`: its argv up to the options, [path]) on `trace` once for each of `queries`
    ({name: sql}) and returns {name: {"columns": [...], "rows": [[...]]}}: its -q mode prints the last statement's result as CSV
    with a header. A query that fails is {"error": text}. The only code that runs the processor."""
    tables = {}
    with tempfile.TemporaryDirectory() as tmp:
        for name, sql in queries.items():
            query = pathlib.Path(tmp) / ("%s.sql" % name)
            query.write_text(sql + ";\n", encoding="utf-8")
            try:
                done = subprocess.run(list(command) + ["-q", str(query), str(trace)], capture_output=True, timeout=timeout)
            except (OSError, subprocess.TimeoutExpired) as error:
                tables[name] = {"error": "%s: %s" % (type(error).__name__, error)}
                continue
            text = done.stdout.decode("utf-8", "replace")
            if done.returncode != 0:
                tables[name] = {"error": "exit %d: %s" % (done.returncode, done.stderr.decode("utf-8", "replace").strip()[-300:])}
                continue
            lines = list(csv.reader(io.StringIO(text)))
            lines = [l for l in lines if l]
            if not lines:
                tables[name] = {"columns": [], "rows": []}
                continue
            tables[name] = {"columns": lines[0], "rows": [[cell(c) for c in l] for l in lines[1:]]}
    return tables


def records(table):
    """A table's rows as dicts by column name; [] for a table that is missing or failed."""
    if not table or "rows" not in table:
        return []
    return [dict(zip(table["columns"], row)) for row in table["rows"]]


# ---------------------------------------------------------------------------------------------------------------------------
# The causes


def flags_of(jank):
    return {f.strip() for f in str(jank or "").split(",") if f.strip() and f.strip() != "None"}


def cause_of(flags, late):
    """(miss, cause, stuffing) of a frame with these jank flags; `late`: its present said it was late or dropped. Every miss has
    exactly one cause (PRECEDENCE, else Unknown); Buffer Stuffing alone is no miss."""
    flags = set(flags)
    stuffing = STUFFING in flags
    flags.discard(STUFFING)
    if not flags and not late:
        return False, None, stuffing
    return True, next((c for c in PRECEDENCE if c in flags), "Unknown"), stuffing


# ---------------------------------------------------------------------------------------------------------------------------
# The sources


def read_latency(text):
    """SurfaceFlinger's --latency reads (sf-latency.txt): (period_ns or None, frames, from_ns). Each read is a "# read" line, the
    display's period, then rows of desired present, actual present and frame ready (ns). Frames are kept once (reads overlap),
    pending ones left out; from_ns is the newest present of the first read (before it: the frames before the capture began)."""
    periods, frames, from_ns, first = [], {}, None, None
    block = -1
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("# read"):
            block += 1
            continue
        parts = line.split()
        if not parts or not all(re.fullmatch(r"-?\d+", p) for p in parts):
            continue
        values = [int(p) for p in parts]
        if len(values) == 1:
            if values[0] > 0:
                periods.append(values[0])
            continue
        if len(values) < 3:
            continue
        desired, actual, ready = values[:3]
        if actual <= 0 or actual >= NEVER or ready <= 0 or ready >= NEVER:
            continue
        if block <= 0:
            first = actual if first is None else max(first, actual)
        frames[(actual, ready, desired)] = {"desired": desired if 0 < desired < NEVER else None, "present": actual, "ready": ready}
    if block > 0 and first is not None:
        from_ns = first
    kept = [f for k, f in sorted(frames.items()) if from_ns is None or k[0] > from_ns]
    return (statistics.median(periods) if periods else None), kept, from_ns


def jank_dump_counts(text):
    """`dumpsys SurfaceFlinger --frametimeline -jank`: each jank type's count, a cross-check kept as it is."""
    out = {}
    for m in re.finditer(r"Jank Type\s*:\s*([^\n\r]+)", text):
        out[m.group(1).strip()] = out.get(m.group(1).strip(), 0) + 1
    return out


def game_layer(rows):
    """The layer of the game's app rows: one whose name holds "SurfaceView" (the game's surface), the one with most rows."""
    counts = {}
    for r in rows:
        counts[r.get("layer_name") or ""] = counts.get(r.get("layer_name") or "", 0) + 1
    if not counts:
        return None
    return max(counts, key=lambda name: ("SurfaceView" in name, counts[name]))


def trace_frames(tables):
    """(app frames of the game's layer, display frames by token, layer, the trace's clock less CLOCK_MONOTONIC or None)."""
    rows = records(tables.get("frames"))
    app = [r for r in rows if APP in str(r.get("process_name") or "") and r.get("surface_frame_token") not in (None, 0)]
    display = {}
    for r in rows:
        if "surfaceflinger" in str(r.get("process_name") or "") and r.get("display_frame_token") is not None:
            display.setdefault(r["display_frame_token"], r)
    layer = game_layer(app)
    app = [r for r in app if (r.get("layer_name") or "") == layer] if layer is not None else []
    expected = {r["surface_frame_token"]: r["ts"] for r in records(tables.get("expected"))
                if APP in str(r.get("process_name") or "") and r.get("surface_frame_token") not in (None, 0)}
    frames = []
    for r in sorted(app, key=lambda r: r.get("ts") or 0):
        d = display.get(r.get("display_frame_token"))
        present = (d["ts"] + d["dur"]) if d is not None and d.get("dur") is not None and d["dur"] >= 0 else None
        unsignaled = [v for k, v in r.items() if "unsignaled" in k and isinstance(v, int)]
        frames.append({"ready": r["ts"] + (r.get("dur") or 0), "present": present if r.get("present_type") != "Dropped Frame" else None,
                       "vsync": expected.get(r["surface_frame_token"]), "flags": sorted(flags_of(r.get("jank_type"))),
                       "late": r.get("present_type") in ("Late Present", "Dropped Frame"),
                       "dropped": r.get("present_type") == "Dropped Frame", "gpu": r.get("gpu_composition"),
                       "unsignaled": sum(unsignaled) if unsignaled else None})
    snapshots = {}
    for r in records(tables.get("clocks")):
        snapshots.setdefault(r.get("snapshot_id"), {})[r.get("clock_id")] = r.get("clock_value")
    offset = next((s[BOOTTIME] - s[MONOTONIC] for s in snapshots.values() if BOOTTIME in s and MONOTONIC in s), None)
    return frames, display, layer, offset


def game_starts(timeline):
    """The game's starts of work from its timeline (frame_timeline.h): each step's (StepWaitEnd) and each in-between picture's walk
    (WalkBegin, b=1), in ns of the game's clock, sorted. [] without a timeline."""
    path = pathlib.Path(timeline)
    if not path.is_file():
        return []
    data = path.read_bytes()
    if len(data) < 64 or data[:8] != b"NBTL0001":
        return []
    freq, count = struct.unpack_from("<QQ", data, 16)
    starts = []
    for i in range(count):
        offset = 64 + 32 * i
        if offset + 32 > len(data):
            break
        qpc, _a, b, _thread, event, valid = struct.unpack_from("<QQQIHH", data, offset)
        if valid and (event == STEP_WAIT_END or (event == WALK_BEGIN and b == 1)):
            starts.append(qpc * 1_000_000_000 // freq if freq else qpc)
    return sorted(starts)


def latest_at_or_before(sorted_values, moment):
    lo, hi = 0, len(sorted_values)
    while lo < hi:
        mid = (lo + hi) // 2
        if sorted_values[mid] <= moment:
            lo = mid + 1
        else:
            hi = mid
    return sorted_values[lo - 1] if lo else None


def bin_of(refreshes):
    return BINS[0] if refreshes <= 1 else BINS[1] if refreshes == 2 else BINS[2] if refreshes == 3 else BINS[3]


# ---------------------------------------------------------------------------------------------------------------------------
# The report


def report_of(tables, latency_text, jank_text, starts, method="auto", start="step", refresh_ms=None, notes=None):
    """The rows of one run from what its capture gave: the trace processor's tables ({} when the trace was not read), the --latency
    reads, the jank dump and the game's starts of work (ns, CLOCK_MONOTONIC). refresh_ms: the display's refresh when the sources
    give none (pace.py passes the [pace] lines')."""
    notes = list(notes or [])
    frames_ft, display, layer, offset = trace_frames(tables) if tables else ([], {}, None, None)
    period_latency, frames_lat, _from = read_latency(latency_text or "")
    used = method if method != "auto" else ("frametimeline" if frames_ft else "latency")
    if method == "auto" and not frames_ft:
        notes.append("the game's layer has no FrameTimeline app rows (SurfaceViews are not supported by FrameTimeline): rows 1 "
                     "and 2 from --latency and SurfaceFlinger's display-frame jank types; a frame-timeline token "
                     "(ASurfaceTransaction_setFrameTimeline) is a later question only if that is not enough")
    period = period_latency or (refresh_ms * 1e6 if refresh_ms else None)
    if period is None and display:
        ends = sorted({d["ts"] + d["dur"] for d in display.values() if d.get("dur") is not None})
        gaps = [b - a for a, b in zip(ends, ends[1:]) if b > a]
        period = statistics.median(gaps) if gaps else None
    # The display frames by when they were shown, for the latency method's causes.
    shown_at = {}  # a moment of the display: the jank flags of the display frames shown then
    for d in display.values():
        if d.get("dur") is not None:
            shown_at.setdefault(d["ts"] + d["dur"], set()).update(flags_of(d.get("jank_type")))
    shown_moments = sorted(shown_at)
    if used == "frametimeline":
        frames = frames_ft
        shift = offset if offset is not None else 0
        if start == "step" and offset is None and starts:
            notes.append("the trace has no clock snapshot: the game's moments are taken as the trace's own clock")
        starts_here = [s + shift for s in starts]
    else:
        shift = 0
        starts_here = list(starts)
        frames = []
        for f in frames_lat:
            late = f["desired"] is not None and period is not None and f["present"] - f["desired"] > period
            flags = set()
            if late and shown_moments and period:
                # The display frame shown at this frame's present (the trace's clock: moved from CLOCK_MONOTONIC).
                at = f["present"] + (offset or 0)
                near = latest_at_or_before(shown_moments, at + period / 2)
                if near is not None and abs(near - at) <= period / 2:
                    flags |= shown_at[near]
            if late and f["desired"] is not None and f["ready"] > f["desired"]:
                flags.add("App Deadline Missed")
            frames.append({"ready": f["ready"], "present": f["present"], "vsync": None, "flags": sorted(flags), "late": late,
                           "dropped": False, "gpu": None, "unsignaled": None})
        if not frames_lat:
            notes.append("no --latency frames of the game's layer")
    out = {"format": FORMAT, "method": used, "asked_method": method, "start": start, "layer": layer,
           "refresh_ms": round(period / 1e6, 4) if period else None, "app_rows": len(frames_ft), "frames": len(frames),
           "refreshes": {b: 0 for b in BINS}, "refresh_frames": 0, "causes": {c: 0 for c in CAUSES}, "misses": 0,
           "buffer_stuffing": 0, "gpu_composition": None, "gpu_frames": 0, "latched_unsignaled": None,
           "sf_jank_dump": jank_dump_counts(jank_text or ""), "notes": notes}
    if start == "vsync" and used != "frametimeline":
        notes.append("row 1 not measured: the Choreographer vsync of a frame needs FrameTimeline's app rows")
    if start == "step" and not starts_here:
        notes.append("row 1 not measured: the run's timeline has no step moments (--diagnostics timeline)")
    gpu, unsignaled = [], []
    refresh_sum = 0.0
    for f in frames:
        miss, cause, stuffing = cause_of(f["flags"], f["late"])
        out["misses"] += 1 if miss else 0
        if miss:
            out["causes"][cause] += 1
        out["buffer_stuffing"] += 1 if stuffing else 0
        if f["gpu"] is not None:
            gpu.append(1 if f["gpu"] else 0)
        if f["unsignaled"] is not None:
            unsignaled.append(f["unsignaled"])
        if f["present"] is None or not period:
            continue
        begin = f["vsync"] if start == "vsync" else latest_at_or_before(starts_here, f["ready"]) if starts_here else None
        if begin is None or f["present"] - begin > 8 * period:  # no start of work near it (a load's gap)
            continue
        out["refreshes"][bin_of(round((f["present"] - begin) / period))] += 1
        out["refresh_frames"] += 1
        refresh_sum += (f["present"] - begin) / period
    out["refreshes_mean"] = round(refresh_sum / out["refresh_frames"], 4) if out["refresh_frames"] else None
    if gpu:
        out["gpu_composition"], out["gpu_frames"] = sum(gpu), len(gpu)
    if unsignaled:
        out["latched_unsignaled"] = sum(unsignaled)
    out.update(shares(out))
    out["shown"] = shown_events(frames_lat, period) if frames_lat and period else None
    return out


# ---------------------------------------------------------------------------------------------------------------------------
# What the screen showed (G10.4m): from the present times SurfaceFlinger recorded, not from the labels of FrameTimeline

SEAM_REFRESHES = 8   # a gap between two shown pictures of more than this many refreshes is a load's or a pause's seam, not a picture's
JUMP_NS = 1_000_000  # brief AD bar 2: the change of (shown moment - game moment) between two pictures within 1 ms
VERDICTS = ("repeated", "skipped", "shifted", "on time")


def pair_verdict(before, now, period):
    """What the screen showed between two pictures shown one after the other (--latency frames: desired = the game moment the picture
    was asked for, present = when it was shown): None across a seam or where a time is missing; "repeated" when the screen held the
    picture before for two refreshes or more; "skipped" when the game's moment moved on more than the screen's did (a picture
    between them was never shown); "shifted" when (shown - asked) changed by over 1 ms in any other way; else "on time": one new
    picture a refresh, as asked, whatever its distance from the time first asked for."""
    if before.get("desired") is None or now.get("desired") is None or not period:
        return None
    held = now["present"] - before["present"]
    asked = now["desired"] - before["desired"]
    if held <= 0 or held > SEAM_REFRESHES * period or abs(asked) > SEAM_REFRESHES * period:
        return None
    if round(held / period) >= 2:
        return "repeated"
    if asked - held > JUMP_NS:
        return "skipped"
    if abs(asked - held) > JUMP_NS:
        return "shifted"
    return "on time"


def shown_events(frames, period):
    """The screen's events of a run, from its --latency frames in the order shown: the pairs of pictures shown one after the other
    by verdict (pair_verdict), the pairs whose (shown - asked) changed by over 1 ms (`jumps`, bar 2; a repeat can be one), the
    pairs across a seam, `events` = repeated + skipped + shifted (bar 1's repeated or skipped pictures and the other jumps, each pair
    once) and the figure the A/B judges: events a minute of the pictures judged."""
    counts = {v: 0 for v in VERDICTS}
    jumps = seams = 0
    covered = 0.0
    for a, b in zip(frames, frames[1:]):
        verdict = pair_verdict(a, b, period)
        if verdict is None:
            seams += 1
            continue
        counts[verdict] += 1
        covered += b["present"] - a["present"]
        if abs((b["desired"] - a["desired"]) - (b["present"] - a["present"])) > JUMP_NS:
            jumps += 1
    events = counts["repeated"] + counts["skipped"] + counts["shifted"]
    minutes = covered / 6e10
    return {"pairs": sum(counts.values()), "seams": seams, "repeated": counts["repeated"], "skipped": counts["skipped"],
            "shifted": counts["shifted"], "on_time": counts["on time"], "jumps": jumps, "events": events,
            "minutes": round(minutes, 3), "events_per_minute": round(events / minutes, 3) if minutes else None,
            "jumps_per_minute": round(jumps / minutes, 3) if minutes else None}


def classify_late(frames, period):
    """One verdict for each frame the latency method flags late (shown more than a refresh after the time it asked for): the pair's
    verdict (pair_verdict) with the figures it rests on, in refreshes: [{index, present_ns, late, held, asked, verdict}]. A frame
    that is the run's first or follows a seam has the verdict "seam"."""
    rows = []
    for i, f in enumerate(frames):
        if f.get("desired") is None or f["present"] - f["desired"] <= period:
            continue
        verdict = pair_verdict(frames[i - 1], f, period) if i else None
        before = frames[i - 1] if i else None
        rows.append({"index": i, "present_ns": f["present"], "late": round((f["present"] - f["desired"]) / period, 3),
                     "held": round((f["present"] - before["present"]) / period, 3) if before else None,
                     "asked": round((f["desired"] - before["desired"]) / period, 3) if before and before.get("desired") else None,
                     "verdict": verdict or "seam"})
    return rows


def episodes(frames, period):
    """The runs of consecutive late frames (classify_late's flags) with what closed each: the verdict of the pair that follows the
    run's last frame (a late run ends where the screen skips to the game's moment again), [{frames, opened, closed}]."""
    out, i, n = [], 0, len(frames)
    late = [f.get("desired") is not None and f["present"] - f["desired"] > period for f in frames]
    while i < n:
        if not late[i]:
            i += 1
            continue
        j = i
        while j + 1 < n and late[j + 1]:
            j += 1
        opened = pair_verdict(frames[i - 1], frames[i], period) if i else None
        closed = pair_verdict(frames[j], frames[j + 1], period) if j + 1 < n else None
        out.append({"frames": j - i + 1, "opened": opened or "seam", "closed": closed or "seam"})
        i = j + 1
    return out


# ---------------------------------------------------------------------------------------------------------------------------
# The A/B rule and the power rule (G10.4m)


def spread(values):
    return max(values) - min(values)


def median(values):
    return statistics.median(values)


def ab_wins(baseline_values, variant_values, lower_is_better=True):
    """True when a variant beats the baseline on the A/B row: its median is better than the baseline's by MORE than the larger of
    the two ranges (max - min). A tie, or a win inside the range, is no win: the baseline stays. At least 2 runs of each (a range
    of one run is 0); the card asks for 5."""
    if len(baseline_values) < 2 or len(variant_values) < 2:
        raise ValueError("the A/B rule needs at least 2 runs of each (the card: 5)")
    margin = max(spread(baseline_values), spread(variant_values))
    gain = median(baseline_values) - median(variant_values)
    return (gain if lower_is_better else -gain) > margin


def power_raises(baseline_values, variant_values):
    """True when a variant raises a power measure (a heat slope or forecast, a CPU or GPU clock: a higher value costs more) by MORE
    than the baseline's own range: its median above the baseline's by more than max - min of the baseline's runs."""
    if len(baseline_values) < 2 or len(variant_values) < 2:
        raise ValueError("the power rule needs at least 2 runs of each (the card: 5)")
    return median(variant_values) - median(baseline_values) > spread(baseline_values)


def ab_choice(baseline, variants, measures):
    """The chosen variant of an A/B at one rate. baseline: {"row": [values], "power": {measure: [values]}}; variants: {name: the
    same}; measures: the power measures to hold. Returns (name, or None for the baseline, {name: {wins, raises, gain, margin}}).
    A candidate wins on the row (ab_wins) and raises no measure (power_raises). Of the candidates the best has the lowest median
    row; those it does not beat by more than the larger range (a tie) are weighed by their power: the one whose measures, each
    over the baseline's median, add up to the least is kept (a tie never costs power). No candidate: the baseline stays."""
    detail, candidates = {}, []
    for name, v in variants.items():
        wins = ab_wins(baseline["row"], v["row"])
        raised = [m for m in measures if m in v["power"] and m in baseline["power"]
                  and power_raises(baseline["power"][m], v["power"][m])]
        detail[name] = {"wins": wins, "raises": raised, "gain": median(baseline["row"]) - median(v["row"]),
                        "margin": max(spread(baseline["row"]), spread(v["row"]))}
        if wins and not raised:
            candidates.append(name)
    if not candidates:
        return None, detail
    best = min(candidates, key=lambda n: (median(variants[n]["row"]), n))
    tied = [n for n in candidates if n == best or not ab_wins(variants[n]["row"], variants[best]["row"])]

    def cost(name):
        return sum(median(variants[name]["power"][m]) / median(baseline["power"][m]) for m in measures
                   if m in variants[name]["power"] and m in baseline["power"] and median(baseline["power"][m]))
    return min(tied, key=lambda n: (cost(n), median(variants[n]["row"]), n)), detail


def shares(report):
    """A report's three judged shares (None where it has no frames to share)."""
    frames, judged = report.get("frames") or 0, report.get("refresh_frames") or 0
    three = report["refreshes"][BINS[2]] + report["refreshes"][BINS[3]]
    return {"late_share": report["misses"] / frames if frames else None,
            "three_refresh_share": three / judged if judged else None,
            "unnamed_share": report["causes"]["Unknown"] / frames if frames else None}


def unknown_fails(report):
    return report.get("unnamed_share") is not None and report["unnamed_share"] > UNKNOWN_SHARE


def read(run_folder, method="auto", start="step", processor="", refresh_ms=None, timeout=600):
    """Reads a run folder's capture (Capture) and writes its report (REPORT) beside it: the report, or None when the run has no
    capture. A trace with no processor to read it, or one it cannot read, leaves the trace's part out with a note."""
    folder = pathlib.Path(run_folder)
    capture = folder / "capture.json"
    if not capture.is_file():
        return None
    notes = []
    tables = {}
    trace = folder / TRACE_FILE
    command = processor_path(processor)
    if not trace.is_file():
        notes.append("no trace in the run folder (capture.json says why)")
    elif not command:
        notes.append("the trace was not read: no trace processor (--trace-processor or %s)" % PROCESSOR_ENV)
    elif not pathlib.Path(command).is_file():
        notes.append("the trace was not read: the trace processor %s is not there" % command)
    else:
        tables = run_processor([command], trace, QUERIES, timeout)
        failed = ["%s: %s" % (n, t["error"]) for n, t in tables.items() if "error" in t]
        if failed:
            notes.append("the trace processor failed: " + "; ".join(failed))
    text = lambda name: (folder / name).read_text(encoding="utf-8", errors="replace") if (folder / name).is_file() else ""
    report = report_of(tables, text("sf-latency.txt"), text("sf-jank.txt"), game_starts(folder / "timeline.bin"), method, start,
                       refresh_ms, notes)
    (folder / REPORT).write_text(json.dumps(report, indent=1), encoding="utf-8")
    return report


# ---------------------------------------------------------------------------------------------------------------------------
# The baseline


def rate_of(report):
    """The display's rate a report was read at, as its baseline key: "60", "120"; None without a refresh."""
    return str(int(round(1000.0 / report["refresh_ms"]))) if report.get("refresh_ms") else None


def row_values(report):
    """A report's rows (ROWS) as figures: None where it has none."""
    shown = report.get("shown") or {}
    values = {row: report.get(row) for row in SHARES + ("refreshes_mean",)}
    for row in ("events_per_minute", "jumps_per_minute"):
        values[row] = shown.get(row)
    return values


def bound_of(values):
    """The bound a baseline sets for a row: the median of its runs plus their range (max - min), the range rule of G10.4m (d): a run
    is no worse than the baseline until it is beyond what the baseline's own runs spread over."""
    return median(values) + spread(values)


def baseline(reports, made=None):
    """The baseline of several runs' reports (one method and one start): by rate, rows 1 to 4 summed, the frames of each cause, the
    shares of the runs together, each run's rows (`runs_rows`) and the bound of each row (`bounds`: the median of the runs plus their
    range, what judge() holds a run to), and the largest share of one run ("most", kept as before)."""
    methods = {(r["method"], r["start"]) for r in reports}
    if len(methods) != 1:
        raise ValueError("the reports were read by more than one method and start: %s" % sorted(methods))
    method, start = methods.pop()
    rates = {}
    for r in reports:
        key = rate_of(r)
        if key is None:
            raise ValueError("a report with no refresh: no rate to file it under")
        total = rates.setdefault(key, {"runs": 0, "frames": 0, "refresh_frames": 0, "refreshes": {b: 0 for b in BINS},
                                       "causes": {c: 0 for c in CAUSES}, "misses": 0, "buffer_stuffing": 0, "gpu_composition": 0,
                                       "gpu_frames": 0, "latched_unsignaled": None, "most": {s: 0.0 for s in SHARES},
                                       "runs_rows": []})
        total["runs"] += 1
        for k in ("frames", "refresh_frames", "misses", "buffer_stuffing", "gpu_frames"):
            total[k] += r[k]
        total["gpu_composition"] += r["gpu_composition"] or 0
        if r["latched_unsignaled"] is not None:
            total["latched_unsignaled"] = (total["latched_unsignaled"] or 0) + r["latched_unsignaled"]
        for b in BINS:
            total["refreshes"][b] += r["refreshes"][b]
        for c in CAUSES:
            total["causes"][c] += r["causes"][c]
        for s in SHARES:
            if r.get(s) is not None:
                total["most"][s] = max(total["most"][s], r[s])
        total["runs_rows"].append(row_values(r))
    for total in rates.values():
        total.update(shares(total))
        total["bounds"] = {}
        for row in ROWS:
            values = [v[row] for v in total["runs_rows"] if v.get(row) is not None]
            if len(values) >= 2:
                total["bounds"][row] = bound_of(values)
    return {"format": FORMAT, "method": method, "start": start, "made": made or datetime.date.today().isoformat(), "rates": rates}


def bound_for(stored, row, held=None):
    """The bound of a row: HELD's ({row: bound}, a bound the caller fixes whatever the runs spread over), else in a stored rate's
    figures its `bounds` (format 2), else the largest share of one run (format 1)."""
    if held and row in held:
        return held[row]
    if row in stored.get("bounds", {}):
        return stored["bounds"][row]
    return stored.get("most", {}).get(row)


def rows_beyond(report, base, held=None):
    """The rows of a report that are beyond the bounds of its rate (bound_for), [(row, figure, bound)]; [] when the baseline has no
    figure for the rate or the report was read another way."""
    stored = base.get("rates", {}).get(rate_of(report))
    if stored is None or (report.get("method"), report.get("start")) != (base.get("method"), base.get("start")):
        return []
    values = row_values(report)
    return [(row, values[row], bound_for(stored, row, held)) for row in ROWS
            if values.get(row) is not None and bound_for(stored, row, held) is not None
            and values[row] > bound_for(stored, row, held) + 1e-12]


def judge(report, base, held=None):
    """(verdict, text) of a run's report against a stored baseline: FAIL when a row (ROWS) is beyond the bound of its rate (bound_for:
    HELD's, else the median of the baseline's runs plus their range); recorded when the baseline has no figure for its rate; not
    measured when it was read another way or has no frames. Which rows refuse is pace.py's (BASELINE_REFUSES, BASELINE_HELD)."""
    key = rate_of(report)
    if (report.get("method"), report.get("start")) != (base.get("method"), base.get("start")):
        return "not measured: read by %s from %s, the baseline by %s from %s" % (
            report.get("method"), report.get("start"), base.get("method"), base.get("start")), ""
    stored = base.get("rates", {}).get(key)
    if stored is None:
        return "recorded", "no stored figure at %s Hz" % key
    values = row_values(report)
    parts = []
    for row in ROWS:
        bound = bound_for(stored, row, held)
        if bound is None:
            continue
        if values.get(row) is None:
            if row in SHARES:
                return "not measured: no %s" % row.replace("_", " "), ""
            continue
        if row in SHARES:
            parts.append("%s %.3f%% (bound %.3f%%)" % (row.replace("_share", "").replace("_", " "), 100 * values[row], 100 * bound))
        else:
            parts.append("%s %.*f (bound %.*f)" % (row.replace("_", " "), 2 if row == "refreshes_mean" else 1, values[row],
                                                  2 if row == "refreshes_mean" else 1, bound))
    worse = rows_beyond(report, base, held)
    return ("FAIL" if worse else "pass"), "; ".join(parts)


# ---------------------------------------------------------------------------------------------------------------------------
# The capture


def trace_config(duration_ms, apps):
    """Perfetto's text config: FrameTimeline in a buffer of its own (the ftrace part cannot crowd it out), the ftrace part in the
    second; DISCARD keeps the start of the run when a buffer fills."""
    atrace_apps = " ".join('atrace_apps: "%s"' % a for a in apps)
    return ("buffers { size_kb: 16384 fill_policy: DISCARD }\n"
            "buffers { size_kb: 65536 fill_policy: DISCARD }\n"
            "duration_ms: %d\n"
            'data_sources { config { name: "android.surfaceflinger.frametimeline" target_buffer: 0 } }\n'
            'data_sources { config { name: "linux.ftrace" target_buffer: 1 ftrace_config {\n'
            '    ftrace_events: "power/cpu_frequency" ftrace_events: "power/gpu_frequency" ftrace_events: "sched/sched_switch"\n'
            '    atrace_categories: "gfx" atrace_categories: "view" %s } } }\n' % (duration_ms, atrace_apps))


LAYER_LINE = re.compile(r"RequestedLayerState\{(.*?)(?: parentId=.*)?\}?\s*$")


def layer_name(line):
    """A layer listing line's layer name: Android 12 to 15 list the bare name ("SurfaceView[...](BLAST)#7"); Android 16 lists
    `RequestedLayerState{<name>#<id> parentId=...}`, whose name is the text between the brace and " parentId=" (the name
    `--latency` and the trace take)."""
    line = line.strip()
    found = LAYER_LINE.fullmatch(line)
    return found.group(1).strip() if found else line


def pick_layer(listing, package):
    """The game's layer in `dumpsys SurfaceFlinger --list`: a SurfaceView of the package, the BLAST one first, never its
    "Background for" layer; "" when there is none. Its bare name, whatever the listing's format (layer_name)."""
    names = [layer_name(l) for l in listing.splitlines()]
    names = [n for n in names if package in n and "SurfaceView" in n and not n.startswith("Background")]
    names.sort(key=lambda n: "(BLAST)" not in n)
    return names[0] if names else ""


class Capture:
    """One device run's capture (see the module's help). `device` is the harness's Device (shell, shell_input, adb, log_find);
    poll() is called with the game's process id and the log's tail at each progress look (every 2 s), tick() at every turn of
    the harness's loop between them (half a second apart: --latency holds 128 frames, 1.07 s at 120 Hz), finish() once at the
    run's end. No call raises: what fails is written into capture.json."""

    def __init__(self, device, run_folder, package, limit_seconds, clock=time.time):
        self.device, self.folder, self.package, self.clock = device, pathlib.Path(run_folder), package, clock
        self.duration_ms = int((limit_seconds + 60) * 1000)
        self.started, self.pid, self.layer, self.interval, self.next_read, self.reads = False, "", "", 1.0, 0.0, 0
        self.said = {"started": None, "layer": None, "perfetto": None, "latency_reads": 0, "trace": None, "errors": []}

    def _error(self, what, error):
        self.said["errors"].append("%s: %s" % (what, str(error)[:200]))

    def poll(self, game_pid, log_text=""):
        try:
            hz = REFRESH_HZ.findall(log_text or "")
            if hz:
                self.interval = 1.0 if int(hz[-1]) > 90 else 2.0
            if not self.started:
                if FRAME_ZERO.search(log_text or "") or self.device.log_find(game_pid, "harness: frame 0 is game frame"):
                    self.start()
                return
            if self.layer and self.clock() >= self.next_read:
                self.read_latency()
        except Exception as error:  # noqa: BLE001 - a capture never stops the run
            self._error("poll", error)

    def tick(self):
        """Between two progress looks: the --latency read when it is due, once the capture has started."""
        try:
            if self.started and self.layer and self.clock() >= self.next_read:
                self.read_latency()
        except Exception as error:  # noqa: BLE001 - a capture never stops the run
            self._error("tick", error)

    def start(self):
        self.started = True
        self.said["started"] = datetime.datetime.now().strftime("%H:%M:%S")
        code, listing = self.device.shell("dumpsys SurfaceFlinger --list", timeout=30)
        (self.folder / "sf-list.txt").write_text(listing, encoding="utf-8")
        self.layer = pick_layer(listing, self.package)
        self.said["layer"] = self.layer or None
        if not self.layer:
            self._error("layer", "no SurfaceView of %s in dumpsys SurfaceFlinger --list (exit %s)" % (self.package, code))
        config = trace_config(self.duration_ms, [self.package, self.package + ":game"])
        (self.folder / "trace-config.txt").write_text(config, encoding="utf-8")
        code, out = self.device.shell_input("perfetto --background --txt -c - -o %s" % TRACE_REMOTE, config, timeout=60)
        found = re.findall(r"\b(\d+)\b", out.strip().splitlines()[-1]) if out.strip() else []
        self.pid = found[-1] if code == 0 and found else ""
        self.said["perfetto"] = "pid %s" % self.pid if self.pid else "not started (exit %s): %s" % (code, out.strip()[:200])
        if self.layer:
            self.read_latency()

    def read_latency(self):
        code, out = self.device.shell("dumpsys SurfaceFlinger --latency %s" % shlex.quote(self.layer), timeout=30)
        with open(self.folder / "sf-latency.txt", "a", encoding="utf-8", newline="\n") as f:
            f.write("# read %d at %.3f exit %s\n%s\n" % (self.reads, self.clock(), code, out.strip()))
        self.reads += 1
        self.said["latency_reads"] = self.reads
        self.next_read = self.clock() + self.interval

    def remote_size(self):
        """The trace's size on the device in bytes, None when it cannot be read."""
        code, out = self.device.shell("stat -c %%s %s" % TRACE_REMOTE, timeout=30)
        return int(out.strip()) if code == 0 and out.strip().isdigit() else None

    def perfetto_alive(self):
        """Whether Perfetto's process still lives. `kill -0` as the shell user answered 1 for a process that was still writing
        (G10.2m, Android 16), so the process is looked for in /proc, where a living process has its directory."""
        code, _out = self.device.shell("[ -d /proc/%s ]" % self.pid, timeout=30)
        return code == 0

    def wait_written(self, sleep=time.sleep):
        """Waits, after SIGTERM, until Perfetto has written its trace out: its process has ended and the trace's size has stood
        still for SETTLED reads (half a second apart), at most SETTLE_SECONDS. The process alone is no proof (the shell user may
        not see it), the size alone is none (a write can pause): both. True when settled."""
        last, same = None, 0
        for _ in range(SETTLE_SECONDS * 2):
            size = self.remote_size()
            same = same + 1 if size is not None and size == last else 0
            last = size
            if same >= SETTLED and not self.perfetto_alive():
                return True
            sleep(0.5)
        return False

    def finish(self, sleep=time.sleep):
        try:
            if self.started:
                if self.layer:
                    self.read_latency()
                _code, jank = self.device.shell("dumpsys SurfaceFlinger --frametimeline -jank", timeout=60)
                (self.folder / "sf-jank.txt").write_text(jank, encoding="utf-8")
                if self.pid:
                    self.device.shell("kill -TERM %s" % self.pid, timeout=30)
                    settled = self.wait_written(sleep)
                    if not settled:
                        self._error("trace", "Perfetto was still writing the trace after %d s" % SETTLE_SECONDS)
                    pulled = self.device.adb("pull", TRACE_REMOTE, str(self.folder / TRACE_FILE), timeout=600)
                    self.said["trace"] = "pulled" if pulled.returncode == 0 and (self.folder / TRACE_FILE).is_file() else (
                        "not pulled: %s" % pulled.stderr.decode("utf-8", "replace").strip()[:200])
                    remote = self.remote_size()
                    local = (self.folder / TRACE_FILE).stat().st_size if (self.folder / TRACE_FILE).is_file() else None
                    if self.said["trace"] == "pulled" and remote is not None and local != remote:
                        self._error("trace", "pulled %s bytes of %d on the device" % (local, remote))
                    self.device.shell("rm -f %s" % TRACE_REMOTE, timeout=30)
            else:
                self.said["started"] = False
        except Exception as error:  # noqa: BLE001 - a capture never stops the run
            self._error("finish", error)
        try:
            (self.folder / "capture.json").write_text(json.dumps(self.said, indent=1), encoding="utf-8")
        except OSError:
            pass


# ---------------------------------------------------------------------------------------------------------------------------


def classify_runs(runs, out, first):
    """The results file of the classification: for each run the counts of its late frames by verdict and of its episodes (a run of
    late frames, what opened and closed it), then the first `first` late frames, each with its verdict."""
    lines = ["# frametimeline.py classify: a late frame (shown more than a refresh after the time it asked for) is judged by the pair it "
             "makes with the picture shown before it: held = refreshes between the two presents, asked = refreshes between the two "
             "times asked for; repeated (held 2 or more), skipped (asked more than held by over 1 ms), shifted (the other jumps over "
             "1 ms), on time (one picture a refresh as asked); seam (no picture before it, or a gap over %d refreshes)" % SEAM_REFRESHES,
             "run\tlate\trepeated\tskipped\tshifted\ton time\tseam\tepisodes\topened by a repeat\tclosed by a skip"]
    detail = ["run\tframe\tpresent_ns\tlate (refreshes)\theld\tasked\tverdict"]
    for run in runs:
        folder = pathlib.Path(run)
        period, frames, _ = read_latency((folder / "sf-latency.txt").read_text(encoding="utf-8", errors="replace"))
        rows = classify_late(frames, period)
        counts = {v: sum(1 for r in rows if r["verdict"] == v) for v in VERDICTS + ("seam",)}
        eps = episodes(frames, period)
        name = folder.parts[folder.parts.index("runs") + 1] if "runs" in folder.parts else str(run)
        lines.append("\t".join(str(x) for x in (name, len(rows), counts["repeated"], counts["skipped"], counts["shifted"], counts["on time"],
                                                 counts["seam"], len(eps), sum(1 for e in eps if e["opened"] == "repeated"),
                                                 sum(1 for e in eps if e["closed"] == "skipped"))))
        for r in rows[:first]:
            detail.append("\t".join(str(x) for x in (name, r["index"], r["present_ns"], r["late"], r["held"], r["asked"], r["verdict"])))
    pathlib.Path(out).write_text("\n".join(lines + [""] + detail) + "\n", encoding="utf-8", newline="\n")
    print("frametimeline.py: %d runs classified in %s" % (len(runs), out))
    return 0


def parser():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="command", required=True)
    r = sub.add_parser("read", help="a run folder's capture to its frametimeline.json")
    r.add_argument("run")
    r.add_argument("--method", choices=METHODS, default="auto")
    r.add_argument("--start", choices=STARTS, default="step")
    r.add_argument("--trace-processor", default="", help="trace_processor_shell (else %s)" % PROCESSOR_ENV)
    r.add_argument("--refresh-ms", type=float, default=None, help="the display's refresh where the sources give none")
    c = sub.add_parser("classify", help="one verdict for each late frame of runs' --latency reads (G10.4m, what the screen showed)")
    c.add_argument("runs", nargs="+")
    c.add_argument("--out", required=True, help="the results file (tab separated)")
    c.add_argument("--first", type=int, default=50, help="how many late frames of each run get a line of their own (all are counted)")
    b = sub.add_parser("baseline", help="the baseline of several runs' frametimeline.json")
    b.add_argument("runs", nargs="+")
    b.add_argument("--out", required=True)
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    if args.command == "read":
        report = read(args.run, args.method, args.start, args.trace_processor, args.refresh_ms)
        if report is None:
            print("frametimeline.py: %s has no capture" % args.run, file=sys.stderr)
            return 1
        print(json.dumps(report, indent=1))
        return 0
    if args.command == "classify":
        return classify_runs(args.runs, args.out, args.first)
    reports = []
    for run in args.runs:
        path = pathlib.Path(run) / REPORT if pathlib.Path(run).is_dir() else pathlib.Path(run)
        reports.append(json.loads(path.read_text(encoding="utf-8")))
    try:
        base = baseline(reports)
    except ValueError as error:
        print("frametimeline.py: %s" % error, file=sys.stderr)
        return 1
    pathlib.Path(args.out).write_text(json.dumps(base, indent=1) + "\n", encoding="utf-8")
    print("frametimeline.py: the baseline of %d runs in %s (%s)" % (len(reports), args.out, ", ".join(
        "%s Hz: %d runs" % (k, v["runs"]) for k, v in sorted(base["rates"].items()))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
