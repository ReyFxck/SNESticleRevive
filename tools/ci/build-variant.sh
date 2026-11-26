#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
case "${1:-}" in
    release) diagnostics=0; target_profile=1; filename=SNESticle.elf ;;
    diag1) diagnostics=1; target_profile=0; filename=SNESticle-diag1.elf ;;
    diag2) diagnostics=2; target_profile=0; filename=SNESticle-diag2.elf ;;
    *) echo 'usage: build-variant.sh release|diag1|diag2' >&2; exit 2 ;;
esac
: "${PS2_PACKER_SRC_DIR:?PS2_PACKER_SRC_DIR is required}"
: "${CI_PYTHON_ENV:?CI_PYTHON_ENV is required}"
# Never reuse objects compiled with a different diagnostics/profile setting.
# The toolchain, packer, and Python environment live outside build/.
make AUTO_INSTALL=no COLOR=0 clean
# Preserve the project's supported parallel compilation model. Limit the jobs
# to the available runner CPUs and four workers.
jobs=$(nproc)
if (( jobs > 4 )); then jobs=4; fi
make -j"$jobs" --output-sync=target AUTO_INSTALL=no COLOR=0 JOBS="$jobs" strip \
    SNES_DIAGNOSTICS="$diagnostics" SNES_VU0_SHADOW=0 PS2_BOOT_RECOVERY=0 \
    SNES_TARGET_PROFILE="$target_profile" PROFILE=0 PACK=0

if [[ "$diagnostics" == 0 ]]; then
    # Run symbol-dependent CPU/echo/voice checks on the linked ELF, but do not
    # upload it. The only delivered release is stripped and packed.
    "$CI_PYTHON_ENV/bin/python" tools/cputest/check_ps2_cpu.py build/SNESticle.elf
    "$CI_PYTHON_ENV/bin/python" tools/cputest/check_ps2_echo.py build/SNESticle.elf
    "$CI_PYTHON_ENV/bin/python" tools/cputest/check_ps2_voice.py build/SNESticle.elf
fi

mkdir -p artifacts
input="$PWD/build/SNESticle.stripped.elf"
output="$PWD/artifacts/$filename"
# Run in the packer's directory, so its cached LZMA module and stub are found.
(cd "$PS2_PACKER_SRC_DIR" && ./ps2-packer "$input" "$output")
test -s "$output"
file "$output"
ls -lh "$output"
