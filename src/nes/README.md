# NES support

SNESticle Revive uses **MesenCE** as its NES emulation core.

The PS2-specific integration is intentionally split from the vendored core:

```text
src/nes/
├── mesence/
│   ├── mesence_bridge.cpp   C boundary and PS2-facing MesenCE adapter
│   ├── mesence_bridge.h
│   └── mesence_palette.h
└── system/
    ├── nesrom.cpp/.h        iNES/FDS frontend ROM objects
    ├── nessystem.h          Emu::System wrapper
    └── nessystem_mesence.cpp

src/third_party/mesence/
└── ...                      vendored MesenCE core
```

The old InfoNES/K6502/APU/mapper backend and its `nes_snd_emu` dependency were
removed after the MesenCE-only build path was validated on the PS2 frontend.

Save states for NES are variable-sized MesenCE serializer snapshots.  The
outer SNESticle state container tags them with `MESENCE_STATE_FORMAT`, so old
legacy NES state payloads are rejected instead of being interpreted as MesenCE
states.
