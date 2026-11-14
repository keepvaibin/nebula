#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The developer's tool for diagnostics reports (docs/DIAGNOSTICS_UPLOAD.md). Opening uses the game's own code
(NebulaNative --open-report), so there is one crypto implementation.

  diag_reports.py keygen  --exe <NebulaNative> --out <offline folder>
  diag_reports.py open    <folder of .nbdr files> --exe <NebulaNative> --key <private key file> [--out <folder>]
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


def load_reports(folder):
    reports = []
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".json"):
            continue
        try:
            with open(os.path.join(folder, name), encoding="ascii") as handle:
                report = json.load(handle)
            reports.append((name[:-5], report["benchmark"]))
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return reports


def scene_rows(report_id, bench):
    """One row per measured scene: the facts the summary and the CSV are made of."""
    device, settings = bench.get("device") or {}, bench.get("measured_with") or {}
    rows = []
    for scene in bench.get("scenes") or []:
        interval = scene.get("interval_ms") or {}
        passes = {k: v for k, v in (scene.get("gpu_pass_ms") or {}).items() if isinstance(v, (int, float))}
        top_pass = max(passes, key=passes.get) if passes else ""
        cpu = {k: v for k, v in (scene.get("cpu_ms") or {}).items() if k != "process" and isinstance(v, (int, float))}
        top_thread = max(cpu, key=cpu.get) if cpu else ""
        rows.append({
            "report": report_id,
            "gpu": device.get("gpu", ""),
            "vendor_id": device.get("vendor_id"),
            "device_id": device.get("device_id"),
            "gpu_class": gpu_class(device),
            "backend": bench.get("backend", ""),
            "cpu": device.get("cpu", ""),
            "logical_cores": device.get("logical_cores"),
            "ram_mb": device.get("ram_mb"),
            "os": device.get("os", ""),
            "output": "x".join(str(v) for v in device.get("output") or []),
            "internal_height": settings.get("internal_height"),
            "stage": scene.get("stage", ""),
            "mpixels": round((scene.get("pixels") or 0) / 1e6, 3),
            "p50_ms": interval.get("p50"),
            "p95_ms": interval.get("p95"),
            "p99_ms": interval.get("p99"),
            "gpu_ms": scene.get("gpu_ms"),
            "top_pass": top_pass,
            "top_pass_ms": passes.get(top_pass),
            "game_ms": cpu.get("game"),
            "render_ms": cpu.get("render"),
            "audio_ms": cpu.get("audio"),
            "top_thread": top_thread,
            "verdict": (bench.get("summary") or {}).get("verdict", ""),
        })
    return rows


def number(value):
    return value if isinstance(value, (int, float)) else 0.0


def summarise(reports, top=20):
    """Text of the summary: per GPU class and back end the costliest pass and thread, at what size, in which scene;
    then the slowest devices first."""
    rows = [row for report_id, bench in reports for row in scene_rows(report_id, bench)]
    lines = [f"{len(reports)} reports, {len(rows)} scenes"]
    groups = defaultdict(list)
    for row in rows:
        groups[(row["gpu_class"], row["backend"])].append(row)

    def group_cost(item):
        return statistics.median(number(r["p95_ms"]) for r in item[1])

    lines.append("")
    lines.append("By GPU class and back end (slowest median p95 first):")
    for (klass, backend), members in sorted(groups.items(), key=group_cost, reverse=True):
        pass_ms, thread_ms = defaultdict(list), defaultdict(list)
        for r in members:
            if r["top_pass"]:
                pass_ms[r["top_pass"]].append(number(r["top_pass_ms"]))
            for thread in ("game", "render", "audio"):
                if isinstance(r[f"{thread}_ms"], (int, float)):
                    thread_ms[thread].append(r[f"{thread}_ms"])
        worst = max(members, key=lambda r: number(r["p95_ms"]))
        top_pass = max(pass_ms, key=lambda k: statistics.mean(pass_ms[k])) if pass_ms else "-"
        top_thread = max(thread_ms, key=lambda k: statistics.mean(thread_ms[k])) if thread_ms else "-"
        devices = len({r["report"] for r in members})
        lines.append(
            f"  {klass} / {backend}: {devices} devices, median p95 {group_cost(((klass, backend), members)):.1f} ms; "
            f"costliest pass {top_pass}"
            + (f" ({statistics.mean(pass_ms[top_pass]):.2f} ms)" if pass_ms else "")
            + f"; costliest thread {top_thread}"
            + (f" ({statistics.mean(thread_ms[top_thread]):.2f} ms)" if thread_ms else "")
            + f"; worst scene {worst['stage']} at {worst['mpixels']} Mpx (height {worst['internal_height']}),"
            f" p95 {number(worst['p95_ms']):.1f} ms"
        )

    lines.append("")
    lines.append(f"Slowest devices (worst scene p95), top {top}:")
    worst_by_report = {}
    for row in rows:
        if row["report"] not in worst_by_report or number(row["p95_ms"]) > number(worst_by_report[row["report"]]["p95_ms"]):
            worst_by_report[row["report"]] = row
    for row in sorted(worst_by_report.values(), key=lambda r: number(r["p95_ms"]), reverse=True)[:top]:
        lines.append(
            f"  {number(row['p95_ms']):6.1f} ms  {row['gpu']} ({row['backend']}), {row['cpu']}, {row['logical_cores']} threads,"
            f" {row['ram_mb']} MB, {row['output']}: {row['stage']} at {row['mpixels']} Mpx, GPU {number(row['gpu_ms']):.1f} ms"
            f" (top pass {row['top_pass'] or '-'}), top thread {row['top_thread'] or '-'}"
        )
    return "\n".join(lines), rows


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
    text, rows = summarise(load_reports(a.folder), a.top)
    print(text)
    if a.csv:
        write_csv(rows, a.csv)
        print(f"CSV: {a.csv} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
