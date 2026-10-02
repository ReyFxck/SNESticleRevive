#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
flags=(-std=c99 -O2 -Wall -Wextra -Werror -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0)
if [[ ${SANITIZE:-0} == 1 ]]; then
    flags+=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all)
fi
"${CC:-gcc}" "${flags[@]}" -Iinclude -I../../src/common/base -I../../src/modules/audio \
    -I../../src/snes/core -I../../src/third_party/rfauds2/include \
    adapter_test.c ../../src/modules/audio/audio_rfauds2.c -o adapter_test
./adapter_test
