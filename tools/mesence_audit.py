#!/usr/bin/env python3
"""Static first-pass SNESticle Revive vs MesenCE audit helper.

This tool does not decide hardware correctness. It finds suspicious lines and
compares register/address coverage so differences can be reviewed manually.

Usage:
  python3 tools/mesence_audit.py --mesen ../MesenCE
  python3 tools/mesence_audit.py --mesen ../MesenCE --markdown audit.md
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

REVIVE_FILES = [
    "src/snes/core/snes.cpp",
    "src/snes/core/sndma.cpp",
    "src/snes/core/snmemmap.cpp",
    "src/snes/core/snsa1.cpp",
    "src/snes/core/sngsu.cpp",
    "src/snes/core/sncx4.cpp",
    "src/snes/ppu/snppu.cpp",
    "src/snes/cpu/sncpu_c.c",
    "src/snes/cpu/sn65816.S",
    "src/snes/apu/snspc_c.c",
]

MESEN_FILES = [
    "Core/SNES/InternalRegisters.cpp",
    "Core/SNES/RegisterHandlerB.cpp",
    "Core/SNES/SnesDmaController.cpp",
    "Core/SNES/SnesMemoryManager.cpp",
    "Core/SNES/SnesPpu.cpp",
    "Core/SNES/SnesCpu.cpp",
]

SUSPICIOUS = re.compile(
    r"TODO|FIXME|unimplemented|open[- ]bus|approx|mario \?\?|return\s+0x(?:00|FF)\b",
    re.IGNORECASE,
)
CASE_HEX = re.compile(r"\bcase\s+0x([0-9A-Fa-f]{4})\s*:")
HEX_ADDR = re.compile(r"\b0x((?:21|42|43)[0-9A-Fa-f]{2})\b")

RANGES = [
    (0x2100, 0x213F, "PPU"),
    (0x2140, 0x217F, "APUIO"),
    (0x2180, 0x2183, "WRAM port"),
    (0x4200, 0x421F, "CPU internal"),
    (0x4300, 0x437F, "DMA/HDMA"),
]

FEATURE_DIRS = {
    "BS-X / Satellaview": "Core/SNES/Coprocessors/BSX",
    "MSU1": "Core/SNES/Coprocessors/MSU1",
    "Super Game Boy": "Core/SNES/Coprocessors/SGB",
    "SPC7110": "Core/SNES/Coprocessors/SPC7110",
    "ST018": "Core/SNES/Coprocessors/ST018",
    "Sufami Turbo": "Core/SNES/Coprocessors/SufamiTurbo",
}


def read_text(root: Path, rel: str) -> str:
    path = root / rel
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def suspicious_lines(root: Path, files: list[str]) -> list[tuple[str, int, str]]:
    found = []
    for rel in files:
        for line_no, line in enumerate(read_text(root, rel).splitlines(), 1):
            if SUSPICIOUS.search(line):
                found.append((rel, line_no, line.strip()))
    return found


def register_set(root: Path, files: list[str]) -> set[int]:
    out: set[int] = set()
    for rel in files:
        source = read_text(root, rel)
        for regex in (CASE_HEX, HEX_ADDR):
            for match in regex.finditer(source):
                value = int(match.group(1), 16)
                if any(lo <= value <= hi for lo, hi, _ in RANGES):
                    out.add(value)
    return out


def fmt_addr(value: int) -> str:
    return "$%04X" % value


def build_report(revive_root: Path, mesen_root: Path) -> str:
    revive_regs = register_set(revive_root, REVIVE_FILES)
    mesen_regs = register_set(mesen_root, MESEN_FILES)

    lines = [
        "# Static MesenCE audit scan",
        "",
        "> Triage only: address presence is not proof of correct semantics.",
        "",
        "## Suspicious Revive lines",
        "",
    ]

    for rel, line_no, line in suspicious_lines(revive_root, REVIVE_FILES):
        lines.append("- %s:%d — %s" % (rel, line_no, line))

    lines += ["", "## Register/address scan", ""]

    for lo, hi, name in RANGES:
        revive = {value for value in revive_regs if lo <= value <= hi}
        mesen = {value for value in mesen_regs if lo <= value <= hi}
        only_mesen = sorted(mesen - revive)
        only_revive = sorted(revive - mesen)

        lines += [
            "### %s %s-%s" % (name, fmt_addr(lo), fmt_addr(hi)),
            "- Revive literals/cases: %d" % len(revive),
            "- MesenCE literals/cases: %d" % len(mesen),
            "- Seen only in MesenCE scan: " + (
                ", ".join(fmt_addr(v) for v in only_mesen) if only_mesen else "none"
            ),
            "- Seen only in Revive scan: " + (
                ", ".join(fmt_addr(v) for v in only_revive) if only_revive else "none"
            ),
            "",
        ]

    lines += ["## MesenCE feature directories", ""]
    for name, rel in FEATURE_DIRS.items():
        state = "present" if (mesen_root / rel).is_dir() else "not found"
        lines.append("- %s: %s (%s)" % (name, state, rel))

    lines += [
        "",
        "## Manual checks the script cannot decide",
        "",
        "- open-bus source/update timing",
        "- read/write side effects",
        "- NMI/IRQ edge timing",
        "- multiply/divide latency",
        "- DMA/HDMA cycle timing and bus conflicts",
        "- PPU latch behavior",
        "- ROM mirroring/non-power-of-two mapping",
        "- coprocessor arbitration",
        "",
    ]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--revive",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="SNESticle Revive checkout (default: repository root)",
    )
    parser.add_argument("--mesen", type=Path, required=True, help="MesenCE checkout")
    parser.add_argument("--markdown", type=Path, help="Write report to this file")
    args = parser.parse_args()

    report = build_report(args.revive.resolve(), args.mesen.resolve())
    if args.markdown:
        args.markdown.write_text(report, encoding="utf-8")
        print(args.markdown)
    else:
        print(report)


if __name__ == "__main__":
    main()
