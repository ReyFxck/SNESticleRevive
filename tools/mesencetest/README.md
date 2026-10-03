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

`python3 tools/mesencetest/check_input.py` compiles the actual frontend mapping
and checks every combination, both turbo phases and disconnected pads. With the
default PS2 mapping, Cross=A, Square=B, Circle=turbo A, Triangle=turbo B; both
ports retain D-pad, Select and Start. These fixtures do not certify multitaps,
Zapper or physical controller drivers.
