# Round 17 — 65C816 memory, branches and stack

Incremental patch over Round 15; compatible with Round 16 CI. No changes to the
three published ELF variants, dependency setup, PPU, audio, VU or boot.

## Native interpreter changes

- Plain interpreter: fill the existing branch delay slots in the mapped 8-bit
  read/write helpers. Nonzero-page access executes one fewer instruction.
- Plain direct-page indirect word reads: when the low byte is not $FF and the
  address was constructed by the canonical direct-page addressing macro, the
  second byte cannot cross the direct page or bank boundary. Skip the full
  wrap calculation (four instructions versus nine). Preserve all original
  checks at $xxFF, and the original SA-1 path.
- Plain mapped word reads load exactly the requested bytes rather than merging
  a 32-bit unaligned word and masking. Avoid the extra byte reads at the end of
  a mapping. No change to open bus, access cycles or counters. Instruction count
  is similar; actual cost on the EE is not measured.
- Plain instruction-stream word fetch: allow the last two bytes of a mapped
  8 KiB page to use the fast path, and combine the guard and decrement in a
  branch delay slot. Cross-page and program-bank wrap keep bytewise fetching.
- Share a taken-branch tail among BPL/BMI/BEQ/BNE/BCC/BCS/BVC/BVS/BRA in the plain
  interpreter. This reduces instruction footprint (the not-taken case gains a
  jump). Preserve the original E-mode page-cross calculation and timing.
- Both assembly variants: correctly restrict the stack pointer to 16 bits
  after arithmetic. Raw multi-byte stack transfers wrap at $FFFF/$0000 but
  still defer the emulation-mode page-$01 restriction until the transfer ends.
  Previously a native-mode underflow could feed a negative address into the
  bank mapper. This is a correctness fix, not a speedup. It adds instructions
  to some stack sequences. Stack bus timing outside normally mapped RAM is
  separate and not addressed here.

## Validation

- Fresh portable CPU build and built-in open bus, bus wrap, IRQ and MVN checks
  passed. No external JSON opcode corpus present (the test reports zero corpus
  cases, not zero built-in tests).
- Preprocess/assemble both variants with generic Clang MIPS64r2 n32 assembler:
  PASS. Generic plain text size 51,096 -> 50,796 bytes (-300); shared text size
  59,524 -> 59,800 bytes (+276 from stack corrections). Not a PS2 benchmark.
- Python differential checker syntax passes. Added 270 cases to existing CI
  C-vs-linked-ELF test: 42 immediate word fetch cases, 36 indirect-pointer cases,
  192 stack edge cases across both interpreter variants. They have NOT run
  against a native linked ELF here.
- Existing portable ROM Lab, SA-1 and SuperFX executables rerun successfully;
  their portable code was unchanged by this patch.

## Limits / next steps

No PS2DEV or Unicorn in this environment; CI must validate the linked ELF.
A standard diag2 log (without shadow VU0) and a packed release benchmark on
matching scenes are required before measuring the performance delta. Branch
footprint, delay slots, and byte loads may have different costs on physical
R5900 vs NetherSX2. Do not assume a net gain or enable new title-specific idle
skips. Further changes to dispatch architecture, memory traps, scheduler,
stack bus timing and SA-1 arbitration should be isolated and benchmarked.
