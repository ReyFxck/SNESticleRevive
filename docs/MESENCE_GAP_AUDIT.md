# MesenCE gap audit — SNESticle Revive

This branch is an investigation branch. It does not intentionally change emulator behavior.

## Pinned baselines

- SNESticle Revive: \`main\` at \`09668f7c70deff5c6e8230f1b0370c7123d74472\`
- MesenCE: \`master\` at \`a60e79feb4d6dcced5922d636f9211837d01e381\`

MesenCE is used as a behavioral reference. PS2-specific architecture/performance remains separate from SNES semantic correctness.

## Classification

- **CONFIRMED GAP**: observably different hardware behavior with a clear register-role mismatch.
- **HIGH-RISK APPROXIMATION**: intentionally simplified behavior that can affect software.
- **AUDIT CANDIDATE**: suspicious difference that needs a focused test before changing code.
- **FEATURE GAP**: support present in MesenCE but absent from Revive; not automatically a regression.

## First-pass findings

### A1 — Global open-bus model — HIGH-RISK APPROXIMATION

Revive does not maintain a general S-CPU open-bus byte comparable to MesenCE's
\`SnesMemoryManager::_openBus\`.

Examples:
- \`SnesSystem::Read2000()\` falls back to \`uAddr >> 8\`.
- \`SnesSystem::Read4000()\` falls back to \`uAddr >> 8\`.
- \`SnesSystem::ReadMem()\` returns \`0\`.
- SA-1 unmapped/open-bus paths commonly return \`0xFF\`.
- GSU unknown register reads return \`0x00\`.

MesenCE updates the bus value on normal accesses and returns it for unmapped reads.
It also tracks separate PPU1/PPU2 open-bus values.

### A2 — $4213 RDIO — CONFIRMED GAP

Revive stores \`$4201 WRIO\` in \`m_Regs.wrio\`, but \`$4213 RDIO\` currently returns \`0\`.
MesenCE returns the programmable I/O output latch for \`$4213\`.

### A3 — WRIO bit 7 external H/V latch edge — CONFIRMED GAP

Revive's \`$4201\` write stores the byte but does not perform the H/V latch on a bit-7
1->0 transition. MesenCE explicitly latches the PPU H/V counters on that falling edge.

### A4 — CPU multiply/divide latency — HIGH-RISK APPROXIMATION

Revive computes \`$4202/$4203\` multiplication and \`$4204-$4206\` division immediately.
MesenCE models multiplication over 8 CPU cycles and division over 16 CPU cycles, including
overlapping-write/read timing.

### A5 — $4210/$4211/$4212 unused bits and timing — HIGH-RISK APPROXIMATION

Revive tracks the main flags but mostly returns the stored register byte.
MesenCE combines live flags with open-bus bits:
- \`$4210\`: NMI flag + CPU revision + open-bus bits 4-6.
- \`$4211\`: IRQ flag + open-bus bits 0-6.
- \`$4212\`: VBlank/HBlank/auto-joy state + open-bus bits 1-5.

MesenCE also preserves short NMI/IRQ flag timing windows. Existing Terranigma timing work
must be preserved during this audit.

### A6 — $2137 and PPU read-side open bus — HIGH-RISK APPROXIMATION

Revive's \`$2137 SLHV\` path latches H/V counters and returns \`0\`.
MesenCE latches the counters and returns open bus.

Audit \`$213B-$213F\` together because MesenCE uses distinct PPU1/PPU2 open-bus state for
unused/high bits.

### A7 — DMA unused offsets $43xC-$43xE — AUDIT CANDIDATE

Revive's DMA reader returns \`0x00\` for unhandled channel offsets.
MesenCE returns S-CPU open bus for offsets that are not real DMA registers.
The \`$43xB/$43xF\` scratch-register mirror already exists in both.

### A8 — portable 65816 C core correctness debt — AUDIT CANDIDATE

\`src/snes/cpu/sncpu_c.c\` still documents:
- program-counter bank-wrap problems;
- missing direct-page extra-cycle behavior.

The PS2 S-CPU uses the MIPS core, but the C core remains a reference/test path and is used
by non-MIPS builds; SA-1 also has C and MIPS execution paths. Add differential tests before
changing semantics.

### A9 — opcode fallback coverage — AUDIT CANDIDATE

The 65816 C core and SPC700 C core retain a default \`unimplemented opcode\` fallback.
This does not prove a legal opcode is missing. Add generated coverage tests for all 256
opcode bytes in required CPU modes.

The old \`WDM\` comment in \`sn65816.S\` is stale: current MIPS code does consume its
signature byte.

### A10 — coprocessor/platform feature inventory — FEATURE GAP

Revive has dedicated implementations for SA-1, SuperFX/GSU, CX4, S-DD1, OBC1, S-RTC,
DSP-1, DSP-2 and DSP-4.

MesenCE additionally contains BS-X/Satellaview, MSU1, Super Game Boy, SPC7110,
ST018 and Sufami Turbo implementations. DSP-3 is also not a complete Revive implementation.

## Register audit matrix

| Range | Audit target |
|---|---|
| $2100-$2133 | PPU write semantics, latches, invalidation, timing |
| $2134-$213F | read side effects, PPU1/PPU2 open bus, counters/status |
| $2140-$217F | APUIO mirrors and synchronization |
| $2180-$2183 | WRAM port and 17-bit wrap |
| $4016-$4017 | serial controller/multitap behavior |
| $4200-$420D | NMI/IRQ/autojoy, WRIO, ALU, timer targets, FastROM |
| $4210-$421F | read flags, open bus, controller auto-read |
| $4300-$437F | DMA/HDMA registers, mirrors, invalid offsets |
| chip-specific | SA-1, GSU, CX4, S-DD1, DSP, OBC1, RTC |

## Required tests before fixes

1. MMIO read/write table tests.
2. Controlled open-bus residue tests.
3. 65816 opcode/addressing/cycle differential tests.
4. SPC700 opcode/flag/direct-page differential tests.
5. DMA/HDMA mode/address-wrap tests.
6. PPU read-latch/status/open-bus tests.
7. Synthetic-ROM mapping probes.
8. Coprocessor register/memory-map probes.

## Branch rule

No game-specific PC hacks and no forced register values. Every proposed fix should carry:
the behavioral difference, a MesenCE reference location, a focused regression test, and
PS2 validation after host correctness passes.
