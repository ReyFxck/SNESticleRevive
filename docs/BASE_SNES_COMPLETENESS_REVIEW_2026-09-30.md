# Base SNES completeness review — 2026-09-30

Baseline: main after PR #93 (`e84db14115b6296be7eb20b07d5200ff0632551c`)
Work branch: `fix/base-snes-completeness-pass`
Draft PR: #95

This review covers the ordinary SNES base console. Cartridge enhancement chips
(DSP-1/2/3/4, SA-1, GSU/SuperFX, CX4, S-DD1, S-RTC, OBC1, etc.) remain a
separate compatibility pass.

## Closed in PR #95

### SPC700 / S-SMP

- All 256 SPC700 opcodes remain decoded.
- 16-bit instruction fetches wrap at `$ffff -> $0000`.
- `$F0 TEST` now models RAM writable/disabled and global timer gates.
- Timer 0/1/2 use stage0 -> stage1 edge -> stage2 target -> 4-bit stage3.
- `SLEEP` and `STOP` persist instead of re-fetching the halt opcode.
- TEST/CONTROL/timer state is preserved through the existing SPC state layout.
- APURAM reads/writes from the interpreter go through TEST RAM gating.

### S-DSP

- DSPADDR bit 7 remains read-only for writes.
- BRR invalid ranges 13-15 follow hardware behavior.
- BRR blocks and sample-directory words wrap across 64 KiB APURAM.
- Envelope uses the native 11-bit representation and 30720-step rate counter.
- The hidden envelope candidate is kept separate from committed ENVX.
- Noise uses the hardware LFSR/rate counter, including the voice-0 phase order.
- Main/echo/PMON consume the same hardware-even voice-output latch.
- KON has a five-sample setup delay; BRR is prepared during the setup window.
- Queued DSP writes split mixer chunks at the next native sample boundary.
- Echo reads/FIR/writes shared APURAM with 16-bit wrapping.
- FLG.5 is echo-memory write-disable, not a global echo-disable switch.
- FLG.6 mutes the DAC without freezing voice/envelope/noise/echo state.
- FLG.7 soft reset silences/releases voices while the DSP pipeline continues.
- PS2 voice accumulation remains in an isolated R5900 assembly kernel.

### Save states

A versioned extension is appended to the old `SnesStateT` prefix. New states
serialize:

- full and silent DSP counters;
- noise LFSR/phase;
- echo FIR history, address/page/length/write-disable latch;
- silent-mixer voice state;
- pending DSP register-write queue;
- pending APUIO queue and CPU-port publication state.

The PS2 state loader and ROM Lab accept both the legacy prefix size and the new
extended size, for raw and deflated states. Legacy states restore the new
transient state from conservative reset defaults.

## Base PPU / CPU status

The current main already includes the broad register/timing pass:

- PPU register decode, retained/open buses and status side effects;
- OAM/VRAM/CGRAM live access restrictions;
- Mode 4/5/6, pseudo-hires and logical interlace;
- OBJ interlace;
- overscan and NTSC/PAL field cadence;
- MDMA/HDMA timing and bus restrictions;
- 5A22 multiply/divide, NMI/IRQ and auto-joy timing.

PR #93 corrected the HDMA scheduler so HBlank begins at 1096 master clocks and
HDMA starts at 1104 (H=276), fixing the DKC background regression without
reverting the PPU/DMA accuracy work.

## Remaining base-console limitations

These are deliberately not hidden behind a "100%" claim.

1. **S-SMP TEST wait-state bits 4-7.**
   RAM/timer controls are modeled, but the programmable internal/external
   clock dividers are not yet charged per bus access. These modes are obscure
   and can deliberately slow or deadlock real hardware; implementing them
   exactly requires per-access cycle accounting in the SPC700 interpreter.

2. **S-DSP 32-subphase timing.**
   DSP register writes are now applied at native 32 kHz sample boundaries,
   rather than being delayed by cache-size blocks. The implementation is not
   a full 32-phase-per-sample DSP scheduler, so 1-2 DSP-clock latch quirks for
   KON/KOFF/ENDX/ENVX/OUTX remain approximated.

3. **KON BRR byte pipeline.**
   The five-sample KON delay is represented. Revive's BRR decoder still
   decodes a complete 16-sample block at once instead of one BRR byte across
   the DSP phases; output is held during setup, but internal subphase state is
   not serialized byte-for-byte like ares.

4. **CPU-driven mid-scanline visual changes.**
   Memory data ports use live H/V timing and HDMA visual writes are ordered per
   scanline. Other PPU control/scroll/window/color-math writes made by the CPU
   in the middle of a visible scanline are still rendered at scanline
   granularity. True dot/segment rendering would be a larger renderer change.

5. **Native 448/478-line PS2 presentation.**
   Logical Mode 5/6/interlace behavior exists, but the current PS2 carrier is
   256 pixels high. True double-height presentation is a GS/output task, not a
   base game-logic blocker.

## Merge gate

Before merging PR #95:

1. host regression suite must be green;
2. PS2 build/link must be green;
3. test at least one ordinary audio-heavy game on real PS2;
4. check for distorted/slow audio, missing channels, broken echo, or a state
   load that resumes with a different soundtrack;
5. keep PR #95 draft until real-hardware audio smoke testing is done.
