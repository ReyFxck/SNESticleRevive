#!/usr/bin/env python3
# Copyright (c) 1997-2004-2022 Icer Addis
# Re-Worked By ReyFxck, Claude Aí, ChatGPT
#
# Description:
#   Normalizes headers and harmless whitespace in project-owned source files.

"""Apply the SNESticleRevive source-header and whitespace policy.

The ownership allowlist is deliberately narrower than the repository tree.
Vendored engines, libraries and individual files carrying another author's
notice are never selected.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

COPYRIGHT = "Copyright (c) 1997-2004-2022 Icer Addis"
REWORKED = "Re-Worked By ReyFxck, Claude Aí, ChatGPT"
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".S", ".s", ".sh", ".py"}

OWNED_ROOTS = (
    "src/app",
    "src/common",
    "src/modules/audio",
    "src/modules/cdvd",
    "src/modules/mcsave",
    "src/modules/netplay",
    "src/platform/ps2",
    "src/snes",
    "tools",
)

EXCLUDED_PREFIXES = (
    "src/modules/cdfs_stream/",
    "src/modules/netplay/protocol/",
    "src/nes/",
    "src/third_party/",
)

EXCLUDED_FILES = {
    "src/platform/ps2/cdvd/cd.c",
    "src/platform/ps2/cdvd/cd.h",
    "src/platform/ps2/gs/gpprim.c",
    "src/platform/ps2/gs/gpprim.h",
    "src/platform/ps2/gs/gs.h",
    "src/platform/ps2/lowlevel/excepHandler.c",
    "src/platform/ps2/lowlevel/excepHandler.h",
    "src/platform/ps2/lowlevel/hw.h",
    "src/platform/ps2/lowlevel/hw.s",
    "src/platform/ps2/lowlevel/libxmtap.c",
    "src/platform/ps2/lowlevel/libxmtap.h",
    "src/platform/ps2/lowlevel/libxpad.c",
    "src/platform/ps2/lowlevel/libxpad.h",
    "src/platform/ps2/system/titleman.c",
    "src/platform/ps2/system/titleman.h",
    "src/snes/core/dsp4emu.cpp",
    "src/snes/core/dsp4emu.h",
}

AREA_DESCRIPTIONS = (
    ("src/app/", "the emulator application layer"),
    ("src/common/base/", "shared base utilities"),
    ("src/common/debug/", "shared debugging support"),
    ("src/common/io/", "shared input and output support"),
    ("src/common/media/", "shared media decoding"),
    ("src/common/render/", "shared rendering and audio buffers"),
    ("src/modules/audio/", "the PlayStation 2 audio backend"),
    ("src/modules/cdvd/", "the PlayStation 2 CD/DVD RPC client"),
    ("src/modules/mcsave/", "the PlayStation 2 memory-card save client"),
    ("src/modules/netplay/", "the emulator netplay frontend"),
    ("src/platform/ps2/common/", "shared PlayStation 2 platform definitions"),
    ("src/platform/ps2/gs/", "the PlayStation 2 Graphics Synthesizer backend"),
    ("src/platform/ps2/input/", "PlayStation 2 controller input"),
    ("src/platform/ps2/lowlevel/", "low-level PlayStation 2 support"),
    ("src/platform/ps2/memcard/", "PlayStation 2 memory-card access"),
    ("src/platform/ps2/system/", "the PlayStation 2 application runtime"),
    ("src/platform/ps2/ui/", "the PlayStation 2 user interface"),
    ("src/snes/apu/", "SNES audio processing"),
    ("src/snes/core/", "the SNES emulation core"),
    ("src/snes/cpu/", "SNES CPU emulation"),
    ("src/snes/ppu/", "SNES picture processing"),
    ("src/snes/rom/", "SNES cartridge loading and mapping"),
    ("src/snes/state/", "SNES save-state serialization"),
)

SPECIAL_DESCRIPTIONS = {
    "Makefile": "Builds, packages and validates SNESticleRevive for PlayStation 2.",
    "tools/fetch_libretro_covers.py": "Downloads and indexes matching Libretro artwork for ROM collections.",
    "tools/font_gen_ui.py": "Generates the embedded user-interface font assets.",
    "tools/snesdiag/analyze.py": "Summarizes snesdiag-v1 records from Android emulator text logs.",
    "tools/snesdiag/test_analyze.py": "Tests the snesdiag-v1 text-log analyzer.",
    "tools/normalize_project_sources.py": "Normalizes headers and harmless whitespace in project-owned source files.",
}

SEPARATOR_COMMENT = re.compile(
    r"^\s*(?://[/=*#\- ]{3,}|/\*[/=*#\- ]{4,}\*/)\s*$"
)
SECTION_BANNER = re.compile(
    r"^\s*/\*[-= ]*(?:include files?|preprocessor (?:defines?|definitions?)|"
    r"type definitions?|prototypes?|variables?|functions?|local functions?|"
    r"global variables?|private (?:functions?|implementation)|"
    r"public (?:functions?|implementation))[-= ]*\*/\s*$",
    re.IGNORECASE,
)
EMPTY_LINE_COMMENT = re.compile(r"^\s*//\s*$")
PREPROCESSOR_DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")
PROTECTED_OVERVIEW_MARKERS = (
    "licensed under",
    "license",
    "permission is hereby granted",
    "written by",
    "author:",
    "adapted from",
    "ported from",
    "derived from",
    "original implementation by",
    "nick van veen",
    "sjeep",
    "vzzrzzn",
    "zsnes",
    "snes9x",
    "ps2dev",
    "pukko",
    "tord lindstrom",
    "marcus r. brown",
    "http://",
    "https://",
)

def is_owned(relative: str) -> bool:
    if relative == "Makefile":
        return True
    if relative in EXCLUDED_FILES:
        return False
    if relative.startswith(EXCLUDED_PREFIXES):
        return False
    path = Path(relative)
    if path.suffix not in SOURCE_SUFFIXES:
        return False
    return any(relative == root or relative.startswith(root + "/") for root in OWNED_ROOTS)

def selected_files(root: Path) -> list[Path]:
    files = []
    for path in root.rglob("*"):
        if path.is_file() and not path.is_symlink():
            relative = path.relative_to(root).as_posix()
            if is_owned(relative):
                files.append(path)
    makefile = root / "Makefile"
    if makefile.is_file() and makefile not in files:
        files.append(makefile)
    return sorted(files, key=lambda item: item.relative_to(root).as_posix())

def description_for(relative: str) -> str:
    if relative in SPECIAL_DESCRIPTIONS:
        return SPECIAL_DESCRIPTIONS[relative]

    path = Path(relative)
    if relative.startswith("tools/"):
        suite = path.parent.name.replace("_", " ")
        if path.name == "build.sh":
            return f"Builds the {suite} host-side regression suite."
        if "test" in path.stem:
            return f"Exercises {path.stem.replace('_', ' ')} behavior in the {suite} regression suite."
        return f"Supports the {suite} development and regression tools."

    area = "SNESticleRevive"
    for prefix, candidate in AREA_DESCRIPTIONS:
        if relative.startswith(prefix):
            area = candidate
            break

    stem = path.stem.replace("_", " ")
    if path.suffix == ".h":
        return f"Declares the {stem} interface for {area}."
    if path.suffix in {".S", ".s"}:
        return f"Implements the {stem} assembly path for {area}."
    return f"Implements {stem} behavior for {area}."

def c_header(description: str) -> str:
    return (
        "/*\n"
        f" * {COPYRIGHT}\n"
        f" * {REWORKED}\n"
        " *\n"
        " * Description:\n"
        f" *   {description}\n"
        " */\n"
    )

def hash_header(description: str) -> str:
    return (
        f"# {COPYRIGHT}\n"
        f"# {REWORKED}\n"
        "#\n"
        "# Description:\n"
        f"#   {description}\n"
    )

def remove_standard_header(text: str, hash_style: bool) -> str:
    if hash_style:
        pattern = re.compile(
            rf"\A# {re.escape(COPYRIGHT)}\n"
            rf"# {re.escape(REWORKED)}\n"
            r"#\n# Description:\n#   .*?\n\n?",
        )
        return pattern.sub("", text, count=1)

    pattern = re.compile(
        rf"\A/\*\n \* {re.escape(COPYRIGHT)}\n"
        rf" \* {re.escape(REWORKED)}\n"
        r" \*\n \* Description:\n \*   .*?\n \*/\n\n?",
    )
    return pattern.sub("", text, count=1)

def replace_old_iaddis_header(text: str) -> str:
    stripped = text.lstrip("\n")
    if not stripped.startswith("/*!"):
        return text
    end = stripped.find("*/")
    if end < 0:
        return text
    block = stripped[: end + 2]
    if "Icer Addis" not in block or "\\File" not in block:
        return text
    return stripped[end + 2 :].lstrip("\n")

def remove_project_overview(text: str, filename: str) -> str:
    stripped = text.lstrip("\n")
    if not stripped.startswith("/*"):
        return text
    end = stripped.find("*/")
    if end < 0:
        return text
    block = stripped[: end + 2]
    lowered = block.lower()
    if any(marker in lowered for marker in PROTECTED_OVERVIEW_MARKERS):
        return text

    content = []
    for line in block.splitlines():
        cleaned = line.strip().lstrip("/*!").rstrip("*/").strip()
        if cleaned:
            content.append(cleaned)
    if not content:
        return stripped[end + 2 :].lstrip("\n")

    first = content[0].lower()
    if filename.lower() in first or "\\file" in lowered:
        return stripped[end + 2 :].lstrip("\n")
    return text

def remove_if_zero_blocks(text: str) -> str:
    lines = text.splitlines()
    output = []
    index = 0
    while index < len(lines):
        match = PREPROCESSOR_DIRECTIVE.match(lines[index])
        if not match or match.group(1) != "if" or not re.match(r"\s+0(?:\s|$)", match.group(2)):
            output.append(lines[index])
            index += 1
            continue

        depth = 1
        cursor = index + 1
        first_branch = None
        while cursor < len(lines):
            nested = PREPROCESSOR_DIRECTIVE.match(lines[cursor])
            if nested:
                directive = nested.group(1)
                if directive in {"if", "ifdef", "ifndef"}:
                    depth += 1
                elif directive == "endif":
                    depth -= 1
                    if depth == 0:
                        break
                elif depth == 1 and directive in {"elif", "else"} and first_branch is None:
                    first_branch = (cursor, directive, nested.group(2))
            cursor += 1

        if cursor >= len(lines):
            output.append(lines[index])
            index += 1
            continue

        if first_branch is not None:
            branch_index, directive, condition = first_branch
            retained = lines[branch_index + 1 : cursor]
            if directive == "elif":
                retained.insert(0, f"#if{condition}")
                retained.append("#endif")
            cleaned = remove_if_zero_blocks("\n".join(retained))
            output.extend(cleaned.splitlines())
        index = cursor + 1
    return "\n".join(output) + "\n"

def is_commented_code(line: str) -> bool:
    match = re.match(r"^\s*//\s?(.*)$", line)
    if not match:
        return False
    code = match.group(1).strip()
    if re.match(r"^#\s*include\b", code):
        return True
    if re.match(r"^(?:if|for|while|switch)\s*\(", code):
        return True
    if re.match(r"^(?:return|assert|typedef|using)\b.*;\s*$", code):
        return True
    if re.match(r"^[A-Za-z_]\w*(?:->\w+|\.\w+|\[[^]]+\])?\s*=.*;\s*$", code):
        return True
    if re.match(r"^[A-Za-z_]\w*\s*\(.*\)\s*;\s*$", code):
        return True
    return False

def clean_lines(text: str, suffix: str) -> str:
    output = []
    for raw_line in text.splitlines():
        line = raw_line.rstrip()
        leading = re.match(r"^[ \t]+", line)
        if leading and "\t" in leading.group(0):
            indent = re.sub(r" +\t", "\t", leading.group(0))
            line = indent + line[len(leading.group(0)) :]
        if suffix not in {".S", ".s"}:
            if SEPARATOR_COMMENT.match(line):
                continue
            if SECTION_BANNER.match(line):
                continue
            if EMPTY_LINE_COMMENT.match(line):
                continue
            if is_commented_code(line):
                continue
        if not line:
            if output and output[-1] == "":
                continue
            output.append("")
        else:
            output.append(line)
    while output and output[0] == "":
        output.pop(0)
    while output and output[-1] == "":
        output.pop()
    return ("\n".join(output) + "\n") if output else ""

def normalize(path: Path, root: Path) -> str:
    relative = path.relative_to(root).as_posix()
    suffix = path.suffix
    text = path.read_text(encoding="utf-8-sig")
    text = text.replace("\r\n", "\n").replace("\r", "\n")

    shebang = ""
    if suffix in {".sh", ".py"} and text.startswith("#!"):
        first, _, remainder = text.partition("\n")
        shebang = first + "\n"
        text = remainder

    hash_style = suffix in {".sh", ".py"} or relative == "Makefile"
    text = remove_standard_header(text.lstrip("\n"), hash_style)
    text = replace_old_iaddis_header(text)
    text = remove_project_overview(text, Path(relative).name)
    text = remove_if_zero_blocks(text)
    text = clean_lines(text, suffix)

    description = description_for(relative)
    header = hash_header(description) if hash_style else c_header(description)
    body = ("\n" + text) if text else ""
    if shebang:
        return shebang + header + body
    return header + body

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="report files that need normalization")
    parser.add_argument("--list", action="store_true", help="print the selected project-owned files")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    root = args.root.resolve()
    changed = []
    files = selected_files(root)
    if args.list:
        for path in files:
            print(path.relative_to(root).as_posix())
        return 0

    for path in files:
        original = path.read_text(encoding="utf-8-sig")
        updated = normalize(path, root)
        if original.replace("\r\n", "\n").replace("\r", "\n") == updated:
            continue
        changed.append(path.relative_to(root).as_posix())
        if not args.check:
            path.write_text(updated, encoding="utf-8", newline="\n")

    if changed:
        prefix = "needs normalization" if args.check else "normalized"
        for relative in changed:
            print(f"{prefix}: {relative}")
    print(f"selected={len(files)} changed={len(changed)} mode={'check' if args.check else 'write'}")
    return 1 if args.check and changed else 0

if __name__ == "__main__":
    sys.exit(main())
