# SNESticle Revive v1.0.8

Released: 2026-09-19

## Highlights

- Fixed the MX4SIO restart regression reported in #63.
  - The shared BDM + FatFs core is now loaded before `mx4sio_bd.irx`.
  - USB transport remains lazy, preserving the safer OPL hardware-boot path.
  - The fix was validated on real PS2 hardware across full restart and IGR.
- Added clean system exit options from the PS2 UI (#17):
  - return to the PS2 Browser;
  - launch `mc0:/BOOT/BOOT.ELF` with `mc1:` fallback;
  - power off the console cleanly.
- Exit actions save changed SRAM first and cancel the exit if that save fails.
- Restored the original Select-only access behavior for the System / Tools screen.
- Updated project licensing/source notices and general maintenance cleanup.

## MX4SIO diagnostics

If MX4SIO initialization still fails, Video Config now shows the numeric driver error as `Err <code>`, making hardware reports easier to diagnose.
