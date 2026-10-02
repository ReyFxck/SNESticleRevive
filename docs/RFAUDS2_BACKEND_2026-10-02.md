# Experimental RFAuds2 backend and Spawn investigation

This candidate replaces output transport through the existing Aud_* API. It does not replace the SNES DSP, SPC700, scheduler, MMI mixer, gain or 32 -> 48 kHz conversion. No game-name/CRC cases, pitch changes, missing effects or frame-dependent DSP shortcuts are added. The main branch is not changed.

## Demonstrated transport defect

The current audsrv wrapper ignores a successful short byte count from audsrv_play_audio, then AudMixBuffer reuses its output buffer. PS2SDK's IOP audsrv limits the accepted byte count to available ring space; its EE wrapper advances by the requested chunk, rather than retrying the unaccepted tail. Consequently a full queue can lose part of an audio block even when the call returns a nonnegative result. Calling the existing Aud_EnqueueAsync does not make SIF asynchronous: that wrapper still invokes synchronous audsrv_play_audio.

Source comparison: PS2SDK 4996c6f, ee/rpc/audsrv/src/audsrv_rpc.c and iop/sound/audsrv/src/audsrv.c. The original backend is deliberately retained as the A/B reference. This defect can affect any producer; it does not establish the cause of the reported Spawn intro distortion.

## Ownership and synchronization

An aligned 8192-stereo-frame EE FIFO uses 32768 bytes. Input is interleaved and copied before the producer can reuse it. One RPC owns at most 960 contiguous frames; the adapter retains its ring prefix until completion, advances only by the accepted count, and retries partial/zero-admission tails. The RFAuds2 EE client owns separate aligned SIF staging and a complete reply cache line.

Regular service polls/launches without waiting for an IOP space semaphore or a synchronous occupancy query. Async telemetry shares the same guarded RPC slot; BGM availability uses the last completed IOP occupancy and counts unsent EE frames conservatively. Advertising the entire EE FIFO as BGM demand would create excessive latency and synthesis. The main loop services audio before normal work and after gameplay output; menu/BGM calls also service it.

The existing physical IOP ring is 4096 frames; this candidate selects a 2048-frame admission limit (43 ms setting, rounded to 512-frame blocks). Playback starts after 1536 real frames are available, leaving a refill after priming both SPU2 halves. Initialization and occasional controls remain synchronous after collecting the bounded outstanding reply. A small legacy blocking enqueue can force startup. Explicit pause/clear discards the queues, matching the previous wrapper.

If the bounded EE FIFO becomes full, the producer applies lossless back-pressure and yields until admissions free space. The default path is asynchronous, not an unlimited queue or a claim that every API call can never wait. Fatal transport errors are reported once and disable the adapter rather than retrying an unknown admission and duplicating samples.

The driver uses direct SPU2 ownership: the RFA build loads neither audsrv nor FREESD/LIBSD. All other input/storage/network modules retain their existing load paths. RFAuds2 is pinned to 9c5e5b4; unmodified public headers, client and IOP sources/license are in src/third_party/rfauds2. The bundled IOP binary rebuilds byte-for-byte from these sources using the documented toolchain.

## Builds and diagnostics

Use `make fast AUDIO_BACKEND=rfauds2 SNES_DIAGNOSTICS=0 SNES_TARGET_PROFILE=0` for normal comparison, or AUDIO_BACKEND=audsrv for the original transport. The compile-mode marker includes the backend, so switching does not reuse incompatible objects. SNES_TARGET_PROFILE=1 adds the v3 attribution rows; RFA also shows Q EE, IO and cumulative UND without a new synchronous RPC. UND rising establishes missing output frames at refill, not their cause. More queue capacity cannot compensate indefinitely for an emulation rate below real time.

Four comparable ELF variants are supplied: audsrv/RFAuds2, normal/profile. The profile has overhead; compare speed with normal variants on the same scene and settings. Check boot, menu music, gameplay audio, volume, menu/game transitions and the Spawn intro without input. A queue/UND capture supports diagnosis even when an emulator log is unavailable.

## Evidence and limits

- The production adapter fixture preserves 249680 stereo frames under delayed completion, source overwrite, repeated wraps, full/partial/zero admissions and back-pressure. Controls with an outstanding request, small blocking startup, idle occupancy and fatal completion are exercised. ASan/UBSan pass.
- RFAuds2's separate production EE/IOP protocol fixture preserves 393216 streaming frames across 512/2048/4096 limits, validates cached/delayed stats, paused/stopped full queues and malformed/error replies. Existing resampler/mixer tests pass.
- 22 existing PPU/audio/SPC executables pass. PS2 normal/profile builds compile for both backends; normal builds exclude target probes and deep SNES diagnostics. Host fixtures mock SIF/SPU2/DMA and do not establish audible or physical timing correctness.
- Spawn runs 10800 frames (180 emulated seconds) without input through the portable full-mixer core. Captured 32-kHz PCM has very few saturated samples (0.001% in the first 20 seconds; none in subsequent measured windows). This does not certify the MMI mixer, frontend resampling or physical backend, and does not demonstrate the reported distortion is fixed. Commercial ROM and PCM are not included in the package.
- Photo 2608 has CORE 48.9 ms and PPU 5.2 ms. Most core time remains unattributed by the earlier HUD. V3 measures CPU executor, MDMA, HDMA and renderer boundaries. No Mode 7 60-FPS claim or later Star Ocean/Tales crash fix is made.

The adapter and mounted-HDD state resolver are now part of the same review branch in PR #97, with separate implementation commits. The four backend/profile variants were rebuilt together and preserve the backend-specific SPU2 ownership; host adapter and state fixtures pass on this combined base.

RFAuds2 needs actual FAT/Slim/SPU2 playback, DMA/interrupt stress and long-running A/B before promotion to main. The other requested chip, SMB, complete save-state, CRT and NES integration tasks remain tracked in PS2_WORK_PRIORITIES_2026-10-02.md. The newer MesenCE source ZIP was received and its standalone NES-only build compiled; integration remains pending. See CACHE_CAPACITY_2026-10-02.md.
