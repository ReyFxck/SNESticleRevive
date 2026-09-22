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

echo "OK -> ./sa1_test"
