#!/usr/bin/env python3
# Copyright (c) 1997-2004-2022 Icer Addis
# Re-Worked By ReyFxck, Claude Aí, ChatGPT
# Conservative whitespace cleanup for project-owned source files.

"""Normalize only non-semantic formatting; preserve credits and hardware notes."""

from __future__ import annotations

import argparse
import io
import re
import tokenize
from pathlib import Path

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
}

# Skip generated font data; generated sources should be changed by their generator.
EXCLUDED_FILES.update({"src/common/base/font_cjk_data.cpp", "src/common/base/font_ui.cpp"})

# Recognize literals and comments before touching physical lines. A raw C++
# literal can span lines; whitespace in it is data, not formatting.
C_LEX = re.compile(
    r'(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)")'
    r'|(?P<literal>(?:u8|u|U|L)?(?:"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'))'
    r'|(?P<block>/\*.*?\*/)|(?P<line>//[^\n]*)',
    re.DOTALL,
)
DECORATION = re.compile(r'//(?:\s*|[/=*#\- ]{3,})\Z')


def is_owned(relative: str) -> bool:
    if relative == "Makefile":
        return True
    if relative in EXCLUDED_FILES or relative.startswith(EXCLUDED_PREFIXES):
        return False
    return (Path(relative).suffix in SOURCE_SUFFIXES
            and any(relative.startswith(root + "/") for root in OWNED_ROOTS))


def selected_files(root: Path) -> list[Path]:
    return sorted(path for path in root.rglob("*")
                  if path.is_file() and not path.is_symlink()
                  and is_owned(path.relative_to(root).as_posix()))


def compact_header(text: str) -> str:
    # Only the known generated header; keep the original attribution and description.
    return re.sub(r'\A(/\*\n \* Copyright \(c\) 1997-2004-2022 Icer Addis\n'
                  r' \* Re-Worked By ReyFxck, Claude Aí, ChatGPT\n)'
                  r' \*\n \* Description:\n \*   ([^\n]+)\n \*/',
                  r'\1 * \2\n */', text, count=1) if text.startswith("/*") else re.sub(
        r'\A((?:\#![^\n]*\n)?# Copyright \(c\) 1997-2004-2022 Icer Addis\n'
        r'# Re-Worked By ReyFxck, Claude Aí, ChatGPT\n)#\n# Description:\n#   ([^\n]+)\n',
        r'\1# \2\n', text, count=1)


def c_layout(text: str) -> str:
    lines = text.splitlines(keepends=True)
    protected: set[int] = set()
    decoration: set[int] = set()
    # A trailing backslash (including suspicious whitespace) can splice lines.
    for i, line in enumerate(lines):
        if line.rstrip().endswith("\\") or line.rstrip().endswith("??/"):
            protected.update((i, i + 1))
    for match in C_LEX.finditer(text):
        start = text.count("\n", 0, match.start())
        end = start + match.group().count("\n")
        if match.lastgroup in {"raw", "literal", "block"} and end > start:
            protected.update(range(start, end + 1))
        if (match.lastgroup == "line" and DECORATION.fullmatch(match.group())
                and not lines[start][:match.start() - (text.rfind("\n", 0, match.start()) + 1)].strip()):
            decoration.add(start)
    output: list[str] = []
    for i, line in enumerate(lines):
        if i in protected:
            output.append(line)
            continue
        if i in decoration:
            continue
        line = line.rstrip(" \t\r\n") + "\n"
        if line == "\n" and (not output or output[-1] == "\n"):
            continue
        output.append(line)
    while output and output[-1] == "\n":
        output.pop()
    return "".join(output)


def python_signature(text: str) -> list[tuple[int, str]]:
    ignored = {tokenize.COMMENT, tokenize.NL, tokenize.ENCODING, tokenize.ENDMARKER}
    return [(token.type, token.string) for token in tokenize.generate_tokens(io.StringIO(text).readline)
            if token.type not in ignored]


def python_layout(text: str) -> str:
    protected: set[int] = set()
    for token in tokenize.generate_tokens(io.StringIO(text).readline):
        if token.type == tokenize.STRING and token.start[0] != token.end[0]:
            protected.update(range(token.start[0], token.end[0] + 1))
    output: list[str] = []
    for i, line in enumerate(text.splitlines(keepends=True), 1):
        if i in protected or line.rstrip().endswith("\\"):
            output.append(line)
            continue
        output.append(line.rstrip(" \t\r\n") + "\n")
    updated = "".join(output)
    if python_signature(text) != python_signature(updated):
        raise ValueError("Python token stream changed")
    return updated


def normalize(path: Path, root: Path) -> str:
    original = path.read_bytes().decode("utf-8")
    # Do not convert newline conventions or a BOM silently.
    if "\r" in original or original.startswith("\ufeff"):
        return original
    text = compact_header(original)
    if path.suffix in {".c", ".cpp", ".h", ".S", ".s"}:
        return c_layout(text)
    if path.suffix == ".py":
        return python_layout(text)
    # Make/shell recipes may embed here-docs or multi-line strings. Only compact
    # the known top-of-file header; never rewrite the recipe body automatically.
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report changes without writing")
    parser.add_argument("--list", action="store_true", help="list project-owned files")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    files = selected_files(root)
    changed = []
    for path in files:
        relative = path.relative_to(root).as_posix()
        if args.list:
            print(relative)
            continue
        original = path.read_bytes().decode("utf-8")
        updated = normalize(path, root)
        if original != updated:
            changed.append(relative)
            if not args.check:
                path.write_bytes(updated.encode("utf-8"))
            print(f"{'needs normalization' if args.check else 'normalized'}: {relative}")
    if not args.list:
        print(f"selected={len(files)} changed={len(changed)} mode={'check' if args.check else 'write'}")
    return int(args.check and bool(changed))


if __name__ == "__main__":
    raise SystemExit(main())
