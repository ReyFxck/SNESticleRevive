#!/usr/bin/env bash
# Copyright (c) 1997-2004-2022 Icer Addis
# Re-Worked By ReyFxck, Claude Aí, ChatGPT
#
# Description:
#   Builds the superfxtest host-side regression suite.

# Bancada host-side para o core SuperFX/GSU.
# Compila o codigo REAL (src/snes/core/sngsu.cpp) num binario de PC e roda
# os testes deterministicos do core.
#
# Uso:  cd tools/superfxtest && ./build.sh && ./gsu_test
set -e
cd "$(dirname "$0")"
ROOT=../..

ln -sf "$ROOT/src/snes/core/sngsu.cpp" sngsu.cpp

g++ -O0 -g -fsigned-char -fpermissive \
    -I . \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/core" \
    gsu_test.cpp sngsu.cpp -o gsu_test

echo "OK -> ./gsu_test"
