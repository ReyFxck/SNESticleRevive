#!/usr/bin/env bash
# Copyright (c) 1997-2004-2022 Icer Addis
# Re-Worked By ReyFxck, Claude Aí, ChatGPT
#
# Description:
#   Builds the pputest host-side regression suite.

# Bancada host-side para regressões pequenas do renderer SNES/PPU.
#
# Uso:  cd tools/pputest && ./build.sh && execute os *_test
set -e
cd "$(dirname "$0")"
ROOT=../..

"${CXX:-g++}" -O2 -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I "$ROOT/src/common/base" -I "$ROOT/src/snes/core" \
    targetprofile_test.cpp -o targetprofile_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/render" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/snes/ppu" \
    -I "$ROOT/src/snes/core" \
    -I "$ROOT/src/snes/cpu" \
    -I "$ROOT/src/snes" \
    -I "$ROOT/src" \
    obj_test.cpp "$ROOT/src/snes/ppu/snppuobj.cpp" -o obj_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/render" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/snes/ppu" \
    -I "$ROOT/src/snes/core" \
    -I "$ROOT/src/snes/cpu" \
    -I "$ROOT/src/snes" \
    -I "$ROOT/src" \
    oam_test.cpp "$ROOT/src/snes/ppu/snppu.cpp" \
    "$ROOT/src/snes/core/snesreg.cpp" \
    "$ROOT/src/snes/core/sndma.cpp" -o oam_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/system" \
    -I "$ROOT/src/common/render" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/app" \
    -I "$ROOT/src/snes/apu" \
    -I "$ROOT/src/snes/ppu" \
    -I "$ROOT/src/snes/core" \
    -I "$ROOT/src/snes/cpu" \
    -I "$ROOT/src/snes" \
    -I "$ROOT/src" \
    io_register_test.cpp "$ROOT/src/snes/core/snio.cpp" \
    "$ROOT/src/snes/core/sndma.cpp" -o io_register_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/ppu" \
    chrcache_test.cpp -o chrcache_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -DSNPPU_BG_CHR_CACHE=0 -DSNPPU_OBJ_CACHE=1 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/ppu" \
    chrcache_test.cpp -o chrcache_obj_only_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/render" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/snes/ppu" \
    -I "$ROOT/src/snes/core" \
    -I "$ROOT/src/snes" \
    -I "$ROOT/src" \
    bglinecache_test.cpp -o bglinecache_test

"${CXX:-g++}" -O1 -fsanitize=undefined -fno-sanitize-recover=undefined \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/core" \
    mask_alignment_test.cpp "$ROOT/src/snes/core/snmask128.cpp" \
    -o mask_alignment_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/render" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/snes/ppu" \
    -I "$ROOT/src/snes/core" \
    -I "$ROOT/src/snes/cpu" \
    -I "$ROOT/src/snes" \
    -I "$ROOT/src" \
    hires_test.cpp -o hires_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/render" \
    audioschedule_test.cpp -o audioschedule_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/ppu" \
    mode7_test.cpp -o mode7_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/core" \
    queue_test.cpp -o queue_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/render" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/snes/apu" \
    -I "$ROOT/src/snes/ppu" \
    -I "$ROOT/src/snes/core" \
    -I "$ROOT/src/snes/cpu" \
    -I "$ROOT/src/snes" \
    -I "$ROOT/src" \
    spcio_test.cpp "$ROOT/src/snes/apu/snspcio.cpp" \
    "$ROOT/src/snes/apu/snspctimer.cpp" -o spcio_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/snes/apu" \
    -I "$ROOT/src/snes/core" \
    spc700_test.cpp "$ROOT/src/snes/apu/snspc_c.c" -o spc700_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/apu" \
    dspmix_test.cpp -o dspmix_test

"${CXX:-g++}" -O2 -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I "$ROOT/src/common/base" -I "$ROOT/src/common/render" \
    -I "$ROOT/src/snes/ppu" -I "$ROOT/src/snes/core" \
    vramstamp_test.cpp -o vramstamp_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I "$ROOT/src/common/base" -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/common/render" -I "$ROOT/src/modules/audio" \
    audio_output_test.cpp "$ROOT/src/common/render/audmixbuffer.cpp" \
    "$ROOT/src/common/render/mixbuffer.cpp" -o audio_output_test

"${CC:-gcc}" -O2 -ffunction-sections -fdata-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I "$ROOT/src/common/base" -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/snes/apu" \
    -c "$ROOT/src/snes/apu/snspcbrr.c" -o mixer_feedback_brr.o

"${CXX:-g++}" -O2 -fno-strict-aliasing -fwrapv \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" -I "$ROOT/src/common/render" \
    -I "$ROOT/src/common/debug" -I "$ROOT/src/snes/apu" \
    -I "$ROOT/src/snes/core" -I "$ROOT/src/snes/cpu" \
    mixer_feedback_test.cpp "$ROOT/src/snes/apu/snspcmix.cpp" \
    "$ROOT/src/snes/apu/snspcdsp.cpp" mixer_feedback_brr.o \
    "$ROOT/src/common/render/mixbuffer.cpp" -o mixer_feedback_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/platform/ps2/system" \
    safe_frameskip_test.cpp -o safe_frameskip_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/platform/ps2/system" \
    regioncadence_test.cpp -o regioncadence_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -DSNDBG_LOG=1 -DSNDBG_DEEP=1 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/snes/core" \
    diag_test.cpp -o diag_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/render" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/snes/ppu" \
    -I "$ROOT/src/snes/core" \
    brightness_test.cpp \
    "$ROOT/src/common/base/pixelformat.cpp" \
    "$ROOT/src/common/render/surface.cpp" \
    "$ROOT/src/common/render/rendersurface.cpp" \
    "$ROOT/src/snes/ppu/snppublend_c.cpp" \
    "$ROOT/src/snes/ppu/snppucolor.cpp" \
    -o brightness_test

echo "OK -> ./obj_test && ./oam_test && ./io_register_test && ./chrcache_test && ./chrcache_obj_only_test && ./bglinecache_test && ./vramstamp_test && ./mask_alignment_test && ./hires_test && ./audioschedule_test && ./audio_output_test && ./mode7_test && ./queue_test && ./spcio_test && ./spc700_test && ./dspmix_test && ./mixer_feedback_test && ./safe_frameskip_test && ./regioncadence_test && ./diag_test && ./brightness_test"
