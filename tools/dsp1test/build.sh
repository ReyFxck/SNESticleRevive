#!/usr/bin/env bash
# Bancada de teste host-side para o DSP-1 HLE.
# Compila o codigo REAL (src/snes/core/sndsp1.cpp) num binario de PC
# e roda casos de teste (helpers + round-trip Project<->Target).
#
# Uso:  cd tools/dsp1test && ./build.sh && ./dsp1_test
set -e
cd "$(dirname "$0")"
ROOT=../..

# sndsp1.cpp faz #include "snes.h"; o include com aspas resolve no
# diretorio do proprio .cpp primeiro.  Um symlink local faz o compilador
# achar o stub vazio (snes.h) em vez do snes.h pesado do emulador.
ln -sf "$ROOT/src/snes/core/sndsp1.cpp" sndsp1.cpp

g++ -O0 -g -fsigned-char \
    -include cstdlib -include cmath \
    -I . \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/core" \
    dsp1_test.cpp sndsp1.cpp -o dsp1_test

echo "OK -> ./dsp1_test"
