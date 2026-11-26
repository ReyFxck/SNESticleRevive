# Round 13 — Boot isolation and real PS2 recovery

Base: reconstructed Round 12 (original project + rounds 7/8/9/12). This is an incremental patch, not an ELF. Apply after Round 12. Do not overwrite later unrelated local edits without checking the diff.

## Findings and changes

- Unknown/absent/oversized argv[0] no longer assumes `host:`. Explicit `host:` remains supported (including ps2link on real hardware and SAF on emulators). An explicit host origin enables the browser entry by default; saved settings may override that. The existing verified SAF URI retry code is unchanged. File-log capture likewise starts with an unknown origin instead of `host:`.
- Boot IRX loading uses embedded modules and ROM fallback only, with no blind HostFS/CD-ROM searches. Search-path concatenation is bounded.
- BGM discovery runs only in the outer main loop after 120 completed menu presentations, not from nested render calls or boot markers. No discovery without initialized audio. Existing decoder can feed PCM during nested modals (no file lookup); existing helper handles blocking I/O scopes.
- Host BGM discovery requires explicit host boot and enabled HostFS. No automatic HDD/MMCE module start by the BGM scanner; HDD scanning requires an existing mount, MMCE scanning requires a resident driver and cached slot mask. Local BGM discovery can still block after the menu appears; no general RPC cancellation mechanism is claimed.
- No MX4SIO start merely because a saved toggle is enabled. Explicit mass-device browser access attempts configured MX4SIO first, then USB if enabled and needed. MMCE root shows possible ports until the first explicit access probes them; successful probe retains verified ports. Boot state.cfg discovery no longer mounts/probes MMCE/HDD. Configs stored only on optional storage that is not resident will not be read at startup; normal MC0/MC1 settings still load. No change to save data format or SRAM files.
- On a returned memory-card stack initialization error, skip automatic settings/palette reads and do not mark the MC subsystem initialized. No forced save icon rewrite at boot; existing lazy SRAM creation and on-demand video config directory creation cover ordinary saving.
- Extra stage markers around GS allocation, pad module/bind, audio module/bind, config, SNES/NES/movie buffers and browser. Stage markers now go to DLog as well as the visible boot log. The mandatory IOP reset, gsKit flip, mandatory IRX calls and hardware/video timing are not replaced. A hang in a synchronous SDK call may still occur.
- CI keeps normal packed release and adds an identical stripped but unpacked release before packing. Adds separate diagnostic `PS2_BOOT_RECOVERY=1` build, uncompressed, using initial 480i/auto TV settings, no startup audio module, no BGM discovery, no saved video/state/palette loading, no automatic video config writes. It still initializes essential IOP, pad, GS and cores. Use for diagnosis, not performance or regular play.

## Validation performed here

- `python3 tools/test_boot_origin.py`: 16 compiled C++ normalization cases + 11 static call-site assertions passed.
- `python3 tools/test_hostfs_saf.py`: 13 assertions/URI encoding passed.
- `bash tools/rfauds2test/build.sh`: adapter test passed (host model).
- CI YAML parsed, recovery matrix and command verified; `git diff --check` clean.
- No native PS2DEV compiler or physical PS2 available here. None of the above validates GS/IOP startup, native module initialization, or actual boot on real hardware. No claim of confirmed root cause or measured FPS change.

## Test order on the same physical PS2, same launcher

1. `SNESticle-release-unpacked` → `SNESticle-unpacked.elf`.
2. `SNESticle-release` → `SNESticle.elf` (same compiled release, packed).
3. If both fail, `SNESticle-boot-recovery` → `SNESticle-boot-recovery.elf`.

Keep old working ELF and existing saves. Disable external GSM while testing. Note model SCPH, launcher/version, actual path, and last visible boot marker (photo/video). If recovery works and normal unpacked doesn't, suspect audio/config or a startup path disabled by recovery, not automatically HostFS. If only unpacked works, investigate loader/packer. If none display the first boot marker, mandatory pre-menu init or video is still suspect. If visible marker stops at a specific stage, inspect that operation first.
