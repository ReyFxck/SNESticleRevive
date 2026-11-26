# Round 12 — targeted accuracy and hot paths

Base: Round 11 Ultra (includes Round 10). The cumulative ZIP carries Round 10, Round 11 and the changes below; it can be applied on top of the unchanged Round 9, 10 or 11 source. It is not a full repository. Preserve local changes before extraction.

## Implemented

- Mode 7 vertical mosaic: shared PPU vertical counter is advanced at the scanline renderer boundary; reset at the first visible line and reloaded on all-disabled to enabled transitions. BG1's mosaic enable controls vertical sampling for both Mode 7 layers; horizontal mosaic remains per layer. Sprites use their original physical line. The queue continues to apply writes in original order. Mid-dot effects cannot be accurately reproduced by the existing line-based renderer. Old save state format preserved; phase is reconstructed at the next frame boundary, not serialized. Other modes retain the previous mosaic implementation.
- Mode 7 Direct Color, PS2 normal-resolution non-clipped/no-color-math path: decode RGB from BG1's 8-bit texel, keep index zero as CGRAM backdrop and keep BG2/OBJ in CGRAM. The OBJ compositor reports only actual accepted draws (including edge clipping), not candidates. BG1 is identified by its composed layer rank. Final 256 BGR555 pixels are duplicated into the existing 512-dot GS texture using the established finalized-line transfer. The original indexed main line remains intact for internal CGRAM bus approximation. No global palette substitution. Modes 3/4, pseudo-hires, clipping, and color math combinations are not implemented by this change. Native GS output still requires visual validation.
- MMI: simplify the existing native Mode 7 64-bit opacity loop to branchless pceqb + high-bit gather. No new 128-bit kernel or native speed claim. Host tests validate the equivalent byte mask; native MMI compilation and timing are unverified.
- Mode 1/general BG composition: when the final resolved opacity mask is all ones, copy the entire fine-X-shifted indexed line, after updating priorities and color math. Mixed masks follow existing path. No cache-key change.
- HDMA modes 2/6 fixed-address scroll and Mode 7 matrix ports: select bank and source offset once, avoid generic port dispatch, preserve separate A-bus reads, source increment with 16-bit wrap, queue ordering, per-byte clocks and slow fallback. B->A and memory ports unchanged. No scheduler redesign.
- GS/GIF: enumerate only nonempty CLUT rows in two 32-bit halves. Same ascending row order and same per-row contiguous group geometry, packet and wait policy. No new GIF PATH1 traffic or asynchronous staging ownership changes.
- Round 11 DLog correction for VU0 retained, experimental renderer still shadow only. VU1 is not implemented: native toolchain and validation are necessary before introducing concurrent GIF PATH1/PATH3 traffic.

## Verification

- `tools/pputest/test_hotpath.sh`: all three cache configurations passed; each: 12,288 direct-color samples (all 256 values, 16 brightness levels and 3 source types), 65,536 sparse CLUT row masks, 16 full-opacity/fine-X cases, 4,096 Mode 7 BG1/EXTBG lines and 16,384 actual OBJ draws including clipped edges and drawn-pixel masks. Existing CHR/map/mask/cache tests passed.
- `tools/pputest/test_hardware_accuracy.sh`: 10,752 static vertical mosaic samples + 12 queued mid-frame enable samples, 200 scroll/matrix HDMA cases, 480 CGRAM HDMA cases, 4,096 OAM bulk cases and existing VRAM, I/O, SPC and OBC1 tests passed.
- `tools/pputest/test_vu0_shadow.sh`: 67,504 portable model comparisons and fault injection passed (not COP2 hardware).
- `tools/pputest/build.sh` and all 22 generated test executables: passed.
- `tools/romlab/build.sh`: passed.
- No PS2DEV toolchain or PS2/NetherSX2 runtime available. No FPS measurement, visual ROM validation, actual VU0/VU1/MMI hardware test, or proof of zero regressions.

## References

- https://sneslab.net/wiki/Mosaic
- https://github.com/ares-emulator/ares/blob/master/ares/sfc/ppu/mosaic.cpp
- https://github.com/ares-emulator/ares/blob/master/ares/sfc/ppu/mode7.cpp
- https://github.com/ares-emulator/ares/blob/master/ares/sfc/ppu/io.cpp
- https://snes.nesdev.org/wiki/Direct_color
- https://snes.nesdev.org/wiki/PPU_registers

## Next validation

Compile native CI, compare release and diag2 Top Gear/F-Zero/Super Mario Kart at identical input progression. Test ActRaiser 2 world map for Direct Color and sprites; compare mosaic-enabled scenes to a reference. Keep VU0 diagnostic-only. Do not interpret increased guest capacity as measured real FPS. Check [ps2-audio-rpc], [ps2-audio], [snes-gs-deep], [snes-frame], [ps2-cadence], [snes-vu0]. Remaining: full Direct Color across modes and color math, mosaic mid-dot accuracy, more advanced Mode 1/CPU optimization, scheduler rewrite, native VU1 and deeper GIF DMA changes.
