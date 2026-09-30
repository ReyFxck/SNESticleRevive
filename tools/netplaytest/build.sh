#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../..
"${CC:-gcc}" -std=c99 -O2 -Wall -Wextra -DCODE_PLATFORM=1 -I"$ROOT/src/common/base" -I"$ROOT/src/modules/netplay" -I"$ROOT/src/modules/netplay/protocol" -c "$ROOT/src/modules/netplay/protocol/netqueue.c" -o netqueue.o
"${CXX:-g++}" -std=c++11 -O2 -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=undefined -DCODE_PLATFORM=1 -I"$ROOT/src/common/base" -I"$ROOT/src/app" -I"$ROOT/src/modules/netplay" -I"$ROOT/src/modules/netplay/protocol" netplay_test.cpp netqueue.o -o netplay_test
echo "OK -> ./netplay_test"
