#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CXX:-g++} -O2 -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
    -I src/common/base -I src/snes/ppu tools/pputest/vu0_shadow_test.cpp -o "$work/test"
"$work/test"
