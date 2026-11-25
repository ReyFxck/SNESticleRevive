# Hardware accuracy round: PPU memory ports, windows, SPC timers

This incremental patch follows the SPC/BRR/VRAM performance round. It fixes documented hardware behavior rather than changing game speed or adding per-ROM hacks.

## Sources and scope

- SNESdev PPU registers: https://snes.nesdev.org/wiki/PPU_registers — VRAM writes outside VBlank/forced blank are ignored in the ordinary active-display case; VMADD increments according to VMAIN anyway.
- SNESdev reading/writing PPU memory: https://snes.nesdev.org/wiki/Reading_and_writing_PPU_memory
- SnesLab HDMA: https://www.sneslab.net/wiki/HDMA — window selection and logic registers may be changed per scanline.
- SnesLab SPC700 timing: https://sneslab.net/wiki/SPC700/Timing
- Anomie's timing notes: https://wiki.superfamicom.org/timing — prescaler runs regardless of timer enable.
- Anomie's SPC700 document: https://github.com/yupferris/TasmShiz/blob/master/spc700.txt — rising enable clears target and output stages, zero target denotes 256, target below current count requires wrap.

## Fixed

- Active-display VRAM writes were incorrectly deferred, replayed next frame, and on the timed single-byte path incremented VMADD twice. Drop blocked writes while preserving a single increment, including pairs and odd-length bulk transfers. No future cache invalidations for rejected writes. Keep the existing allowed-upload hotpath. Remove deferred buffer, which could also overwrite the other byte of a word with stale data.
- W12SEL/W34SEL/WOBJSEL/WBGLOG/WOBJLOG now apply when the existing raster scheduler dispatches the write, rather than at the next frame. This also prevents unrelated buffered registers from being overwritten by stale values. The scheduler is still scanline-based, not dot-accurate.
- Clear the PPU VBlank flag on reset. No changes to other reset register values.
- SPC timers retain a free-running prescaler phase, reset stage 2 and output only on enable rising edge, compare by 8-bit equality, and wrap output to four bits. The bulk math skips individual ticks; time differences use unsigned subtraction to handle 32-bit wrap. Existing master-clock scale is unchanged. TEST-register timer gating, target-write glitches and exact bus-cycle placement remain unimplemented.
- Public SPC byte read/write/peek helpers wrap 16-bit addresses (including debugger/bulk reads crossing $FFFF). The previous interpreter optimization already covered its own internal accessors.

## State compatibility

No serialized structure fields were added or removed. `SNSpcTimerT::nElapsedCycles` now encodes target-stage count times tick period plus free-running phase, which coincides with the older representation during uninterrupted enabled operation with unchanged target. Old states may resume with an approximate timer phase because the old model did not retain it while disabled. Removed PPU buffers were not serialized. No save-state format changes or claims of complete state accuracy.

## Tests

Run `bash tools/pputest/test_hardware_accuracy.sh`. It executes the full OAM/VRAM suite (including existing 528 wide-prefix cases) and 312,517 timer/memory assertions, including 100,000 randomized events against a tick-by-tick model. The old suite had 15 failures: one double increment and 14 expectations from four block-upload test cases that did not set forced blank. Those fixtures now explicitly set forced blank; new tests cover rejected timed/pair/odd DMA writes, no replay, per-line window changes, unrelated-register preservation and VBlank reset. Existing PPU hotpath, SPC/BRR and mixer tests also run separately.

Host-only validation; PS2DEV build, hardware/NetherSX2 runtime, exact DMA boundary timing, PPU dot timing, DSP shared ARAM echo, 5A22 interrupt/ALU precision and special chips are not certified by these tests. Re-test from cold boot (not old savestates), and compare video and audio with known-good references. No game-specific patches.
