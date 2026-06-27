/*
 * uiCover.h - Cover-art (capas) display for the ROM browser.
 *
 * PNG decoding via upng (src/third_party/upng, zlib license). Decoded +
 * scaled covers are kept in a small, bounded RAM cache (LRU) so moving
 * between already-seen / prefetched entries is instant. The browser
 * prefetches the current selection's neighbours one-per-frame while idle
 * so single d-pad steps land on a warm cache. A single fixed 128x128 GS
 * texture slot is reused for whatever cover is currently on screen.
 *
 * Memory: the cache is malloc'd lazily on first use and freed
 *   - on disable (CoverSetEnabled(FALSE) / toggling off), and
 *   - when a ROM is launched (CoverFreeCache(), to hand RAM back to the
 *     emulator core).
 * It is bounded (COVER_CACHE_SLOTS) so it never grows without limit.
 *
 * NOTE (untested on real PS2): the cover texture lives in a hard-coded
 * VRAM slot from the legacy GS layout (COVER_TEX in mainloop_init.cpp).
 */

#ifndef _UICOVER_H
#define _UICOVER_H

#include "types.h"

/* One-time setup: bind the GS VRAM slot (TBP, 256-byte units) used for
   the on-screen cover texture. Call once after FontInit(). Does NOT
   allocate the RAM cache (that happens lazily on first use). */
void CoverInit(Uint32 uVramTBP);

/* Toggle / query the on/off state (default OFF). Disabling frees the
   RAM cache. */
void CoverSetEnabled(Bool bEnabled);
Bool CoverIsEnabled(void);
void CoverToggle(void);

/* Make romPath the displayed cover. CoverShow may hit the disk (cold
   load through the cache). CoverShowCached only displays it if it is
   already in the RAM cache - it never touches the disk, so it is safe
   to call every frame for instant response. Pass NULL / "" when the
   current selection is not a ROM.

   Candidate PNG paths tried (first hit wins):
       $(COVERS_PATH)/<base>.png   (only if built with -DCOVERS_PATH)
       <dir>/<base>.png
       <dir>/covers/<base>.png
       <dir>/Named_Boxarts/<base>.png
   where <base> is the ROM file name without extension. */
void CoverShow(const char *romPath);
void CoverShowCached(const char *romPath);

/* Warm the cache for romPath without changing what is displayed.
   Returns TRUE only if it actually performed a disk load on this call,
   so the caller can cap prefetching to one load per frame. */
Bool CoverPrefetch(const char *romPath);

/* Display-state queries for the browser's right-side panel. */
Bool CoverHasImage(void);   /* a real cover is currently shown      */
Bool CoverNoImage(void);    /* checked this entry, none found        */

/* Draw the current cover fit (aspect-preserving, centered) inside the
   box [bx,by]..[bx+bw,by+bh]. Only meaningful when CoverHasImage(). */
void CoverDraw(Float32 bx, Float32 by, Float32 bw, Float32 bh);

/* Free the entire RAM cache. Call when launching a ROM so the memory
   goes back to the emulator core. (Also called on disable.) */
void CoverFreeCache(void);

#endif /* _UICOVER_H */
