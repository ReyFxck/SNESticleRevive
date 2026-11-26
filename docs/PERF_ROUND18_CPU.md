# Round 18 — CPU dispatch, ALU and raster scheduler

Incremental over Round 17. No changes to the three-ELF CI workflow, dependency
cache, rendering, audio transport or boot.

## Changes

- Plain R5900 interpreter: absolute JMP enters the existing instruction-fetch
  remapping path directly. No speculative reuse of the old mapping: mapper
  updates and trap pages still resolve through the current bank table. The
  page-boundary dispatcher exits directly when the budget is exhausted rather
  than incrementing and rewinding the PC.
- ADC/SBC (8/16-bit, both interpreter variants): place the accumulator copy
  and overflow flag merge in existing delay slots. Same arithmetic and decimal
  helper, two fewer executed native instructions per call. Plain BEQ/BNE test
  the masked zero state directly; BCC/BCS test the already-normalized carry
  flag. Existing branch timing and relative address logic remain unchanged.
- Plain mapped byte and word writes fill unused return delay slots. Long
  immediate/absolute operand fetch allows three bytes at the end of a mapped
  page and loads exactly three bytes, not four. Crossing pages still refetch
  bytewise. The byte-load path can cost more on nonboundary accesses; speed is
  not measured.
- Both variants: reserve a dedicated 32-bit spill at stack offset 0x8c (inside
  the existing 0x90-byte frame) for wrapped pointer addresses. Previously the
  first trapped byte read could overwrite SP_SaveTrap before the next read.
- MVN/MVP: preserve 16-bit A in the native interpreter, invalidate mapped PC
  count when refetch crosses a page, and preserve PB on the portable core's
  three-byte rewind. Do not batch or skip block-move bytes. These correctness
  checks add native instructions to the ordinary block-move path.
- ExecuteTimedSlice: remove the redundant outer loop; each slice can split at
  most once for the pending light-gun latch (cleared immediately afterwards).
  Preserve both ExecuteWithIRQ calls, clocks, and the zero-remainder case.

## Validation performed

- Fresh portable CPU build: open bus, 24-bit bus wrap, interrupt semantics,
  interruptible MVN and 72 new block-move page/program-bank boundary cases
  PASS. No external JSON opcode corpus installed.
- Both assembly variants preprocess and assemble with generic Clang MIPS64r2
  n32, PASS. Under identical command lines, plain object text is 46,564 ->
  46,576 bytes; shared 55,560 -> 55,592 bytes. This is not PS2DEV or a speed test.
- Fresh portable ROM Lab self-test PASS.
- Fresh SA-1 regression (80,000 timer cases, 16,777,216 addresses, 85,000 bus
  events, deferred bus tests) PASS; SuperFX 17,960 fuzz cases PASS.
- Python linked-ELF checker parses. Added 338 test invocations to existing CI
  (128 arithmetic, 12 trapped wrapped pointers, 144 block-move, 54 jump/long
  operand boundary). These cases have not executed against a linked ELF here.

## Limits

No PS2DEV/Unicorn or physical PS2 available. Neither generic assembly nor
portable tests establishes that the native ELF will pass or be faster. The
full dispatch redesign, broader WRAM/ROM fast paths, SA-1 arbitration changes,
HDMA scheduler and instruction-cache tuning remain future work after a
standard diag2 measurement. Keep VU0 disabled in the three published builds.
