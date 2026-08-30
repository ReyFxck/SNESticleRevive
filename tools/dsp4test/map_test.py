#!/usr/bin/env python3
# Copyright (c) 1997-2004-2022 Icer Addis
# Re-Worked By ReyFxck, Claude Aí, ChatGPT
#
# Description:
#   Exercises map test behavior in the dsp4test regression suite.

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_PATH = ROOT / "src/snes/core/snmemmap.cpp"
TABLE_NAME = "_SnesMemMap_LoRom_DSP4"
ENTRY_RE = re.compile(
    r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,"
    r"\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,"
    r"\s*SNCPU_CYCLE_FAST\s*,\s*SNESMEM_TYPE_DSP1\s*\}"
)

def fail(message):
    print("DSP-4 map: FAIL: " + message, file=sys.stderr)
    raise SystemExit(1)

def expected(bank, address):
    bank_selected = 0x30 <= bank <= 0x3F or 0xB0 <= bank <= 0xBF
    return bank_selected and address >= 0x8000

source = SOURCE_PATH.read_text(encoding="utf-8")
marker = "static SnesMemMapT " + TABLE_NAME + "[]={"
start = source.find(marker)
if start < 0:
    fail("tabela dedicada ausente")

end = source.find("\n};", start)
if end < 0:
    fail("fim da tabela nao encontrado")

entries = [tuple(int(value, 16) for value in match.groups())
           for match in ENTRY_RE.finditer(source[start:end])]
if len(entries) != 8:
    fail("esperava 8 paginas de 8 KiB, encontrou %d" % len(entries))

for start_bank, end_bank, start_address, end_address in entries:
    if start_address & 0x1FFF or end_address - start_address + 1 != 0x2000:
        fail("entrada nao representa uma pagina alinhada de 8 KiB")
    if start_bank > end_bank:
        fail("intervalo de bancos invertido")

for bank in range(0x100):
    for address in range(0, 0x10000, 0x2000):
        mapped = sum(
            start_bank <= bank <= end_bank
            and start_address <= address
            and address + 0x1FFF <= end_address
            for start_bank, end_bank, start_address, end_address in entries
        )
        if mapped != int(expected(bank, address)):
            fail("decodificacao incorreta em $%02X:%04X" % (bank, address))

dsp4_branch = re.search(
    r"if\s*\(uFlags\s*&\s*SNROM_FLAG_DSP4\)\s*\{(?P<body>.*?)\n\s*\}",
    source,
    re.DOTALL,
)
if not dsp4_branch:
    fail("selecao SNROM_FLAG_DSP4 ausente")

body = dsp4_branch.group("body")
if "MapMem(" + TABLE_NAME + ");" not in body:
    fail("SNROM_FLAG_DSP4 nao seleciona a tabela dedicada")
if "MapMem(_SnesMemMap_LoRom_DSP1);" in body:
    fail("SNROM_FLAG_DSP4 ainda reutiliza o mapa DSP-1")

print("DSP-4 map: PASS")
