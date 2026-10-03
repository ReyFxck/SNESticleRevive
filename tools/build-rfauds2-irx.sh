#!/usr/bin/env bash
# Rebuild the pinned, in-tree IOP driver. PS2SDK source rules are required.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${PS2SDK:?Set PS2SDK to the installed PS2SDK}"
: "${PS2SDKSRC:?Set PS2SDKSRC to a PS2SDK source checkout with iop/Rules.make}"
make -C src/third_party/rfauds2/src/iop
cp src/third_party/rfauds2/src/iop/irx/rfauds2.irx irx/rfauds2.irx
sha256sum irx/rfauds2.irx
