/* gskit_backend.c
 *
 * gsKit-based replacement for the original direct-GS pipeline.
 * See gskit_backend.h for the public API.
 *
 * Fase 1 GS->gsKit migration.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <gsKit.h>
#include <dmaKit.h>
#include <gsInline.h>
#include <gsToolkit.h>

#include "types.h"
#include "ps2dma.h"
#include "gs.h"
#include "gskit_backend.h"
#include "gpprim.h"

/* Physical framebuffer size.
 *
 * 640x448 is the only resolution that survives both the native NTSC /
 * PAL PCRTC magnification *and* the resolution-forcing performed by
 * OPL's GSM (480i / 480p / 720i / 1080i) and by passive PS2toHDMI
 * cables that latch the GS into 480p.  All of those PCRTC programmings
 * read 640 visible pixels per scanline, so the framebuffer must be at
 * least that wide; the height covers full-frame NTSC 480 and the
 * useful part of PAL 512 (the top/bottom 32 lines are inside overscan
 * on every TV we know of).
 *
 * The UI / SNES blit still drives the renderer in the legacy 256x240
 * logical coordinate space; gpprim.c scales that 1:1 -> physical via
 * GPPrimSetScale below.  See the long comment at the top of gpprim.c
 * for the rationale. */
#define GSK_FB_WIDTH    640
#define GSK_FB_HEIGHT   448

/* Legacy logical coordinate space the entire UI was written in. */
#define GSK_LOGICAL_W   256
#define GSK_LOGICAL_H   240

/* The original headers use these constants for mode / interlace. They
   live in gs.h but we want this TU to compile without dragging the
   register-level header in, so re-declare the values that match. */
#ifndef GS_NTSC
#define GS_NTSC          2
#define GS_PAL           3
#define GS_INTERLACE     1
#define GS_NONINTERLACE  0
#endif

static GSGLOBAL *_pGsGlobal = NULL;
static int       _gsk_initialised = 0;
static int       _gsk_invalidate_pending = 0;

/* Video mode + display offset (selectable in the Settings screen).
   Default is 480i (interlaced) - the only mode NetherSX2 runs at 60fps
   (it runs progressive NTSC / 240p at 30Hz).  240p is 60fps on real
   PS2/CRT but 30fps on the emulator, so it stays opt-in. */
int g_GskVideoMode = GSK_VIDMODE_480I;
int g_GskDispOffX  = 0;
int g_GskDispOffY  = 0;
int g_GskOverscan  = 0;   /* 0..100 shrink of display area */
static int _gsk_vck         = 4;   /* display-offset VCK units            */
static int _gsk_fb_width    = 640; /* active FB width                     */
static int _gsk_fb_height   = 448; /* active FB height                    */
static int _gsk_active_mode = GSK_VIDMODE_480I; /* mode the GS is in now   */

/* gsKit's computed DISPLAY params, captured after gsKit_init_screen so
   overscan/widescreen can be recomputed from a clean baseline. */
static int _gsk_base_dw, _gsk_base_dh, _gsk_base_magh, _gsk_base_magv;
static int _gsk_base_startx, _gsk_base_starty;

static void _GskApplyDisplay(void);   /* offset + overscan + widescreen */

/* Saved GSK_Init arguments so GSK_ReinitVideo() can replay them. */
static int _gsk_arg_w, _gsk_arg_h, _gsk_arg_dispx, _gsk_arg_dispy;
static int _gsk_arg_psm, _gsk_arg_psmz, _gsk_arg_mode, _gsk_arg_interlace;

GSGLOBAL *GSK_GetGlobal(void)
{
    return _pGsGlobal;
}

void GSK_Init(int width, int height,
              int dispx, int dispy,
              int psm, int psmz,
              int mode, int interlace)
{
    if (_gsk_initialised) {
        return;
    }

    _gsk_arg_w     = width;  _gsk_arg_h        = height;
    _gsk_arg_dispx = dispx;  _gsk_arg_dispy    = dispy;
    _gsk_arg_psm   = psm;    _gsk_arg_psmz     = psmz;
    _gsk_arg_mode  = mode;   _gsk_arg_interlace = interlace;

    _pGsGlobal = gsKit_init_global();
    if (!_pGsGlobal) {
        return;
    }

    /* Map iaddis-style mode constants to gsKit's enum.
       The original code uses GS_NTSC=2 / GS_PAL=3 which happen to
       match GS_MODE_NTSC / GS_MODE_PAL exactly, but go through the
       check anyway in case a caller passes something else. */
    _pGsGlobal->Mode = (mode == GS_PAL) ? GS_MODE_PAL : GS_MODE_NTSC;

    /* Force interlaced output regardless of what the caller asked for.
     *
     * The legacy code defaulted to GS_NONINTERLACED (240p / 288p) because
     * the original 256x240 framebuffer fit a 240p PCRTC scanout exactly.
     * That choice falls apart on two of our reported setups:
     *
     *   - PS2toHDMI cables / modern TVs:  Most passive adapters refuse
     *     to lock onto 240p (no signal / black screen) and even active
     *     ones (Pound HDMI, Hyperkin) often misdetect 240p as 480i and
     *     deinterlace the wrong way, producing dropped frames or jitter.
     *   - OPL GSM 480p / 1080i:  GSM rewrites SMODE2 / DISPLAY to a
     *     progressive mode and computes its DISPLAY adaptation against
     *     the assumption that the source framebuffer is the standard
     *     480-line interlaced layout (DH=479).  Driving a 240p source
     *     through that path gives a half-height picture with red/green
     *     stripes (the symptom Adriano photographed).
     *
     * 480i is the safe lowest-common-denominator: CRTs handle it
     * natively, every PS2toHDMI cable expects it as the primary input,
     * and GSM's adaptation tables target it directly. */
    (void)interlace;
    switch (g_GskVideoMode)
    {
    case GSK_VIDMODE_480P:
        /* DTV 480p progressive: what OPL GSM / PS2toHDMI expect natively,
           so the PCRTC does no interlace conversion (no RGB stripes). */
        _pGsGlobal->Mode      = GS_MODE_DTV_480P;
        _pGsGlobal->Interlace = GS_NONINTERLACED;
        _pGsGlobal->Field     = GS_FRAME;
        _gsk_fb_width         = 640;
        _gsk_fb_height        = 480;
        _gsk_vck              = 2;
        break;

    case GSK_VIDMODE_480I:
        /* NTSC 640x448 interlaced (the previous default). */
        _pGsGlobal->Mode      = GS_MODE_NTSC;
        _pGsGlobal->Interlace = GS_INTERLACED;
        _pGsGlobal->Field     = GS_FIELD;
        _gsk_fb_width         = 640;
        _gsk_fb_height        = 448;
        _gsk_vck              = 4;
        break;

    case GSK_VIDMODE_240P:
    default:
        /* NTSC 320x240 progressive (4:3), 60Hz - native SNES/NES.
           GS_FIELD (not GS_FRAME): on NTSC, FRAME mode runs at the 30Hz
           frame rate while FIELD runs at the 60Hz field rate, so 240p
           must use FIELD to get 60fps. (480p/DTV is a dedicated 60Hz
           progressive mode, so it keeps GS_FRAME.) */
        _pGsGlobal->Mode      = GS_MODE_NTSC;
        _pGsGlobal->Interlace = GS_NONINTERLACED;
        _pGsGlobal->Field     = GS_FIELD;
        _gsk_fb_width         = 320;
        _gsk_fb_height        = 240;
        _gsk_vck              = 2;
        break;
    }
    _gsk_active_mode = g_GskVideoMode;

    /* Force the framebuffer dimensions to 640x448 regardless of what
     * the caller passed.  See the GSK_FB_WIDTH/HEIGHT defines above and
     * the long comment at the top of gpprim.c for the rationale.
     *
     * The width/height arguments are still honoured below via
     * GPPrimSetScale so the existing 256x240 UI layout is preserved. */
    (void)width;
    (void)height;
    _pGsGlobal->Width  = _gsk_fb_width;
    _pGsGlobal->Height = _gsk_fb_height;
    _pGsGlobal->PSM    = psm;
    _pGsGlobal->PSMZ   = psmz;

    /* Match the original code's behaviour: no Z buffer is needed for
       the 2D blit-style rendering this app does. The Z buffer in the
       old draw_env was only there because GS_SetEnv set it up; the
       actual prims always use TEST_1 with Z disabled. */
    _pGsGlobal->ZBuffering      = GS_SETTING_OFF;
    _pGsGlobal->DoubleBuffering = GS_SETTING_ON;
    _pGsGlobal->PrimAAEnable    = GS_SETTING_OFF;
    _pGsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    _pGsGlobal->Dithering       = GS_SETTING_OFF;
    _pGsGlobal->DrawOrder       = GS_PER_OS;

    /* DMA setup. The SNES blender (snppublend_gs.cpp) also kicks raw
       DMA chains on the GIF channel; gsKit and the blender share the
       same channel, so they must be serialised via GSK_DrainAndWait. */
    /* RCYC=8, no source/dest stall, no MFIFO. The "STS" channel is
       UNSPEC because we do not opt into source-stall behaviour. */
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    /* Zero the entire 4MB of GS VRAM before gsKit_init_screen allocates
       and programs the framebuffer. This matches the canonical sequence
       used by picodrive's PS2 port (irixxxx fork, platform/ps2/emu.c
       around video_init): gsKit_set_clamp -> gsKit_vram_clear ->
       gsKit_init_screen.

       Why it matters here, even though the previous code worked on
       PCSX2 / NetherSX2 / a CRT TV without it:

       The framebuffer this app uses is 256x240 (NES native), but users
       running through OPL's GSM (Graphics Synthesizer Mode selector)
       force the GS into 480p / 720i / 1080i and rely on the PCRTC
       magnifying our 256x240 surface to fill the screen. The PCRTC,
       when magnifying past the actual framebuffer width/height, keeps
       reading neighbouring VRAM pixels -- and on real PS2 hardware
       those pixels start out as whatever the BIOS / uLaunchELF / OPL
       left there, which gsKit_init_screen's own register programming
       does not touch outside our 256x240 region.

       Symptom on the user's setup (Adriano, modern TV via PS2toHDMI
       cable + GSM forcing 480p): a strip of red and green vertical
       bars surrounding the menu area, and the boot appearing to
       freeze on that frame. Same root cause that killed Open-PS2-
       Loader's GSM compatibility for several emulator forks until
       they adopted the picodrive pattern.

       On a CRT (Yamark) without GSM the PCRTC only reads inside the
       256x240 window, so o leftover VRAM is invisible -- which is
       why the bug only showed up for users with HDMI converters /
       progressive scan / OPL GSM. */
    gsKit_vram_clear(_pGsGlobal);

    gsKit_init_screen(_pGsGlobal);

    /* Capture gsKit's computed DISPLAY params as the baseline for the
       overscan / widescreen transform. */
    _gsk_base_dw     = _pGsGlobal->DW;
    _gsk_base_dh     = _pGsGlobal->DH;
    _gsk_base_magh   = _pGsGlobal->MagH;
    _gsk_base_magv   = _pGsGlobal->MagV;
    _gsk_base_startx = _pGsGlobal->StartX;
    _gsk_base_starty = _pGsGlobal->StartY;

    /* Apply offset + overscan + widescreen (0/off = gsKit's defaults). */
    _GskApplyDisplay();

    /* Map the legacy 256x240 logical coordinate space used by every UI
     * call site (PolyRect, FontPuts, browser, modals, menu, ...) onto
     * the physical framebuffer.  See the long comment at the top of
     * gpprim.c for the full rationale. */
    GPPrimSetScale((float)_gsk_fb_width  / (float)GSK_LOGICAL_W,
                   (float)_gsk_fb_height / (float)GSK_LOGICAL_H);

    /* PMODE / DISPLAY1 / DISPLAY2 are now left at the values that
       gsKit_init_screen programmed (PMODE=0x8046 with CRTMD=1,
       DISPLAY1/2 with gsKit's auto-computed magnification). The
       previous code re-emitted those three registers with the
       iaddis legacy layout (PMODE=0xFF61, DW=2560, MagV=0) to
       work around a NetherSX2-only artefact, but that combination
       puts the PCRTC in a non-standard mode (CRTMD=0, EN1=1,
       EN2=0) that the real PS2 silicon does not handle the same
       way as emulators - on real hardware the picture comes up
       small in the centre with vertical-stripe garbage around it,
       because gsKit_sync_flip only updates DISPFB2 and DISPFB1
       (the only one being read with EN1=1, EN2=0) is left at its
       initial value, breaking double buffering.

       picodrive and Open-PS2-Loader both let gsKit handle PMODE
       and DISPLAY entirely, and both render correctly on real
       PS2. We follow the same pattern.

       If the NetherSX2 visual issue resurfaces, gate the override
       behind a build flag (e.g. -DBUILD_FOR_NETHERSX2=1) instead
       of penalising real hardware. */
    (void)dispx;
    (void)dispy;
    (void)width;
    (void)height;

    /* COLCLAMP is re-emitted every frame in GSK_ResetFrame (see
       comment there). The original iaddis pipeline (gs.c) set
       COLCLAMP=1 as part of GS_SetEnv but gsKit_init_screen does
       not touch it, so it sits at the GS reset default (0). */

    gsKit_set_test (_pGsGlobal, GS_ZTEST_OFF);
    gsKit_set_clamp(_pGsGlobal, GS_CMODE_REPEAT);
    gsKit_set_primalpha(_pGsGlobal,
        GS_SETREG_ALPHA(0, 1, 0, 1, 0x80), 0); /* Cs*As + Cd*(1-As) */

    gsKit_TexManager_init(_pGsGlobal);
    gsKit_mode_switch(_pGsGlobal, GS_ONESHOT);

    /* Clear both buffers so the screen starts black. */
    gsKit_clear(_pGsGlobal, 0);
    gsKit_queue_exec(_pGsGlobal);
    gsKit_finish();
    gsKit_sync_flip(_pGsGlobal);
    gsKit_clear(_pGsGlobal, 0);
    gsKit_queue_exec(_pGsGlobal);
    gsKit_finish();
    gsKit_sync_flip(_pGsGlobal);

    _gsk_initialised = 1;
}

static void _GskApplyDisplay(void)
{
    GSGLOBAL *gs = _pGsGlobal;
    int dw, dh, magh, startx, starty;

    if (!_gsk_initialised || !gs) {
        return;
    }

    dw     = _gsk_base_dw;
    dh     = _gsk_base_dh;
    magh   = _gsk_base_magh;
    startx = _gsk_base_startx;
    starty = _gsk_base_starty;

    /* Overscan: shrink the active area and recentre (adds a border to
       compensate TVs that crop the edges). */
    if (g_GskOverscan > 0)
    {
        int sx = (_gsk_base_dw * g_GskOverscan) / 1300;
        int sy = (_gsk_base_dh * g_GskOverscan) / 1300;
        dw     = _gsk_base_dw - sx * 2;
        dh     = _gsk_base_dh - sy * 2;
        startx = _gsk_base_startx + sx;
        starty = _gsk_base_starty + sy;
    }

    gs->DW     = dw;
    gs->DH     = dh;
    gs->MagH   = magh;
    gs->MagV   = _gsk_base_magv;
    gs->StartX = startx;
    gs->StartY = starty;

    /* Re-emit DISPLAY1/2 (also folds in the user X/Y offset). */
    gsKit_set_display_offset(gs, g_GskDispOffX * _gsk_vck, g_GskDispOffY);
}

void GSK_SetDisplayOffset(int x, int y)
{
    g_GskDispOffX = x;
    g_GskDispOffY = y;
    _GskApplyDisplay();
}

void GSK_SetOverscan(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    g_GskOverscan = percent;
    _GskApplyDisplay();
}

void GSK_ReinitVideo(void)
{
    if (!_gsk_initialised) {
        return;
    }
    /* Allow GSK_Init to run again; it re-programs the PCRTC and
       reallocates VRAM for the (possibly new) g_GskVideoMode.  The
       caller is responsible for re-uploading its textures (FontInit)
       since the VRAM allocator is reset. */
    _gsk_initialised = 0;
    GSK_Init(_gsk_arg_w, _gsk_arg_h, _gsk_arg_dispx, _gsk_arg_dispy,
             _gsk_arg_psm, _gsk_arg_psmz, _gsk_arg_mode, _gsk_arg_interlace);
}

int GSK_GetActiveVideoMode(void)
{
    return _gsk_active_mode;
}

Uint32 GSK_VramAllocTBP(Uint32 nBytes)
{
    u32 addr;

    if (!_gsk_initialised || !_pGsGlobal) {
        return 0;
    }

    addr = gsKit_vram_alloc(_pGsGlobal, nBytes, GSKIT_ALLOC_USERBUFFER);
    if (addr == GSKIT_ALLOC_ERROR) {
        return 0;
    }
    return addr / 256;
}

void GSK_DrainAndWait(void)
{
    if (!_gsk_initialised) {
        return;
    }

    gsKit_queue_exec(_pGsGlobal);
    gsKit_finish();
    DmaSyncGIF();
}

void GSK_FlushFrame(void)
{
    if (!_gsk_initialised) {
        return;
    }

    gsKit_queue_exec(_pGsGlobal);
    gsKit_finish();
}

void GSK_SyncFlip(void)
{
    if (!_gsk_initialised) {
        return;
    }

    gsKit_sync_flip(_pGsGlobal);
}

void GSK_ResetFrame(void)
{
    GSGLOBAL *gs;
    u64 *p_data;

    if (!_gsk_initialised || !_pGsGlobal) {
        return;
    }

    gs = _pGsGlobal;

    /* Allocate a four-register A+D GIF tag in gsKit's heap.  The
       queue will dispatch it before any subsequent prim, so FRAME_1,
       XYOFFSET_1, ALPHA_1 and COLCLAMP are all refreshed before
       drawing actually happens.

       XYOFFSET_1 must be restored here because the SNES per-scanline
       blender (snppublend_gs.cpp) overwrites it on every Exec() call
       with a line-specific value (0x8000, 0x8000 - iLine*16).  The
       blender's End() restores it through the GPFifo chain, but that
       chain is dispatched *after* gsKit's queue has already drained
       (see GPFifoPause → GSK_DrainAndWait ordering).  Any gsKit
       textured prim queued between End() and GPFifoFlush therefore
       draws with the blender's stale XYOFFSET, which shifts the
       sprite hundreds of pixels off-screen — the visible symptom is a
       permanently frozen menu image because the game output never
       lands inside the visible framebuffer area.

       ALPHA_1 must be restored here for the symmetric reason:  the
       blender's per-scanline DMA chain rewrites ALPHA_1 several times
       (snppublend_gs.cpp _SNPPUBlendBuildList) and leaves it at
       GS_SET_ALPHA(1, 2, 0, 2, 0x80), i.e. output = (Cd - 0) * As + 0
       = Cd * As.  The blender's End() does *not* restore ALPHA_1, and
       gsKit's prim helpers (gsKit_prim_sprite,
       gsKit_prim_sprite_texture_3d, ...) emit only PRIM / color / XY
       per draw — they never re-emit ALPHA_1.  The gsKit init value
       set via gsKit_set_primalpha (GS_SETREG_ALPHA(0, 1, 0, 1, 0x80)
       = standard (Cs - Cd) * As + Cd) therefore stays clobbered for
       the rest of the session.  Any subsequent gsKit prim drawn with
       ABE = 1 (every font draw, every PolyBlend(TRUE) rect, the menu
       selection bar, the "SRAM saved." modal, ...) ends up computing
       output = Cd, which leaves the framebuffer unchanged and makes
       the entire menu overlay invisible — the visible symptom on the
       L2+R2 game-exit path is a frozen darkened game frame with no
       menu UI on top, while audio and input keep responding.  This
       is the same class of bug PR #60 fixed for FRAME_1 / XYOFFSET_1
       but in the opposite (game → menu) direction. */
    /* COLCLAMP = 1 clamps per-channel alpha-blend / colour-math
       results to 0..255.  The GS reset default is 0 (wrap on
       overflow); the original iaddis pipeline (gs.c) programmed
       COLCLAMP = 1 in GS_SetEnv but the gsKit migration dropped
       that write.  Without it, any final composition draw that
       saturates a channel (sprite or BG2/BG3 region overlapping
       BG1 with alpha) ends up wrapping the high bits, which on
       the visible framebuffer appears as banded/striped corruption
       in those regions while BG1-only pixels (the borders) stay
       intact.  Restoring it per-frame here matches the cadence of
       the FRAME / XYOFFSET / ALPHA restores. */
    p_data = (u64 *)gsKit_heap_alloc(gs, 4, 64, GIF_AD);
    if (!p_data) {
        return;
    }

    *p_data++ = GIF_TAG_AD(4);
    *p_data++ = GIF_AD;
    *p_data++ = GS_SETREG_FRAME_1(
        gs->ScreenBuffer[gs->ActiveBuffer & 1] / 8192,
        gs->Width / 64,
        gs->PSM,
        0);
    *p_data++ = GS_REG_FRAME_1;
    *p_data++ = GS_SETREG_XYOFFSET_1(gs->OffsetX, gs->OffsetY);
    *p_data++ = GS_XYOFFSET_1;
    /* Standard alpha blend: output = (Cs - Cd) * As + Cd.  Matches the
       value gsKit_set_primalpha() programmed at GSK_Init() time. */
    *p_data++ = GS_SETREG_ALPHA(0, 1, 0, 1, 0x80);
    *p_data++ = GS_REG_ALPHA_1;
    /* COLCLAMP = 1 (clamp).  Register 0x46 takes a single bit. */
    *p_data++ = (u64)1;
    *p_data++ = (u64)GS_REG_COLCLAMP;
}

void GSK_InvalidateTextureCache(void)
{
    _gsk_invalidate_pending = 1;
}

/* Internal: callers in gpprim.c should consult this and emit a
   TEXFLUSH register write before binding a texture if a blender
   chain has run since the last bind. */
int GSK_TakeInvalidatePending(void)
{
    int p = _gsk_invalidate_pending;
    _gsk_invalidate_pending = 0;
    return p;
}

void *GSK_AsUncached(void *ptr)
{
    Uint32 addr = (Uint32)ptr;

    /* NULL stays NULL. */
    if (!addr) {
        return ptr;
    }

    /* Only KSEG0 / KUSEG cached pointers (top nibble 0x0..0x1, i.e.
       byte address < 0x20000000) can be aliased through KSEG1 by
       setting bit 29. Anything in 0x20000000+ is already uncached
       (KSEG1) or is a kernel/io segment that must not be touched
       through the alias trick.

       PS2 main RAM is 32MB at 0x00000000-0x01FFFFFF, so any legitimate
       pointer into a buffer the EE allocates falls well below the
       0x10000000 threshold. We assert a tighter bound (<256MB) to
       catch accidental use with stack/scratchpad/IO addresses while
       still allowing future memory-map changes. The assert is
       compile-out in CODE_RELEASE so it has zero hot-path cost. */
    assert((addr & 0xF0000000) == 0 &&
           "GSK_AsUncached: pointer is outside physical RAM (<256MB)");

    return (void *)(addr | 0x20000000);
}
