# SA-1 / S-CPU active-path audit V5

## Base and scope

Overlay the original project archive with Audit V3, Cleanup V3 and Audit V4 before applying these files. The remote main was not independently checked out for this build. The archive contains only changed source, test and documentation files. No ROM-specific address/CRC rules or disabled contention.

## Evidence from the V4 diag2 log

ROM CRC 1B8A0625; ELF CRC 8C6F8B40. Two loads share the same diagnostic session number; the second has 207 windows / 6210 frames, 7,839,491 average EE cycles/frame, 37.62 FPS equivalent core capacity, 5474 frames above the 105% threshold. Sampled bus/step/IRQ cycles: 23,354,120 / 246,702,061 / 19,103,760 (8.08 / 85.32 / 6.61%). These are sampled components, not exclusive whole-frame shares. 8,103,112 VBlank iterations skipped and zero I-RAM iterations skipped. The worst window ends at f1860, 15,005,518 cycles/frame. V3 and V4 captures differ in duration and inputs. Early f120 comparison: 4,880,889 -> 3,142,749 core cycles/frame; frontend loop 9,839,505 -> 4,919,499.

## Changes

- Active S-CPU polling: decode one or two native 8-bit LDA/BEQ/BNE pairs in private WRAM (aligned DP, absolute, long); preserve individual real I-RAM reads, bus event recording, cycle charges, flags, open bus and IRQ aborts. Existing stable idle and VBlank fast paths remain. Restrict to canonical shared-memory traps, same mapped code page and safe CPU state. No skipping active SA-1 execution.
- Scheduler: avoid empty IRQ/NMI service calls, simplify the existing SA-1 polling decoder, avoid finalizing bus events while the slice only repays previous instruction overshoot, inline pending host IRQ check.
- R5900 assembly: skip untouched ROM/I-RAM penalty ranges using finalized highwater marks; remove generic penalty stack spill; cache host instruction page bus type; compact zero-argument opcode epilogue and native branch timing; direct 16-bit bank-zero I-RAM access with both byte penalties and same-page protection check on writes. Preserve open bus in direct-page 8-bit fast paths. Boundary and protection cases use existing semantics.
- Diagnostics: `[snes-cpu-poll-active] iram-reads=N` reports real reads executed through new fast path (not skipped cycles). Comparator separates ROM reloads and selects last by default; optional `--baseline-run` and `--candidate-run`.

## Validation performed

- Host SA-1 suite: 80,000 timer cases; 16,777,216 addresses, 85,000 packed bus events; 4,900 slices per deferred/eager path, including wake and DMA. PASS.
- ROM Lab release, diag1, diag2 self-tests PASS. Seven new active-poll fixtures: 42,069 real reads per build, complete saved-state, CPU cycle and open-bus comparisons against normal interpreter. Includes native masked/unmasked IRQ fixtures. Existing polling and renderer tests pass.
- Three assembly variants assembled by Clang MIPS4 generic target, not PS2SDK; no R5900 execution tests here.
- Diagnostic Python test suite and multi-load selection on supplied log PASS.
- Active host synthetic benchmark, 1,310,000 slices: V4 median 229.317 ms, V5 median 254.834 ms; three runs each overlapped in range, run concurrently on same host so not an isolated performance test. All final counters identical. No demonstrated active host speedup; R5900-specific improvements require target measurements.
- No PS2SDK, physical console, NetherSX2 execution or full external opcode corpus here. No guarantee of target FPS or complete compatibility. Prioritize A/B with same inputs and diagnostic level; review any changes made after V4 before overwriting.

References: https://sneslab.net/wiki/SA-1_Hardware_Behavior ; https://snes.nesdev.org/wiki/MMIO_registers ; https://sneslab.net/wiki/SA-1
