#!/usr/bin/env bash
# Use a valid restored cache; install only the missing pieces.
set -euo pipefail
cd "$(dirname "$0")/../.."

prepare_ps2() {
    : "${PS2DEV:?PS2DEV is required}" "${PS2SDK:?PS2SDK is required}" "${PS2_PACKER_SRC_DIR:?PS2_PACKER_SRC_DIR is required}"
    local compiler
    compiler="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"
    if [[ ! -x "$compiler" ]]; then compiler="$PS2DEV/ee/bin/ee-gcc"; fi
    if [[ -d "$PS2SDK" && -x "$compiler" ]] && "$compiler" --version >/dev/null; then
        echo '[ci] PS2DEV already available; no SDK download or extraction'
    else
        echo '[ci] PS2DEV missing or unusable; preparing toolchain'
        # Force the install target if the cached executable is unusable;
        # ensure-ps2dev only checks whether it exists, not whether it runs.
        make AUTO_INSTALL=yes COLOR=0 install-ps2dev-tar
    fi
    # Validate the required SDK/IRX files even when the cache was restored.
    make AUTO_INSTALL=no COLOR=0 check-env

    local packer="$PS2_PACKER_SRC_DIR/ps2-packer"
    local stub="$PS2_PACKER_SRC_DIR/stub/lzma-1d00-stub"
    local module="$PS2_PACKER_SRC_DIR/lzma-packer.so"
    packer_ready() {
        [[ -x "$packer" && -s "$stub" && -s "$module" ]] && "$packer" -h >/dev/null 2>&1
    }
    if packer_ready; then
        echo '[ci] ps2-packer, LZMA module and stub already available; no rebuild'
    else
        # On hosted Ubuntu zlib is normally already installed. Do not update apt
        # or install a list of packages unless a required header/library is absent.
        if ! printf '#include <zlib.h>\nint main(void){return zlibVersion()==0;}\n' | cc -x c - -lz -o "${RUNNER_TEMP:-/tmp}/snesticle-zlib-check"; then
            sudo apt-get update
            sudo apt-get install -y --no-install-recommends zlib1g-dev
        fi
        if [[ ! -d "$PS2_PACKER_SRC_DIR/.git" ]]; then
            mkdir -p "$(dirname "$PS2_PACKER_SRC_DIR")"
            # Existing Makefile installer handles an incomplete source checkout.
            make AUTO_INSTALL=yes COLOR=0 ensure-local-ps2-packer
        fi
        # The old fix-packer target could leave a host binary without its module
        # or stubs on a cold cache. Build the complete upstream tool when needed.
        if ! packer_ready; then
            make -C "$PS2_PACKER_SRC_DIR" clean
            make -C "$PS2_PACKER_SRC_DIR" all CC=cc BIN2C="$PS2SDK/bin/bin2c" PS2DEV="$PS2DEV" PS2SDK="$PS2SDK"
        fi
    fi
    test -x "$packer"
    test -s "$stub"
    test -s "$module"
    "$packer" -h >/dev/null
}

prepare_python() {
    : "${CI_PYTHON_ENV:?CI_PYTHON_ENV is required}"
    local py="$CI_PYTHON_ENV/bin/python"
    # Import both libraries, including Unicorn's native extension, and verify
    # their actual installed versions against the pinned dependency file.
    if [[ -x "$py" ]] && "$py" - <<'PY'
import importlib.metadata as m
from pathlib import Path
import unicorn, elftools
for line in Path('tools/ci/requirements.txt').read_text().splitlines():
    name, version = line.split('==', 1)
    if m.version(name) != version:
        raise SystemExit(1)
PY
    then
        echo '[ci] CPU-test Python environment valid; skipping pip install'
    else
        echo '[ci] CPU-test Python environment missing or invalid; installing pinned requirements'
        python3 -m venv --clear "$CI_PYTHON_ENV"
        "$py" -m pip install --disable-pip-version-check --quiet -r tools/ci/requirements.txt
    fi
    "$py" -m pip check
}

case "${1:-}" in
    ps2) prepare_ps2 ;;
    python) prepare_python ;;
    *) echo 'usage: prepare-deps.sh ps2|python' >&2; exit 2 ;;
esac
