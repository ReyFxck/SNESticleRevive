#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
ROOT=../..

"${CXX:-g++}" -O2 -Wall -Wextra \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/core" \
    -I "$ROOT/src" \
    sa1_test.cpp \
    "$ROOT/src/snes/core/snsa1.cpp" \
    -o sa1_test

echo "OK -> ./sa1_test"
