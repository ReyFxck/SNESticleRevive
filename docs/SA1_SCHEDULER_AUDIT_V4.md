# SA-1 scheduler and bus audit V4

Base: Audit V3 plus Source Cleanup V3. No ROM-specific PC or CRC rules.

## Changes

- Defer bus finalization until DMA or real SA-1 execution needs it. Preserve queued events until finalization or the normal scanline boundary. Idle slices do not construct unused penalty tables.
- Pack each host bus event into one 32-bit word; classify host pages and omit private accesses before queue insertion. Keep ROM, BW-RAM and I-RAM contention, including preceding BW-RAM ticks and fast I-RAM overlap. Range-based bit marking replaces repeated per-tick bitset updates.
- Avoid repeated same-clock synchronization work. Cache the BW-RAM mirror mask, with modulo fallback for non-power-of-two sizes. Inline I-RAM reads.
- Correct the assembly mixed-page penalty path's temporary-register clobber.
- Recognize bounded S-CPU polling loops in private WRAM on SA-1 cartridges: 8-bit BIT $4212 with VBlank mask and simple LDA/conditional-branch polling of I-RAM. Require safe CPU and event conditions; shared-memory polling is accelerated only while the SA-1 is already in a stable idle state. Measure a real iteration, skip whole intermediate iterations, execute the final iteration normally. No shortcut for an actively computing SA-1. ROM Lab instruction traces are not accelerated by default.
- Seed the optional safe frameskip scheduler from the known 50/60 Hz host period, instead of learning a slow startup cadence. No forced frameskip, no removal of VSync.
- Expose S-CPU polling iteration counts and extend diagnostic analysis/comparison. V4 sampled scheduler timing separates actual deferred bus work; compare totals with care across instrumentation versions.

## Local validation

- SA-1: 80,000 timer differential cases; all 16,777,216 host addresses classified against the reference; 85,000 randomized bus events over 500 lines; 4,900 deferred/eager slices per path, including I-RAM wake and DMA. Passed.
- ROM Lab self-test: release, diag1, diag2 passed. Synthetic VBlank and I-RAM polling comparisons skipped 14,009 and 11,568 iterations respectively; compared machine state, cycles and open bus, excluding diagnostic slice counters.
- CPU functional tests: open bus, 24-bit wrap, IRQ semantics and interruptible MVN passed. External opcode vectors were not run.
- SuperFX: 17,960 fuzz cases and 98,304 plane cases passed. PPU/HDMA, SPC700/audio, region cadence and safe frameskip tests passed. Diagnostic Python tests passed.
- R5900 assembly preprocessing checked; no PS2SDK target assembly/link or physical console test in this environment.

## Synthetic host benchmarks

200 frames, 1,310,000 slices, x86-64, median of three runs per build. Not PS2 FPS.

| Workload | V3 | V4 |
|---|---:|---:|
| Stable idle | 49.942 ms | 21.315 ms |
| Continuously active NOP/BRA | 227.358 ms | 238.658 ms |

Idle reduction 57.3%; active median approximately 5.0% slower, with overlapping run ranges. Both benchmarks had matching final counters (71,473,600 master clocks, 35,736,800 scheduled and executed ticks, zero dropped events, same timer and PC). The active path needs target profiling; no global FPS improvement is claimed. Run `bash tools/sa1test/bench.sh 200 idle` or `bash tools/sa1test/bench.sh 200 active`.

## Remaining work / target verification

- Compile and run the R5900 target; compare release and diag1 for real performance and diag2 against the earlier diag2 log for hotspot comparison. Use identical input and scene durations, and verify graphics/audio/IRQ behavior. Test SMW, Super Mario RPG, Kirby Super Star and Kirby's Dream Land 3 as separate regressions.
- Active SA-1 interpreter and active shared-memory polling remain potential bottlenecks. Do not skip CPU work across observable communication or interrupts. The safe frameskip change does not guarantee that the 30 Hz VBlank cadence observed in the log is resolved.
- No PPU algorithm changes in V4: the measured hot path was the shared bus and CPUs. No target-specific performance guarantee.

## References

- https://sneslab.net/wiki/SA-1_Hardware_Behavior
- https://sneslab.net/wiki/SA-1
- https://snes.nesdev.org/wiki/MMIO_registers
- https://github.com/ps2dev/gsKit
