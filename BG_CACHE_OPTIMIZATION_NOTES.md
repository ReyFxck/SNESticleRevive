# BG line-cache optimization test build

This branch is derived from the user-provided `SNESticleRevive-bgcache-test.zip`.

Changes:

- Replaces per-map-word 16-word stamp validation with at most two 32-word tilemap-row checks.
- Uses conservative 32-word CHR dependency stamps (2 x 4bpp tiles / 4 x 2bpp tiles) and skips consecutive duplicate chunks.
- Adds `SNES_BG_CACHE_MODE1` and `SNES_BG_CACHE_MODE5` Makefile switches for clean A/B tests.
- Adds `[snes-bg-line-cost] avg validate/restore/store=...` to diagnostic logs.

Recommended A/B builds:

```sh
make clean && make SNES_DIAGNOSTICS=2 SNES_TARGET_PROFILE=0 SNES_BG_CACHE_MODE1=1 SNES_BG_CACHE_MODE5=1
make clean && make SNES_DIAGNOSTICS=2 SNES_TARGET_PROFILE=0 SNES_BG_CACHE_MODE1=0 SNES_BG_CACHE_MODE5=1
```

For actual play-speed testing use `SNES_DIAGNOSTICS=0`; deep diagnostics add measurable EE work.

## faststamp-v3 row memo

This package adds a conservative dependency-generation row memo on top of v2.

- Fine X/Fine Y do not change the set of physical VRAM chunks used by a decoded tile row.
- While the VRAM epoch is unchanged, the same BG/tile-row dependency result is reused instead of re-walking all 33 tiles on each of the eight fine-Y scanlines.
- Any VRAM write increments the epoch, so the memo is invalidated before it could hide a changed tilemap or CHR chunk.
- The Mode 1/5 CHR dependency loop also hoists invariant work and removes the generic cross-chunk check that cannot occur for aligned 2bpp/4bpp BG tiles.

A/B diagnostic builds:

```sh
make clean && make SNES_DIAGNOSTICS=2 SNES_TARGET_PROFILE=0 SNES_BG_CACHE_MODE1=1 SNES_BG_CACHE_MODE5=1 SNES_BG_CACHE_ROWMEMO=1
make clean && make SNES_DIAGNOSTICS=2 SNES_TARGET_PROFILE=0 SNES_BG_CACHE_MODE1=1 SNES_BG_CACHE_MODE5=1 SNES_BG_CACHE_ROWMEMO=0
```

The key metric is `[snes-bg-line-cost] validate`: it should fall substantially while `hit/miss` and rendered output remain stable. For actual play-speed, repeat with `SNES_DIAGNOSTICS=0`.
