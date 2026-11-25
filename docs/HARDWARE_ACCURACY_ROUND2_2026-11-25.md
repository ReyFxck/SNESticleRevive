# Hardware accuracy round 2 — 2026-11-25

Incremental patch on top of HARDWARE_ACCURACY_ROUND_2026-11-25. Focus: physical APU RAM addressing, BRR invalid ranges and DMA A-bus restrictions. No ROM-specific overrides.

## Changes

- DSP sample directory and BRR block fetches wrap at 16 bits and see physical ARAM rather than the SPC700 IPL ROM overlay. Contiguous blocks retain the direct-pointer path. Address zero is a valid block address, not an end-of-stream sentinel. A former padding byte records this case without enlarging the serialized channel structure. Start fresh rather than relying on old snapshots for accuracy testing.
- Initialize the modeled FLG register to E0 on DSP reset (deterministic internal reset state; raw power-on register readback is not guaranteed by hardware).
- SPC control-port input clear operations no longer overwrite underlying ARAM.
- BRR ranges 13–15 use sign-only expansion (-2048 or 0 before prediction). Other predictor behavior is unchanged.
- DMA A-bus restrictions also cover system-bank 4000–41FF and 4200–421F. HDMA table reads, indirect address reads and transfer reads/writes use the existing restricted A-bus helpers. WRAM-port conflict handling is shared with the existing MDMA semantics. This is not a subcycle bus timing implementation.

## References

- https://sneslab.net/wiki/S-DSP/BRR
- https://snes.nesdev.org/wiki/S-DSP_registers
- https://snes.nesdev.org/wiki/DMA_registers
- https://patrickjohnston.org/ASM/ROM%20data/snestek.htm

## Host tests executed

- test_hardware_accuracy.sh: OAM/VRAM pass; 312,517 SPC timer/memory assertions; 1,181,729 APU physical-memory assertions; all 16,777,216 A-bus addresses checked against independent range classification; I/O register suite pass.
- test_spc_brr_hotpath.sh: 18,432 SPC differential cases, 3,002 I/O events, 131,072 BRR blocks and 131,072 overlapping blocks pass. Invalid BRR range reference updated to documented sign-only behavior, not a claim that all other BRR behavior matches hardware.
- test_mixer_feedback.sh: six previous PCM/state hashes preserved at 32/44.1/48 kHz.
- A-bus exhaustive test verifies classification, not full DMA/HDMA bus timing or all transfer sequences.

## Remaining limitations

Not compiled with PS2DEV or tested on PS2/NetherSX2 here. No performance measurement for this patch. SPC TEST register semantics, shared-ARAM echo and DSP synchronization, exact 5A22 IRQ/ALU and DMA timing, SA-1 arbitration, GSU timing, native interlace and missing cartridge chips remain separate tasks. Existing special-chip fast paths are not fully audited by this patch.
