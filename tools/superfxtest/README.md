# SuperFX regression and bitplane conversion

Run `bash tools/superfxtest/build.sh` and `tools/superfxtest/gsu_test`.
The first command also checks 98,304 bitplane cases against the previous
conversion, covering every pixel coverage mask, 2/4/8 bpp, unaligned colors
and destination guards. The core suite includes a 2,101,000-instruction job
that must retain GO without inventing STOP/IRQ across bounded slices.

With PS2DEV, Unicorn 2.1.4 and pyelftools 0.32 installed, run
`python3 tools/superfxtest/check_ps2_planes.py`. It compiles the old and new
conversion wrappers using GCC for R5900 and executes 3,072 comparisons.
Full-cache mean instruction counts in the fixture are:

| Bitplanes | Previous loop | Transpose |
|---|---:|---:|
| 2 | 266.75 | 88 |
| 4 | 521 | 100 |
| 8 | 1026.25 | 124 |

The transformation computes eight plane bytes once with scalar 64-bit
operations, then preserves RAM bits outside the coverage mask. Partial
caches keep their previous RAM reads; full caches avoid those reads as
before. Color conversion, addresses and writes remain equivalent.

These counts exclude chip/RAM timing, cache misses and whole-frame work;
they are not PS2 FPS measurements. The current scheduler still counts
384/960 instructions per scanline. Correct SuperFX clocks, instruction
costs, memory/cache waits and CPU synchronization remain open in issue #31.
