#!/usr/bin/env bash
# Run an identical synthetic workload on two source trees for a host-only A/B.
set -euo pipefail
cd "$(dirname "$0")"
bash build.sh
ROOT=../..
"${CXX:-c++}" -O2 -fwrapv -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 \
  -I"$ROOT/src/common/base" -I"$ROOT/src/snes/core" -I"$ROOT/src/snes/cpu" -I"$ROOT/src" \
  sa1_bench.cpp sncpu.o sncpu_c.o snsa1.o -o "${TMPDIR:-/tmp}/snes-sa1-bench-$$"
trap 'rm -f "${TMPDIR:-/tmp}/snes-sa1-bench-$$"' EXIT
for i in 1 2 3; do "${TMPDIR:-/tmp}/snes-sa1-bench-$$" "${1:-200}" "${2:-idle}"; done
