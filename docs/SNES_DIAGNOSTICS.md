# Universal SNES diagnostics (`snesdiag-v1`)

The diagnostic build observes the same core path for every ROM. ROM names,
checksums and game-specific flags never select a renderer workaround. Cartridge
identity is printed only to explain which mapper and coprocessor were active.

## Build levels

```bash
make SNES_DIAGNOSTICS=1   # rolling low-overhead report
make SNES_DIAGNOSTICS=2   # report plus event-triggered deep capture
```

`SNES_DIAGNOSTICS=0` is the release default. The preprocessor removes the
counters and capture code from that build. Level 2 is intentionally more
intrusive and should be used for short reproductions.

All records go through `DLog()`, so Android emulator builds place them in their
normal TXT log. Each rolling window starts with:

```text
[snes-diag] schema=snesdiag-v1 ...
```

Consumers should check `schema` instead of assuming that field order from an
older build is unchanged. `session` changes whenever a ROM or core reset
starts a clean set of counters, while `f` stays monotonic for log ordering.

To turn a long Android emulator log into a short fault report:

```bash
python3 tools/snesdiag/analyze.py emulog.txt
python3 tools/snesdiag/analyze.py --json emulog.txt
```

The analyzer accepts emulator prefixes before each tag, ranks CPU/PPU/APU/DMA
hotspots and reports queue pressure, frame-budget misses, OBJ collapses, GS
copy mismatches and coprocessor failures. `--strict` is available for CI.

## Rolling report

The report covers 120 emulated frames and separates configuration from actual
work. This interval deliberately keeps the complete TXT stream below the SIO
throughput ceiling instead of making the logger itself steal frame time:

| Tag | Meaning |
|---|---|
| `[snes-frame]` | ROM region, physical 50/60 Hz host target, EE Count budget, min/average/max cycles, slow-frame count and estimated compute capacity. |
| `[snes-perf]` | Inclusive CPU, PPU, GSU, APU, mixer, MDMA and HDMA time. Inclusive percentages can overlap. |
| `[snes-ppu-stage]` | PPU sync, BG register decode, offset-per-tile, map, CHR, main/sub composition and color-math cost. |
| `[snes-ppu-modes]` | Visible rendered lines in modes 0 through 7, mode transitions and forced blank. |
| `[snes-ppu-layers]` | Main-screen, sub-screen and actually fetched BG1-BG4 lines. |
| `[snes-ppu-features]` | Mosaic, offset, windows, color math, direct color, interlace, overscan, hires and EXTBG usage. |
| `[snes-bg-depth]` | Decoded BG rows split into 2, 4 and 8 bpp. |
| `[snes-obj]` / `[snes-obj-cache]` | OAM work, hardware range/time limits, OBJ pixels in deep mode and OBJ-only cache efficiency. |
| `[snes-sync]` / `[snes-dma]` / `[snes-hdma]` | PPU queue pressure and transfer destinations, modes, size and wrapping. |
| `[snes-audio]` | Mixer requests, sample range and zero-sample anomalies; this is not an audsrv underrun claim. |
| `[snes-cart]` | Video type, mapping, flags and DSP/GSU/OBC1/CX4/S-DD1/S-RTC register traffic. |
| `[snes-sdd1]` | Bounded S-DD1 DMA/decompression, remap and unmapped-source totals; per-DMA text is deep-capture only. |
| `[snes-core-state]` | Final 65816 and SPC700 execution state for the window, useful for comparing freezes. |
| `[snes-gsu]` | SuperFX job state; per-instruction details require level 2. |

`capacity` is the speed of the measured emulation work on the EE, not the
number of host VBlanks that were presented. A frame is marked slow only above
105% of the active output budget: 2,457,600 EE Count ticks at 60 Hz or
2,949,120 at 50 Hz. ROM region is reported separately from the PS2's physical
refresh rate, so a PAL ROM on an NTSC console is not given a false 20 ms budget.

## Deep capture

Level 2 no longer dumps periodically. It captures the next frame only when an
event requests it, with a 60-frame cooldown to keep serial logging from causing
the problem being measured.

| Bit | Reason |
|---:|---|
| `0x01` | Manual: R3, or L2+R2 on touch layouts. |
| `0x02` | Frame exceeded 105% of its NTSC/PAL budget. |
| `0x04` | PPU write queue filled and forced a sync. |
| `0x08` | MDMA crossed the 16-bit source-address boundary. |
| `0x10` | OBJ output collapsed or selected tiles decoded empty. |
| `0x20` | Safe catch-up skipped video for an emulated frame. |
| `0x40` | The full audio mixer received a zero-sample request. |
| `0x80` | A coprocessor invariant failed, currently S-DD1 source mapping or the GSU watchdog. |

The `[snes-capture] begin` record prints the combined bitmask. The captured
frame then adds bounded PPU control-write, DMA/OAM and OBJ scanline traces,
S-DD1 decompression traces, CPU/SPC state and state hashes. Bulk OAM, VRAM and
CGRAM bytes are counted but not printed one by one. `[snes-ppu-reg]` lists the
four busiest PPU ports in the window. A capture prints at most 32 ordinary PPU
writes, 16 MDMA starts, eight S-DD1 DMAs/remaps and eight OBJ records on each
of the two sampled scanlines.

## Renderer cache policy

BG tiles always use the direct decoder. The experimental BG cache and its
2-bpp table were removed rather than hidden behind a switch. The remaining
physical cache is OBJ-only 4-bpp data: 149,504 bytes instead of 448,512 bytes,
saving 299,008 bytes and reducing VRAM invalidation to 16-word OBJ tile
boundaries. `SNES_OBJ_CACHE=0` remains available only for controlled A/B tests.

This framework makes failures comparable across the catalog; it does not by
itself claim that every ROM is already correct. Fixes found with it still
belong in the generic CPU, PPU, DMA, APU or coprocessor implementation.
