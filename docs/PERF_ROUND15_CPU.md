# Round 15 — 65C816 interpreter and scheduler

Incremental patch on top of Round 14 (also compatible with Round 13's CPU files).

## Changes

- Split the R5900 architectural-state macros from the private callback spill
  macros. Five reads of uninitialized/dead stack slots at entry and five dead
  writes at exit are removed per interpreter invocation. Callback spill/restore
  sequences remain intact. No `SNCpuT` offsets or ABI changes.
- Plain (non-SA-1) interpreter: combine three byte reads in one mapped 8 KiB
  page for 24-bit indirect pointers. Charge exactly three bus accesses using the
  page's configured speed, update the CPU-cycle counter by three and latch the
  high byte as open bus. The path uses three byte loads, never reads a fourth
  byte, and keeps the previous bytewise path for I/O, page boundaries, and
  16-bit bank wrapping. Shared SA-1 bus paths are unchanged.
- Fix mapped consecutive opcode fetches so they update open bus just as the
  first opcode fetch and the portable C interpreter do. Do not update it on
  the budget-exhausted terminal probe. This correctness fix adds a byte store
  on the common opcode dispatch and can offset some performance improvements.
- Scheduler: when there is no pending signal and no SA-1, dispatch the normal
  CPU executor directly. Keep the same budget, execution wrapper and dynamic
  IRQ reschedule check. Every signalled CPU state still follows the original
  DMA/NMI/IRQ/WAI/STP/reset path. No merging of raster slices or clock changes.
- Extend existing linked-ELF C-vs-assembly CI checker by 57 cases (36 opcode
  streams, 21 long-pointer cases), including exhausted budgets, 8 KiB/64 KiB
  boundaries, I/O traps, and 8-/16-bit operand modes.

## Verification in this environment

- Portable CPU built-in open-bus, 24-bit bus wrap, interrupt and interruptible
  MVN regressions: PASS. No external instruction corpus was supplied.
- ROM Lab freshly built, integrated self-test: PASS.
- SA-1 host suite: PASS (80,000 timer cases; 16,777,216 address classifications,
  85,000 bus events, 4,900 slices per deferred bus path).
- SuperFX host suite: PASS (17,960 fuzz cases, zero failures).
- Both preprocessed CPU assembly variants assembled with generic Clang MIPS64r2
  n32 assembler. This is a syntax check, not R5900 execution validation.
- Python CI checker syntax and git diff --check: PASS.

## Limits

The PS2DEV toolchain and Unicorn runtime were unavailable locally. The new
linked-ELF differential cases have not been executed yet; CI must run them
before treating the R5900 path as validated. No claim of real-world FPS gain.
Changes do not modify boot, renderer, sprites, audio or VU kernels. Future
work includes profiling the opcode dispatch and memory helpers, actual
interrupt/raster scheduling costs, and SA-1 arbitration. Avoid changing CPU
cycles or adding title-specific idle-loop shortcuts to claim higher FPS.
