#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for flags in '' '-DSNPPU_BG_CHR_CACHE=0 -DSNPPU_BG_CHR_REPEAT_FINAL=0' '-DSNPPU_BG_CACHE_PACKED_KEY=0 -DSNPPU_BG_LINE_CACHE_WAYS=1'; do
    ${CXX:-g++} -O2 -fno-strict-aliasing -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 $flags \
        -I src/common/base -I src/common/render -I src/common/debug -I src/common/system \
        -I src/snes/ppu -I src/snes/core -I src/snes/cpu -I src/snes -I src \
        tools/pputest/ppu_hotpath_test.cpp src/snes/ppu/snppuobj.cpp -o "$work/test"
    "$work/test"
done
${CXX:-g++} -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I src/common/base -I src/common/render -I src/common/debug \
    -I src/snes/ppu -I src/snes/core -I src/snes/cpu -I src/snes -I src \
    tools/pputest/oam_test.cpp src/snes/ppu/snppu.cpp src/snes/core/snesreg.cpp \
    src/snes/core/sndma.cpp -o "$work/vram-test"
"$work/vram-test" --vram-hotpath

${CXX:-g++} -O2 -fno-strict-aliasing -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I src/common/base -I src/snes/ppu tools/pputest/mode7_test.cpp -o "$work/mode7-test"
"$work/mode7-test"

# Exact dependency walk versus region-aware memo, including counter wrap.
${CXX:-g++} -O2 -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I src/common/base -I src/common/render -I src/snes/ppu -I src/snes/core tools/pputest/vramstamp_test.cpp -o "$work/vramstamp-test"
"$work/vramstamp-test"
