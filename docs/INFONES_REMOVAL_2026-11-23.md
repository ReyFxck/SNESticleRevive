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

The active NES frontend remains:

- `src/nes/system/nesrom.*`
- `src/nes/system/nessystem.h`
- `src/nes/system/nessystem_mesence.cpp`
- `src/nes/mesence/*`
- `src/third_party/mesence/*`

NES save states continue to use MesenCE's variable-size serializer snapshot,
tagged with `MESENCE_STATE_FORMAT` in the outer SNESticle state container.
Legacy NES state payloads are rejected rather than interpreted as MesenCE
states.

This change is intended to be runtime-neutral for the already-tested
MesenCE-only configuration. Folder/name cleanup is deliberately deferred to a
separate change so any regression can be attributed to the removal itself.
