# InfoNES removal — 2026-11-23

MesenCE is now the only NES core in SNESticle Revive.

After the MesenCE-only build path was validated at runtime, the unused legacy
backend was physically removed instead of being kept as dead source.

Removed:

- `src/nes/core/` — InfoNES core
- `src/nes/cpu/` — K6502
- `src/nes/apu/` — legacy NES APU bridge
- `src/nes/mapper/` — InfoNES mapper set
- `src/nes/state/nesstate.h` — fixed legacy snapshot
- `src/nes/system/InfoNES_System_PS2.cpp`
- `src/nes/system/nessystem.cpp` — legacy system wrapper
- `src/third_party/nes_snd_emu/`
- legacy InfoNES-only include paths and backend conditionals

The active NES integration is now organized as:

- `src/nes/frontend/nesrom.*`
- `src/nes/frontend/nessystem.h`
- `src/nes/frontend/nessystem.cpp`
- `src/nes/mesence/*`
- `src/third_party/mesence/*`

NES save states continue to use MesenCE's variable-size serializer snapshot,
tagged with `MESENCE_STATE_FORMAT` in the outer SNESticle state container.
Legacy NES state payloads are rejected rather than interpreted as MesenCE
states.

This change is intended to be runtime-neutral for the already-tested
MesenCE-only configuration. The subsequent layout-only cleanup moved the
surviving frontend files from
`src/nes/system/` to `src/nes/frontend/` and renamed the MesenCE-only wrapper
implementation to `nessystem.cpp`. No save path, SRAM handling or state format
changed as part of that cleanup.
