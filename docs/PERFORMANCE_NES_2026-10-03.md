# NES/MesenCE: CPU dispatch and memory locality

Reference: main `321abdac`, immediately after restricting the browser root
to `mass0:` and `mass1:`. Other numbered BDM paths remain usable directly.

The CPU now uses direct switch dispatch from one shared 256-opcode map.
Addressing modes and instruction bodies are unchanged. Removing the instance
member-function/addressing tables saves 3,072 bytes per CPU on R5900; the
addressing table is now shared readonly data. Zero/negative flag updates use
the equivalent bit expression, and no longer call their small helper from
the linked R5900 instruction dispatcher.

The two 65,536-pointer memory handler arrays used 524,288 bytes on PS2.
Their replacement uses a 4,096-byte page directory and 1,024 bytes for each
page whose addresses have different handlers. The normal NROM register
layout needs two such split pages (6,144 bytes total); unusual peripherals
may require more. Full pages map without split allocations. Overrides,
unregistration, independent read/write handlers and open-bus updates retain
their original semantics. Internal RAM reads/writes bypass a virtual call
only when the actual registered handler owns that address.

The sprite evaluator is visible to the PPU dot loop; OAM clearing, overflow,
decay, sprite-zero behavior and PAL evaluation still execute. DMC scheduling
and IRQ predicates are inline, including the existing ProcessClock side
effects. The per-CPU-cycle console dispatcher is inline, maintaining the
mapper, APU and controller write order.

## Validation

`bash tools/mesencetest/build.sh` passes the existing integration and audio
checks plus the new hot-path reference checks:

- 24,576 opcode/bus/register/cycle/IRQ cases across NTSC, PAL and Dendy:
  `eebbbac2e387d8f3`, identical to the reference.
- 600 frames with rendering, OAM DMA and maximum-rate DMC: video
  `12ea3ffd4bc544f3`, states `e6aa6e8098898889`, PCM
  `db0cc8b63703a953`, with 527,278 stereo sample frames, identical.
- RAM mirrors, partial register pages, read/write ownership, overrides,
  unregister-to-open-bus behavior, $4015 and $FFFF pass.
- ASan and UBSan pass. LeakSanitizer process inspection is blocked by this
  execution environment, so this run uses `ASAN_OPTIONS=detect_leaks=0`;
  it does not claim a LeakSanitizer pass.
- Existing tests cover both input ports, turbo/disconnect mapping, NTSC/PAL
  frame rates, exact state replay/rollback, PRG/CHR battery, reset and
  MMC1/MMC3 bank writes/states.

The final normal build uses `SNES_DIAGNOSTICS=0`, MesenCE and RFAuds2.
R5900 text/data/bss are 4,299,575 / 639,736 / 14,421,392 bytes, compared with
4,297,947 / 639,736 / 14,421,392 before this round. The memory-table saving
is on the heap and is therefore absent from these static section sizes.

## Host timing and limits

Three alternating reference/candidate pairs each run 1,500 NTSC plus 1,500
PAL frames of the original DMA workload, hashing video and PCM:

| Pair | Reference seconds | Candidate seconds |
| --- | ---: | ---: |
| 1 | 5.711586 | 5.263373 |
| 2 | 5.807371 | 5.259724 |
| 3 | 5.945351 | 5.337954 |

The median elapsed time decreases 9.37% in this host workload. All video and
PCM hashes match. This is not a PS2 FPS estimate: EE cache timing, GS/SIF,
SPU2 playback, hardware inputs and real cartridges need testing on the target.
The linked R5900 CPU dispatcher is 9,004 bytes and the normal PPU Run loop
5,848 bytes; avoiding broad forced inlining keeps that growth bounded.
No ROM-specific workaround or clock/timing reduction was introduced.
