# SPC700, BRR, CPU branch and VRAM hot paths (2026-11-25)

Incremental patch on top of CPU/DSP round (`e9d50d8` in the user's repository).
Keep original SNES speed, sample rate, display cadence and rendering options.
No fixed internal FPS ceiling. No claimed PS2 FPS increase before measurement.

- SPC700: publish `Cycles` only at observable I/O traps and interpreter exit,
  rather than for every plain RAM access. Word access publishes once before
  both bytes, preserving callback order and any callback clock adjustment.
  Debug/ROMLAB keep eager stores; `SNSPC_DEFER_RAM_CYCLES=0` provides an A/B.
  Mask indexed effective addresses to 16 bits before RAM/ROM shadow/I/O access;
  the previous code could access past 64 KiB for absolute indexed instructions.
  This wrap is an accuracy correction, not just an optimization.
- BRR: consume source bytes first (overlap-safe), fuse nibble decode and filter,
  remove 16-sample intermediate buffer, use predictor-free filter 0 and single
  history tap for filter 1. Preserve existing Revive invalid-range handling,
  saturation, wrap and end/loop bits. This is equivalence to the existing
  decoder, not an independent assertion of hardware accuracy.
- 65C816: skip emulation-mode page crossing penalty math for native taken
  relative branches in the plain R5900 interpreter; leave SA-1 variant alone.
  Generic MIPS64r2 assembly succeeds. Added 144 linked CPU test cases to CI
  for taken/not-taken, forward/backward and same/cross-page branch scenarios.
  Linked PS2 test and target timing have not been executed locally.
- VRAM DMA: in accessible contiguous increment-by-one path, compare aligned
  little-endian 16-byte chunks for the unchanged prefix. Fall back to exact
  word comparison on mismatch, unaligned buffers, tail or wrap. Same first
  changed word, suffix invalidation, odd tail, counters and VMADDR semantics.

Host validation:
- SPC differential (eager vs deferred, both with wrap fix): 18,432 cases,
  3,002 observed I/O callbacks, whole state and trace match. SPC smoke tests
  include three explicit absolute-index wrap checks.
- BRR: 131,072 blocks and the same number of overlapping buffers against a
  separate scalar implementation, every header and signed history extremes.
- Existing six mixer feedback hashes unchanged at 32/44.1/48 kHz.
- PPU hotpath suite including 528 DMA prefix cases passes (alignment, physical
  wrap, every first changed word and identical block). No full OAM/VRAM suite
  claim: previously documented failures remain outside this targeted run.
- Generic MIPS object assembled; no PS2DEV linked ELF/hardware execution here.
- CI runs the new host suite and linked CPU tests, and keeps prior SA-1/GSU tests.

Validate with `bash tools/pputest/test_spc_brr_hotpath.sh`,
`bash tools/pputest/test_hotpath.sh`, `bash tools/pputest/test_mixer_feedback.sh`.
For runtime measurements compare same ROM, scene, input, diagnostic level and
HUD. Use release without HUD in addition to diag2. Test SA-1 and SuperFX ROMs
separately before optimizing their timing or bus arbitration.
