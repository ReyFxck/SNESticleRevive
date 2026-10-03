# MesenCE frontend regression

Run `bash tools/mesencetest/build.sh`. Use `SANITIZE=1` to enable ASan and
UBSan, `JOBS=4` to select build concurrency, and `MESENCE_TEST_BUILD_DIR`
to keep a separate build. C++20 GCC and pthreads are required.

The test compiles the actual selected MesenCE sources and frontend bridge.
Its cartridges are original generated programs, with no commercial ROM
data. It checks NTSC/PAL frame rates, input affecting battery RAM/video,
all 256 serial button combinations on both ports, 48 kHz audible stereo output, texture guards, exact save/restore replay,
rollback of incomplete snapshots, PRG/CHR nonvolatile RAM, reset, and
MMC1/MMC3 bank writes and states. It does not certify all NES mappers,
accessories, PS2 FPS, GS uploads, SIF or SPU2 playback.

The source selection/provenance and integration changes are documented in
[MESENCE_INTEGRATION](../../docs/MESENCE_INTEGRATION_2026-10-02.md).

The audio fixture compares the retained pre-optimization Hermite implementation
with the production resampler for 40,000 chunk/rate/volume/reset/pending/fill/add
calls, including the exact 96→48 kHz fast path. It exhaustively checks 1,015,808
nonlinear base-channel combinations and 100,000 expansion-channel sums against
the original double mixer expressions. Other volume/panning settings retain the
original mixer path.

`hotpath_test` checks RAM mirrors, partial register pages, read/write overrides,
open bus and the last CPU address. Its CPU trace covers every opcode with 32
operand/register/flag patterns in NTSC, PAL and Dendy (24,576 cases), compared
with hashes recorded from main `321abdac` before the changes. A 600-frame
rendering/OAM-DMA/DMC workload also checks exact video, serialized states and
527,278 stereo PCM frames against that reference. These are original generated
programs, and these checks do not measure physical PS2 performance.

Run `tools/mesencetest/build/hotpath_test 1500` for the optional host benchmark
(1,500 NTSC plus 1,500 PAL frames). It hashes video and audio in both builds,
so its elapsed time includes that verification work. LeakSanitizer needs a
host that permits its process inspection; `ASAN_OPTIONS=detect_leaks=0` leaves
ASan and UBSan enabled when that inspection is blocked.

`python3 tools/mesencetest/check_input.py` compiles the actual frontend mapping
and checks every combination, both turbo phases and disconnected pads. With the
default PS2 mapping, Cross=A, Square=B, Circle=turbo A, Triangle=turbo B; both
ports retain D-pad, Select and Start. These fixtures do not certify multitaps,
Zapper or physical controller drivers.
