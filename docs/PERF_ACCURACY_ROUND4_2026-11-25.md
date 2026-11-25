# Performance and accuracy round 4 — 2026-11-25

Incremental against the local reconstruction after hardware-accuracy2. No round-3 dependency. The Round 1 SMW diagnostic suggests focusing on BG cache hits, not enlarging the caches. Baseline is diagnostic-mode guest EE cycles; not a measurement of physical PS2 or Android wall-clock throughput.

## Changes

- BG line cache: exact existing fetch key plus validated VRAM epoch avoids a repeated dependency walk when no VRAM change occurred. A changed epoch uses the old dependency validator; wrap clears entries. Existing entry size/alignment checks remain (768/416 bytes). New `[snes-bg-line-epoch] hits=` counts shortcut use, not time saved.
- Cache-hit masks now apply the same post-shift horizontal mosaic as misses, including native-hires paired masks. Pixel carriers are already mosaicked. Existing blank/priority classification stays conservative. MMI combines two exclusion masks and uses aligned zero-shift mask copies; portable fallback tested. Target instructions not executed locally.
- Public CPU memory access wraps 24-bit addresses before selecting a bank (including bulk reads). This does not change interpreter addressing-mode semantics. ALU read phase correctly crosses the cycle counter rollover and does not mistakenly run billions of cycles after a same-cycle write. No interrupt timing changes.
- SPC timer short path skips a division when no prescaler edge occurs. TEST $F0 bits 3/0 gate timer pulses, independent of CONTROL; PSW.P protects TEST. Gate transition model derives the high/low level from the prescaler half-period. This is not subcycle-verified hardware behavior. RAM read/write protection and TEST wait-state/clock selection remain unimplemented. Reuses a padding byte in the legacy timer state; use fresh boots for comparisons.
- DSP FLG.5 disables echo writes, not FIR readback or address advancement (while the mixer is not muted). Removed unused unsafe direct-pointer ARAM option. Private echo buffer remains; sample scheduling, physical ARAM/ESA wrapping, FIR saturation and mute behavior still need dedicated work. Extra readback work when write disabled may cost cycles.
- Diagnostic-only `[ps2-vblank-irq]` samples the existing nonblocking IRQ counter before/after GS flip. It changes no pacing, queue policy or wait. Counter can be unavailable or reset by a backend; zeros or invalid intervals must not be equated with physical missed VBlanks. Log analyzer supports both new counters.

## References

- SNESdev wiki: https://snes.nesdev.org/wiki/CPU_registers
- Fullsnes: https://patrickjohnston.org/ASM/ROM%20data/snestek.htm — SPC TEST, DSP FLG and timer stages.
- SnesLab: https://sneslab.net/wiki/Main_Page — CPU, PPU and sound hardware reference.

## Host validation

- PPU hotpath: default, CHR cache/repeat off, unpacked one-way BG key. Each: 16,384 CHR scanlines, 24,576 map cases, 65,536 randomized cases for each fused mask operation including aliases, 4,096 padded stores, 16,384 mosaic-mask cases and 64 epoch checks. Mask checks exercise the actual portable implementations, not EE MMI.
- Hardware suite: complete OAM/VRAM suite, 528 wide DMA cases, 312,521 timer/memory assertions, 1,181,738 physical APU/TEST assertions, exhaustive 65,536 8-bit multiplies and ALU rollover, 16,777,216 DMA address checks.
- CPU host built-in checks: open bus, public 24-bit wrap, IRQ/NMI semantics, interruptible MVN. No external instruction vectors in this invocation.
- SPC/BRR: 18,432 differential cases with 3,002 I/O events, 131,072 BRR blocks plus 131,072 overlapping blocks.
- Mixer: echo-write-disable readback PASS and all six previous PCM/state hashes unchanged.
- CI now invokes host CPU built-in checks and expanded existing suites.

## Remaining work

No local PS2DEV build or PS2/NetherSX2 runtime measurements. Test with fresh boot and same recorded input, compare release vs diag2; inspect epoch hit rate, BG validate/restore cost, frame cadence and VBlank IRQ availability. VU0/VU1 offload is not implemented: evaluate transfer/synchronization costs before changing the renderer. SA-1/GSU timing and missing coprocessors remain separate tasks. No accuracy hacks, frameskip or game speed changes.
