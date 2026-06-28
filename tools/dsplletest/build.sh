#!/usr/bin/env bash
# Bancada de teste host-side para o nucleo uPD7725 LLE.
# Compila o codigo REAL (src/snes/core/sndsp1_lle.cpp) num binario de PC.
#
# Uso:  cd tools/dsplletest && ./build.sh && ./dsplle_test <firmware.rom>
set -e
cd "$(dirname "$0")"
ROOT=../..

# sndsp1_lle.cpp inclui apenas "types.h" e "sndsp1_lle.h" (nao puxa o
# snes.h pesado), entao nao precisamos de stub.  Symlink local para o
# .cpp real ser compilado lado a lado com o teste.
ln -sf "$ROOT/src/snes/core/sndsp1_lle.cpp" sndsp1_lle.cpp

g++ -O0 -g -fsigned-char \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/core" \
    dsplle_test.cpp sndsp1_lle.cpp -o dsplle_test

echo "OK -> ./dsplle_test <firmware.rom> [byteHex ...] [-r N]"
