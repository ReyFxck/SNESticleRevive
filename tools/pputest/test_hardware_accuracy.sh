#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
flags=(-O2 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0
       -I src/common/base -I src/common/render -I src/common/debug -I src/snes/apu -I src/snes/ppu -I src/snes/core -I src/snes/cpu -I src/snes -I src)
${CXX:-g++} "${flags[@]}" tools/pputest/oam_test.cpp src/snes/ppu/snppu.cpp src/snes/core/snesreg.cpp src/snes/core/sndma.cpp -o "$work/oam"
"$work/oam"
${CXX:-g++} "${flags[@]}" tools/pputest/spc_timer_accuracy_test.cpp src/snes/apu/snspctimer.cpp src/snes/apu/snspc.c -o "$work/timer"
"$work/timer"
