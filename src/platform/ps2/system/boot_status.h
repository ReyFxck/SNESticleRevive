#pragma once

/* Boot-time debug helper. When DEBUG_BOOT_SCREEN is on, BootStatusLog()
   writes a numbered, padded status line to a fixed position at the
   bottom of the BIOS debug screen so we always know which step the
   boot is currently on - even if the rest of scr_printf output got
   wrapped, scrolled or partially clobbered by GS framebuffer activity.
   It also prints the same message inline at the current cursor so we
   keep a scrollable history above the status line.

   When DEBUG_BOOT_SCREEN is off it falls back to printf, which the
   user can still see on real PS2 / ps2link / a kit. */

#ifdef __cplusplus
extern "C" {
#endif

void BootStatusLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* BootProbeReclaim(): reset the BIOS debug screen (init_scr + scr_clear)
   and print a status line. Used to verify, after each suspect GS pipeline
   call, that the GS is still in a state where init_scr() can take it over
   and put pixels on screen. If the screen still updates after a probe
   labelled "after X" then step X did not break the GS irrecoverably.
   The very last probe label that is visible on screen tells us the last
   working step. */
void BootProbeReclaim(const char *label);

#ifdef __cplusplus
}
#endif
