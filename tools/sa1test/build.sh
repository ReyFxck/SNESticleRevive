#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../..
CC="${CC:-cc}"
CXX="${CXX:-c++}"
CFLAGS="-O2 -ffunction-sections -fdata-sections -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -I$ROOT/src/common/base -I$ROOT/src/snes/core -I$ROOT/src/snes/cpu -I$ROOT/src"

"$CC" $CFLAGS -c "$ROOT/src/snes/cpu/sncpu.c" -o sncpu.o
"$CC" $CFLAGS -c "$ROOT/src/snes/cpu/sncpu_c.c" -o sncpu_c.o
"$CXX" $CFLAGS -c "$ROOT/src/snes/core/snsa1.cpp" -o snsa1.o
"$CXX" $CFLAGS -c sa1_test.cpp -o sa1_test.o
"$CXX" -Wl,--gc-sections sa1_test.o snsa1.o sncpu.o sncpu_c.o -o sa1_test

# Compile the full SNES state serializer too. This is intentionally compile-only:
# it catches layout/API integration errors without pulling the whole emulator into
# the host-side SA-1 unit-test executable.
"$CXX" $CFLAGS \
    -I "$ROOT/src/app" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/common/io" \
    -I "$ROOT/src/common/render" \
    -I "$ROOT/src/snes/apu" \
    -I "$ROOT/src/snes/ppu" \
    -I "$ROOT/src/snes/rom" \
    -I "$ROOT/src/snes/state" \
    -c "$ROOT/src/snes/state/snstate.cpp" -o snstate_compile.o

echo "OK -> ./sa1_test (snstate.cpp compile checked)"
