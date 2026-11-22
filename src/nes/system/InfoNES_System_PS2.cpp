/* InfoNES_System_PS2.cpp - PS2 platform layer for InfoNES.
 *
 * InfoNES_System.h declares a contract that every InfoNES platform must
 * satisfy: input poll, framebuffer flip, sound output, memcpy/memset
 * trampolines, debug print, etc. This file is the PS2 implementation
 * plus a one-frame stepper used by NesSystem::ExecuteFrame.
 *
 * Current status:
 *   - InfoNES_PadState  - reads g_pNesInputState (SNES bit layout) and
 *                         remaps to NES PAD1_Latch / PAD2_Latch.
 *   - InfoNES_LoadFrame - presents WorkFrame[256*240], which is drawn
 *                         directly into the RGBA8 target surface.
 *   - InfoNES_RunOneFrame - inlined InfoNES_Cycle body, bounded to a
 *                         single NES frame (262 scanlines).  Called by
 *                         NesSystem::ExecuteFrame.
 *   - InfoNES_MemoryCopy / MemorySet - libc trampolines.
 *   - InfoNES_DebugPrint / MessageBox - printf.
 *   - InfoNES_Sound*    - forwards the cycle-timed 32-kHz NES stream
 *                         to CMixBuffer in converter-safe blocks.
 *
 * NesPalette[] is Mesen2's 64-entry default NTSC 2C02 palette, stored
 * directly in the PS2 RGBA8 surface byte order.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "types.h"

#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_Types.h"
#include "K6502.h"

#include "emuinput.h"     /* Emu::SysInputT */
#include "rendersurface.h"
#include "pixelformat.h"
#include "mixbuffer.h"    /* CMixBuffer (audsrv-backed audio sink) */

/* SNES bit layout we need to translate FROM.  _MainLoopInput is still
   wired to _MainLoopSnesInput at this point so every connected pad in
   SysInputT.uPad[i] uses these bits regardless of which emulator is
   running. Phase 4 will route per-system. */
#include "snio.h"
#include "prof.h"   /* PROF_ENTER/LEAVE (no-ops unless PROFILE=1) */


/* Per-frame state owned by nessystem.cpp; we just read it.

   InfoNES is compiled as C++ (.cpp files), so plain `extern` is enough
   here - everything links with C++ linkage. We deliberately don't use
   `extern "C"` because the InfoNES headers themselves don't, and any
   linkage mismatch on these globals would silently break at link time. */
extern CRenderSurface       *g_pNesTargetSurface;
extern Emu::SysInputT       *g_pNesInputState;
extern CMixBuffer           *g_pNesMixBuffer;

/* SpriteJustHit lives in InfoNES.cpp but isn't externed by InfoNES.h.
   Declare it here so InfoNES_RunOneFrame can mirror the sprite-0 hit
   timing exactly the way InfoNES_Cycle does. */
extern int SpriteJustHit;


/* ---- NTSC NES 2C02 master palette, exact RGBA8 --------------------
 *
 * The previous table was the old high-saturation FCEUX palette reduced to
 * RGB555.  Greens in games such as Side Pocket consequently clipped toward
 * neon green, and every component lost three low bits before reaching the
 * RGBA8 render target.  These are Mesen2's default 2C02 colors stored in the
 * PS2 surface's little-endian [R,G,B,A] byte order.  K6502_rw masks palette
 * writes to the hardware's six-bit color index before looking them up.
 */
unsigned int NesPalette[ 64 ] =
{
  0xff666666u, 0xff882a00u, 0xffa71214u, 0xffa4003bu, 0xff7e005cu, 0xff40006eu, 0xff00066cu, 0xff001d56u,
  0xff003533u, 0xff00480bu, 0xff005200u, 0xff084f00u, 0xff4d4000u, 0xff000000u, 0xff000000u, 0xff000000u,
  0xffadadadu, 0xffd95f15u, 0xffff4042u, 0xfffe2775u, 0xffcc1aa0u, 0xff7b1eb7u, 0xff2031b5u, 0xff004e99u,
  0xff006d6bu, 0xff008738u, 0xff00930cu, 0xff328f00u, 0xff8d7c00u, 0xff000000u, 0xff000000u, 0xff000000u,
  0xfffffeffu, 0xffffb064u, 0xffff9092u, 0xffff76c6u, 0xffff6af3u, 0xffcc6efeu, 0xff7081feu, 0xff229eeau,
  0xff00bebcu, 0xff00d888u, 0xff30e45cu, 0xff82e045u, 0xffdecd48u, 0xff4f4f4fu, 0xff000000u, 0xff000000u,
  0xfffffeffu, 0xffffdfc0u, 0xffffd2d3u, 0xffffc8e8u, 0xffffc2fbu, 0xffeac4feu, 0xffc5ccfeu, 0xffa5d8f7u,
  0xff94e5e4u, 0xff96efcfu, 0xffabf4bdu, 0xffccf3b3u, 0xfff2ebb5u, 0xffb8b8b8u, 0xff000000u, 0xff000000u
};


/* (A 5-bit->8-bit expansion LUT used to live here. The PS2 profiler
 * showed the LUT was actually the bottleneck -- 3 dependent byte loads
 * per pixel stall on the EE's load-use latency -- so InfoNES_LoadFrame
 * now expands inline with (v<<3)|(v>>2), which is cheaper. LUT removed.)
 */


/* ------------------------------------------------------------------ *
 *  InfoNES_RunOneFrame                                                *
 * ------------------------------------------------------------------ *
 * NesSystem::ExecuteFrame calls this to advance the emulator by
 * exactly one NES frame.  It's a stripped-down copy of InfoNES_Cycle
 * from InfoNES.cpp - same instruction stream + HSync calls - but
 * bounded by scanline count so it returns at end-of-frame instead of
 * looping forever waiting for a PAD_SYS_QUIT.
 *
 * One frame = 263 scanlines (SCAN_VBLANK_END is 262 inclusive).  At
 * scanline 240 InfoNES_HSync calls InfoNES_LoadFrame, which writes the
 * fully rendered WorkFrame[] into the target surface.  At scanline
 * 243 (SCAN_VBLANK_START) InfoNES_HSync calls InfoNES_PadState; we
 * also handle NMI on VBlank there.
 *
 * If InfoNES_HSync ever returns -1 (PAD_SYS_QUIT) we break early.
 * Our PadState never sets QUIT so this is just a safety net.
 */
void InfoNES_RunOneFrame(void)
{
    /* One NES frame.  PPU_Scanline wraps from SCAN_VBLANK_END (262)
       back to 0 inside InfoNES_HSync; we just need to step enough
       scanlines that we land back at the start of the next frame. */
    /* Render direto: aponta o WorkFrame para a surface de saida, entao
       o InfoNES_DrawLine escreve RGBA8 direto na textura (sem passada de
       conversao separada). Surface e' 256-wide RGBA8, pitch = 256*4, logo
       WorkFrame[y*256 + x] mapeia exatamente o pixel (x,y). */
    if (g_pNesTargetSurface)
        WorkFrame = (unsigned int *)g_pNesTargetSurface->GetLinePtr(0);

    for (int sl = 0; sl < 263; sl++)
    {
        int nStep;

        if (SpriteJustHit == PPU_Scanline &&
            PPU_ScanTable[PPU_Scanline] == SCAN_ON_SCREEN)
        {
            /* Sprite-0 hit needs the CPU to be advanced to the correct
               X position within the scanline before R2_HIT_SP fires.
               STEP_PER_SCANLINE is 113 PPU dots; SPR_X = sprite-0 X. */
            nStep = SPRRAM[SPR_X] * STEP_PER_SCANLINE / NES_DISP_WIDTH;
            K6502_Step((WORD)nStep);

            if ((PPU_R1 & R1_SHOW_SP) && (PPU_R1 & R1_SHOW_SCR))
                PPU_R2 |= R2_HIT_SP;

            if ((PPU_R0 & R0_NMI_SP) && (PPU_R1 & R1_SHOW_SP))
                NMI_REQ;

            K6502_Step((WORD)(STEP_PER_SCANLINE - nStep));
        }
        else
        {
            K6502_Step((WORD)STEP_PER_SCANLINE);
        }

        /* Frame IRQ counter tick (matches InfoNES.cpp:629-635). */
        FrameStep += STEP_PER_SCANLINE;
        if (FrameStep > STEP_PER_FRAME && FrameIRQ_Enable)
        {
            FrameStep %= STEP_PER_FRAME;
            IRQ_REQ;
            APU_Reg[0x15] |= 0x40;
        }

        /* Per-mapper hsync callback. */
        MapperHSync();

        /* Standard InfoNES per-scanline housekeeping (also draws the
           visible scanline, polls input at VBlank, etc.). */
        if (InfoNES_HSync() == -1)
            break;
    }
}


/* ------------------------------------------------------------------ *
 *  InfoNES_LoadFrame                                                  *
 * ------------------------------------------------------------------ *
 * Called once per visible NES frame from inside InfoNES_HSync (at
 * SCAN_UNKNOWN_START, after every scanline 0..239 has been rendered
 * by InfoNES_DrawLine). WorkFrame points directly at the RGBA8 target;
 * NesPalette and PalTable therefore preserve the full 8-bit components.
 *
 * Target is a 256x256 RGBA8 surface (mainloop_init.cpp:274 allocates
 * _fbTexture[] as PIXELFORMAT_RGBA8).  We write the NES visible 240
 * lines and leave 16 padding lines below as black (they're outside
 * the on-screen quad in mainloop_render.cpp anyway).
 *
 * Surface byte layout: RR GG BB AA (little-endian: R at byte offset 0).
 */
void InfoNES_LoadFrame(void)
{
    CRenderSurface *pTarget = g_pNesTargetSurface;
    if (!pTarget) return;

    PixelFormatT *pFmt = pTarget->GetFormat();
    if (!pFmt || pFmt->uBitDepth != 32) return;

    Uint32 uWidth  = pTarget->GetWidth();
    Uint32 uHeight = pTarget->GetHeight();
    if (uWidth < NES_DISP_WIDTH || uHeight < NES_DISP_HEIGHT) return;

    PROF_ENTER("NesLoadFrame");

    /* InfoNES_DrawLine ja' escreveu RGBA8 direto nas linhas 0..239 desta
       surface (WorkFrame aponta pra ela) -- entao NAO ha' mais passada de
       conversao (era o custo de ~1.7M ciclos que removemos). So' apagamos
       as linhas de padding abaixo da imagem de 240 linhas do NES. */
    for (Uint32 iY = NES_DISP_HEIGHT; iY < uHeight; iY++)
    {
        Uint8 *pDst = pTarget->GetLinePtr((Int32)iY);
        if (pDst) memset(pDst, 0, uWidth * 4);
    }

    PROF_LEAVE("NesLoadFrame");
}


/* ------------------------------------------------------------------ *
 *  InfoNES_PadState                                                   *
 * ------------------------------------------------------------------ *
 * Polled once per frame at SCAN_VBLANK_START from InfoNES_HSync.
 * We read g_pNesInputState (SNES bit layout) and remap into NES
 * controller bits.
 *
 * Standard NES bit order (lowest first, matches the serial protocol
 * that K6502_rw.h::PAD1 reads bit-by-bit):
 *
 *   bit 0 = A         bit 4 = UP
 *   bit 1 = B         bit 5 = DOWN
 *   bit 2 = SELECT    bit 6 = LEFT
 *   bit 3 = START     bit 7 = RIGHT
 *
 * Player mapping (matches how _MainLoopSnesInput already turned the
 * PS2 buttons into SNES bits).  PS2 convention is that the bottom of
 * the diamond (Cross) is the primary action button -- in NES Mario
 * games that's the JUMP button, which is NES A.  Triangle (top) maps
 * to NES A too so a SF-style 4-face controller still works.  Square /
 * Circle map to NES B (run/secondary).
 *
 *   PS2 Cross  / Triangle (= SNES B / SNES X) -> NES A (jump)
 *   PS2 Square / Circle   (= SNES Y / SNES A) -> NES B (run)
 *   PS2 Select / Start                         -> NES Select / Start
 *
 * PAD_System is for emulator-level commands like PAD_SYS_QUIT; we
 * never set it so InfoNES_HSync never breaks out of InfoNES_Cycle on
 * QUIT.  Menu return is handled by _MainLoopInputProcess instead.
 */

static Bool s_TurboPhase = FALSE;

static DWORD MapSnesToNes(Uint16 snes)
{
    DWORD nes = 0;
    if (snes == EMUSYS_DEVICE_DISCONNECTED) return 0;

if (snes & SNESIO_JOY_B)                       nes |= 0x01; /* Cross -> A */
if (snes & SNESIO_JOY_Y)                       nes |= 0x02; /* Square -> B */

if ((snes & SNESIO_JOY_X) && s_TurboPhase)     nes |= 0x02; /* Triangle -> turbo B */
if ((snes & SNESIO_JOY_A) && s_TurboPhase)     nes |= 0x01; /* Circle -> turbo A */
    if (snes &  SNESIO_JOY_SELECT)                  nes |= 0x04; /* SELECT */
    if (snes &  SNESIO_JOY_START)                   nes |= 0x08; /* START  */
    if (snes &  SNESIO_JOY_UP)                      nes |= 0x10;
    if (snes &  SNESIO_JOY_DOWN)                    nes |= 0x20;
    if (snes &  SNESIO_JOY_LEFT)                    nes |= 0x40;
    if (snes &  SNESIO_JOY_RIGHT)                   nes |= 0x80;
    return nes;
}

void InfoNES_PadState( DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem )
{
    s_TurboPhase = !s_TurboPhase;
    Emu::SysInputT *pInput = g_pNesInputState;
    DWORD p1 = 0, p2 = 0;

    if (pInput)
    {
        p1 = MapSnesToNes(pInput->uPad[0]);
        p2 = MapSnesToNes(pInput->uPad[1]);
    }

    if (pdwPad1)   *pdwPad1   = p1;
    if (pdwPad2)   *pdwPad2   = p2;
    if (pdwSystem) *pdwSystem = 0;
}


/* ---------------------------------------------------------------- *
 *  Other platform stubs                                              *
 * ---------------------------------------------------------------- */

int InfoNES_Menu( void )
{
    /* InfoNES_Main() calls this in a loop. Returning -1 tells the core
       to exit gracefully. NesSystem::ExecuteFrame never enters
       InfoNES_Main, so this is dead code today. */
    return -1;
}

int InfoNES_ReadRom( const char *pszFileName )
{
    /* NesSystem hands ROM data directly to the InfoNES globals,
       bypassing InfoNES_Load entirely. Provided only so the link
       resolves. */
    (void)pszFileName;
    return -1;
}

void InfoNES_ReleaseRom( void )
{
}

void *InfoNES_MemoryCopy( void *dest, const void *src, int count )
{
    return memcpy(dest, src, (size_t)count);
}

void *InfoNES_MemorySet( void *dest, int c, int count )
{
    return memset(dest, c, (size_t)count);
}

void InfoNES_DebugPrint( char *pszMsg )
{
    if (pszMsg) printf("%s", pszMsg);
}

void InfoNES_Wait( void )
{
}

/* Nes_Snd_Emu/Blip_Buffer now performs cycle-timed synthesis, band limiting
   and 2A03 channel weighting directly at the frontend's 32-kHz input rate.
   This PS2 layer only keeps up to three samples so AudMixBuffer always gets
   multiples of four (required by its 32->48-kHz 2:3 converter). */
static Int16  s_NesPending[4];
static int    s_NesPendingCount;

void InfoNES_SoundReset( void )
{
    memset(s_NesPending, 0, sizeof(s_NesPending));
    s_NesPendingCount = 0;
}

void InfoNES_SoundInit( void )
{
    InfoNES_SoundReset();
}

int InfoNES_SoundOpen( int samples_per_sync, int sample_rate )
{
    /* audsrv/SPU2 and CMixBuffer are owned by the shared frontend. */
    (void)samples_per_sync;
    (void)sample_rate;
    InfoNES_SoundReset();
    return 1;
}

void InfoNES_SoundClose( void )
{
    InfoNES_SoundReset();
}

void InfoNES_SoundOutputSamples( const short *samples, int count )
{
    CMixBuffer *pMix = g_pNesMixBuffer;
    static Int16 s_NesOut[1028];
    int nOut = s_NesPendingCount;
    int nFlush;
    int i;

    if (!samples || count <= 0)
        return;
    if (count > 1024)
        count = 1024;
    if (!pMix)
    {
        s_NesPendingCount = 0;
        return;
    }

    for (i = 0; i < s_NesPendingCount; i++)
        s_NesOut[i] = s_NesPending[i];
    for (i = 0; i < count; i++)
        s_NesOut[nOut++] = (Int16)samples[i];

    nFlush = nOut & ~3;
    s_NesPendingCount = nOut - nFlush;
    for (i = 0; i < s_NesPendingCount; i++)
        s_NesPending[i] = s_NesOut[nFlush + i];
    if (nFlush > 0)
        pMix->OutputSamplesMono(s_NesOut, nFlush);

    pMix->Flush();
}

void InfoNES_MessageBox( const char *pszMsg, ... )
{
    va_list ap;
    char Buf[1024];

    va_start(ap, pszMsg);
    vsnprintf(Buf, sizeof(Buf), pszMsg, ap);
    va_end(ap);

    printf("[InfoNES] %s\n", Buf);
}
