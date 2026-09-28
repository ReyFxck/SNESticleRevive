#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tools/romlab"
CC=${CC:-cc}
CXX=${CXX:-c++}
SANITIZE=${ROMLAB_SANITIZE:-0}
DIAGNOSTICS=${ROMLAB_DIAGNOSTICS:-0}
BG_CACHE=${ROMLAB_BG_CACHE:-1}
BG_CHR_CACHE=${ROMLAB_BG_CHR_CACHE:-1}
OBJ_CACHE=${ROMLAB_OBJ_CACHE:-1}
BG_CACHE_WAYS=${ROMLAB_BG_CACHE_WAYS:-2}
BUILD_TAG=${ROMLAB_BUILD_TAG:-}
TRACE_HANDLED_IO=${ROMLAB_TRACE_HANDLED_IO:-0}

if [[ "$BG_CACHE" != 0 && "$BG_CACHE" != 1 ]]; then
    echo "ROMLAB_BG_CACHE must be 0 or 1" >&2
    exit 2
fi
if [[ "$DIAGNOSTICS" != 0 && "$DIAGNOSTICS" != 1 && "$DIAGNOSTICS" != 2 ]]; then
    echo "ROMLAB_DIAGNOSTICS must be 0, 1 or 2" >&2
    exit 2
fi
if [[ "$BG_CHR_CACHE" != 0 && "$BG_CHR_CACHE" != 1 ]]; then
    echo "ROMLAB_BG_CHR_CACHE must be 0 or 1" >&2
    exit 2
fi
if [[ "$OBJ_CACHE" != 0 && "$OBJ_CACHE" != 1 ]]; then
    echo "ROMLAB_OBJ_CACHE must be 0 or 1" >&2
    exit 2
fi
if [[ "$BG_CACHE_WAYS" != 1 && "$BG_CACHE_WAYS" != 2 ]]; then
    echo "ROMLAB_BG_CACHE_WAYS must be 1 or 2" >&2
    exit 2
fi
if [[ "$TRACE_HANDLED_IO" != 0 && "$TRACE_HANDLED_IO" != 1 ]]; then
    echo "ROMLAB_TRACE_HANDLED_IO must be 0 or 1" >&2
    exit 2
fi
if [[ -n "$BUILD_TAG" && ! "$BUILD_TAG" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "ROMLAB_BUILD_TAG contains unsupported characters" >&2
    exit 2
fi

if [[ "$SANITIZE" == 1 ]]; then
    BUILD="$OUT/.build-sanitize${BUILD_TAG:+-$BUILD_TAG}"
    TARGET="$OUT/romlab-sanitize${BUILD_TAG:+-$BUILD_TAG}"
else
    BUILD="$OUT/.build${BUILD_TAG:+-$BUILD_TAG}"
    TARGET="$OUT/romlab${BUILD_TAG:+-$BUILD_TAG}"
fi

mkdir -p "$BUILD"

DEFS=(
    -DCODE_PLATFORM=1
    -DCODE_DEBUG=0
    -DCODE_PROFILE=0
    -DSNDBG_LOG="$([[ "$DIAGNOSTICS" == 0 ]] && echo 0 || echo 1)"
    -DSNDBG_DEEP="$([[ "$DIAGNOSTICS" == 2 ]] && echo 1 || echo 0)"
    -DSNESTICLE_ROMLAB=1
    -DSNPPU_OBJ_CACHE="$OBJ_CACHE"
    -DSNPPU_BG_CHR_CACHE="$BG_CHR_CACHE"
    -DSNPPU_BG_CACHE="$BG_CACHE"
    -DSNPPU_BG_LINE_CACHE_WAYS="$BG_CACHE_WAYS"
    -DROMLAB_TRACE_HANDLED_IO="$TRACE_HANDLED_IO"
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
