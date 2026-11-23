# NES support

SNESticle Revive uses **MesenCE** as its only NES emulation core.

The integration is split so frontend policy stays separate from the vendored
core:

```text
src/nes/
├── frontend/
│   ├── nesrom.cpp/.h        iNES/FDS frontend ROM objects
│   ├── nessystem.h          Emu::System-facing NES interface
│   └── nessystem.cpp        frontend wrapper that drives MesenCE
└── mesence/
    ├── mesence_bridge.cpp   C boundary around the C++20 MesenCE core
    ├── mesence_bridge.h
    └── mesence_palette.h

src/third_party/mesence/
└── ...                      vendored MesenCE core
```

`src/nes/frontend/` owns SNESticle-facing behavior such as the `Emu::System`
wrapper and ROM objects. `src/nes/mesence/` is the narrow adapter boundary,
while `src/third_party/mesence/` remains the vendored upstream implementation.

SRAM paths, save-state slots/banks, the outer SNESticle state container and
MesenCE's serializer format are intentionally unchanged by this layout cleanup.
NES states remain variable-sized MesenCE snapshots tagged with
`MESENCE_STATE_FORMAT`; legacy NES state payloads are rejected rather than
interpreted as MesenCE states.
