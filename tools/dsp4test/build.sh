#!/usr/bin/env bash
# Copyright (c) 1997-2004-2022 Icer Addis
# Re-Worked By ReyFxck, Claude Aí, ChatGPT
#
# Description:
#   Builds the dsp4test host-side regression suite.

# Bancada host-side para o DSP-4 HLE.
# Compila o codigo REAL (src/snes/core/sndsp4.cpp + dsp4emu.cpp, o HLE do
# ZSNES portado) num binario de PC e roda os testes de protocolo/funcionais.
#
# Uso:  cd tools/dsp4test && ./build.sh && ./dsp4_test
set -e
cd "$(dirname "$0")"
ROOT=../..

# symlinks locais dos fontes reais (mantem os includes "" resolvendo certo)
ln -sf "$ROOT/src/snes/core/sndsp4.cpp" sndsp4.cpp
ln -sf "$ROOT/src/snes/core/dsp4emu.cpp" dsp4emu.cpp

CXXFLAGS=(-O0 -g -fsigned-char -fpermissive
    -I . -I "$ROOT/src/common/base" -I "$ROOT/src/snes/core")

PYTHONDONTWRITEBYTECODE=1 python3 map_test.py

g++ "${CXXFLAGS[@]}" dsp4_test.cpp sndsp4.cpp dsp4emu.cpp -o dsp4_test
echo "OK -> ./dsp4_test"

# runner de vetores (TDD com vetores capturados do DSP-4 de referencia)
g++ "${CXXFLAGS[@]}" dsp4_vectors.cpp sndsp4.cpp dsp4emu.cpp -o dsp4_vectors
echo "OK -> ./dsp4_vectors   (roda ./vectors/*.vec)"
