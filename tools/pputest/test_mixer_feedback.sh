#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
flags=(-O2 -fno-strict-aliasing -fwrapv -ffunction-sections -fdata-sections
       -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0
       -I src/common/base -I src/common/render -I src/common/debug
       -I src/snes/apu -I src/snes/core -I src/snes/cpu)
${CC:-cc} "${flags[@]}" -c src/snes/apu/snspcbrr.c -o "$work/brr.o"
${CXX:-c++} "${flags[@]}" -Wl,--gc-sections tools/pputest/mixer_feedback_test.cpp \
    src/snes/apu/snspcmix.cpp src/snes/apu/snspcdsp.cpp "$work/brr.o" \
    src/common/render/mixbuffer.cpp -o "$work/test"
"$work/test"
