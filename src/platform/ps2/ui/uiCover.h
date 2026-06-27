/*
 * uiCover.h - Cover-art (capas) display for the ROM browser.
 *
 * Loads a PNG cover for the currently-selected ROM (via the upng
 * decoder, zlib-licensed, in src/third_party/upng) into a small GS
 * texture and draws it in a box on the right side of the browser.
 *
 * The whole feature is gated behind an on/off toggle (default OFF), so
 * when disabled the browser renders exactly as before - the ROM list
 * goes back to its original full width. Bound to L1 in the browser.
 *
 * NOTE (untested on real PS2): the cover texture lives in a hard-coded
 * VRAM slot picked from the legacy GS layout in mainloop_init.cpp. See
 * COVER_TEX_TBP there. Relocate by changing that single define if it
 * ever collides on hardware.
 */

#ifndef _UICOVER_H
#define _UICOVER_H

#include "types.h"

/* One-time setup: bind the GS VRAM slot (TBP, 256-byte units) used for
   the cover texture. Call once after FontInit(). */
void CoverInit(Uint32 uVramTBP);

/* Toggle / query the on/off state (default OFF). */
void CoverSetEnabled(Bool bEnabled);
Bool CoverIsEnabled(void);
void CoverToggle(void);

/* Load the cover for a ROM, given its full path (e.g.
   "mass:/snes/Super Mario World (USA).sfc"). Tries, in order:
       <dir>/<base>.png
       <dir>/covers/<base>.png
       <dir>/Named_Boxarts/<base>.png
   where <base> is the file name without extension. Reloading the same
   path is a cheap no-op. Returns TRUE if a cover is now loaded. */
Bool CoverLoadForRomPath(const char *pRomPath);

/* Drop the currently-loaded cover (e.g. when a directory is selected
   instead of a ROM). */
void CoverClearCurrent(void);

/* TRUE if a decoded cover is ready to draw. */
Bool CoverHasImage(void);

/* Draw the cover fit (aspect-preserving, centered) inside the box
   [bx,by]..[bx+bw,by+bh] in the browser's 256x240 logical space.
   No-op when disabled or no image is loaded. */
void CoverDraw(Float32 bx, Float32 by, Float32 bw, Float32 bh);

#endif /* _UICOVER_H */
