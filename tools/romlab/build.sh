#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tools/romlab"
CC=${CC:-cc}
CXX=${CXX:-c++}
SANITIZE=${ROMLAB_SANITIZE:-0}

if [[ "$SANITIZE" == 1 ]]; then
    BUILD="$OUT/.build-sanitize"
    TARGET="$OUT/romlab-sanitize"
else
    BUILD="$OUT/.build"
    TARGET="$OUT/romlab"
fi

mkdir -p "$BUILD"

DEFS=(
    -DCODE_PLATFORM=1
    -DCODE_DEBUG=0
    -DCODE_PROFILE=0
    -DSNDBG_LOG=0
    -DSNDBG_DEEP=0
    -DSNESTICLE_ROMLAB=1
    -DSNPPU_OBJ_CACHE=1
    -DSNPPU_BG_CACHE=1
)

INCLUDES=(
    -I"$ROOT/src"
    -I"$ROOT/src/app"
    -I"$ROOT/src/common/base"
    -I"$ROOT/src/common/debug"
    -I"$ROOT/src/common/io"
    -I"$ROOT/src/common/render"
    -I"$ROOT/src/snes"
    -I"$ROOT/src/snes/apu"
    -I"$ROOT/src/snes/core"
    -I"$ROOT/src/snes/cpu"
    -I"$ROOT/src/snes/ppu"
    -I"$ROOT/src/snes/rom"
    -I"$ROOT/src/snes/state"
)

CFLAGS=(-O2 -g -ffunction-sections -fdata-sections -fno-strict-aliasing)
CXXFLAGS=(-O2 -g -std=gnu++17 -ffunction-sections -fdata-sections -fno-strict-aliasing -fwrapv)
LDFLAGS=(-Wl,--gc-sections)
WARNINGS=(-Wall -Wextra -Wformat=2 -Wno-unused-parameter
          -Wno-unused-variable -Wno-unused-function -Wno-sign-compare
          -Wno-missing-field-initializers)
CFLAGS+=("${WARNINGS[@]}")
CXXFLAGS+=("${WARNINGS[@]}")

if [[ "$SANITIZE" == 1 ]]; then
    CFLAGS+=(-O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    CXXFLAGS+=(-O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    LDFLAGS+=(-fsanitize=address,undefined)
fi

C_SOURCES=(
    src/snes/cpu/sncpu.c
    src/snes/cpu/sncpu_c.c
    src/snes/cpu/sndisasm.c
    src/snes/apu/snspcbrr.c
    src/snes/apu/snspc.c
    src/snes/apu/snspc_c.c
    src/snes/apu/snspcdisasm.c
    src/snes/apu/snspcrom.c
)

CPP_SOURCES=(
    src/app/emurom.cpp
    src/app/emusys.cpp
    src/common/base/console.cpp
    src/common/base/dataio.cpp
    src/common/base/pixelformat.cpp
    src/common/render/mixbuffer.cpp
    src/common/render/rendersurface.cpp
    src/common/render/surface.cpp
    src/snes/core/sncx4.cpp
    src/snes/core/sndma.cpp
    src/snes/core/sndsp1.cpp
    src/snes/core/sndsp2.cpp
    src/snes/core/sndsp4.cpp
    src/snes/core/snes.cpp
    src/snes/core/snesreg.cpp
    src/snes/core/sngsu.cpp
    src/snes/core/snio.cpp
    src/snes/core/snmask128.cpp
    src/snes/core/snmemmap.cpp
    src/snes/core/snobc1.cpp
    src/snes/core/snsa1.cpp
    src/snes/core/snsdd1.cpp
    src/snes/core/snsrtc.cpp
    src/snes/ppu/snppu.cpp
    src/snes/ppu/snppubg.cpp
    src/snes/ppu/snppublend_c.cpp
    src/snes/ppu/snppucolor.cpp
    src/snes/ppu/snppuobj.cpp
    src/snes/ppu/snppurender.cpp
    src/snes/ppu/snppurender8.cpp
    src/snes/rom/snrom.cpp
    src/snes/apu/snspcdsp.cpp
    src/snes/apu/snspcio.cpp
    src/snes/apu/snspcmix.cpp
    src/snes/apu/snspctimer.cpp
    src/snes/state/snstate.cpp
    tools/romlab/input_movie.cpp
    tools/romlab/romlab_hooks.cpp
    tools/romlab/state_io.cpp
    tools/romlab/trace.cpp
    tools/romlab/romlab.cpp
)

OBJECTS=()
for source in "${C_SOURCES[@]}"; do
    object="$BUILD/${source//\//_}.o"
    "$CC" "${CFLAGS[@]}" "${DEFS[@]}" "${INCLUDES[@]}" \
        -c "$ROOT/$source" -o "$object"
    OBJECTS+=("$object")
done

for source in "${CPP_SOURCES[@]}"; do
    object="$BUILD/${source//\//_}.o"
    "$CXX" "${CXXFLAGS[@]}" "${DEFS[@]}" "${INCLUDES[@]}" \
        -c "$ROOT/$source" -o "$object"
    OBJECTS+=("$object")
done

"$CXX" "${LDFLAGS[@]}" "${OBJECTS[@]}" -lz -o "$TARGET"
echo "OK -> $TARGET"
