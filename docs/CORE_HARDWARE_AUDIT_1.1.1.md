# SNESticle Revive 1.1.1 - Core / PS2 Hardware Audit

This branch is for broad emulator correctness and PS2-specific optimization.
It must not contain game-specific fixes.

## Goals

1. Complete SNES address/register behavior before optimizing around bugs.
2. Audit CPU/APU/PPU/DMA/HDMA/cart mappings and open-bus/mirror behavior.
3. Use the PS2 EE efficiently: MIPS assembly where it wins, MMI for packed
   integer work, scratchpad/DMA for hot staging.
4. Evaluate VU0/VU1 only for sufficiently large, regular workloads where
   transfer/synchronization cost is lower than the saved EE work.
5. Validate changes with cross-game/component tests instead of a single ROM.

## Current architecture findings

### 65816 / EE MIPS

The PS2 frontend already selects `SNCPUExecute_ASM` from
`src/snes/cpu/sn65816.S`. The portable C core remains useful as a
correctness oracle. Work here is therefore an opcode/timing/addressing audit,
not a rewrite merely for the sake of assembly.

Priority:
- compare ASM state against the C core for every opcode/addressing mode;
- verify WAI/STP/interrupt edges and 24-bit bus wrap;
- move only measured C-side helpers on hot paths into MIPS/MMI.

### SPC700 / APU

The PS2 frontend currently selects `SNSPCExecute_C`. This is a major target
because the hot SPC interpreter is still C on the EE.

The C core also documents correctness debt:
- half-carry behavior is disabled/incomplete;
- 16-bit direct-page accesses do not wrap correctly;
- the default opcode path is retained as an unimplemented fallback.

There is an `opspc700_mips.h` instruction macro source in the tree, but no
active PS2 SPC ASM execution core is wired into the frontend. The plan is to
first make the C behavior complete/testable, then introduce an EE MIPS engine
with state-differential tests against C.

### PPU / MMI / GS

The code already uses EE MMI in mask operations and parts of color/blend/render
work. The next optimization work must be profiler-driven and preserve PPU
semantics.

Candidates:
- CHR unpack/plane merge;
- mask composition;
- OBJ row preparation;
- color math/conversion;
- Mode 7 inner loops;
- scanline data movement.

Do not cache large derived state without exact invalidation rules.

### VU0 / VU1

No active SNES core workload currently runs on VU0/VU1.

Potential VU candidates must be branch-light and process enough data per kick
to amortize setup/synchronization. Mode 7 affine blocks, bulk color math or
other fixed-width transforms are candidates for experiments. CPU instruction
emulation, MMIO, DMA state machines and other branch-heavy logic stay on EE.

VU1 should not be used just because it exists: it is best reserved for work
that naturally fits the graphics/vector pipeline. VU0 is the first candidate
for isolated vector kernels if profiling justifies it.

### Address/register/chip completeness

Known item already explicit in the source:
- DSP-3 has no HLE implementation; its mapped interface is intentionally inert.

Audit all of:
- CPU I/O $4200-$421F and mirrors/open bus;
- PPU $2100-$213F read/write semantics and latch side effects;
- APUIO $2140-$217F mirroring and timing;
- WRAM ports $2180-$2183;
- DMA/HDMA $4300-$437F modes, wrapping and edge timing;
- joypad/auto-read registers;
- LoROM/HiROM/ExLoROM/ExHiROM mirrors;
- DSP1/2/3/4, SuperFX, CX4, S-DD1, S-RTC and OBC1 mappings.

## Work order

### Phase A - coverage before speed

Create host tests for every address/register group and every CPU/SPC opcode
family. Record missing or approximate behavior explicitly.

### Phase B - correctness gaps

Fix address mirrors, open bus, read/write side effects, IRQ/NMI/DMA/HDMA
timing, SPC flags/direct-page wrapping and missing cartridge-chip behavior.

### Phase C - EE hot paths

Profile representative games and tests. Convert only proven hot paths to
MIPS/MMI, retaining portable reference implementations for A/B and
differential tests.

### Phase D - VU experiments

Prototype isolated VU0/VU1 kernels behind build flags. Keep them only when
they are measurably faster on PS2 and bit/cycle behavior remains correct.

### Phase E - regression matrix

Validate multiple workloads: ordinary LoROM/HiROM, Mode 7, hires/Mode 5/6,
DMA/HDMA-heavy scenes, SPC-heavy audio, SuperFX, DSP, CX4 and S-DD1.

## Rules for this branch

- No per-game PC/address hacks.
- No forced register values to make one title advance.
- Correctness first; optimization must have a reference path or test.
- Keep PS2-specific acceleration isolated from portable SNES semantics.
- Main is updated only after a group of changes passes broad regression tests.
