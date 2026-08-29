#!/usr/bin/env python3
"""Download matching Libretro artwork for SNES/NES ROM folders.

The destination mirrors the ROM directory tree and uses the folders already
understood by SNESticle Revive:

    Named_Boxarts/<rom base>.png
    Named_Titles/<rom base>.png
    Named_Snaps/<rom base>.png
    Named_Logos/<rom base>.png

The ROM itself is never modified. Missing or ambiguously named games are
reported and skipped so an ISO build can still finish.
"""

from __future__ import print_function

import argparse
import concurrent.futures
import os
import re
import sys
import tempfile
import zipfile
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen


DEFAULT_BASE_URL = "https://thumbnails.libretro.com"
MAX_PNG_BYTES = 8 * 1024 * 1024

SYSTEM_DIRS = {
    "snes": "Nintendo - Super Nintendo Entertainment System",
    "nes": "Nintendo - Nintendo Entertainment System",
}

CATEGORIES = (
    "Named_Boxarts",
    "Named_Titles",
    "Named_Snaps",
    "Named_Logos",
)

INDEX_FILE_NAME = "COVERS.IDX"
INDEX_HEADER = "SNESTICLE-COVERS-1"
INDEX_CODES = {
    None: "R",
    "Named_Boxarts": "B",
    "Named_Titles": "T",
    "Named_Snaps": "S",
    "Named_Logos": "L",
}

ROM_SYSTEM_BY_SUFFIX = {
    ".smc": "snes",
    ".sfc": "snes",
    ".swc": "snes",
    ".fig": "snes",
    ".nes": "nes",
    ".fds": "nes",
}

INVALID_THUMBNAIL_CHARS = '&*/:`<>?"\\|'
INVALID_TRANSLATION = str.maketrans(
    {character: "_" for character in INVALID_THUMBNAIL_CHARS}
)

REGION_ALIASES = (
    ("(U)", "(USA)"),
    ("(E)", "(Europe)"),
    ("(J)", "(Japan)"),
    ("(W)", "(World)"),
)


def _add_unique(items, value):
    value = value.strip()
    if value and value not in items:
        items.append(value)


def _archive_info(path):
    """Return (systems, member bases) for supported files inside a ZIP."""
    systems = set()
    member_bases = []
    try:
        with zipfile.ZipFile(str(path), "r") as archive:
            for name in archive.namelist():
                if name.endswith("/"):
                    continue
                member = Path(name)
                system = ROM_SYSTEM_BY_SUFFIX.get(member.suffix.lower())
                if not system:
                    continue
                systems.add(system)
                _add_unique(member_bases, member.stem)
    except (OSError, zipfile.BadZipFile, RuntimeError):
        return set(), []
    return systems, member_bases


def _rom_info(path, forced_system):
    suffix = path.suffix.lower()
    base = path.stem
    inner_bases = []

    if forced_system != "auto":
        system = forced_system
    elif suffix in ROM_SYSTEM_BY_SUFFIX:
        system = ROM_SYSTEM_BY_SUFFIX[suffix]
    elif suffix == ".gz":
        system = ROM_SYSTEM_BY_SUFFIX.get(Path(base).suffix.lower())
        if system:
            inner_bases.append(Path(base).stem)
    elif suffix == ".zip":
        systems, inner_bases = _archive_info(path)
        system = next(iter(systems)) if len(systems) == 1 else None
    else:
        system = None

    if not system:
        return None, []

    seeds = [base]
    if len(inner_bases) == 1:
        _add_unique(seeds, inner_bases[0])

    candidates = []
    for seed in seeds:
        forms = []
        _add_unique(forms, seed)

        no_tags = re.sub(r"(?:\s*\[[^\[\]]*\])+\s*$", "", seed).strip()
        _add_unique(forms, no_tags)

        for source in list(forms):
            region_name = source
            for short, full in REGION_ALIASES:
                region_name = region_name.replace(short, full)
            _add_unique(forms, region_name)

        for source in list(forms):
            marker = source.find(" (")
            if marker > 0:
                _add_unique(forms, source[:marker])

        for source in forms:
            _add_unique(candidates, source)
            _add_unique(candidates, source.translate(INVALID_TRANSLATION))

    return system, candidates


def _valid_png_bytes(data):
    if len(data) < 29 or data[:8] != b"\x89PNG\r\n\x1a\n":
        return False, "not a PNG"
    if data[12:16] != b"IHDR":
        return False, "missing IHDR"
    if data[28] != 0:
        return False, "Adam7/interlaced PNG is unsupported"
    return True, ""


def _valid_png_file(path):
    try:
        with path.open("rb") as handle:
            header = handle.read(29)
    except OSError:
        return False
    return _valid_png_bytes(header)[0]


def _thumbnail_url(base_url, system, category, candidate):
    return "%s/%s/%s/%s" % (
        base_url.rstrip("/"),
        quote(SYSTEM_DIRS[system], safe=""),
        quote(category, safe=""),
        quote(candidate + ".png", safe=""),
    )


def _fetch_one(task, base_url, timeout, dry_run):
    rom_key, system, category, destination, candidates = task

    if _valid_png_file(destination):
        return rom_key, category, "existing", str(destination), ""

    network_error = ""
    for candidate in candidates:
        url = _thumbnail_url(base_url, system, category, candidate)
        request = Request(url, headers={"User-Agent": "SNESticleRevive/1.0.7"})
        try:
            with urlopen(request, timeout=timeout) as response:
                data = response.read(MAX_PNG_BYTES + 1)
        except HTTPError as error:
            if error.code == 404:
                continue
            network_error = "HTTP %d" % error.code
            continue
        except URLError as error:
            # file:// fixtures and offline builds also arrive here. Try every
            # flexible candidate before reporting the final failure.
            network_error = str(error.reason)
            continue
        except OSError as error:
            network_error = str(error)
            continue

        if len(data) > MAX_PNG_BYTES:
            network_error = "PNG exceeds 8 MiB"
            continue
        valid, reason = _valid_png_bytes(data)
        if not valid:
            network_error = reason
            continue

        if not dry_run:
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary_name = None
            try:
                with tempfile.NamedTemporaryFile(
                    prefix=".cover-", suffix=".png", dir=str(destination.parent),
                    delete=False
                ) as temporary:
                    temporary.write(data)
                    temporary_name = temporary.name
                os.replace(temporary_name, str(destination))
            finally:
                if temporary_name and os.path.exists(temporary_name):
                    os.unlink(temporary_name)

        return rom_key, category, "downloaded", str(destination), candidate

    if network_error:
        return rom_key, category, "error", str(destination), network_error
    return rom_key, category, "missing", str(destination), ""


def _find_roms(root):
    supported = set(ROM_SYSTEM_BY_SUFFIX)
    supported.update((".zip", ".gz"))
    return sorted(
        path for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in supported
    )


def _write_cover_indexes(rom_root, output_root, roms):
    """Write one compact runtime index beside the ROMs in each directory.

    CDFS can read this small sequential file instead of enumerating all four
    Named_* directories. The emulator still supports manual layouts without an
    index by falling back to its normal directory scan.
    """
    parents = {
        output_root / rom.relative_to(rom_root).parent
        for rom in roms
    }
    index_count = 0
    entry_count = 0

    for parent in sorted(parents, key=lambda item: str(item).casefold()):
        records = []
        locations = [(None, parent)] + [
            (category, parent / category) for category in CATEGORIES
        ]
        for category, directory in locations:
            if not directory.is_dir():
                continue
            for artwork in sorted(
                directory.iterdir(), key=lambda item: item.name.casefold()
            ):
                if not artwork.is_file() or artwork.suffix.lower() != ".png":
                    continue
                name = artwork.name
                encoded_name = name.encode("utf-8")
                if (len(encoded_name) >= 256 or "\t" in name or "\r" in name
                        or "\n" in name or not _valid_png_file(artwork)):
                    continue
                records.append((INDEX_CODES[category], name))

        index_path = parent / INDEX_FILE_NAME
        if not records:
            # Remove only our generated metadata; artwork and ROMs are never
            # touched. This prevents a stale index after all art was removed.
            try:
                index_path.unlink()
            except FileNotFoundError:
                pass
            continue

        payload = INDEX_HEADER + "\n" + "".join(
            "%s\t%s\n" % record for record in records
        )
        parent.mkdir(parents=True, exist_ok=True)
        temporary_name = None
        try:
            with tempfile.NamedTemporaryFile(
                prefix=".covers-index-", suffix=".tmp", dir=str(parent),
                delete=False
            ) as temporary:
                temporary.write(payload.encode("utf-8"))
                temporary_name = temporary.name
            os.replace(temporary_name, str(index_path))
        finally:
            if temporary_name and os.path.exists(temporary_name):
                os.unlink(temporary_name)

        index_count += 1
        entry_count += len(records)

    return index_count, entry_count


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Download Libretro SNES/NES artwork matching ROM filenames"
    )
    parser.add_argument("--roms", required=True, help="ROM directory to scan")
    parser.add_argument(
        "--output", help="destination root (default: write beside the ROMs)"
    )
    parser.add_argument(
        "--system", choices=("auto", "snes", "nes"), default="auto",
        help="system detection mode (default: auto)"
    )
    parser.add_argument("--jobs", type=int, default=6, help="parallel downloads")
    parser.add_argument("--timeout", type=int, default=12, help="seconds per URL")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    rom_root = Path(args.roms).expanduser().resolve()
    output_root = Path(args.output).expanduser().resolve() if args.output else rom_root
    if not rom_root.is_dir():
        parser.error("ROM directory does not exist: %s" % rom_root)
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")

    roms = _find_roms(rom_root)
    tasks_by_destination = {}
    skipped = []
    recognized = 0

    for rom in roms:
        system, candidates = _rom_info(rom, args.system)
        relative_rom = rom.relative_to(rom_root)
        rom_key = str(relative_rom)
        if not system:
            skipped.append(rom_key)
            continue
        recognized += 1
        destination_parent = output_root / relative_rom.parent
        for category in CATEGORIES:
            destination = destination_parent / category / (rom.stem + ".png")
            key = str(destination)
            if key not in tasks_by_destination:
                tasks_by_destination[key] = (
                    rom_key, system, category, destination, tuple(candidates)
                )

    tasks = list(tasks_by_destination.values())
    print(
        "[ COVER ] ROMs: %d found, %d recognized, %d skipped; %d artwork checks"
        % (len(roms), recognized, len(skipped), len(tasks))
    )
    if skipped:
        preview = ", ".join(skipped[:5])
        if len(skipped) > 5:
            preview += ", ..."
        print("[ COVER ] skipped (unknown/ambiguous system): %s" % preview)

    counts = {"downloaded": 0, "existing": 0, "missing": 0, "error": 0}
    roms_with_art = set()

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = [
            executor.submit(
                _fetch_one, task, args.base_url, args.timeout, args.dry_run
            )
            for task in tasks
        ]
        for future in concurrent.futures.as_completed(futures):
            rom_key, category, status, destination, detail = future.result()
            counts[status] += 1
            if status in ("downloaded", "existing"):
                roms_with_art.add(rom_key)
            if status == "downloaded":
                suffix = " (matched %s)" % detail if detail else ""
                print("[ COVER ] + %s%s" % (destination, suffix))
            elif status == "error":
                print("[ COVER ] ! %s/%s: %s" % (rom_key, category, detail))

    if not args.dry_run:
        index_count, index_entries = _write_cover_indexes(
            rom_root, output_root, roms
        )
        print(
            "[ COVER ] index: %d COVERS.IDX file(s), %d artwork entries"
            % (index_count, index_entries)
        )

    verb = "would download" if args.dry_run else "downloaded"
    print(
        "[ COVER ] done: %d ROM(s) with artwork; %s=%d, existing=%d, "
        "missing=%d, errors=%d"
        % (
            len(roms_with_art), verb, counts["downloaded"], counts["existing"],
            counts["missing"], counts["error"]
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
