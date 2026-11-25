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
${CC:-cc} -O2 -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -I src/common/base -I src/common/debug -I src/snes/apu -c src/snes/apu/snspcbrr.c -o "$work/brr.o"
${CC:-cc} -O2 -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -I src/common/base -I src/snes/apu -c src/snes/apu/snspcrom.c -o "$work/rom.o"
${CC:-cc} -O2 -ffunction-sections -fdata-sections -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -I src/common/base -I src/common/debug -I src/snes/apu -c src/snes/apu/snspc.c -o "$work/spc.o"
${CXX:-g++} "${flags[@]}" -fwrapv -fno-strict-aliasing tools/pputest/apu_memory_accuracy_test.cpp src/snes/apu/snspcdsp.cpp src/snes/apu/snspcmix.cpp src/snes/apu/snspcio.cpp src/snes/apu/snspctimer.cpp "$work/spc.o" "$work/brr.o" "$work/rom.o" src/common/render/mixbuffer.cpp -o "$work/apumem"
"$work/apumem"
${CXX:-g++} "${flags[@]}" -I src/common/system -I src/app tools/pputest/io_register_test.cpp src/snes/core/snio.cpp src/snes/core/sndma.cpp -o "$work/io"
"$work/io"

${CXX:-g++} "${flags[@]}" -I src/platform/ps2/system tools/pputest/late_diag_test.cpp -o "$work/late"
"$work/late"

${CXX:-g++} "${flags[@]}" tools/pputest/obc1_test.cpp src/snes/core/snobc1.cpp -o "$work/obc1"
"$work/obc1"
