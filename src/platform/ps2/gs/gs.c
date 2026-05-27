/* gs.c
 *
 * Original SNESticle code talked to the Graphics Synthesiser through
 * raw memory-mapped registers and a hand-rolled GIF chain. As of the
 * Fase 1 GS->gsKit migration, the public API in gs.h is preserved
 * but every entry point routes through gsKit (see gskit_backend.[ch]).
 *
 * The SNES per-scanline blender (snes/ppu/snppublend_gs.cpp) still
 * builds its own GIF chains and kicks DMA on the GIF channel
 * directly; gsKit is forced to drain via GSK_DrainAndWait() before
 * the blender runs, and the blender restores the FRAME / XYOFFSET
 * registers via GS_GetFrameReg() / GS_GetOffsetReg() once it is
 * done. Both helpers now read the gsKit GSGLOBAL state instead of
 * the old draw_env structures.
 */

#include <tamtypes.h>
#include <kernel.h>
#include <string.h>

#include <gsKit.h>

#include "hw.h"
#include "gs.h"
#include "types.h"
#include "ps2dma.h"
#include "gskit_backend.h"

/* Captured by GS_InitGraph / GS_SetDispMode and consumed by
   GS_SetEnv, since gsKit needs the full setup in one shot. */
static int _gs_mode      = GS_NTSC;
static int _gs_interlace = GS_NONINTERLACE;
static int _gs_dispx     = 0;
static int _gs_dispy     = 0;
static int _gs_width     = 256;
static int _gs_height    = 240;

void GS_InitGraph(int mode, int interlace)
{
    _gs_mode      = mode;
    _gs_interlace = interlace;
}

void GS_SetDispMode(int dx, int dy, int width, int height)
{
    _gs_dispx  = dx;
    _gs_dispy  = dy;
    _gs_width  = width;
    _gs_height = height;
}

void GS_SetEnv(int width, int height, int fbp1, int fbp2,
               int psm, int zbp, int zbpsm)
{
    /* fbp1 / fbp2 / zbp are kept here for ABI compatibility but the
       gsKit path picks the framebuffer addresses itself. The
       previously hardcoded layout would have placed FB0 / FB1 / Z
       early in VRAM; gsKit's allocator does the same so this is
       behaviourally equivalent. */
    (void)fbp1;
    (void)fbp2;
    (void)zbp;

    _gs_width  = width;
    _gs_height = height;

    GSK_Init(_gs_width, _gs_height,
             _gs_dispx, _gs_dispy,
             psm, zbpsm,
             _gs_mode, _gs_interlace);
}

u64 GS_GetFrameReg(void)
{
    GSGLOBAL *gs = GSK_GetGlobal();
    if (!gs) {
        return 0;
    }
    /* FBP is in 8KB pages. ScreenBuffer[] holds byte offsets. */
    return GS_SET_FRAME(
        gs->ScreenBuffer[gs->ActiveBuffer & 1] / 8192,
        gs->Width / 64,
        gs->PSM,
        0);
}

u64 GS_GetOffsetReg(void)
{
    GSGLOBAL *gs = GSK_GetGlobal();
    if (!gs) {
        return 0;
    }
    /* gsKit centres the draw window in the 4096-pixel GS coord
       space; OffsetX / OffsetY are 4-bit fixed-point pixel offsets
       into that space, exactly what the XYOFFSET register expects. */
    return GS_SET_XYOFFSET(gs->OffsetX, gs->OffsetY);
}
