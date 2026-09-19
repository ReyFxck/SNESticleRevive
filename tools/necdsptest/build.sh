#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../..

g++ -O2 -Wall -Wextra -fsigned-char -fpermissive \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/core" \
    necdsp_test.cpp "$ROOT/src/snes/core/sndsp4.cpp" \
    -o necdsp_test

./necdsp_test
