#!/usr/bin/env bash
# Bancada host-side para deteccao de header/mapeamento de ROM SNES.
set -e
cd "$(dirname "$0")"
ROOT=../..

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/app" \
    -I "$ROOT/src/snes/core" \
    -I "$ROOT/src/snes/rom" \
    -I "$ROOT/src" \
    rom_test.cpp \
    "$ROOT/src/snes/rom/snrom.cpp" \
    "$ROOT/src/app/emurom.cpp" \
    "$ROOT/src/common/base/dataio.cpp" \
    -o rom_test

echo "OK -> ./rom_test"
