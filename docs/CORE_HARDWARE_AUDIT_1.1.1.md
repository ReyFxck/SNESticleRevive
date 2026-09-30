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

The previous half-carry, direct-page word wrapping and missing-opcode gaps are
now covered by the host SPC700 suite.  The default opcode path remains only as
an explicit detector; ROM Lab treats reaching it as a failed run instead of
silently continuing.

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

Completed generic mapping item:
- ExHiROM SRAM exposes the strict `$80-$BF:6000-$7FFF` window and the
  `$20-$3F:6000-$7FFF` compatibility mirror.  Both resolve to the same SRAM;
  a synthetic test covers `$30:7808`/`$B0:7808`.

### Base-register pass completed

The base-console address pass now decodes every register window instead of
treating valid open-bus traffic as a missing implementation:

| Area | Implemented behavior |
|---|---|
| `$2100-$2133` | Complete write decode; write-only reads use the correct PPU1 or CPU-bus class. |
| `$2134-$213F` | Mode-7 product, OAM/VRAM/CGRAM data reads, H/V latch toggles, PPU1/PPU2 retained-bus bits, chip revisions, region/field and sticky OBJ range/time flags. |
| VRAM ports | 15-bit VMADDR, remapping, increment-port selection and the word prefetch/dummy-read sequence. Writes no longer corrupt the read buffer. |
| `$2140-$217F` | Four APUIO ports mirrored throughout the complete window, with the existing CPU/SPC synchronization. |
| `$2180-$2183` | WRAM data/address ports and 17-bit wrapping; `$2184-$21FF` is decoded as open bus/ignored writes without the former game-specific exception. |
| `$4200-$421F` | Write-only/read-only direction, WRIO readback and counter-latch edge, NMI/IRQ status side effects, multiplication/division result ports, H/V IRQ registers and joypad result ports. |
| `$4300-$437F` | All eight DMA channel images, including the `$43xB/$43xF` storage mirror and open-bus `$43xC-$43xE`. |
| Base expansion space | Device-less `$2000-$20FF`, `$2200-$3FFF`, `$4000-$5FFF` and `$00-$3F/$80-$BF:6000-$7FFF` probes no longer generate false missing-address reports. Cartridge handlers still replace these windows when a board decodes them. |

The register tests cover power-on values, DMA mirrors/open bus, PPU retained
buses and status side effects, Mode-7 multiplication, counter latching,
CGRAM/OAM ports and VRAM dummy reads. ROM Lab additionally completed
deterministic runs of the full three-minute Trials of Mana intro, a later
Trials gameplay state, the Top Gear attract/race path and a Super Mario RPG
SA-1 boot path without an unhandled base-console access.

The follow-up MesenCE accuracy pass closes the former base-console timing
approximations without per-game values:

- the 5A22 now retains a global CPU open-bus byte across ordinary accesses,
  traps and DMA-visible bus activity, while PPU1/PPU2 retained buses remain
  independent;
- multiply/divide uses the hardware 8/16-cycle result latency, including
  overlapping writes and early result reads;
- NMI/IRQ status windows and auto-joy progression use master-clock timing;
- active-display OAM/VRAM/CGRAM accesses use the live beam position and PPU
  internal addresses/restrictions instead of unrestricted memory access;
- MDMA/HDMA uses 8-master-clock synchronization, global/channel overheads,
  per-byte timing, A/B-bus restrictions and the WRAM/$2180 conflict cases;
- field cadence includes the non-interlace odd-field 1360-clock line 240, and
  Mode 5/6 plus OBJ fetches now select field-correct vertical source rows.

Native 448/478-line PS2 presentation is still separate from emulated PPU
semantics because the current output carrier is 256 pixels high. Special-chip
completeness also remains a separate audit; this pass does not claim that every
DSP/GSU/CX4/S-DD1/S-RTC/OBC1 behavior is complete.

### Measured PS2 rendering change

Top Gear's profiled normal-resolution lines select fixed color as the second
color-math operand (`CGWSEL.1=0`). On those lines the EE renderer now stops
after main-screen composition, and the GS chain omits the unused 256-byte sub
screen copy, texture upload and draw. Window, add/subtract, half-color and
brightness behavior remain in the same GS chain. Brightness or palette changes
do not create additional SNES sprites; they change color math, so this path
removes color-composition work rather than altering OAM or sprite count.

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
