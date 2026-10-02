# MesenCE frontend regression

Run `bash tools/mesencetest/build.sh`. Use `SANITIZE=1` to enable ASan and
UBSan, `JOBS=4` to select build concurrency, and `MESENCE_TEST_BUILD_DIR`
to keep a separate build. C++20 GCC and pthreads are required.

The test compiles the actual selected MesenCE sources and frontend bridge.
Its cartridges are original generated programs, with no commercial ROM
data. It checks NTSC/PAL frame rates, input affecting battery RAM/video,
48 kHz audible stereo output, texture guards, exact save/restore replay,
rollback of incomplete snapshots, PRG/CHR nonvolatile RAM, reset, and
MMC1/MMC3 bank writes and states. It does not certify all NES mappers,
accessories, PS2 FPS, GS uploads, SIF or SPU2 playback.

The source selection/provenance and integration changes are documented in
[MESENCE_INTEGRATION](../../docs/MESENCE_INTEGRATION_2026-10-02.md).
