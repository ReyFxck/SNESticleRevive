# src/nes/

NES emulation core for SNESticle Revive, based on InfoNES.

## Upstream

This directory contains a copy of [InfoNES](https://github.com/jay-kumogata/InfoNES)
(version 0.97J), licensed under the Apache License 2.0. The upstream LICENSE is
preserved at `LICENSE.InfoNES`.

InfoNES is © 2000-2006 InfoNES Project (originally based on pNesX by Jay).

## Why InfoNES

iaddis's original SNESticle source tree (circa 2004) references a parallel
`NESticle/Source/` project that was never publicly released. The Makefile and
mainloop have full integration scaffolding for NES support, gated behind
`#if 0` because the NES core implementation files are missing.

InfoNES was chosen as the NES core because:

- Apache 2.0 license is compatible with this project's MIT license
- The codebase is small (~5500 LoC) and self-contained
- It already targets multiple platforms including embedded ones (GBA, PSP)
  so porting to PS2 bare-metal is straightforward
- ~85% compatibility covers all canonical commercial NES/Famicom titles
- The code style is close to iaddis's, making integration into SNESticle's
  iaddis-derived mainloop natural

## Tree layout (mirrors src/snes/)

```
src/nes/
├── core/    InfoNES core, PPU rendering, scanline loop, types
├── cpu/     K6502 (6502 CPU emulation core)
├── apu/     InfoNES_pAPU (NES audio: 2 pulse + triangle + noise + DPCM)
├── mapper/  InfoNES_Mapper dispatcher
│   └── mapper/   per-mapper implementations (138 mapper files,
│                 #included into InfoNES_Mapper.cpp as a single TU
│                 -- matches upstream InfoNES layout)
├── ppu/     (reserved; PPU is currently inside core/InfoNES.cpp)
├── rom/     (reserved; will hold NesRom wrapper - Phase 2)
├── state/   (reserved; will hold NesStateT - Phase 5)
└── system/  PS2 platform layer + NesSystem / NesRom / NesDisk
              wrappers around InfoNES (added in later phases)
```

## Status

This is **Phase 1** of the NES integration:
- ✓ Sources copied, structure created
- ✗ Not yet referenced by the Makefile
- ✗ Not yet wired into mainloop (#if 0 blocks still gate NES)

See `NES_INTEGRATION_PLAN.md` in repo root for the full phased plan.
