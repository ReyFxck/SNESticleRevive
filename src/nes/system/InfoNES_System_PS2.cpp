/* InfoNES_System_PS2.cpp - PS2 platform layer for InfoNES.
 *
 * InfoNES_System.h declares a contract that every InfoNES platform must
 * satisfy: input poll, framebuffer flip, sound output, memcpy/memset
 * trampolines, debug print, etc. This file is the PS2 implementation
 * plus a one-frame stepper used by NesSystem::ExecuteFrame.
 *
 * Phase 3 status:
 *   - InfoNES_PadState  - reads g_pNesInputState (SNES bit layout) and
 *                         remaps to NES PAD1_Latch / PAD2_Latch.
 *   - InfoNES_LoadFrame - converts WorkFrame[256*240] (RGB555) into
 *                         g_pNesTargetSurface (RGBA8 256x256).
 *   - InfoNES_RunOneFrame - inlined InfoNES_Cycle body, bounded to a
 *                         single NES frame (262 scanlines).  Called by
 *                         NesSystem::ExecuteFrame.
 *   - InfoNES_MemoryCopy / MemorySet - libc trampolines.
 *   - InfoNES_DebugPrint / MessageBox - printf.
 *   - InfoNES_Sound*    - still no-op stubs (Phase 4).
 *
 * The NesPalette[] table is the 64-entry NES master palette in RGB555
 * form that InfoNES uses internally. Values from upstream InfoNES.
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


/* ---- NES master palette, RGB555 (standard NES palette, R high bits) -
 *
 * Upstream InfoNES ships an idiosyncratic palette (NesPalette[0]=0x39ce
 * for the universal backdrop, which decodes to a magenta/pink instead
 * of the black/dark-grey every other NES emulator uses).  That was the
 * cause of the bright-pink sky in Super Mario Bros 3 vs. the black sky
 * shown by RetroArch / FCEUX / Mesen.
 *
 * The values below are the standard FCEUX-style master palette
 * (8-bit RGB triples documented at https://emudev.de and
 * https://www.nesdev.org/nespal.txt) converted to RGB555 with R in the
 * top 5 bits, G in the middle 5, B in the bottom 5 -- the same bit
 * order InfoNES's WorkFrame[] uses.
 */
WORD NesPalette[ 64 ] =
{
  0x3def, 0x001f, 0x0017, 0x20b7, 0x4810, 0x5404, 0x5440, 0x4440,
  0x28c0, 0x01e0, 0x01a0, 0x0160, 0x010b, 0x0000, 0x0000, 0x0000,
  0x5ef7, 0x01ff, 0x017f, 0x351f, 0x6c19, 0x700b, 0x7ce0, 0x7162,
  0x55e0, 0x02e0, 0x02a0, 0x02a8, 0x0231, 0x0000, 0x0000, 0x0000,
  0x7fff, 0x1eff, 0x363f, 0x4dff, 0x7dff, 0x7d73, 0x7deb, 0x7e88,
  0x7ee0, 0x5fe3, 0x2f6a, 0x2ff3, 0x03bb, 0x3def, 0x0000, 0x0000,
  0x7fff, 0x539f, 0x5eff, 0x6eff, 0x7eff, 0x7e98, 0x7b56, 0x7f95,
  0x7f6f, 0x6fef, 0x5ff7, 0x5ffb, 0x03ff, 0x7f7f, 0x0000, 0x0000
};


/* ---- 5-bit -> 8-bit expansion LUT --------------------------------- *
 *
 * The RGB555 -> RGBA8 conversion in InfoNES_LoadFrame needs to expand
 * each 5-bit channel to 8 bits.  The mathematically-correct formula is
 * (v << 3) | (v >> 2), which evaluates 0x1F -> 0xFF and 0 -> 0.  Doing
 * that math inline runs ~245k arithmetic ops per frame (256*240*3 +
 * the OR), which on the EE is enough latency to matter at 60 fps.
 *
 * Precomputing the 32 outputs once and indexing them by the 5-bit
 * channel removes those shifts from the hot path and lets the compiler
 * fold the inner loop into a few loads + a single SW (store-word). */
static Uint8 Lut5to8[ 32 ];

static void _InitLut5to8(void)
{
    for (int i = 0; i < 32; i++)
        Lut5to8[i] = (Uint8)((i << 3) | (i >> 2));
}


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
 * by InfoNES_DrawLine).  WorkFrame[256*240] is in RGB555 (5 bits per
 * channel, LSB = blue, MSB = unused) thanks to NesPalette being in
 * that format and PalTable[] mirroring it.
 *
 * Target is a 256x256 RGBA8 surface (mainloop_init.cpp:274 allocates
 * _fbTexture[] as PIXELFORMAT_RGBA8).  We write the NES visible 240
 * lines and leave 16 padding lines below as black (they're outside
 * the on-screen quad in mainloop_render.cpp anyway).
 *
 * Bit layout:
 *   RGB555: 0 RRRRR GGGGG BBBBB
 *   RGBA8:  RR GG BB AA (little-endian: R is first byte at offset 0).
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

    /* One-time init of the 5->8 expansion LUT. Cheap to re-check. */
    static int s_bLutReady = 0;
    if (!s_bLutReady) { _InitLut5to8(); s_bLutReady = 1; }

    const Uint8 *pLut = Lut5to8;

    /* Convert 240 NES lines into the top 240 rows of the texture.
       The inner loop writes one Uint32 per pixel (single SW on EE)
       and pulls the 5->8 expansion out into a 32-entry LUT so the
       only arithmetic per pixel is three shifts + one OR + one
       table-indexed load per channel. */
    for (Uint32 iY = 0; iY < NES_DISP_HEIGHT; iY++)
    {
        Uint8 *pDstBytes = pTarget->GetLinePtr((Int32)iY);
        if (!pDstBytes) continue;

        Uint32 *pDst = (Uint32 *)pDstBytes;
        const WORD *pSrc = &WorkFrame[iY * NES_DISP_WIDTH];

        for (Uint32 iX = 0; iX < NES_DISP_WIDTH; iX++)
        {
            WORD w = pSrc[iX];

            /* RGB555 -> RGBA8 (R = lowest byte, A = highest byte
               in the Uint32 little-endian word, matching what the
               SNES PPU writes to the same surface). */
            Uint32 r8 = pLut[(w >> 10) & 0x1F];
            Uint32 g8 = pLut[(w >>  5) & 0x1F];
            Uint32 b8 = pLut[ w        & 0x1F];

            pDst[iX] = 0xFF000000u | (b8 << 16) | (g8 << 8) | r8;
        }
    }

    /* Black out the 16 padding rows below the NES image (texture is
       256 high, NES is 240).  Without this they'd hold stale pixels
       from whatever ran last (SNES PPU, menu surface, etc.). */
    for (Uint32 iY = NES_DISP_HEIGHT; iY < uHeight; iY++)
    {
        Uint8 *pDst = pTarget->GetLinePtr((Int32)iY);
        if (pDst) memset(pDst, 0, uWidth * 4);
    }
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
static DWORD MapSnesToNes(Uint16 snes)
{
    DWORD nes = 0;
    if (snes == EMUSYS_DEVICE_DISCONNECTED) return 0;

    if (snes & (SNESIO_JOY_B | SNESIO_JOY_X))      nes |= 0x01; /* A=jump */
    if (snes & (SNESIO_JOY_A | SNESIO_JOY_Y))      nes |= 0x02; /* B=run  */
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

/* NES APU output sample rate, captured by InfoNES_SoundOpen (44100 by
   default in this build).  Used to resample at a constant ratio to the
   mix buffer's input rate so the pitch is correct. */
static int s_NesSampleRate = 44100;

void InfoNES_SoundInit( void )
{
}

int InfoNES_SoundOpen( int samples_per_sync, int sample_rate )
{
    /* The audsrv/SPU2 stream is brought up by the platform at boot
       (SjPCM_Init in mainloop_iop.cpp) and handed to us per-frame as
       g_pNesMixBuffer, so there is nothing to open here.  We DO keep
       the NES sample rate: InfoNES_SoundOutput needs it to resample at
       a constant ratio (NES rate -> mix-buffer rate) so the pitch is
       correct. */
    (void)samples_per_sync;
    s_NesSampleRate = (sample_rate > 0) ? sample_rate : 44100;
    return 1;
}

void InfoNES_SoundClose( void )
{
}

/* Channel-mix tuning.  Per-sample byte ranges produced by this InfoNES
   build: square1/2 0..255, triangle 0..255, noise 0..15, dpcm 0..127
   (summed ~0..900, unsigned).  We subtract a DC centre and scale into
   signed 16-bit with clamping.  Adjust if the mix is too quiet / loud
   or clips. */
#define NES_MIX_CENTER  280
#define NES_MIX_SCALE   36

/* InfoNES hands us the five APU channels as separate byte buffers once
   per frame (from InfoNES_pAPUVsync).  Mix them down to signed 16-bit
   mono and push them into the SAME CMixBuffer the SNES uses (audsrv
   backend).  Only one system runs at a time, so the SNES audio path is
   untouched.  InfoNES emits a fixed 1/60 s block (samples @ 44100) per
   frame, so we resample at a CONSTANT ratio to the mix buffer's input
   rate (32000) to keep the pitch correct; the audsrv backend drops any
   overflow best-effort, so we don't chase its fill level here. */
void InfoNES_SoundOutput( int samples, BYTE *wave1, BYTE *wave2,
                          BYTE *wave3, BYTE *wave4, BYTE *wave5 )
{
    CMixBuffer  *pMix = g_pNesMixBuffer;
    Int32        nOut;
    int          i;
    static Int16 s_NesMix[1024];
    static Int16 s_NesOut[2048];
    const int    capMix = (int)(sizeof(s_NesMix) / sizeof(s_NesMix[0]));
    const int    capOut = (int)(sizeof(s_NesOut) / sizeof(s_NesOut[0]));

    if (!pMix || samples <= 0)
        return;

    if (samples > capMix)
        samples = capMix;

    /* 1) Mix the five channels -> signed 16-bit mono at the NES rate. */
    for (i = 0; i < samples; i++)
    {
        int s = (int)wave1[i] + (int)wave2[i] + (int)wave3[i]
              + (int)wave4[i] + (int)wave5[i];
        s = (s - NES_MIX_CENTER) * NES_MIX_SCALE;
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        s_NesMix[i] = (Int16)s;
    }

    /* 2) Resample at a CONSTANT ratio (NES rate -> mix-buffer input
          rate) so the pitch is correct and stable.
          NOTE: do NOT use the mix buffer's GetOutputSamples() here.
          That returns a buffer-fill-driven, per-frame-varying count;
          the SNES path can satisfy it because its SPC engine renders
          an arbitrary number of samples on demand, but InfoNES emits a
          fixed 1/60 s (samples) per frame.  Resampling that fixed block
          to a varying count stretches/compresses it every frame and
          warbles the pitch -- exactly the reported symptom. */
    {
        Uint32 mixRate = 32000, mixBits = 16, mixCh = 2;
        int    nesRate = (s_NesSampleRate > 0) ? s_NesSampleRate : 44100;
        pMix->GetFormat(&mixRate, &mixBits, &mixCh);
        if (mixRate == 0) mixRate = 32000;
        nOut = (Int32)(((long long)samples * (long long)mixRate) / (long long)nesRate);
    }
    if (nOut <= 0)
        nOut = samples;
    if (nOut > capOut)
        nOut = capOut;

    if (nOut == samples)
    {
        pMix->OutputSamplesMono(s_NesMix, nOut);
    }
    else
    {
        /* 16.16 fixed-point linear interpolation. */
        unsigned int step = ((unsigned int)samples << 16) / (unsigned int)nOut;
        unsigned int pos  = 0;
        int j;
        for (j = 0; j < nOut; j++)
        {
            int idx  = (int)(pos >> 16);
            int frac = (int)(pos & 0xFFFF);
            int a, b;
            if (idx >= samples) idx = samples - 1;
            a = s_NesMix[idx];
            b = (idx + 1 < samples) ? s_NesMix[idx + 1] : a;
            s_NesOut[j] = (Int16)(a + (((b - a) * frac) >> 16));
            pos += step;
        }
        pMix->OutputSamplesMono(s_NesOut, nOut);
    }

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
