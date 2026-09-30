# SPC700 / S-DSP audit against ares (2026-09-30)

Reference: `ares-emulator/ares` master `4cb8d92b441557cb6bcaf133c4cbc7f6819b1122`.
Revive baseline: main `b9c19d58ca747e59f7a18aa265d9c0dd98af3a34`.
This review concerns the **audio SPC700/S-DSP**, not cartridge DSP-1/2/3/4.

## What was verified

- `src/snes/apu/opspc700_c.h` contains 199 opcodes and
  `src/snes/apu/snspc_c.c` contains the other 57, covering all 256 byte
  values. This is **dispatch coverage**, not proof of instruction accuracy.
- The active SPC700 executor is `SNSPCExecute_C` (set up by the emulator).
  `opspc700_mips.h` is an old opcode table, **not an active MIPS backend**.
  Do not enable it without matching its semantics, IO traps and cycle counts.
- The S-DSP output stage already uses PS2 MMI assembly in `_MixEcho`, but
  the individual voice/echo paths were scalar C/C++.
- The existing PS2 Makefile deliberately compiles `snspcmix.cpp` at `-O1`
  because an `-O2` build broke the 128-bit MMI final mixer; do not
  globally change that optimization level.

## Changes isolated on this branch

1. **16-bit SPC700 fetch wrap:** `_SNSPCFetch8/16` now wrap operands at
   `$ffff -> $0000`, including opcodes placed at the end of ARAM.
   Host tests cover an immediate, a 16-bit JMP operand and a branch.
2. **One envelope multiply per voice output:** `BuildVoiceOutput` already
   creates hardware-even, envelope-adjusted samples for PMON/OUTX; main/echo
   mixing now consumes that buffer instead of multiplying PCM by the
   envelope a second time. This also aligns the mixing inputs with the
   even-output S-DSP latch seen in `ares/sfc/dsp/voice.cpp`. This can
   change low-order output bits compared with the old approximation.
3. **PS2 MIPS stereo voice accumulator:** new standalone
   `snspcmix_voice_ps2.S`, for the EE only; all non-PS2 builds continue
   using scalar C. This is **not** a full MIPS SPC700 interpreter.
   PS2 assembly build success alone does not establish speedup or
   runtime/audio equivalence on real hardware.

## Remaining correctness gaps (not changed here)

| Area | Revive today | ares reference / requirement |
| --- | --- | --- |
| `$F0` TEST | `SNSpcIO::Write8Trap` has no TEST case; SPC RAM is always readable/writable under existing mapping. | `ares/sfc/smp/io.cpp` and `memory.cpp` model RAM writable/disable, timer global gates and internal/external wait states. Requires persistent, savestated IO mode and clock handling, not a one-line register write. |
| SPC700 timers | `snspctimer.cpp` accumulates elapsed time and divides by (tick interval × target). | `ares/sfc/smp/timing.cpp` uses separate stage0/1/2/3 counters and edge gating; current approximation can diverge at target changes and enables. |
| SLEEP vs STOP | Both opcode handlers simply hold the PC and repeatedly consume three cycles. | `ares/sfc/smp/smp.cpp` models separate wait/stop states with different wake behavior. |
| DSP register timing | Register writes are queued; S-DSP output is mixed in blocks, generally after SPC execution for a frame. | ares schedules DSP and SMP clocks together, so reads, KON/KOFF and per-sample writes can become visible at precise times. |
| Echo ARAM / FIR | `snspcmix.cpp` currently calls `FilterEcho(..., FALSE)`: uses a private buffer; not observable in SPC700 RAM. | `ares/sfc/dsp/echo.cpp` reads/writes shared 64 KiB APURAM with 16-bit wrap and FLG gating during DSP clock phases. Enabling the existing shared-memory switch without rescheduling would corrupt timing. |
| BRR edge cases | `snspcbrr.c` approximates invalid range values, and `FetchBlock` reads nine consecutive bytes without masking each 16-bit APURAM byte address. | ares `dsp/brr.cpp` decodes invalid ranges according to DSP behavior and wraps APURAM addresses. Correctness change needs regression fixtures before MMI optimization. |
| Envelope/noise timing | Tables with millisecond approximations and a chunked output mixer. | ares `dsp/envelope.cpp` and `counter.cpp` tick actual DSP rate phases. |

## PS2 performance verification before merging

1. Build the branch and run host `spc700_test`, `dspmix_test`, `spcio_test`
   and the existing broad suite; inspect PS2 link to ensure the new
   `SNSpcMixVoicePS2` symbol resolves.
2. Compare the same ROM, audio options, location and real PS2 hardware on
   main vs this branch. Record FPS, audio glitches and the APU vs DSP
   profile counters separately. A slower ROM is not proof that SPC700 is
   its bottleneck; GS/PPU/SA-1 contention may dominate.
3. For bit-exact comparison, run a deterministic buffer test of the MIPS
   kernel and scalar fallback on actual EE hardware (different volumes,
   negative samples, 0/1/odd block sizes and negative accumulators).
4. Keep the new backend on a branch until those checks pass. The existing
   128-bit `_MixEcho` is left unchanged.

## Sources

- https://github.com/ares-emulator/ares/tree/master/ares/sfc/smp
- https://github.com/ares-emulator/ares/tree/master/ares/sfc/dsp
- https://github.com/ReyFxck/SNESticleRevive/tree/main/src/snes/apu
