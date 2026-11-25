# PPU performance round — VRAM, CHR and BG composition

Baseline: CI-ready HostFS/HUD commit `4fe52e2` plus the separate font optimization. This patch is incremental, not a replacement source archive. No HostFS/BGM/HUD changes. No changes to CPU/APU timing, frame skipping or VBlank.

## Changes

- Immediate VRAM byte and pair writes invalidate derived graphics only when a byte actually changes. Linear DMA scans only the equal prefix, then uses the existing copy loop and conservatively invalidates the suffix; an entirely identical DMA avoids renderer invalidation. VMADDR, read latch and diagnostic write counts are preserved. Deferred/active-display rules are unchanged.
- The 8x8 tilemap decoder walks contiguous spans of each 32-word screen row instead of testing horizontal wrap after every tile. 16x16 and offset-per-tile map paths are unchanged.
- Normal-resolution 2bpp/4bpp CHR decoding keeps the physical row/flip identity, replaces a flip table load with arithmetic and retains final row data/priority in scalar locals. Palette, opacity and cache invalidation behavior remain unchanged.
- BG mask operations fuse `(A & ~B) | C` and avoid writing/reloading an intermediate 32-byte mask. The EE uses existing MMI instructions with memory clobbers and explicit stores. A separate fine-X=0 qword-aligned compositor avoids the extra source load and QFSRV; the scrolling path remains unchanged.
- BG cache mask stores copy exactly 33 meaningful bytes and zero-pad to 40 without reading the source padding. No increase in cache or scratchpad size.
- Diagnostic `[snes-vram-write] skipped-identical-bytes=... identical-dma-blocks=...` counts bytes skipped in immediate accesses and identical DMA prefixes, and fully identical even-byte DMA blocks. Deferred writes are not included. Counters reset with the existing diagnostic window. Release has no additional counters.

## Validation

`bash tools/pputest/test_hotpath.sh`: actual C++ CHR/8x8 tilemap functions against independent pixel/descriptor references, with cache/repeated-row optimizations on and off; fused-mask logic including aliases; raw mask padding and guards; VRAM block equivalence in 12 VMAIN modes, 6 start addresses and varied lengths; repeat DMA, wrap, odd length, pair and individual port tests.

Per configuration: 16,384 CHR scanlines, 24,576 map cases, 65,536 fused-mask inputs (four alias configurations), 4,096 mask stores. Existing host `chrcache_test`, `bglinecache_test`, `vramstamp_test` also passed locally. Diagnostic `snppu.cpp` compiled on host. The full legacy `oam_test` had 16 failures on the baseline, including an active-display double-increment case; 15 remain after correcting the VRAM block-equivalence fixture to enable forced blank. CI runs only the focused VRAM hot-path tests in this round. The unrelated failures are not fixed or hidden by changes to emulator timing.

PS2DEV and R5900 execution are not available in the local test environment; the new MMI assembly paths require target compilation and real hardware/NetherSX2 testing. No FPS gain has been measured. The patch does not assert that host equivalence proves complete emulation accuracy.

## A/B protocol

Keep the ROM CRC, display mode, HUD setting, Android renderer, EE speed and game sequence constant. Test release HUD off and diag2 HUD Full separately, comparing the same workload. Record `[ps2-cadence]`, `[snes-frame]`, `[snes-hot-cyc]`, `[snes-bg-line-cost]`, `[snes-vram-write]`, CHR/OBJ cache hits and invalidations, and video/GS consistency. Verify backgrounds, sprites, scroll, priority and transparency in Mode 1; also exercise Mode 5/6 and Mode 7. If a regression appears, revert this isolated commit rather than disabling cache validity or forcing frame skipping.
