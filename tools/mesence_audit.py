#!/usr/bin/env python3
"""SNESticle Revive vs MesenCE correctness audit helper.

Two modes are intentionally separate:

* --strict: repository-local regression gate. It verifies opcode dispatch
  coverage and invariants for the MMIO/open-bus fixes without needing MesenCE.
* --mesen PATH: adds a structural comparison report against a MesenCE checkout.

The script is triage + regression protection, not a proof that two emulator
implementations are cycle-identical.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REVIVE_FILES = [
    "src/snes/core/snes.cpp",
    "src/snes/core/sndma.cpp",
    "src/snes/core/snmemmap.cpp",
    "src/snes/core/snsa1.cpp",
    "src/snes/core/sngsu.cpp",
    "src/snes/core/sncx4.cpp",
    "src/snes/core/snio.cpp",
    "src/snes/ppu/snppu.cpp",
    "src/snes/cpu/sncpu.c",
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

RANGES = [
    (0x2100, 0x213F, "PPU"),
    (0x2140, 0x217F, "APUIO"),
    (0x2180, 0x2183, "WRAM port"),
    (0x4200, 0x421F, "CPU internal"),
    (0x4300, 0x437F, "DMA/HDMA"),
]

SUSPICIOUS = re.compile(
    r"TODO|FIXME|unimplemented|open[- ]bus|approx|mario \?\?|return\s+0x(?:00|FF)\b",
    re.IGNORECASE,
)
CASE_HEX = re.compile(r"\bcase\s+0x([0-9A-Fa-f]{4})\s*:")
HEX_ADDR = re.compile(r"\b0x((?:21|42|43)[0-9A-Fa-f]{2})\b")

CPU_OP = re.compile(
    r"\bSNCPU_OP(?:_[A-Z0-9]+)?\(\s*(?:0x)?([0-9A-Fa-f]{2})\s*\)"
)
SPC_OP = re.compile(r"\bSNSPC_OP\(\s*(?:0x)?([0-9A-Fa-f]{2})\s*,")
ASM_TABLE = re.compile(r"\bSNCPU_OPTABLE_OP\(\s*0x([0-9A-Fa-f]{3})\s*\)")

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


def find_suspicious(root: Path) -> list[tuple[str, int, str]]:
    found: list[tuple[str, int, str]] = []
    for rel in REVIVE_FILES:
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


def opcode_set(regex: re.Pattern[str], source: str) -> set[int]:
    return {int(match.group(1), 16) for match in regex.finditer(source)}


def fmt_addr(value: int) -> str:
    return "$%04X" % value


def require_text(failures: list[str], source: str, needle: str, label: str) -> None:
    if needle not in source:
        failures.append("missing invariant: %s" % label)


def forbid_regex(
    failures: list[str], source: str, pattern: str, label: str
) -> None:
    if re.search(pattern, source, re.IGNORECASE | re.DOTALL):
        failures.append("forbidden regression: %s" % label)


def strict_checks(root: Path) -> list[str]:
    failures: list[str] = []
    snes = read_text(root, "src/snes/core/snes.cpp")
    dma = read_text(root, "src/snes/core/sndma.cpp")
    io = read_text(root, "src/snes/core/snio.cpp")
    ppu = read_text(root, "src/snes/ppu/snppu.cpp")
    cpu_h = read_text(root, "src/snes/cpu/sncpu.h")
    cpu_c = read_text(root, "src/snes/cpu/sncpu.c")
    cpu_interp = read_text(root, "src/snes/cpu/sncpu_c.c")
    cpu_ops = read_text(root, "src/snes/cpu/op65816.h")
    cpu_asm = read_text(root, "src/snes/cpu/sn65816.S")
    spc = read_text(root, "src/snes/apu/snspc_c.c")

    # Concrete MMIO/open-bus fixes.
    require_text(failures, cpu_h, "SNCPUGetOpenBus", "S-CPU open-bus latch")
    require_text(failures, cpu_c, "SNCPUSetOpenBus(pCpu, uData)", "C memory read bus update")
    require_text(failures, cpu_asm, "SNCPU_uOpenBus", "R5900 open-bus alias")
    require_text(failures, snes, "return pIO->m_Regs.wrio;", "$4213 RDIO")
    require_text(failures, snes, "m_PPU.LatchHV", "$4201/$2137 H/V latch")
    require_text(failures, snes, "return SNCPUGetOpenBus(pCpu);", "MMIO open-bus fallback")
    require_text(failures, dma, "SNCPUGetOpenBus(m_pCPU)", "DMA invalid-register open bus")
    require_text(failures, ppu, "m_PPU1OpenBus", "PPU1 open bus")
    require_text(failures, ppu, "m_PPU2OpenBus", "PPU2 open bus")
    require_text(failures, io, "m_uAluMulCounter = 8", "8-cycle multiplication")
    require_text(failures, io, "m_uAluDivCounter = 16", "16-cycle division")

    forbid_regex(
        failures,
        snes,
        r"case\s+0x4213\s*:.*?return\s+0\s*;",
        "$4213 returning zero",
    )
    forbid_regex(
        failures,
        snes,
        r"case\s+0x420B\s*:.*?return\s+.*?GetMDMAEnable",
        "$420B write-only readback",
    )
    forbid_regex(
        failures,
        snes,
        r"case\s+0x420C\s*:.*?return\s+.*?GetHDMAEnable",
        "$420C write-only readback",
    )
    forbid_regex(
        failures,
        cpu_interp,
        r"PC can does not wrap|Extra cycle for \(DP",
        "stale 65816 correctness-debt comment",
    )

    # Every legal opcode byte must have an explicit C/SPC handler.
    c_coverage = opcode_set(CPU_OP, cpu_interp + "\n" + cpu_ops)
    spc_coverage = opcode_set(SPC_OP, spc)
    asm_table = opcode_set(ASM_TABLE, cpu_ops)

    missing_c = sorted(set(range(0x100)) - c_coverage)
    missing_spc = sorted(set(range(0x100)) - spc_coverage)
    missing_asm = sorted(set(range(0x500)) - asm_table)

    if missing_c:
        failures.append(
            "65816 C opcode bytes missing: "
            + ", ".join("%02X" % value for value in missing_c)
        )
    if missing_spc:
        failures.append(
            "SPC700 opcode bytes missing: "
            + ", ".join("%02X" % value for value in missing_spc)
        )
    if missing_asm:
        failures.append(
            "R5900 dispatch entries missing: "
            + ", ".join("%03X" % value for value in missing_asm[:32])
            + (" ..." if len(missing_asm) > 32 else "")
        )

    return failures


def build_report(revive_root: Path, mesen_root: Path | None) -> str:
    lines = [
        "# Static MesenCE audit scan",
        "",
        "> Triage only: address presence is not proof of correct semantics.",
        "",
        "## Strict local checks",
        "",
    ]

    failures = strict_checks(revive_root)
    lines.append("- result: %s" % ("PASS" if not failures else "FAIL"))
    for failure in failures:
        lines.append("- %s" % failure)

    lines += ["", "## Suspicious Revive lines", ""]
    for rel, line_no, line in find_suspicious(revive_root):
        lines.append("- %s:%d — %s" % (rel, line_no, line))

    if mesen_root is not None:
        revive_regs = register_set(revive_root, REVIVE_FILES)
        mesen_regs = register_set(mesen_root, MESEN_FILES)
        lines += ["", "## Register/address scan vs MesenCE", ""]

        for lo, hi, name in RANGES:
            revive = {value for value in revive_regs if lo <= value <= hi}
            mesen = {value for value in mesen_regs if lo <= value <= hi}
            only_mesen = sorted(mesen - revive)
            only_revive = sorted(revive - mesen)
            lines += [
                "### %s %s-%s" % (name, fmt_addr(lo), fmt_addr(hi)),
                "- Revive literals/cases: %d" % len(revive),
                "- MesenCE literals/cases: %d" % len(mesen),
                "- Seen only in MesenCE scan: "
                + (", ".join(map(fmt_addr, only_mesen)) if only_mesen else "none"),
                "- Seen only in Revive scan: "
                + (", ".join(map(fmt_addr, only_revive)) if only_revive else "none"),
                "",
            ]

        lines += ["## MesenCE feature directories (informational only)", ""]
        for name, rel in FEATURE_DIRS.items():
            state = "present" if (mesen_root / rel).is_dir() else "not found"
            lines.append("- %s: %s (%s)" % (name, state, rel))

    lines += [
        "",
        "## Still requires behavioral tests",
        "",
        "- NMI/IRQ edge timing",
        "- DMA/HDMA cycle timing and bus conflicts",
        "- PPU rendering-time access restrictions",
        "- ROM mirroring/non-power-of-two mapping",
        "- coprocessor arbitration",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--revive",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="SNESticle Revive checkout (default: repository root)",
    )
    parser.add_argument("--mesen", type=Path, help="optional MesenCE checkout")
    parser.add_argument("--markdown", type=Path, help="write report to this file")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="return non-zero when a guarded invariant/opcode check fails",
    )
    args = parser.parse_args()

    revive = args.revive.resolve()
    mesen = args.mesen.resolve() if args.mesen else None
    report = build_report(revive, mesen)

    if args.markdown:
        args.markdown.write_text(report, encoding="utf-8")
        print(args.markdown)
    else:
        print(report)

    failures = strict_checks(revive)
    if args.strict and failures:
        for failure in failures:
            print("ERROR:", failure, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
