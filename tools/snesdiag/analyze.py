#!/usr/bin/env python3
# Copyright (c) 1997-2004-2022 Icer Addis
# Re-Worked By ReyFxck, Claude Aí, ChatGPT
#
# Description:
#   Summarizes snesdiag-v1 records from Android emulator text logs.

"""Summarize snesdiag-v1 records from a PCSX2/NetherSX2/ARMSX2 TXT log."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

TAG_RE = re.compile(r"(\[(?:snes-[a-z0-9-]+|gsu-watchdog|rom-map)\])\s*(.*)", re.I)
PERCENT_KEYS = ("cpu", "ppu", "gsu", "apu", "mix", "mdma", "hdma")
CAPTURE_BITS = {
    0x01: "manual",
    0x02: "slow-frame",
    0x04: "ppu-queue",
    0x08: "dma-wrap",
    0x10: "obj",
    0x20: "frameskip",
    0x40: "audio",
    0x80: "chip",
}

def _uint(payload: str, name: str) -> Optional[int]:
    match = re.search(r"(?:^|\s)" + re.escape(name) + r"=(\d+)", payload)
    return int(match.group(1)) if match else None

def _tuple(payload: str, name: str, count: int) -> Optional[Tuple[int, ...]]:
    match = re.search(
        r"(?:^|\s)" + re.escape(name) + r"=((?:\d+/){" + str(count - 1) + r"}\d+)",
        payload,
    )
    if not match:
        return None
    return tuple(int(value) for value in match.group(1).split("/"))

def _hex(payload: str, name: str) -> Optional[int]:
    match = re.search(r"(?:^|\s)" + re.escape(name) + r"=([0-9a-f]+)", payload, re.I)
    return int(match.group(1), 16) if match else None

def _finding(severity: str, code: str, message: str, value: int = 0) -> Dict[str, object]:
    return {"severity": severity, "code": code, "message": message, "value": value}

def analyze_lines(lines: Iterable[str]) -> Dict[str, object]:
    stats: Counter[str] = Counter()
    schemas: Counter[str] = Counter()
    levels: Counter[int] = Counter()
    roms: Counter[str] = Counter()
    capture_reasons: Counter[str] = Counter()
    perf_sum: Counter[str] = Counter()
    perf_max: Counter[str] = Counter()
    perf_count: Counter[str] = Counter()
    capacity_min: Optional[float] = None
    host_targets: Counter[int] = Counter()

    for raw_line in lines:
        match = TAG_RE.search(raw_line)
        if not match:
            continue
        tag = match.group(1).lower()
        payload = match.group(2)
        stats["records"] += 1

        if tag == "[rom-map]" and payload.startswith("final "):
            title_match = re.search(r"title='([^']*)'", payload)
            mapper_match = re.search(r"mapper=([^\s]+)", payload)
            title = title_match.group(1) if title_match else "unknown"
            mapper = mapper_match.group(1) if mapper_match else "unknown"
            roms[f"{title} ({mapper})"] += 1
        elif tag == "[snes-diag]":
            schema_match = re.search(r"(?:^|\s)schema=([^\s]+)", payload)
            schemas[schema_match.group(1) if schema_match else "missing"] += 1
            level = _uint(payload, "level")
            if level is not None:
                levels[level] += 1
            stats["windows"] += 1
        elif tag == "[snes-frame]":
            stats["slow_frames"] += _uint(payload, "slow") or 0
            target = _uint(payload, "host-target")
            if target is not None:
                host_targets[target] += 1
            cap_match = re.search(r"(?:^|\s)capacity=(\d+)\.(\d+)\s+fps", payload)
            if cap_match:
                capacity = float(f"{cap_match.group(1)}.{cap_match.group(2)}")
                capacity_min = capacity if capacity_min is None else min(capacity_min, capacity)
        elif tag == "[snes-perf]":
            for key in PERCENT_KEYS:
                value_match = re.search(r"(?:^|\s)" + key + r"=(\d+)%", payload)
                if value_match:
                    value = int(value_match.group(1))
                    perf_sum[key] += value
                    perf_count[key] += 1
                    perf_max[key] = max(perf_max[key], value)
        elif tag == "[snes-raster]":
            values = _tuple(payload, "ppu queued/applied/full", 3)
            if values:
                stats["ppu_queue_full"] += values[2]
        elif tag == "[snes-sync]":
            stats["dma_wraps"] += _uint(payload, "wrap") or 0
        elif tag == "[snes-audio]":
            values = _tuple(payload, "mix calls/zero", 2)
            if values:
                stats["audio_zero_mix"] += values[1]
        elif tag == "[snes-video]":
            values = _tuple(payload, "rendered/skipped", 2)
            if values:
                stats["video_skipped"] += values[1]
        elif tag == "[snes-sdd1]":
            values = _tuple(payload, "dma/bytes/remaps/source-fail", 4)
            if values:
                stats["sdd1_dma"] += values[0]
                stats["sdd1_bytes"] += values[1]
                stats["sdd1_source_fail"] += values[3]
        elif tag == "[snes-gs-deep]" and "mismatch" in payload:
            values = _tuple(payload, "stage/copy", 2)
            if values:
                stats["gs_mismatch"] += values[0] + values[1]
        elif tag == "[snes-obj-event]":
            stats["obj_events"] += 1
        elif tag == "[snes-capture]" and " begin " in f" {payload} ":
            stats["captures"] += 1
            reasons = _hex(payload, "reasons") or 0
            for bit, name in CAPTURE_BITS.items():
                if reasons & bit:
                    capture_reasons[name] += 1
        elif tag == "[gsu-watchdog]" and "PBR=" in payload:
            stats["gsu_watchdogs"] += 1
        elif tag == "[snes-sdd1-error]":
            stats["sdd1_error_records"] += 1

    findings: List[Dict[str, object]] = []
    unknown_schemas = sorted(name for name in schemas if name != "snesdiag-v1")
    if unknown_schemas:
        findings.append(_finding("ERROR", "schema", "Unknown or missing schema: " + ", ".join(unknown_schemas)))
    if not stats["windows"]:
        findings.append(_finding("WARN", "no-window", "No complete snesdiag-v1 rolling window was found."))
    if stats["sdd1_source_fail"] or stats["sdd1_error_records"]:
        value = max(stats["sdd1_source_fail"], stats["sdd1_error_records"])
        findings.append(_finding("ERROR", "sdd1-source", "S-DD1 DMA used an unmapped source.", value))
    if stats["gsu_watchdogs"]:
        findings.append(_finding("ERROR", "gsu-watchdog", "The SuperFX watchdog stopped a runaway job.", stats["gsu_watchdogs"]))
    if stats["gs_mismatch"]:
        findings.append(_finding("ERROR", "gs-copy", "CPU-to-GS staging validation mismatched.", stats["gs_mismatch"]))
    warning_specs = (
        ("ppu_queue_full", "ppu-queue", "The PPU write queue filled and forced synchronization."),
        ("dma_wraps", "dma-wrap", "MDMA crossed a 16-bit source-bank boundary."),
        ("audio_zero_mix", "audio-zero", "The full audio mixer received a zero-sample request."),
        ("video_skipped", "frameskip", "Host catch-up rendered hidden emulated frames."),
        ("obj_events", "obj", "OBJ output collapsed or decoded unexpectedly empty."),
        ("slow_frames", "slow-frame", "Frames exceeded 105% of the active host refresh budget."),
    )
    for stat_name, code, message in warning_specs:
        if stats[stat_name]:
            findings.append(_finding("WARN", code, message, stats[stat_name]))

    hotspots = []
    for name in PERCENT_KEYS:
        if perf_count[name]:
            hotspots.append(
                {"name": name, "average_percent": round(perf_sum[name] / perf_count[name], 1), "max_percent": perf_max[name]}
            )
    hotspots.sort(key=lambda item: (item["max_percent"], item["average_percent"]), reverse=True)

    return {
        "schema": dict(schemas),
        "roms": dict(roms),
        "levels": {str(key): value for key, value in sorted(levels.items())},
        "stats": dict(stats),
        "capture_reasons": dict(capture_reasons),
        "host_targets": {str(key): value for key, value in sorted(host_targets.items())},
        "minimum_capacity_fps": capacity_min,
        "hotspots": hotspots,
        "findings": findings,
    }

def render_text(report: Dict[str, object]) -> str:
    stats = report["stats"]
    lines = [
        "snesdiag-v1 summary",
        f"windows={stats.get('windows', 0)} records={stats.get('records', 0)} captures={stats.get('captures', 0)}",
    ]
    roms = report["roms"]
    if roms:
        lines.append("roms=" + ", ".join(sorted(roms)))
    capacity = report.get("minimum_capacity_fps")
    if capacity is not None:
        lines.append(f"minimum compute capacity={capacity:.1f} fps")
    hotspots = report["hotspots"]
    if hotspots:
        lines.append(
            "hotspots=" + ", ".join(
                f"{item['name']} avg {item['average_percent']:.1f}% max {item['max_percent']}%" for item in hotspots[:4]
            )
        )
    reasons = report["capture_reasons"]
    if reasons:
        lines.append("capture reasons=" + ", ".join(f"{name}:{count}" for name, count in sorted(reasons.items())))
    findings = report["findings"]
    if findings:
        lines.append("findings:")
        lines.extend(
            f"- {item['severity']} {item['code']}: {item['message']}" + (f" ({item['value']})" if item["value"] else "")
            for item in findings
        )
    else:
        lines.append("findings: none in the captured windows")
    return "\n".join(lines)

def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="TXT log exported by the Android emulator")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument("--strict", action="store_true", help="exit non-zero when WARN/ERROR findings exist")
    args = parser.parse_args(argv)

    try:
        with args.log.open("r", encoding="utf-8", errors="replace") as handle:
            report = analyze_lines(handle)
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    print(json.dumps(report, indent=2, sort_keys=True) if args.json else render_text(report))
    return 1 if args.strict and report["findings"] else 0

if __name__ == "__main__":
    raise SystemExit(main())
