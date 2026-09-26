# Trials of Mana SPC700 Investigation - Closed

This document records the closed investigation of the **Trials of Mana / Seiken Densetsu 3** gameplay-start hang observed on the PS2 build.

The goal is to preserve what was tested, what was disproven, which experiments regressed behavior, and which SPC700 fixes were considered safe enough to merge.

The investigation is intentionally closed without a game-specific workaround.

## Test target

ROM used throughout the investigation:

- Title: `SeikenDensetsu3`
- Collection release: `Trials of Mana (World) (Rev 1) (Collection of Mana)`
- Mapper: ExHiROM
- Size: 6,291,456 bytes
- CRC32: `173E6097`

Runtime path:

- PS2 frontend
- SPC700 portable C executor: `SNSPCExecute_C`
- Legacy MIPS SPC executor was not used for these tests

## Failure signature

The reproducible failure occurs when gameplay is expected to continue after the transition sequence.

Observed SNES CPU behavior:

- CPU loops around `E8:C26F-E8:C27B`
- the loop polls APUIO `$2142`

Observed SPC behavior:

- SPC continues executing; it is not halted
- final SPC-to-CPU ports repeatedly settle at a pattern equivalent to:
  - `00/02/0F/7F`
  - or `00/03/0F/7F`, depending on the run/input state
- CPU-to-SPC still changes in some runs, so the SPC itself is not globally frozen

The final `0F/7F` values are real SPC driver state, not stale APUIO garbage:

- `02F0: FA 54 F6` publishes direct-page `$54` to output port `$F6`
- `02F3: FA 5C F7` publishes direct-page `$5C` to output port `$F7`

The problematic state observed in the driver is typically:

- `$54 = 0F`
- `$55 = F0`
- `$5C = 7F`

The SPC repeatedly republishes those values while the SNES CPU waits.

## Confirmed SPC700 core work

The useful, broadly applicable SPC700 fixes were completed before the Trials-specific timing experiments began.

The safe integration point is:

- `5f2a99415d2030a99391642a60cad02f5a3eba24`

That point includes the verified core work for:

- missing SPC700 opcodes
- arithmetic and flag behavior
- direct-page word wrapping
- reset stack pointer behavior
- 16-bit wrapping
- SLEEP handling
- write-only I/O read behavior
- Mesen-parity host regression tests
- DIV overflow branch compilation fix

Those changes were merged into `main` through PR #77.

Main merge commit:

- `25c6bc3f94c2ee30c43f39185d4d42d84808fb35`

Everything described below was deliberately excluded from that merge.

## What was tested and rejected

### Opcode FA operand order

Hypothesis:

- `MOV dp,dp` opcode `FA` might have source/destination reversed.

Result:

- rejected

The implementation was checked against Mesen behavior.

For `FA src,dst`:

- first direct-page byte is the source
- second direct-page byte is the destination

The observed Trials code therefore correctly means:

- `FA 54 F6` = `$54 -> $F6`
- `FA 5C F7` = `$5C -> $F7`

Do not swap these operands.

### Save-state corruption as the root cause

Hypothesis:

- a save-state restore was creating the final `0F/7F` APUIO state.

Result:

- rejected as the root cause

A save-state can restore the already-bad APUIO state directly, which explains why some logs appeared to jump into `0F/7F` without normal writes.

However, cold/clean runs also produced the same state through normal SPC execution.

A compatibility issue was found in experimental timer save-state semantics and a conversion patch was written, but later testing showed:

- `legacy-timers=0` for the relevant state
- the Trials hang still reproduced

Therefore save-state compatibility was not the root cause of this game hang.

### Frame counter anchoring experiment

Commit:

- `de7655907a0fbccc18ac2613e4d56347ac85f42e`

Experiment:

- anchor CPU/SPC frame counters directly to live cycle remainders

Result:

- regressed synchronization

The SPC accumulated a large timing deficit relative to the CPU.

This experiment was reverted by:

- `babe52153f78a66b7b6f58a32145cc1e65cbd26b`

Do not reintroduce the `de76559` frame-counter behavior.

### APUIO delayed-latch / pending-write experiments

Several APUIO timing experiments attempted to more closely model delayed CPU-to-SPC visibility and latch behavior.

These included commits around:

- `ca73c67`
- `4a6a814`
- `f340eaa`
- later rollback commits

Result:

- some variants regressed much earlier in the game
- the game could stall before character selection
- one recurring failure was CPU polling `$2143` while SPC output port 3 remained `7F`

Conclusion:

- those experimental APUIO changes were not suitable for integration
- the stable core behavior was restored

### TEST / global timer gate experiments

Experiments attempted to model S-SMP TEST global timer gating separately from timer enable state.

Relevant commits include:

- `bf65e1d`
- `8a594e4`

Concern found during review:

- some versions implemented the global gate through normal timer enable calls
- that could reset internal timer state when hardware should only gate counting

Result:

- not proven to fix Trials
- excluded from main

### Free-running timer phase experiment

Commit:

- `fcb74cc97ad73c2a4ef1e96972e5d980ef02999c`

Goal:

- preserve free-running timer phase and model stage counters more closely

Result:

- did not solve the Trials hang

Timer traces later showed timer0 continuing to fire with a plausible period and valid enable state even while the final hang persisted.

### Timer0 stopped / wrong target hypothesis

Hypothesis:

- timer0 stopped firing or had an invalid target near the hang.

Result:

- rejected

The timer remained enabled and continued returning non-zero events.

Observed diagnostic state included:

- enabled timer0
- global timer gate enabled
- valid timer enable mask
- target values behaving consistently with the driver

The final `F00F` state continued to be recomputed even after timer0 fired.

### Missing update to $54/$55

Hypothesis:

- a branch stopped the routine before it could update `$54/$55`.

Result:

- rejected

Longer traces showed the routine continuing through:

- `0264: BA 1E`
- `0266: DA 54`

The routine did not skip the write.

Instead, it recomputed and stored the same `F00F` value again.

### $5C should wrap from 7F to 80

Hypothesis:

- the failure was caused because `$5C` stopped at `7F` instead of wrapping to `80`.

Result:

- rejected

Tracing showed `$5C` behaving as part of a bounded driver fade/counter path.

Forcing `7F -> 80` would be a game-specific behavioral hack and was not justified.

### ASL / carry behavior

Hypothesis:

- incorrect ASL carry/N/Z behavior caused the `FF -> FE -> FC -> F8 -> F0` sequence.

Result:

- rejected

The SPC700 ASL implementation and flags were compared with Mesen and matched the expected semantics.

### DIV / MUL / ADDW / SUBW path

Hypothesis:

- a 16-bit arithmetic opcode used by the driver was producing the wrong value.

Result:

- no concrete mismatch found

DIV behavior in the traced path matched the expected result for the observed inputs.

No justified opcode fix was identified from these traces.

### ENDX / KON ordering

Hypothesis:

- a queued KON was incorrectly erasing a pending ENDX before the SPC could observe it.

Diagnostics added markers such as:

- `[dsp-endx-latch]`
- `[dsp-endx-read]`
- `[dsp-endx-cleared-by-kon]`

The marker did appear many times.

Result after reference comparison:

- the original interpretation was incorrect

Mesen's DSP pipeline explicitly clears the corresponding ENDX bit when a new KON begins for that voice.

Therefore seeing KON clear ENDX is not, by itself, a DSP bug.

No ENDX workaround was merged.

## What the final traces established

The important sequence is:

1. the driver can be in a healthy state with `F6=FF`
2. the driver computes `$55:$54 = F00F`
3. `02F0: FA 54 F6` changes output F6 from `FF` to `0F`
4. at that moment F7 can still be far below `7F` (for example around `48/49`)
5. later `$5C` reaches `7F`
6. `02F3: FA 5C F7` publishes `7F`
7. the SNES CPU eventually enters the persistent APUIO wait

This means the final `0F/7F` pair is produced by normal execution of the game's SPC driver under the current emulator behavior.

The investigation did not isolate a single incorrect SPC opcode, APUIO event, timer edge, DSP event, or scheduler rule that could be safely changed without risking regressions.

## Mode 7 note

Mode 7 was present around some of the transition scenes, but no evidence established Mode 7 as the cause of the SPC/APUIO deadlock.

The audio handshake state can evolve independently of the PPU mode.

Do not treat this as a Mode 7 bug without new evidence.

## Diagnostic commits kept only for reference

The feature branch contains many useful temporary tracing commits, including traces for:

- APUIO reads/writes
- output-latch changes
- CPU-to-SPC command traffic
- timer reads
- `$54/$55/$5C` state
- final handshake execution
- timer0 handler execution
- fade-path execution
- DSP ENDX/KON events

These diagnostics were intentionally not merged into main.

The branch may be kept as historical evidence if the investigation is resumed later.

## Closed status

Status: **closed / unresolved root cause**

Reason for closing:

- the hang is reproducible
- the final bad handshake state is well characterized
- multiple plausible causes were tested and rejected
- no remaining hypothesis was strong enough to justify another behavioral change
- continuing by forcing values, adding per-game conditions, or changing timing without a reference would risk introducing regressions

If this issue is reopened, the next investigation should start from a new external correctness oracle or a more cycle-accurate DSP/SPC comparison, not by repeating the experiments listed above.

## Integration rule

Main must remain on the verified SPC700 core work only.

Do not merge the later Trials-specific experimental commits unless a future test proves a generic hardware-correct behavior change across multiple games.
