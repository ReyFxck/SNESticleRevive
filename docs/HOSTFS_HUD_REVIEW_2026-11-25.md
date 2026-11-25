# HostFS / HUD review — candidate patch

- `MainHostOpenDirectory`: normal `dopen` first; SAF encoded directory retry only
  when every enumerated non-dot child has the exact requested document parent.
  Root alias, unrelated parent, empty and bare-name listings are rejected.
  Android bridge behavior must be tested; this cannot repair the emulator itself.
- Browser and BGM scanner share the directory helper. BGM manifest entries
  are checked for readable file handles before indexing. Optional flat
  `BGM-*.xm` / `BGM-*.mod` files beside the ELF remain supported.
- Deep and target-profile HUDs use a transparent background, existing FPS
  foreground color and smaller integer font scale at higher video modes.
  The full profiler header uses the same compact scale and spacing.
  The native 240p font and simple FPS counter keep their original scale.
  Actual GS output must be checked.
- No emulation core, CPU timing, VBlank, audio timing, cache correctness or
  frameskip changes are made. The patch is a controlled first step, not a
  claim of full-speed performance.

## Validation
Run `python3 tools/test_hostfs_saf.py` on a host with g++. This checks URI
encoding and the exact shared helper against simulated fileXio calls.
Run `python3 tools/analyze_diag_log.py before.txt after.txt` for complete
diagnostic windows. Match ROM, input sequence, build flags, and host settings.
The supplied log: 316 windows, 9480 frames, 76.46 FPS guest core capacity,
58.01 FPS estimated guest loop, 44 slow frames at the 105% threshold.

No PS2DEV cross compiler/runtime was present during this review. The mock
does not reproduce NetherSX2's actual SAF implementation. Compile using the
existing Main Builds workflow; validate both release and diag2 on Android
and, if possible, on physical PS2. If encoded `dopen` is unsupported, an
explicit BGM.txt with paths such as `BGM/Track.xm` or a flat
`BGM-Track.xm` beside the ELF is a workaround only if file opens work.

## Cadence telemetry
Deep builds now emit `[ps2-cadence]` with guest start-to-start interval,
maximum, tail after prior loop accounting, and counts over 105%/150% of
the nominal budget. Tail includes logging and other unaccounted work;
these counters are not direct VBlank event counts or Android wall FPS.
The analyzer reports them when available. Release timing is unchanged.
CI runs the HostFS mock before the release toolchain build.
