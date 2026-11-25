# Round 6: recovery of BG cache hot path, OAM DMA, and paired cadence diagnostics

Base: Round 5 SA-1/GSU patch, on top of Round 4. This is a conservative follow-up,
not a complete hardware implementation. Test input was an uploaded diag2 log,
ELF CRC F3E9A8C2, Super Mario World (ROM CRC A31BEAD4). The Round 4 counters
identify the diagnostic feature set; the log alone cannot prove which later
commits were included. No PS2DEV/toolchain or PS2/NetherSX2 runtime here.

## Findings from the complete log

42,922 lines; 391 complete core and frontend windows of 30 frames (11,730);
390 complete cadence and IRQ windows (11,700). Nominal guest EE rate 294,912,000.
These are guest cycle estimates, not measured Android wall-clock FPS.

| Metric | Value |
|---|---:|
| Core mean | 3,567,346 cycles / 12.096 ms / 82.67 capacity FPS |
| Worst mean window | frame 8040, 4,262,125 cycles / 69.19 capacity FPS |
| Worst single core frame | 6,729,964 cycles / 22.82 ms |
| Windows with max core frame over 60-Hz budget | 24 |
| Core frames >105% of budget | 42 |
| Frontend work mean/max | 13.061 / 23.998 ms |
| Prep / submit / flip | 0.703 / 0.015 / 3.940 ms |
| Start-to-start cadence | 58.80 FPS, max 33.455 ms |
| Intervals >150% budget | 223 / 11,700 (1.906%) |
| VBlank IRQ before/during flip | 11,789 / 134 |
| VBlank zero/multiple/invalid | 0 / 223 / 0 |
| CHR cache | 2,408,738 hits / 30,348 misses |
| BG line cache | 6,575,489 hits / 522,965 misses |
| OBJ cache | 1,962,024 hits / 83,246 misses |
| BG epoch shortcut | 1,502,017 hits (22.84% of line hits); zero in 155 windows |
| BG validate / restore / store | 119,452 / 88,675 / 8,509 cycles per frame |
| Identical VRAM bytes / blocks | 8,537,754 / 258,750 |
| PPU writes queued/applied/full | 994,646 / 994,646 / 0 |
| Rendered / skipped | 11,730 / 0 |
| Audio samples / mixes / empty | 6,256,000 / 11,730 / 0 |
| OAM comparable / matched | 106 / 106 (31 noncomparable) |
| GS stage/copy mismatch | 0 / 0 |
| HostFS access denials | 63, BGM tracks 0 |

Earlier Round 1 log report: core capacity 83.59, cadence 59.08, first 360
frames 2,802,106 cycles/frame. Current first 360 = 2,814,306 (+0.435%).
The sessions are not a controlled replay. Round 4 also broadened the timed
validation region to include entry lookup/epoch checks, so the validation
increase is not a pure comparison of identical scopes. At frame 360 the
reported hit counts and PPU hashes match, but validation/restore and core
costs rise; no proof that the epoch check is the sole cause. Revert this
experimental path and test A/B. Do not discard the mosaic accuracy fix.

145 windows contain long cadence intervals; in 123 corresponding report
windows the core max is within budget. The aggregate IRQ multiple count
matches the late cadence count, but the IRQ measurement was flip-to-flip,
and cadence was start-to-start. They must not be treated as per-frame
pairs. Likewise the work and cadence report windows are offset by one
iteration. The new diagnostic explicitly pairs the preceding work with the
next start interval. The recorded log ends after a pause around 213 wall
seconds, not a 25-minute soak test.

Inclusive weighted profile: CPU 39.07%, PPU 44.90%, APU 5.91%, DSP mix
10.70%, MDMA 1.03%, HDMA 0.44%. Nested timings are not additive. Average
hot stages in cycles/frame: PPU sync 1,646,587; CHR 428,725; tilemap 135,537;
main 113,413; sub 93,309; OBJ update/fetch/draw 28,121/51,579/30,457;
color math 28,876; HDMA data/table 10,619/2,959.

Only Mode 0/1, LoROM, and standard chips were exercised. Mosaic is present
(134,235 lines). No SA-1, GSU or other special cartridge evidence. No
hires/interlace/Mode 7 evidence. A matched sampled hash or zero skip is not
a guarantee of complete hardware correctness.

## Changes

- Restore pre-Round-4 dependency/row-memo lookup, remove entry epoch field
  and shortcut, retain cache layout and mosaic correction. Make ordinary
  mask restore inline again; keep nonzero mosaic correction out of line.
  Remove alignment probe for mask shift which often fell back anyway.
  The fused mask operation remains. The disabled epoch line is emitted
  as `hits=0 enabled=0` for older parsers.
- OAM block copy: contiguous, even address and byte count, nonoverlapping
  buffers and physical range only. Preserve latch, address and priority.
  Identical blocks do not write or invalidate; all other cases retain the
  original byte path. New `[snes-oam-block]` counters measure its use.
- Simplify system-bank predicate and inline local A-bus DMA accessors.
- OBC1: keep underlying RAM byte on port writes, notably the index at
  $7FF6. Fullsnes specifies mapping and index; Snes9x is a secondary
  behavioral reference for shadow writes/readback, not hardware proof.
  No imported third-party code. No OBC1 game tested.
- `[ps2-late-work]`: for each long start-to-start interval, classify prior
  work over/under budget, whether subtracting measured post-core report
  cycles crosses the budget, and IRQ delta over the same interval. The
  report count includes capture, hashes and reporting after the core
  timing cutoff, not all diagnostic overhead. A report-cross observation
  is not proof of causality. No flip policy changes.
- CI builds release, diag1 and diag2; host ROM Lab self-test added. This
  costs a third build but allows lower-instrumentation A/B. Host C tests
  use -fwrapv consistently with the production Makefile. No state-layout
  changes.

## Tests run locally

- Hotpath: three configurations, each 16,384 CHR cases, 24,576 tilemap,
  65,536 masks + 4,096 stores, 16,384 mosaic + 64 key checks; OAM 4,096
  cases; VRAM 528; PASS.
- Full OAM/VRAM, timer 312,521, APU memory 1,181,738, ALU 65,536,
  A-bus 16,777,216, paired diagnostic, OBC1 131,072: PASS.
- SPC differential 18,432 cases / 3,002 I/O events, BRR 262,144 blocks;
  six mixer PCM hashes unchanged; echo write-disable regression: PASS.
- CPU built-in open bus, 24-bit wrap, interrupt and MVN: PASS; zero external
  opcode vectors supplied. SA-1 host suite and GSU host suite (17,960 fuzz
  cases, 98,304 plane cases) PASS. HostFS mock 13 assertions PASS.
- ROM Lab portable self-test, diagnostic levels 0, 1 and 2: PASS. 96
  empty-math scenes, 4 fine-X scenes, Mode 1/5 cache/scroll scenes,
  mapping, lightgun, deterministic replay/state restore. Level 2 built
  after final counter changes. These do not execute the PS2 frontend/MMI.

## Validation sequence and unresolved work

1. Build Round 6; use diag2 with same game/settings, restart from reset;
   check first 360 frames (same PPU hashes/input needed) and compare
   core, validate/restore, OAM block and cadence counters. Compare diag1
   and release at identical settings; don't compare accelerated gameplay
   or frame skip with normal-speed replay.
2. Use ROM Lab replay/trace for controlled C core checks. For real target
   performance, repeat on PS2 (NetherSX2 timing is not physical hardware).
3. Before further speed changes, use chip-specific logs and ROM tests:
   SMRPG/Kirby Super Star/Dream Land 3 (SA-1); Star Fox/Yoshi's Island/Stunt
   Race FX (GSU); F-Zero/SMK (Mode 7); Metal Combat (OBC1); Star Ocean/SFA2
   (S-DD1); Mega Man X2/X3 (CX4); hires/interlace test ROMs. Use known
   legally obtained ROMs, report exact CRC and symptoms. The Round 5
   changes are not validated by SMW.
4. Still pending: full SPC TEST RAM/wait states, cycle-ordered echo in
   shared ARAM, exact HDMA/interrupt timing, SA-1 arbitration, GSU clock
   accounting, DSP4/DSP3/SPC7110/ST series, chip save states, interlace,
   VU0/VU1 design, and actual HostFS directory support. The HostFS log
   shows the bridge denies relative paths and mislabels BGM; a flat
   BGM-*.xm/mod beside the ELF is an existing workaround to test, not a fix.

References:
- https://sneslab.net/wiki/OBC1
- https://patrickjohnston.org/ASM/ROM%20data/snestek.htm (OBC1, SPC TEST)
- https://sources.debian.org/src/libretro-snes9x/1.53%2Bgit20160522-1/obc1.cpp/
  (behavioral cross-reference for OBC1 shadow writes)
