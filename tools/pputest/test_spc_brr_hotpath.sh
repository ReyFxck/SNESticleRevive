#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
flags=(-O2 -fwrapv -fno-strict-aliasing -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0
       -I src/common/base -I src/common/debug -I src/snes/apu -I src/snes/core)
${CXX:-c++} "${flags[@]}" -DSNSPC_DEFER_RAM_CYCLES=0 -DSNSPCExecute_C=SNSPCExecute_Reference \
    -c src/snes/apu/snspc_c.c -o "$work/reference.o"
${CC:-cc} "${flags[@]}" -c src/snes/apu/snspcbrr.c -o "$work/brr.o"
${CXX:-c++} "${flags[@]}" tools/pputest/spc_brr_hotpath_test.cpp src/snes/apu/snspc_c.c \
    "$work/reference.o" "$work/brr.o" -o "$work/test"
"$work/test"
${CXX:-c++} "${flags[@]}" tools/pputest/spc700_test.cpp src/snes/apu/snspc_c.c -o "$work/spc700_test"
"$work/spc700_test"
