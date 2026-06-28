#!/usr/bin/env bash
# Bancada host-side para o DSP-4 HLE.
# Compila o codigo REAL (src/snes/core/sndsp4.cpp) num binario de PC e
# roda os testes de protocolo.
#
# Uso:  cd tools/dsp4test && ./build.sh && ./dsp4_test
set -e
cd "$(dirname "$0")"
ROOT=../..

# symlink local do .cpp real (mantem os includes "" resolvendo certo)
ln -sf "$ROOT/src/snes/core/sndsp4.cpp" sndsp4.cpp

g++ -O0 -g -fsigned-char -fpermissive \
    -include cstdlib -include cstdio \
    -I . \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/core" \
    dsp4_test.cpp sndsp4.cpp -o dsp4_test

echo "OK -> ./dsp4_test"
