/* nessystem.cpp - NesSystem implementation (Phase 3).
 *
 * SetRom() feeds the iNES image into InfoNES (NesHeader, ROM, VROM
 * globals + optional CHR RAM) and runs InfoNES_Init/Reset.
 *
 * ExecuteFrame() drives InfoNES for exactly one NES frame and converts
 * WorkFrame[256*240] (RGB555) into the 256x256 RGBA8 render surface
 * that mainloop_process.cpp uploads to _OutTex via TextureUpload.
 *
 * When no ROM is bound (defensive: should never happen given the
 * mainloop dispatch logic) we still paint a diagnostic so that "NES
 * selected but rom failed to load" is distinguishable from a hang or
 * solid-black-crash on NetherSX2.
 *
 * Input mapping (NES has 8 buttons; PS2 user is pressing SNES-shaped
 * bits because _MainLoopInput is still routed through _MainLoopSnesInput)
 * is done in InfoNES_System_PS2.cpp::InfoNES_PadState, reading the
 * SysInputT pointer that this file stashes per frame.  Audio output
 * arrives in Phase 4; today InfoNES_SoundOutput is a no-op stub.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nessystem.h"
#include "rendersurface.h"
#include "surface.h"
#include "pixelformat.h"

/* InfoNES is built as C++ (all .cpp files), so its headers can be
   included normally - no extern "C" wrapper. The headers don't declare
   anything with C linkage either. */
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_Types.h"
#include "K6502.h"

/* Per-frame state shared with the InfoNES platform callbacks
   (InfoNES_System_PS2.cpp).  We stash the surface + input pointer on
   the way into ExecuteFrame so InfoNES_LoadFrame and InfoNES_PadState
   (called from deep inside InfoNES_Cycle) can reach them without us
   having to thread them through the InfoNES core.

   Both pointers are valid ONLY for the duration of one ExecuteFrame
   call; they're cleared back to NULL on the way out.  Re-entrancy is
   impossible (single-threaded EE side). */
CRenderSurface       *g_pNesTargetSurface = NULL;
Emu::SysInputT       *g_pNesInputState    = NULL;
/* Per-frame audio sink, stashed by ExecuteFrame so the InfoNES sound
   callback (InfoNES_SoundOutput) can reach it.  Points at the SAME
   CMixBuffer the SNES uses (audsrv backend); only one system runs at a
   time, so this never collides with the SNES audio path. */
CMixBuffer           *g_pNesMixBuffer     = NULL;

/* One-frame runner.  Defined in InfoNES_System_PS2.cpp so it has direct
   access to InfoNES.cpp's globals (PPU_Scanline, SPRRAM, MapperHSync,
   ...) without us having to re-extern them all here. */
void InfoNES_RunOneFrame(void);

/* The mainloop owns these singletons and the Phase 2 wiring routes both
   the .nes case AND the FDS bios case through Emu::Rom*. RTTI is off so
   we can't dynamic_cast; compare pointer identity instead to decide
   which subclass we were actually handed. */
extern NesRom         *_pNesRom;
extern NesFDSBios     *_pNesFDSBios;
extern NesDisk        *_pNesFDSDisk;


/* -------------------------------------------------------------------- *
 *  Construction / destruction                                          *
 * -------------------------------------------------------------------- */

NesSystem::NesSystem()
{
    m_pNesRom      = NULL;
    m_pNesDisk     = NULL;
    m_pCHRRam      = NULL;
    m_bInitialized = FALSE;
    m_bRomReady    = FALSE;
    m_uFrameTick   = 0;
    m_uFrame       = 0;
    m_uLine        = 0;
}

NesSystem::~NesSystem()
{
    if (m_pCHRRam)
    {
        free(m_pCHRRam);
        m_pCHRRam = NULL;
    }
}


/* -------------------------------------------------------------------- *
 *  SetRom: wire the iNES image into InfoNES globals.                   *
 * -------------------------------------------------------------------- *
 * The iNES file layout (NESDEV reference) is:
 *   16-byte header (already validated by NesRom::LoadRom)
 *   optional 512-byte trainer (header byte 6, bit 2)
 *   PRG ROM, byRomSize x 16 KB
 *   CHR ROM, byVRomSize x 8 KB    (absent when byVRomSize == 0)
 *
 * InfoNES expects:
 *   NesHeader = the 16-byte struct verbatim
 *   ROM       = pointer to start of PRG ROM (post-header, post-trainer)
 *   VROM      = pointer to start of CHR ROM, OR an 8 KB CHR RAM buffer
 *               we own when byVRomSize == 0.
 *
 * Calling InfoNES_Init() is safe to do more than once but we only do it
 * the first time SetRom is hit.  InfoNES_Reset() is per-cart.
 */

void NesSystem::SetRom(Emu::Rom *pRom)
{
    /* Tear down whatever was previously loaded.  pCHRRam stays around
       across carts so the buffer is reused; ROM/VROM globals are
       cleared so a downstream Reset/access on a bogus pointer faults
       loudly. */
    m_pNesRom   = NULL;
    m_pNesDisk  = NULL;
    m_bRomReady = FALSE;

    if (!pRom)
    {
        ROM  = NULL;
        VROM = NULL;
        return;
    }

    /* mainloop_load.cpp routes three different Emu::Rom subclasses
       through SetRom (NesRom for .nes, and NesFDSBios when pBios is
       provided for .fds). Use pointer identity against the mainloop
       singletons since RTTI is disabled. Phase 3 only knows how to
       run iNES carts; FDS support arrives in Phase 5. */
    if (pRom != _pNesRom)
    {
        printf("[NesSystem] SetRom: non-iNES image (Phase 3 supports "
               ".nes only). Leaving InfoNES un-wired.\n");
        return;
    }

    NesRom *pNesRom = (NesRom *)pRom;
    Uint8  *pData   = pNesRom->GetData();
    Uint32  uBytes  = pNesRom->GetBytes();

    if (!pData || uBytes < 16)
    {
        printf("[NesSystem] SetRom: invalid rom (%u bytes)\n", uBytes);
        return;
    }

    if (pData[0] != 'N' || pData[1] != 'E' || pData[2] != 'S' ||
        pData[3] != 0x1A)
    {
        /* Belt-and-braces magic check in case the loader hands us a
           NesRom whose contents are not iNES (corrupt download etc.). */
        printf("[NesSystem] SetRom: missing iNES magic at start of buffer "
               "(rom is %u bytes). Refusing to wire.\n", uBytes);
        return;
    }

    /* 1. Header copy. */
    memcpy(&NesHeader, pData, 16);

    /* 2. PRG ROM pointer. */
    Uint8 *p = pData + 16;
    if (NesHeader.byInfo1 & 0x04)
        p += 512;                                /* skip trainer */
    ROM = p;
    Uint32 uPrgBytes = (Uint32)NesHeader.byRomSize * 16 * 1024;
    p += uPrgBytes;

    /* 3. CHR ROM pointer, or fall back to an 8 KB CHR RAM scratchpad
       we own.  Some classic games (Donkey Kong, SMB1, Tetris) ship CHR
       ROM and have byVRomSize > 0; many later titles (SMB2/3, Mega Man)
       use CHR RAM and rely on the mapper to write into VROM. */
    if (NesHeader.byVRomSize > 0)
    {
        VROM = p;
    }
    else
    {
        if (!m_pCHRRam)
            m_pCHRRam = (Uint8 *)malloc(8 * 1024);
        if (!m_pCHRRam)
        {
            printf("[NesSystem] SetRom: out of memory for CHR RAM\n");
            return;
        }
        memset(m_pCHRRam, 0, 8 * 1024);
        VROM = m_pCHRRam;
    }

    /* 4. One-time InfoNES init.  Sets up MapperTable, K6502 hooks, etc. */
    if (!m_bInitialized)
    {
        InfoNES_Init();
        m_bInitialized = TRUE;
    }

    /* 5. Per-cart reset.  Walks MapperTable, calls the matching mapper
       init, K6502_Reset, etc.  Returns -1 if the mapper number isn't in
       the table (InfoNES upstream ships ~150 mappers; everything common
       is covered). */
    if (InfoNES_Reset() < 0)
    {
        printf("[NesSystem] InfoNES_Reset failed (mapper #%u unsupported)\n",
               (Uint32)MapperNo);
        ROM  = NULL;
        VROM = NULL;
        return;
    }

    m_pNesRom    = pNesRom;
    m_bRomReady  = TRUE;
    m_uFrameTick = 0;

    printf("[NesSystem] SetRom OK: mapper=%u prg=%uKB chr=%uKB mirror=%s\n",
           (Uint32)MapperNo,
           uPrgBytes / 1024,
           (Uint32)NesHeader.byVRomSize * 8,
           (NesHeader.byInfo1 & 1) ? "vertical" : "horizontal");
}


/* -------------------------------------------------------------------- *
 *  Reset / SoftReset                                                   *
 * -------------------------------------------------------------------- *
 * The mainloop calls Reset() through Emu::System*.  We just re-run
 * InfoNES_Reset() to put the cart back in a fresh CPU state.  Without a
 * cart bound there's nothing to do.
 */

void NesSystem::Reset()
{
    m_uFrameTick = 0;
    m_uFrame     = 0;
    m_uLine      = 0;
    if (m_bRomReady)
        InfoNES_Reset();
}

void NesSystem::SoftReset()
{
    Reset();
}


/* -------------------------------------------------------------------- *
 *  ExecuteFrame: run one NES frame + present it.                       *
 * -------------------------------------------------------------------- *
 * mainloop_process.cpp passes:
 *   pInput    - controller state (uPad[0..4]), SNES bit layout
 *   pTarget   - the 256x256 RGBA8 surface that will be TextureUpload'd
 *               into _OutTex right after we return
 *   pMixBuf   - audio mixer; Phase 4 will push pAPU samples here
 *   eMode     - accurate vs deterministic; InfoNES ignores it
 *
 * The actual frame stepping lives in InfoNES_RunOneFrame (defined in
 * InfoNES_System_PS2.cpp because that file already pulls InfoNES.cpp's
 * internal globals via InfoNES.h, plus K6502.h for IRQ_REQ/NMI_REQ).
 *
 * The framebuffer conversion lives in InfoNES_LoadFrame() in the same
 * file - that's the platform callback InfoNES expects to call once per
 * visible frame (at SCAN_UNKNOWN_START).
 */

void NesSystem::ExecuteFrame(Emu::SysInputT *pInput,
                             CRenderSurface *pTarget,
                             CMixBuffer *pMixBuf,
                             ModeE eMode)
{
    (void)eMode;

    /* Stash the per-frame pointers so the C callbacks can find them. */
    g_pNesTargetSurface = pTarget;
    g_pNesInputState    = pInput;
    g_pNesMixBuffer     = pMixBuf;

    if (!m_bRomReady || !pTarget)
    {
        /* Defensive: SetRom is supposed to have run successfully before
           the mainloop ever dispatches here.  If we get here without a
           ROM the only useful thing we can do is paint a clearly-not-
           a-game diagnostic frame so the user can tell something's
           wrong without it just looking like a black-screen crash. */
        if (pTarget) DiagnosticPaint(pTarget);
        g_pNesTargetSurface = NULL;
        g_pNesInputState    = NULL;
        g_pNesMixBuffer     = NULL;
        m_uFrameTick++;
        m_uFrame++;
        return;
    }

    /* Run scanlines 0..262 of one NES frame.  InfoNES_RunOneFrame
       internally calls InfoNES_LoadFrame at SCAN_UNKNOWN_START, which
       converts WorkFrame[] into pTarget. */
    InfoNES_RunOneFrame();

    g_pNesTargetSurface = NULL;
    g_pNesInputState    = NULL;
    g_pNesMixBuffer     = NULL;
    m_uFrameTick++;
    m_uFrame++;
}


/* -------------------------------------------------------------------- *
 *  Diagnostic paint: used only when ExecuteFrame is hit without a ROM. *
 * -------------------------------------------------------------------- *
 * Dark blue background + a single bright band sweeping vertically so a
 * frozen frame is visually distinguishable from a working one.  Same
 * pattern users saw in Phase 2; kept here as a "ROM didn't load"
 * fallback rather than the default state.
 */

void NesSystem::DiagnosticPaint(CRenderSurface *pTarget)
{
    pTarget->Clear();

    Uint32 uWidth  = pTarget->GetWidth();
    Uint32 uHeight = pTarget->GetHeight();

    PixelFormatT *pFmt = pTarget->GetFormat();
    if (!pFmt || pFmt->uBitDepth != 32)
        return;

    Uint32 uBandRow = (m_uFrameTick / 2) % uHeight;
    for (Uint32 iY = 0; iY < uHeight; iY++)
    {
        Uint8 *pLine = pTarget->GetLinePtr((Int32)iY);
        if (!pLine) continue;

        for (Uint32 iX = 0; iX < uWidth; iX++)
        {
            pLine[iX * 4 + 0] = 0x10;
            pLine[iX * 4 + 1] = 0x20;
            pLine[iX * 4 + 2] = 0x80;
            pLine[iX * 4 + 3] = 0xFF;
        }

        if (iY == uBandRow)
        {
            for (Uint32 iX = 0; iX < uWidth; iX++)
            {
                pLine[iX * 4 + 0] = 0xFF;
                pLine[iX * 4 + 1] = 0xC0;
                pLine[iX * 4 + 2] = 0x10;
                pLine[iX * 4 + 3] = 0xFF;
            }
        }
    }
}


/* ------------- State + SRAM stubs (real impl in Phase 5) -------- */

Int32 NesSystem::GetStateSize()
{
    return (Int32)sizeof(NesStateT);
}

void NesSystem::SaveState(void *pState, Int32 nStateBytes)
{
    if (!pState || nStateBytes <= 0) return;
    memset(pState, 0, (size_t)nStateBytes);
}

void NesSystem::RestoreState(void *pState, Int32 nStateBytes)
{
    (void)pState;
    (void)nStateBytes;
}

void NesSystem::SaveState(NesStateT *pState)
{
    if (!pState) return;
    memset(pState, 0, sizeof(*pState));
}

Bool NesSystem::RestoreState(NesStateT *pState)
{
    (void)pState;
    return FALSE;
}

Int32 NesSystem::GetSRAMBytes()
{
    /* Phase 5: probe NesRom + InfoNES_Reset to learn the real size. */
    return 0;
}

Uint8 *NesSystem::GetSRAMData()
{
    return NULL;
}

const char *NesSystem::GetString(StringE eString)
{
    switch (eString)
    {
        case STRING_SHORTNAME: return "NES";
        case STRING_FULLNAME:  return "Nintendo Entertainment System";
        case STRING_SRAMEXT:   return "srm";
        case STRING_STATEEXT:  return "nst";
    }
    return "";
}

Uint32 NesSystem::GetSampleRate()
{
    /* IMPORTANTE: este valor configura a TAXA DE ENTRADA do
       SJPCMMixBuffer (via _MainLoopSetSampleRate -> SetSampleRate).
       O backend SjPCM/SPU2 toca SEMPRE a 48000 Hz, e o mix buffer so'
       sabe converter de {48000 (passthrough), 32000 (upsample cubico
       2:3), 24000 (1:2)} para 48000.  Qualquer outra taxa cai no
       'default' do switch em OutputSamplesStereo() e as amostras sao
       copiadas COMO SE fossem 48000 (sem resample) -> tocam ~2.2x mais
       rapido (pitch alto, "chipmunk") e o buffer esvazia (estalos).

       Por isso devolvemos 32000 -- a MESMA taxa que o SNES usa, que
       passa pelo resampler cubico ja' testado.  O InfoNES roda o pAPU
       a 44100 Hz (pAPU_QUALITY=3, 735 amostras/frame) e o
       InfoNES_SoundOutput reamostra 44100->32000 (=533 amostras) a uma
       razao constante; o mix buffer faz 32000->48000 (=800 amostras =
       1/60 s).  Resultado: pitch e duracao corretos.

       NAO devolver 44100 aqui: 44100 tambem nao esta no switch. */
    return 32000;
}
