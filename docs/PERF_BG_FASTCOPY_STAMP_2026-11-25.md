# BG fast-copy / fast-stamp — iteration 1

Base archive: `b0adaa354ced11a0a8ed4e5d3337ddbfb8ad140a` (SNESticleRevive-main.zip).

## Changes

- `SNES_BG_CACHE_FAST_COPY=1`: EE-only aligned 264-byte BG cache copy (16 quadwords and one 64-bit tail). Unaligned addresses and host builds use the exact original `memcpy`. Both primary cached pixel planes are aligned by repurposing eight padding bytes, preserving the 768-byte primary and 416-byte secondary entry sizes. Compile-time offset checks are included. No change to mask data or composition.
- `SNES_BG_CACHE_FAST_STAMP=1`: for aligned CHR base addresses and one physical tile per cell, calculate the same 32-word chunk directly. The old walk remains for unaligned bases and dual physical tiles in native hires.
- Include the entire BGMODE register in the dependency memo configuration key, avoiding reuse across tile-size changes with identical world-state bits. No ROM-specific rules, timing changes or frameskip changes.
- Both switches propagate through C/C++ flags and build configuration tracking. The existing diagnostic line now appends `fast-copy/stamp=1/1` so the new artifact is identifiable.
- Update obsolete host vramstamp API calls and test copy boundaries, config changes, and reference chunk arithmetic.

## Validation

Host pputest suite builds: 21/22 executables pass. `oam_test` has exactly the same 16 VRAM test failures in the original archive and this patch. Host renderer syntax checked with packed/unpacked keys and switches on/off. VRAM-stamp tests passed with fast-stamp on and off. The host pixel-copy test exercises the portable fallback, not R5900 instructions. No PS2DEV cross-compiler or PS2 runtime was available locally; GitHub Actions and real testing remain required. No performance increase has been measured yet.

## A/B

Use the existing CI artifacts (`SNESticle-diag2` and `SNESticle-release`). On the same ROM, settings and input sequence, compare the first 433 complete 30-frame windows only if the same sequence is reproduced. Baseline emulog: 12,990 frames, mean 3,893,923 core cycles/frame, 76 frames above 105% budget; mean BG validation/restore/store 87,838/93,934/21,655 cycles/frame. These are guest diagnostic counts, not wall-clock FPS.

The build flags can be disabled independently:

```sh
make clean
make SNES_DIAGNOSTICS=2 SNES_TARGET_PROFILE=0 SNES_BG_CACHE_FAST_COPY=0 SNES_BG_CACHE_FAST_STAMP=0
```

This control retains the BGMODE key fix and cache alignment; compare with the original commit for a fully original control. Check visual output (including scrolling and hires), audio, frame cost and hashes. For performance on physical hardware, use the release build and measure frame pacing separately.
