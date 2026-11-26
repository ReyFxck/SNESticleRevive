# Round 9 — VU0 Mode 7 shadow experiment

Incremental on Round 8. This is a diagnostic experiment, not a production speedup.

## What changes
- `SNES_VU0_SHADOW=1` is accepted only with `SNES_DIAGNOSTICS=2`; defaults to zero and participates in the fast build signature.
- CI adds `SNESticle-diag-vu0` alongside release/diag1/diag2. It is not a replacement for diag2.
- COP2 macro-mode VF1..VF6 generates groups of four X/Y coordinates, using aligned memory. The candidate uses the unchanged VRAM address helpers. The eligibility guard bounds the endpoints to +/-2^23 and steps to +/-32768; other lines bypass the experiment.
- The existing integer Mode 7 renderer ALWAYS provides the displayed bytes. All 256 candidate raw bytes (including EXTBG priority bits) are compared before the original priority/transparency stages. Any mismatch latches off the candidate until process restart. No changes to OBJ/OAM, HDMA, GS packets or audio. Scratch VF registers are used only in the renderer thread; actual target interrupt/context behavior still needs verification.
- VU1 is not enabled; there is no new VIF/GIF/DMA traffic.

## Testing
`bash tools/pputest/test_vu0_shadow.sh` passed 67,504 line/mode comparisons including injected faults, 8,124 rejected vectors; all coordinates of accepted vectors independently checked. Existing Mode7/OBJ/OAM binaries and freshly compiled hotpath suite passed (including 480 HDMA/CGRAM and 4,096 OAM cases). The portable model uses host floating-point and does NOT execute COP2. No PS2 toolchain available locally: CI must assemble the target and the device must execute it.

## Target experiment
Run the regular diag2 first, then diag-vu0, same scene/settings. Shadow does extra work so do not use its total FPS as a speedup measurement. Log `[snes-vu0]` reports cumulative calls, checked, bypass, mismatch, disabled, ref-cyc and vu-cyc. Compare ref-cyc/checked with vu-cyc/checked; candidate cost includes coordinate generation and scalar VRAM fetch, excludes compare and eligibility. A mismatch line identifies scanline/pixel and disables the path. No logs in non-Mode7 scenes is expected. If target hangs or errors, revert to ordinary diag2; shadow comparison cannot guard illegal instructions, context corruption or hangs. Never enable as production until target equivalence and meaningful timing gains demonstrated. Original sprite behavior is unchanged, not certified against all games.

## VU1 follow-up (not implemented)
Existing GIF DMAC uses PATH3; VU1 XGKICK uses PATH1. A later separate experiment needs explicit GIF path arbitration and completion fences, buffer ownership, palette-change boundaries and shadow packet comparison. Do not assume VIF completion alone guarantees all GIF work is complete. First benchmark a synthetic packet-preparation workload, then compare generated packets byte-for-byte without submitting to GS; only after that test actual submission with a strict fence and fallback. Avoid sharing mutable emulated PPU state or changing sprite order. Mode1 race portions are not accelerated by this Mode7-only experiment.

References: https://psi-rockin.github.io/ps2tek/ ; https://www.ps2dev.com/files/vu-instruction-manual.pdf ; https://github.com/ps2dev/ps2sdk/blob/master/ee/math3d/src/math3d.c ; https://ps2dev.github.io/ps2sdk/group__packet2__types.html
