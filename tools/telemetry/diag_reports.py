#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The developer's tool for diagnostics reports (runtime/include/galaxy/diag_report.h). Opening uses the game's own code
(Nebula --open-report), so there is one crypto implementation.

  diag_reports.py keygen  --exe <Nebula> --out <offline folder>
  diag_reports.py open    <folder of .nbdr files> --exe <Nebula> --key <private key file> [--out <folder>]
  diag_reports.py summary <folder of opened .json> [--csv <file>] [--top N]
"""
import argparse
import csv
import hashlib
import json
import os
import statistics
import subprocess
import sys
from collections import defaultdict

ENVELOPE_MAX = 256 * 1024
ENVELOPE_MIN = 48 + 16
MAGIC = b"NBDR\x01"

VENDORS = {
    0x8086: "Intel", 0x1002: "AMD", 0x1022: "AMD", 0x10DE: "NVIDIA", 0x106B: "Apple", 0x13B5: "Arm Mali",
    0x5143: "Qualcomm Adreno", 0x1010: "Imagination PowerVR", 0x14E4: "Broadcom VideoCore", 0x144D: "Samsung Xclipse",
    0x19E5: "Huawei", 0x1414: "Microsoft (software)",
}


def gpu_class(device):
    """Vendor, integrated or discrete, and the GPU memory budget's tier: the group a device is summarised in."""
    vendor = VENDORS.get(device.get("vendor_id") or 0, "other")
    kind = "integrated" if device.get("shared_memory") else "discrete"
    budget = device.get("gpu_budget_mb") or 0
    tier = "<2GB" if budget < 2048 else "2-4GB" if budget < 4096 else "4-8GB" if budget < 8192 else "8GB+"
    return f"{vendor} {kind} {tier}"


def run_exe(exe, args, timeout=60):
    return subprocess.run([exe] + args, capture_output=True, timeout=timeout)


def exe_opener(exe, key):
    """open_folder's opener on the game's executable: (status, report text or reason)."""
    def opener(path):
        try:
            done = run_exe(exe, ["--open-report", path, "--key", key])
        except subprocess.TimeoutExpired:
            return "junk", "timed out"
        if done.returncode == 0:
            return "ok", done.stdout.decode("ascii", "replace")
        return "junk", done.stderr.decode("ascii", "replace").strip()
    return opener


def open_folder(folder, out, opener):
    """Opens every envelope under `folder` into `out`/<hash>.json. Junk (not ours, other key, changed, not in the
    schema, wrong size) and duplicates (the same envelope, or the same report sealed twice) are skipped and counted."""
    os.makedirs(out, exist_ok=True)
    counts = {"opened": 0, "junk": 0, "duplicate": 0}
    seen_envelopes, seen_reports = set(), set()
    for root, _, files in os.walk(folder):
        for name in sorted(files):
            path = os.path.join(root, name)
            try:
                with open(path, "rb") as handle:
                    data = handle.read(ENVELOPE_MAX + 1)
            except OSError:
                counts["junk"] += 1
                continue
            if not (ENVELOPE_MIN <= len(data) <= ENVELOPE_MAX) or not data.startswith(MAGIC):
                counts["junk"] += 1
                continue
            envelope_hash = hashlib.sha256(data).hexdigest()
            if envelope_hash in seen_envelopes:
                counts["duplicate"] += 1
                continue
            seen_envelopes.add(envelope_hash)
            status, text = opener(path)
            report = None
            if status == "ok":
                try:
                    report = json.loads(text)
                except ValueError:
                    report = None
            if not isinstance(report, dict) or report.get("report") != 1 or not isinstance(report.get("benchmark"), dict):
                counts["junk"] += 1
                continue
            canonical = json.dumps(report, sort_keys=True, separators=(",", ":")).encode()
            report_hash = hashlib.sha256(canonical).hexdigest()
            if report_hash in seen_reports:
                counts["duplicate"] += 1
                continue
            seen_reports.add(report_hash)
            with open(os.path.join(out, report_hash[:32] + ".json"), "wb") as handle:
                handle.write(canonical)
            counts["opened"] += 1
    return counts


SCHEMA = 2  # benchmark_stats.h kResultSchema: reports of another schema are counted and left out


def load_reports(folder):
    """The opened reports in `folder` whose benchmark result has this tool's schema, as [(id, result)], and how many
    were left out for another schema."""
    reports, other = [], 0
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".json"):
            continue
        try:
            with open(os.path.join(folder, name), encoding="ascii") as handle:
                bench = json.load(handle)["benchmark"]
        except (OSError, ValueError, KeyError, TypeError):
            continue
        if not isinstance(bench, dict) or bench.get("schema") != SCHEMA:
            other += 1
            continue
        reports.append((name[:-5], bench))
    return reports, other


def known(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def average(spread):
    """A per-frame cost's average: {average, p99, worst}, or None for null."""
    return spread.get("average") if isinstance(spread, dict) and known(spread.get("average")) else None


STUTTER_MS = 25.0  # benchmark_stats.h kStutterMs


def scene_rows(report_id, bench):
    """One row per measured scene of each part (the player's settings, the standard settings): the facts the summary
    and the CSV are made of."""
    device = bench.get("device") or {}
    rows = []
    for run in bench.get("runs") or []:
        settings = run.get("measured_with") or {}
        for scene in run.get("scenes") or []:
            frame_ms = scene.get("frame_ms") or {}
            passes = {name: cost for name, cost in (scene.get("gpu_passes") or {}).items() if isinstance(cost, dict)}
            top_pass = max(passes, key=lambda name: number(passes[name].get("average"))) if passes else ""
            cpu = scene.get("cpu_ms") or {}
            causes, slow_parts = defaultdict(int), defaultdict(int)
            for frame in scene.get("slowest_frames") or []:
                if number(frame.get("ms")) > STUTTER_MS:
                    causes[frame.get("cause", "")] += 1
                # The render thread's largest part in each of the slowest frames (the ones that make the 1% low).
                timed = {name: ms for name, ms in (frame.get("render_parts") or {}).items() if known(ms)}
                if timed:
                    slow_parts[max(timed, key=timed.get)] += 1
            rows.append({
                "report": report_id,
                "part": run.get("part", ""),
                "gpu": device.get("gpu", ""),
                "vendor_id": device.get("vendor_id"),
                "device_id": device.get("device_id"),
                "driver": device.get("driver_version") or "",
                "gpu_class": gpu_class(device),
                "backend": bench.get("backend", ""),
                "cpu": device.get("cpu", ""),
                "logical_cores": device.get("logical_cores"),
                "ram_mb": device.get("ram_mb"),
                "os": device.get("os", ""),
                "output": "x".join(str(v) for v in device.get("output") or []),
                "internal_height": settings.get("internal_height"),
                "stage": scene.get("stage", ""),
                "mpixels": round(number(scene.get("pixels")) / 1e6, 3),
                "average_fps": scene.get("average_fps"),
                "low_10_fps": scene.get("low_10_fps"),
                "low_1_fps": scene.get("low_1_fps"),
                "p95_ms": frame_ms.get("p95"),
                "p99_ms": frame_ms.get("p99"),
                "stutters": scene.get("stutters"),
                "limiter": scene.get("limiter", ""),
                "gpu_ms": average(scene.get("gpu_ms")),
                "top_pass": top_pass,
                "top_pass_ms": (passes.get(top_pass) or {}).get("average"),
                "top_pass_share": (passes.get(top_pass) or {}).get("share"),
                "game_ms": average(cpu.get("game")),
                "render_ms": average(cpu.get("render")),
                "audio_ms": cpu.get("audio") if known(cpu.get("audio")) else None,
                "stutter_causes": ";".join(f"{name}={count}" for name, count in sorted(causes.items())),
                "slow_frame_parts": ";".join(f"{name}={count}" for name, count in sorted(slow_parts.items())),
                "verdict": (bench.get("summary") or {}).get("verdict", ""),
                "passes": {name: cost.get("share") for name, cost in passes.items() if known(cost.get("share"))},
            })
    return rows


def number(value):
    return value if known(value) else 0.0


def summarise(reports, top=20):
    """Text of the summary, and the CSV's rows. Devices are compared on the standard part, where every device ran the
    same settings: per GPU class and back end, the median 1% low, what limits the frame and the worst scene; then the
    top costs (GPU passes by their share of the GPU's frame, and what the stutters came with); then the slowest devices
    first."""
    rows = [row for report_id, bench in reports for row in scene_rows(report_id, bench)]
    standard = [row for row in rows if row["part"] == "standard"]
    lines = [f"{len(reports)} reports, {len(rows)} scenes ({len(standard)} at the standard settings)"]
    groups = defaultdict(list)
    for row in standard:
        groups[(row["gpu_class"], row["backend"])].append(row)

    def group_low(item):
        return statistics.median(number(r["low_1_fps"]) for r in item[1])

    lines.append("")
    lines.append("By GPU class and back end, standard settings (lowest median 1% low first):")
    for (klass, backend), members in sorted(groups.items(), key=group_low):
        limiters = defaultdict(int)
        for r in members:
            limiters[r["limiter"] or "-"] += 1
        limiter = max(limiters, key=limiters.get)
        worst = min(members, key=lambda r: number(r["low_1_fps"]))
        devices = len({r["report"] for r in members})
        lines.append(
            f"  {klass} / {backend}: {devices} devices, median 1% low {group_low(((klass, backend), members)):.1f} FPS,"
            f" median average {statistics.median(number(r['average_fps']) for r in members):.1f} FPS;"
            f" limited by {limiter} in {limiters[limiter]} of {len(members)} scenes;"
            f" worst scene {worst['stage']}, 1% low {number(worst['low_1_fps']):.1f} FPS"
        )

    lines.append("")
    lines.append("Top costs, both parts (GPU passes by mean share of the GPU's frame; stutters by what came with them; the"
                 " slowest frames by the render thread's largest part):")
    by_group = defaultdict(list)
    for row in rows:
        by_group[(row["gpu_class"], row["backend"])].append(row)
    for (klass, backend), members in sorted(by_group.items()):
        shares, causes, parts = defaultdict(list), defaultdict(int), defaultdict(int)
        for r in members:
            for name, share in r["passes"].items():
                shares[name].append(share)
            for item in filter(None, r["stutter_causes"].split(";")):
                name, _, count = item.rpartition("=")
                causes[name] += int(count)
            for item in filter(None, r["slow_frame_parts"].split(";")):
                name, _, count = item.rpartition("=")
                parts[name] += int(count)
        ranked = sorted(shares, key=lambda name: statistics.mean(shares[name]), reverse=True)[:5]
        passes = ", ".join(f"{name} {100 * statistics.mean(shares[name]):.0f}%" for name in ranked) or "-"
        stutters = ", ".join(f"{name} {count}" for name, count in sorted(causes.items(), key=lambda i: -i[1])) or "none"
        slow = ", ".join(f"{name} {count}" for name, count in sorted(parts.items(), key=lambda i: -i[1])) or "not timed"
        lines.append(f"  {klass} / {backend}: passes {passes}; stutters {stutters}; slowest frames {slow}")

    lines.append("")
    lines.append(f"Slowest devices (lowest 1% low at the standard settings), top {top}:")
    worst_by_report = {}
    for row in standard:
        if row["report"] not in worst_by_report or number(row["low_1_fps"]) < number(worst_by_report[row["report"]]["low_1_fps"]):
            worst_by_report[row["report"]] = row
    for row in sorted(worst_by_report.values(), key=lambda r: number(r["low_1_fps"]))[:top]:
        lines.append(
            f"  {number(row['low_1_fps']):7.1f} FPS  {row['gpu']} ({row['backend']}), {row['cpu']},"
            f" {row['logical_cores']} threads, {row['ram_mb']} MB, {row['output']}: {row['stage']},"
            f" average {number(row['average_fps']):.1f} FPS, GPU {number(row['gpu_ms']):.2f} ms"
            f" (top pass {row['top_pass'] or '-'}), limited by {row['limiter'] or '-'}"
        )
    return "\n".join(lines), [{key: value for key, value in row.items() if key != "passes"} for row in rows]


# A report's strings are anyone's (anyone can seal to the public key): in a CSV cell they stay text. Control characters
# and line breaks are dropped, and a cell a spreadsheet would read as a formula (= + - @ first) starts with a quote.
FORMULA_LEADS = ("=", "+", "-", "@")


def csv_cell(value):
    if not isinstance(value, str):
        return value
    text = "".join(ch for ch in value if ch >= " " and ch != "\x7f")
    return "'" + text if text.startswith(FORMULA_LEADS) else text


def write_csv(rows, path):
    fields = list(rows[0].keys()) if rows else ["report"]
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows({key: csv_cell(value) for key, value in row.items()} for row in rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    keygen = sub.add_parser("keygen", help="make the key pair (the private key stays offline)")
    keygen.add_argument("--exe", required=True)
    keygen.add_argument("--out", required=True)
    opening = sub.add_parser("open", help="open every report in a folder")
    opening.add_argument("folder")
    opening.add_argument("--exe", required=True)
    opening.add_argument("--key", required=True)
    opening.add_argument("--out")
    summary = sub.add_parser("summary", help="aggregate opened reports")
    summary.add_argument("folder")
    summary.add_argument("--csv")
    summary.add_argument("--top", type=int, default=20)
    a = parser.parse_args(argv)

    if a.command == "keygen":
        done = run_exe(a.exe, ["--report-keygen", a.out])
        sys.stdout.write(done.stdout.decode("utf-8", "replace"))
        sys.stderr.write(done.stderr.decode("utf-8", "replace"))
        return done.returncode
    if a.command == "open":
        out = a.out or a.folder.rstrip("/\\") + "-opened"
        counts = open_folder(a.folder, out, exe_opener(a.exe, a.key))
        print(f"opened {counts['opened']}, junk {counts['junk']}, duplicates {counts['duplicate']} -> {out}")
        return 0
    reports, other = load_reports(a.folder)
    text, rows = summarise(reports, a.top)
    print(text)
    if other:
        print(f"{other} reports of another schema left out")
    if a.csv:
        write_csv(rows, a.csv)
        print(f"CSV: {a.csv} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
