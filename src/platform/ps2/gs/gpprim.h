
#ifndef _GPPRIM_H
#define _GPPRIM_H

#include <tamtypes.h>

void GPPrimRect(unsigned x1, unsigned y1, unsigned c1, unsigned x2, unsigned y2, unsigned c2, unsigned z, unsigned abe);
void GPPrimEnableZBuf(void);
void GPPrimDisableZBuf(void);
void GPPrimTexRect(u32 x1, u32 y1, u32 u1, u32 v1, u32 x2, u32 y2, u32 u2, u32 v2, u32 z, u32 colour, unsigned abe);
void GPPrimSetTex(u32 tbp, u32 tbw, u32 texwidthlog2, u32 texheightlog2, u32 tpsm, u32 cbp, u32 cbw, u32 cpsm, int filter);
void GPPrimUploadTexture(int TBP, int TBW, int xofs, int yofs, int pxlfmt, void *tex, int wpxls, int hpxls);

/* Set the logical->physical coordinate scale applied by GPPrimRect and
 * GPPrimTexRect to position coordinates (x,y).  UVs are not scaled.
 *
 * Used by the gsKit init path (gskit_backend.c::GSK_Init) to map the
 * legacy 256x240 logical layout used throughout the UI to the wider
 * 640x448 physical framebuffer that survives OPL GSM / PS2toHDMI mode
 * forcing.  See the long comment at the top of gpprim.c for the full
 * rationale. */
void GPPrimSetScale(float sx, float sy);

#endif


