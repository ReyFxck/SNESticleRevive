# Round 10 — cooperative EE/IOP audio service

Incremental patch on top of Rounds 7, 8 and 9. No VU0, VU1, sprite, PPU register, mixer, BRR, sample-rate or IRX changes.

## Evidence and scope

Round 8 Top Gear diag2 log: 157 complete 30-frame windows (4710 frames), nominal guest EE 294912000 Hz, 785 slow core frames. Last audio snapshot: EE/IOP=798/0, underruns=4760, silent=1694531, missed-refills=0. This is a cached asynchronous snapshot, not a measurement of how long the 798 EE frames waited. The IOP ring has insufficient PCM when refilled; the data do not establish RPC transport as the sole cause. At sustained sub-60 cadence, fixed-rate output consumes more PCM than the emulated console generates. No finite buffer fixes that indefinitely.

## Changes

- A PCM enqueue never launches an idle stats request before appending fresh samples. Existing in-flight requests are collected normally.
- `Aud_ServicePending()` takes the existing semaphore via PS2SDK `PollSema` (no wait), polls at most one RPC and launches at most one PCM submission. It never issues an idle stats RPC or synchronous START. The normal frame-boundary service still handles playback start and stats.
- The SNES calls the optional mix-buffer service once every 32 lines (including VBlank); the async audio sink forwards it to the adapter. The base sink and audsrv fallback do nothing. These calls transfer only already-produced PCM. Emulated clocks, mixer scheduling and sample values stay unchanged.
- Service immediately before and after the frontend VBlank flip, outside the measured flip interval. Do not change presentation timing or GS packet ordering.
- Keep Round 7's 64 ms IOP capacity and 2560-frame prebuffer. Do not synthesize additional audio, discard pending samples or change the 48 kHz output rate.
- New `[ps2-audio-rpc]` counters: submitted counts attempted frames including retries, accepted counts actual admitted frames collected by the EE, zero/partial count replies, stats/service count idle queries/opportunistic work calls, flight is the outstanding submit length, pending is 0 idle / 1 submit / 2 stats. EE counters reset on stream clear/init; IOP underrun counters have their own reset semantics. Both use unsigned counters. Compare within the same stream and do not sum cumulative snapshots. Existing EE queued count includes the in-flight prefix until acknowledged.

Reference: https://ps2dev.github.io/ps2sdk/kernel_8h.html and https://akuhak.github.io/ps2sdk-doxy/ee/group__libkernel.html (PollSema). PollSema itself acquires the mutex; do not follow it with WaitSema.

## Validation

Host: `bash tools/rfauds2test/build.sh` passes, including new first-submit/no-idle-query, accepted-frame accounting, busy-semaphore and concurrent-service checks. `bash tools/pputest/build.sh` compiles all 22 executables and all 22 pass, including exact PCM chunk/volume tests, 480 HDMA/CGRAM cases, 4096 OAM cases, six mixer hashes. `bash tools/romlab/build.sh` compiles the portable core. `bash tools/pputest/test_vu0_shadow.sh` passes 67504 pixel/priority comparisons in the portable model. No PS2DEV cross compiler is installed here; CI and hardware testing are pending.

## Target validation

Compare Round 8/9 ordinary diag2 against Round 10 ordinary diag2 (not the shadow build), same game inputs and emulator settings. Also run release to isolate diagnostic overhead. Watch cadence, work, IOP occupancy, accepted PCM, silence and underruns per comparable interval. Extra service calls carry a small EE overhead; measure this instead of assuming a gain. If cadence stays below full speed, sustained underruns can remain. Further work should address Mode 1, CPU/HDMA and scheduler, without changing video correctness. No promise of eliminating the audible artifact in this patch.
