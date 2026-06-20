/* gskit_backend.h
 *
 * gsKit-based backend for SNESticle's GS layer. Owns the GSGLOBAL
 * pointer and exposes helpers used by gs.c, gpfifo.c, gpprim.c and
 * the SNES blender to coexist on the same DMA path.
 *
 * Fase 1 GS->gsKit migration.
 */

#ifndef _GSKIT_BACKEND_H
#define _GSKIT_BACKEND_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fully initialise the GS via gsKit. Mirrors the parameter set used
   by the original GS_InitGraph + GS_SetDispMode + GS_SetEnv calls.

   width/height : framebuffer size in pixels (e.g. 256x240).
   dispx/dispy  : display position (matches MAINLOOP_DISPX/Y).
   psm/psmz     : pixel formats (GS_PSMCT32 / GS_PSMZ16S, etc).
   mode         : 2 = NTSC, 3 = PAL (matches GS_NTSC / GS_PAL).
   interlace    : 0 = non-interlaced, 1 = interlaced.

   After this returns, GSK_GetGlobal() returns a usable GSGLOBAL *.
*/
void GSK_Init(int width, int height,
              int dispx, int dispy,
              int psm, int psmz,
              int mode, int interlace);

/* ---- Video mode + display offset (selectable in the Settings screen) ----
 *
 * g_GskVideoMode selects the output the GS is programmed for at GSK_Init.
 * 480i is the default and keeps the exact legacy behaviour; 480p is the
 * progressive mode OPL GSM / HDMI adapters expect natively (no interlace
 * conversion -> no red/green stripe artefact). */
#define GSK_VIDMODE_480I  0   /* NTSC/PAL 640x448 interlaced (default)   */
#define GSK_VIDMODE_480P  1   /* DTV      640x480 progressive (GSM/HDMI) */

extern int g_GskVideoMode;    /* one of GSK_VIDMODE_*    */
extern int g_GskDispOffX;     /* horizontal display offset (0 = centred) */
extern int g_GskDispOffY;     /* vertical display offset   (0 = centred) */

/* Set the display offset live (no VRAM realloc) and remember it for the
   next GSK_Init. X is in VCK units, matching FCEUmm-PS2. */
void GSK_SetDisplayOffset(int x, int y);

/* Tear down and rebuild the GS for the current g_GskVideoMode. The caller
   MUST re-upload any textures it owns afterwards (e.g. FontInit). Intended
   to run once at boot after the saved settings are read from the card. */
void GSK_ReinitVideo(void);

/* Returns the active gsKit global, or NULL if GSK_Init has not run. */
struct gsGlobal *GSK_GetGlobal(void);

/* Allocate a region of VRAM via gsKit's user buffer pool and return the
   address in TBP units (i.e. byte_offset / 256). 0 on failure. */
Uint32 GSK_VramAllocTBP(Uint32 nBytes);

/* Drain gsKit's draw queue, wait for path-3 DMA to finish, and clear
   the GIF channel. Use this before the SNES blender kicks its own raw
   DMA chain on the GIF channel. */
void GSK_DrainAndWait(void);

/* Drain gsKit's draw queue and wait. Equivalent to GSK_DrainAndWait
   but kept as a separate name for clarity in the per-frame flush. */
void GSK_FlushFrame(void);

/* Wait for VBlank, swap framebuffers and reset draw queues. Must be
   called once per frame, after GSK_FlushFrame. */
void GSK_SyncFlip(void);

/* Emit FRAME_1 and XYOFFSET_1 register writes into gsKit's queue,
   pointing at the currently active draw buffer and restoring the
   coordinate origin gsKit expects. Use this at the start of a render
   frame so that subsequent gsKit primitives draw to the right buffer
   at the right position.

   Why FRAME_1: the SNES per-scanline blender steers FRAME_1 to its
   render-to-texture target every frame and only restores it through
   the legacy gpfifo chain, which is dispatched *after* gsKit's queue.

   Why XYOFFSET_1: the blender overwrites XYOFFSET_1 on every scanline
   with (0x8000, 0x8000 - iLine*16). Its restore also goes through
   the gpfifo chain, so any gsKit prim queued before gpfifo dispatch
   draws with the blender's stale offset — shifting the sprite
   hundreds of pixels off-screen (the frozen-menu symptom). */
void GSK_ResetFrame(void);

/* Force a TEXFLUSH next time we draw a textured prim. Used by the
   SNES blender after it overwrites texture VRAM via raw DMA. */
void GSK_InvalidateTextureCache(void);

/* Returns the uncached (KSEG1) alias of a kernel-space pointer that
   currently points into cached physical RAM (KSEG0, <256MB). The alias
   is the same byte address with bit 29 set: the EE bus reads/writes
   bypass the data cache, so a peripheral DMA reading the same physical
   line sees stores immediately without a FlushCache.

   Used (or about to be used) by the SNES blender when it patches
   per-scanline parameters into a chain that the GIF DMA is about to
   read. The legacy gslist path achieves the same with
   GSListGetUncachedPtr; this helper is the gsKit-side equivalent that
   does not depend on a gslist context. Returns the input pointer
   unchanged (with an assert in CODE_DEBUG builds) if the address is
   already uncached or otherwise outside physical RAM.

   Fase 1B GS->gsKit migration. */
void *GSK_AsUncached(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* _GSKIT_BACKEND_H */
