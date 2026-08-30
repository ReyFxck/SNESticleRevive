/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the uiCover interface for the PlayStation 2 user interface.
 */

/*
 * uiCover.h - Cover-art (capas) display for the ROM browser.
 *
 * PNG decoding via upng (src/third_party/upng, zlib license). Decoded +
 * scaled covers are kept in a small, bounded RAM cache (LRU) so moving
 * between already-seen / prefetched entries is instant. The browser
 * prefetches the current selection's neighbours at a paced interval while
 * idle so single d-pad steps land on a warm cache. A single fixed 256x256 GS
 * texture slot is reused for whatever cover is currently on screen.
 *
 * Memory: the cache is malloc'd lazily on first use and freed
 *   - on disable (CoverSetEnabled(FALSE) / toggling off), and
 *   - when a ROM is launched (CoverFreeCache(), to hand RAM back to the
 *     emulator core).
 * It is bounded (COVER_CACHE_SLOTS) so it never grows without limit.
 *
 * Its GS VRAM slot is allocated after the active mode's framebuffers and
 * supplied to CoverInit(), rather than being tied to a fixed address.
 */

#ifndef _UICOVER_H
#define _UICOVER_H

#include "types.h"

/* Bind the GS VRAM slot (TBP, 256-byte units) used for the on-screen
   cover texture. Call after FontInit() and after a video-mode reinit. Does NOT
   allocate the RAM cache (that happens lazily on first use). */
void CoverInit(Uint32 uVramTBP);

/* Number of GS VRAM bytes reserved for the 256x256 RGBA cover texture. */
Uint32 CoverGetVramSize(void);

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

   Candidate PNG paths are indexed under COVERS_PATH, beside the ROM and in
   the ELF's covers/ folder. A generated COVERS.IDX is used when available;
   manual layouts without one are scanned once as a fallback. Each base can
   contain a plain <base>.png plus
   the Libretro Named_Boxarts, Named_Titles, Named_Snaps and Named_Logos
   subfolders. Numbered <base>-1.png ... <base>-9.png extras are supported
   in the base folder. <base> is the ROM file name without extension. */
void CoverShow(const char *romPath);
void CoverShowCached(const char *romPath);

/* Warm the cache for romPath without changing what is displayed.
   Returns TRUE only if it actually performed a disk load on this call,
   so the caller can cap prefetching to one load per frame. */
Bool CoverPrefetch(const char *romPath);

/* Cycle to the next artwork variant that exists for the current ROM
   (custom -> boxart -> title -> snap -> logo -> Game-1 ... -> wrap).
   Bound to Square in the browser. No-op if the ROM has only one image. */
void CoverCycleVariant(void);

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
