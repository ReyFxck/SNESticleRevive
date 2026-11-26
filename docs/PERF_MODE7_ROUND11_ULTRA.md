# Round 11 Ultra — Mode 7 correctness + performance

Base: Round 9 applied. The distributable also includes all Round 10 audio changes; applying it on top of an unchanged Round 10 is safe. Review local edits before overwriting. This is an incremental source patch, not a replacement repository or a native ELF.

## Findings and changes

- The previous Mode 7 compositor unconditionally removed BG2 from TM/TS even with SETINI.6 enabled, and stripped the high color bit from BG1. BG1 is always an 8-bit indexed source; EXTBG BG2 uses the lower seven bits and bit 7 as priority. Only BG2 is stripped now. BG2 has independent enable bits, windows and color math.
- Normal Mode 7 BG1 must cover OBJ0 but not OBJ1. The previous layer mask encoded rank 2, blocking OBJ1. It now encodes rank 1. EXTBG BG2 low is rank 0; BG1 rank 1; BG2 high rank 2. BG2 high overrides BG1; BG1 replaces low BG2. Zero index is transparent, including raw 0x80 for BG2 (but not BG1). No priority changes to other modes.
- Removed the shared OBJ fetch's early main-screen window culling: this incorrectly hid sub-screen objects and removed objects before the hardware tile fetch limit. Windows still clip in actual per-screen OBJ composition; this can increase processing on heavily clipped scenes, and is a correctness fix, not a guaranteed performance improvement.
- Horizontal mosaic is applied to each Mode 7 layer independently before opacity/priority extraction. Vertical mosaic timing is still pending; do not interpret this as complete mosaic hardware support.
- Degenerate affine lines reuse a single texel. Clamp/black classify line bounds once: entirely inside can use the existing repeat path; entirely outside one edge can skip tilemap access (tile zero) or fill transparent pixels. Mixed/unknown ranges retain scalar behavior. Guarded signed arithmetic preserves the coordinate contract. No cross-line VRAM caching or stale palette reuse.
- Plain Mode 7, when only BG1 is being composed and no clipping or competing BG is present, copies the indexed line directly and preserves opacity, priority and math masks. The existing PS2 MMI opacity path is unchanged. Portable opacity uses an exact byte mask helper.
- Fixed VU0 shadow log transport from printf to DLog. VU0 shadow remains diagnostic-only and never submits its experimental pixels to the GS.
- Round 10 audio transport included unchanged; audio frequency, BRR, interpolation and firmware not modified in Round 11.

## Validation performed

- All 22 host regression executables from tools/pputest/build.sh passed before the final equivalent fill helper/test-only updates. The final hot-path script re-ran all three cache configurations and the Mode 7 tests.
- 4096 randomized Mode 7 composition scanlines per cache configuration, covering BG1/BG2, separate windows, horizontal mosaic, opacity, sprite rank, math selection; 4 actual OBJ draw priority checks per line. 3 configurations: 12288 composition lines and 49152 sprite draws.
- 65536 randomized nonzero masks, existing byte gather, existing 1000000 affine transform vectors, 10000 fetch vectors x 3 modes and boundary cases, all passed.
- 67504 VU0 shadow portable comparisons plus injected faults; 8124 ineligible lines (not real COP2 execution).
- Hardware accuracy portable tests passed, including 480 HDMA/CGRAM and 4096 OAM equivalence cases; no native PS2 toolchain present here.
- CI hotpath script now runs Mode 7 reference tests and the actual composition/OBJ tests automatically.

## References reviewed

- https://snes.nesdev.org/wiki/Mode_7_transform
- https://snes.nesdev.org/wiki/PPU_registers
- https://wiki.superfamicom.org/backgrounds (Mode 7 / EXTBG priority and direct color)
- https://wiki.superfamicom.org/registers (BGMODE / SETINI)
- https://wiki.superfamicom.org/rendering-the-screen (Mosaic and Color Math)

## Limitations / test request

No native PS2 build, device FPS, visual ROM comparison or GS hardware equivalence was measured here. No native VU1, new native MMI kernels, vertical mosaic anchor, direct-color rendering, full scheduler/HDMA rewrite or general Mode 1 optimization is included. Existing DMA/HDMA/GS optimizations from earlier rounds remain. Direct color needs a separate path that preserves palette-indexed sprites, and should not be approximated via a global palette replacement.

Use release and diag2 for speed, diag-vu0 for shadow validation. Test Top Gear intro and race, F-Zero, Super Mario Kart, Pilotwings, Contra III overhead stages and Trials of Mana world map (EXTBG and transparency). Compare to a known-good reference renderer visually. Use identical settings and input progression. Look for [snes-vu0], [ps2-audio], [ps2-audio-rpc], [snes-frame], [ps2-cadence], [snes-gs-deep] and sprite visibility. Do not enable VU0 in release until the native results are both correct and faster.
