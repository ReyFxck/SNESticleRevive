/* nessystem.cpp - NesSystem implementation.
 *
 * SetRom() feeds the iNES image into InfoNES (NesHeader, ROM, VROM
 * globals + optional CHR RAM) and runs InfoNES_Init/Reset.
 *
 * ExecuteFrame() drives InfoNES for exactly one NES frame. WorkFrame points
 * directly into the 256x256 RGBA8 render surface that mainloop_process.cpp
 * uploads to _OutTex via TextureUpload.
 *
 * When no ROM is bound (defensive: should never happen given the
 * mainloop dispatch logic) we still paint a diagnostic so that "NES
 * selected but rom failed to load" is distinguishable from a hang or
 * solid-black-crash on NetherSX2.
 *
 * Input mapping (NES has 8 buttons; PS2 user is pressing SNES-shaped
 * bits because _MainLoopInput is still routed through _MainLoopSnesInput)
 * is done in InfoNES_System_PS2.cpp::InfoNES_PadState, reading the
 * SysInputT pointer that this file stashes per frame. The cycle-timed NES
 * sample sink sends the five base 2A03 channels through the shared PS2
 * audio buffer.
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
#include "InfoNES_pAPU.h"
#include "InfoNES_Mapper.h"

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
/* Per-frame audio sink, stashed by ExecuteFrame so the NES sample callback
   can reach it. Points at the SAME
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
extern int             SpriteJustHit;

typedef char NesCpuStateCapacityCheck[
    NES_STATE_CPU_BYTES >= K6502_STATE_MAX ? 1 : -1
];
typedef char NesApuStateCapacityCheck[
    NES_STATE_APU_BYTES >= INFONES_APU_STATE_MAX ? 1 : -1
];
typedef char NesMapperStateCapacityCheck[
    NES_STATE_MAPPER_BYTES >= INFONES_MAPPER_STATE_MAX ? 1 : -1
];
typedef char NesCoreArraySizeCheck[
    NES_STATE_RAM_BYTES == RAM_SIZE &&
    NES_STATE_SRAM_BYTES == SRAM_SIZE &&
    NES_STATE_PPURAM_BYTES == PPURAM_SIZE &&
    NES_STATE_SPRRAM_BYTES == SPRRAM_SIZE ? 1 : -1
];


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
       singletons since RTTI is disabled. Cartridge execution, SRAM and
       states are implemented; FDS execution remains a separate task. */
    if (pRom != _pNesRom)
    {
        printf("[NesSystem] SetRom: cartridge core supports .nes only "
               "(FDS execution pending). Leaving InfoNES un-wired.\n");
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
    Uint8 *pTrainer = NULL;
    if (NesHeader.byInfo1 & 0x04)
    {
        pTrainer = p;
        p += 512;                                /* skip trainer */
    }
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

    /* Battery RAM belongs to the newly inserted cartridge. Clear the
       previous game's bytes before the frontend attempts to load its .srm.
       An iNES trainer is mapped at CPU $7000-$71ff (SRAM + $1000). */
    memset(SRAM, 0xFE, SRAM_SIZE);
    if (pTrainer)
        memcpy(SRAM + 0x1000, pTrainer, 512);

    /* 4. One-time InfoNES init.  Sets up MapperTable, K6502 hooks, etc. */
    if (!m_bInitialized)
    {
        InfoNES_Init();
        m_bInitialized = TRUE;
    }

    /* Mapper modules share one writable span. Clear it on cartridge change
       so a state contains only this game's mapper data (and compresses well),
       never stale RAM left by a mapper used by the previous ROM. */
    {
        Int32 nMapperBytes = InfoNES_MapperStateBytes();
        if (nMapperBytes > 0)
            memset(InfoNES_MapperStateBase(), 0, nMapperBytes);
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
 *   pMixBuf   - audio mixer receiving the pAPU output
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


/* -------------------------------------------------------------------- *
 *  Save states                                                         *
 * -------------------------------------------------------------------- */

static Bool NesEncodeBankPointer(const BYTE *pPointer, NesStateBankRefT *pRef)
{
    struct RegionT
    {
        Uint32 eRegion;
        const BYTE *pBase;
        Uint32 nBytes;
    };
    RegionT Regions[8];
    Int32 nRegions = 0;
    Int32 i;
    Int32 nMapperBytes = InfoNES_MapperStateBytes();

    if (!pRef)
        return FALSE;
    pRef->eRegion = NES_STATE_REGION_NONE;
    pRef->uOffset = 0;
    if (!pPointer)
        return FALSE;

#define NES_ADD_REGION(ID, BASE, BYTES) \
    do { \
        Regions[nRegions].eRegion = (ID); \
        Regions[nRegions].pBase = (const BYTE *)(BASE); \
        Regions[nRegions].nBytes = (Uint32)(BYTES); \
        nRegions++; \
    } while (0)
    NES_ADD_REGION(NES_STATE_REGION_ROM, ROM,
                   (Uint32)NesHeader.byRomSize * 16 * 1024);
    NES_ADD_REGION(NES_STATE_REGION_VROM, VROM,
                   NesHeader.byVRomSize
                       ? (Uint32)NesHeader.byVRomSize * 8 * 1024
                       : NES_STATE_CHRRAM_BYTES);
    NES_ADD_REGION(NES_STATE_REGION_RAM, RAM, RAM_SIZE);
    NES_ADD_REGION(NES_STATE_REGION_SRAM, SRAM, SRAM_SIZE);
    NES_ADD_REGION(NES_STATE_REGION_PPURAM, PPURAM, PPURAM_SIZE);
    NES_ADD_REGION(NES_STATE_REGION_SPRRAM, SPRRAM, SPRRAM_SIZE);
    NES_ADD_REGION(NES_STATE_REGION_CHRBUF, ChrBuf, NES_STATE_CHRBUF_BYTES);
    NES_ADD_REGION(NES_STATE_REGION_MAPPER, InfoNES_MapperStateBase(),
                   nMapperBytes > 0 ? nMapperBytes : 0);
#undef NES_ADD_REGION

    for (i = 0; i < nRegions; i++)
    {
        unsigned long uPointer = (unsigned long)pPointer;
        unsigned long uBase = (unsigned long)Regions[i].pBase;
        unsigned long uEnd = uBase + Regions[i].nBytes;

        if (Regions[i].pBase && Regions[i].nBytes &&
            uPointer >= uBase && uPointer < uEnd)
        {
            pRef->eRegion = Regions[i].eRegion;
            pRef->uOffset = (Uint32)(uPointer - uBase);
            return TRUE;
        }
    }

    return FALSE;
}

static BYTE *NesDecodeBankPointer(const NesStateBankRefT *pRef)
{
    BYTE *pBase = NULL;
    Uint32 nBytes = 0;
    Int32 nMapperBytes;

    if (!pRef)
        return NULL;
    if (pRef->eRegion == NES_STATE_REGION_NONE)
        return NULL;

    switch (pRef->eRegion)
    {
        case NES_STATE_REGION_ROM:
            pBase = ROM;
            nBytes = (Uint32)NesHeader.byRomSize * 16 * 1024;
            break;
        case NES_STATE_REGION_VROM:
            pBase = VROM;
            nBytes = NesHeader.byVRomSize
                ? (Uint32)NesHeader.byVRomSize * 8 * 1024
                : NES_STATE_CHRRAM_BYTES;
            break;
        case NES_STATE_REGION_RAM:
            pBase = RAM; nBytes = RAM_SIZE; break;
        case NES_STATE_REGION_SRAM:
            pBase = SRAM; nBytes = SRAM_SIZE; break;
        case NES_STATE_REGION_PPURAM:
            pBase = PPURAM; nBytes = PPURAM_SIZE; break;
        case NES_STATE_REGION_SPRRAM:
            pBase = SPRRAM; nBytes = SPRRAM_SIZE; break;
        case NES_STATE_REGION_CHRBUF:
            pBase = ChrBuf; nBytes = NES_STATE_CHRBUF_BYTES; break;
        case NES_STATE_REGION_MAPPER:
            nMapperBytes = InfoNES_MapperStateBytes();
            pBase = InfoNES_MapperStateBase();
            nBytes = nMapperBytes > 0 ? (Uint32)nMapperBytes : 0;
            break;
        default:
            return NULL;
    }

    if (!pBase || pRef->uOffset >= nBytes)
        return NULL;
    return pBase + pRef->uOffset;
}

Int32 NesSystem::GetStateSize()
{
    return (Int32)sizeof(NesStateT);
}

void NesSystem::SaveState(void *pState, Int32 nStateBytes)
{
    if (!pState || nStateBytes < (Int32)sizeof(NesStateT))
        return;
    SaveState((NesStateT *)pState);
}

void NesSystem::RestoreState(void *pState, Int32 nStateBytes)
{
    if (!pState || nStateBytes != (Int32)sizeof(NesStateT))
        return;
    RestoreState((NesStateT *)pState);
}

void NesSystem::SaveState(NesStateT *pState)
{
    Int32 i;

    if (!pState)
        return;
    memset(pState, 0, sizeof(*pState));

    if (!m_bRomReady || !m_pNesRom || !m_pNesRom->IsLoaded())
        return;

    pState->uVersion = NES_STATE_VERSION;
    pState->nStateBytes = sizeof(*pState);
    pState->uMapper = MapperNo;
    pState->bChrRam = NesHeader.byVRomSize == 0 ? 1 : 0;
    pState->uFrameTick = m_uFrameTick;
    pState->uFrame = m_uFrame;
    pState->uLine = m_uLine;

    pState->nCpuStateBytes = K6502_SaveState(
        pState->aCpuState, sizeof(pState->aCpuState));
    pState->nApuStateBytes = InfoNES_pAPUSaveState(
        pState->aApuState, sizeof(pState->aApuState));
    pState->nMapperStateBytes = InfoNES_MapperSaveState(
        pState->aMapperState, sizeof(pState->aMapperState));
    if (!pState->nCpuStateBytes || !pState->nApuStateBytes ||
        !pState->nMapperStateBytes)
    {
        memset(pState, 0, sizeof(*pState));
        return;
    }

    memcpy(pState->aRam, RAM, sizeof(pState->aRam));
    memcpy(pState->aSram, SRAM, sizeof(pState->aSram));
    memcpy(pState->aPpuRam, PPURAM, sizeof(pState->aPpuRam));
    memcpy(pState->aSpriteRam, SPRRAM, sizeof(pState->aSpriteRam));
    if (pState->bChrRam)
        memcpy(pState->aChrRam, VROM, sizeof(pState->aChrRam));
    memcpy(pState->aChrBuf, ChrBuf, sizeof(pState->aChrBuf));
    memcpy(pState->aPalette, PalTable, sizeof(pState->aPalette));
    memcpy(pState->aPpuScanTable, PPU_ScanTable,
           sizeof(pState->aPpuScanTable));
    memcpy(pState->aApuRegisters, APU_Reg,
           sizeof(pState->aApuRegisters));

    pState->PPU_R0 = PPU_R0; pState->PPU_R1 = PPU_R1;
    pState->PPU_R2 = PPU_R2; pState->PPU_R3 = PPU_R3;
    pState->PPU_R7 = PPU_R7;
    pState->PPU_Scr_V = PPU_Scr_V;
    pState->PPU_Scr_V_Next = PPU_Scr_V_Next;
    pState->PPU_Scr_V_Byte = PPU_Scr_V_Byte;
    pState->PPU_Scr_V_Byte_Next = PPU_Scr_V_Byte_Next;
    pState->PPU_Scr_V_Bit = PPU_Scr_V_Bit;
    pState->PPU_Scr_V_Bit_Next = PPU_Scr_V_Bit_Next;
    pState->PPU_Scr_H = PPU_Scr_H;
    pState->PPU_Scr_H_Next = PPU_Scr_H_Next;
    pState->PPU_Scr_H_Byte = PPU_Scr_H_Byte;
    pState->PPU_Scr_H_Byte_Next = PPU_Scr_H_Byte_Next;
    pState->PPU_Scr_H_Bit = PPU_Scr_H_Bit;
    pState->PPU_Scr_H_Bit_Next = PPU_Scr_H_Bit_Next;
    pState->PPU_Latch_Flag = PPU_Latch_Flag;
    pState->PPU_UpDown_Clip = PPU_UpDown_Clip;
    pState->PPU_NameTableBank = PPU_NameTableBank;
    pState->byVramWriteEnable = byVramWriteEnable;
    pState->FrameIRQ_Enable = FrameIRQ_Enable;
    pState->ChrBufUpdate = ChrBufUpdate;
    pState->PPU_Addr = PPU_Addr; pState->PPU_Temp = PPU_Temp;
    pState->PPU_Increment = PPU_Increment;
    pState->PPU_Scanline = PPU_Scanline;
    pState->PPU_SP_Height = PPU_SP_Height;
    pState->FrameStep = FrameStep; pState->FrameSkip = FrameSkip;
    pState->FrameCnt = FrameCnt;
    pState->SpriteJustHit = SpriteJustHit;
    pState->APU_Mute = APU_Mute;
    pState->PAD1_Latch = (Uint32)PAD1_Latch;
    pState->PAD2_Latch = (Uint32)PAD2_Latch;
    pState->PAD_System = (Uint32)PAD_System;
    pState->PAD1_Bit = (Uint32)PAD1_Bit;
    pState->PAD2_Bit = (Uint32)PAD2_Bit;

    for (i = 0; i < 4; i++)
    {
        BYTE *pBank = i == 0 ? ROMBANK0 : i == 1 ? ROMBANK1 :
                      i == 2 ? ROMBANK2 : ROMBANK3;
        if (!NesEncodeBankPointer(pBank, &pState->RomBanks[i]))
            break;
    }
    if (i != 4 ||
        !NesEncodeBankPointer(SRAMBANK, &pState->SramBank))
    {
        memset(pState, 0, sizeof(*pState));
        return;
    }
    for (i = 0; i < NES_STATE_PPU_BANKS; i++)
    {
        if (!NesEncodeBankPointer(PPUBANK[i], &pState->PpuBanks[i]))
            break;
    }
    if (i != NES_STATE_PPU_BANKS ||
        !NesEncodeBankPointer(PPU_BG_Base, &pState->PpuBgBase) ||
        !NesEncodeBankPointer(PPU_SP_Base, &pState->PpuSpriteBase))
    {
        memset(pState, 0, sizeof(*pState));
        return;
    }

    /* Commit last so a failed component snapshot is unmistakably invalid. */
    pState->uMagic = NES_STATE_MAGIC;
}

Bool NesSystem::RestoreState(NesStateT *pState)
{
    BYTE *pRomBanks[4];
    BYTE *pPpuBanks[NES_STATE_PPU_BANKS];
    BYTE *pSramBank;
    BYTE *pPpuBgBase;
    BYTE *pPpuSpriteBase;
    Int32 i;

    if (!pState || !m_bRomReady || !m_pNesRom ||
        pState->uMagic != NES_STATE_MAGIC ||
        pState->uVersion != NES_STATE_VERSION ||
        pState->nStateBytes != sizeof(*pState) ||
        pState->uMapper != MapperNo ||
        pState->bChrRam != (NesHeader.byVRomSize == 0 ? 1U : 0U) ||
        pState->nCpuStateBytes > sizeof(pState->aCpuState) ||
        pState->nApuStateBytes > sizeof(pState->aApuState) ||
        pState->nMapperStateBytes != (Uint32)InfoNES_MapperStateBytes())
    {
        return FALSE;
    }

    /* Resolve every pointer before mutating the running machine. A bad
       reference then rejects the state without leaving half a restore. */
    for (i = 0; i < 4; i++)
    {
        pRomBanks[i] = NesDecodeBankPointer(&pState->RomBanks[i]);
        if (!pRomBanks[i])
            return FALSE;
    }
    pSramBank = NesDecodeBankPointer(&pState->SramBank);
    if (!pSramBank)
        return FALSE;
    for (i = 0; i < NES_STATE_PPU_BANKS; i++)
    {
        pPpuBanks[i] = NesDecodeBankPointer(&pState->PpuBanks[i]);
        if (!pPpuBanks[i])
            return FALSE;
    }
    pPpuBgBase = NesDecodeBankPointer(&pState->PpuBgBase);
    pPpuSpriteBase = NesDecodeBankPointer(&pState->PpuSpriteBase);
    if (!pPpuBgBase || !pPpuSpriteBase)
        return FALSE;

    memcpy(RAM, pState->aRam, sizeof(pState->aRam));
    memcpy(SRAM, pState->aSram, sizeof(pState->aSram));
    memcpy(PPURAM, pState->aPpuRam, sizeof(pState->aPpuRam));
    memcpy(SPRRAM, pState->aSpriteRam, sizeof(pState->aSpriteRam));
    if (pState->bChrRam)
        memcpy(VROM, pState->aChrRam, sizeof(pState->aChrRam));
    memcpy(ChrBuf, pState->aChrBuf, sizeof(pState->aChrBuf));
    memcpy(PalTable, pState->aPalette, sizeof(pState->aPalette));
    memcpy(PPU_ScanTable, pState->aPpuScanTable,
           sizeof(pState->aPpuScanTable));
    memcpy(APU_Reg, pState->aApuRegisters,
           sizeof(pState->aApuRegisters));
    if (!InfoNES_MapperLoadState(
            pState->aMapperState, pState->nMapperStateBytes))
        return FALSE;

    PPU_R0 = pState->PPU_R0; PPU_R1 = pState->PPU_R1;
    PPU_R2 = pState->PPU_R2; PPU_R3 = pState->PPU_R3;
    PPU_R7 = pState->PPU_R7;
    PPU_Scr_V = pState->PPU_Scr_V;
    PPU_Scr_V_Next = pState->PPU_Scr_V_Next;
    PPU_Scr_V_Byte = pState->PPU_Scr_V_Byte;
    PPU_Scr_V_Byte_Next = pState->PPU_Scr_V_Byte_Next;
    PPU_Scr_V_Bit = pState->PPU_Scr_V_Bit;
    PPU_Scr_V_Bit_Next = pState->PPU_Scr_V_Bit_Next;
    PPU_Scr_H = pState->PPU_Scr_H;
    PPU_Scr_H_Next = pState->PPU_Scr_H_Next;
    PPU_Scr_H_Byte = pState->PPU_Scr_H_Byte;
    PPU_Scr_H_Byte_Next = pState->PPU_Scr_H_Byte_Next;
    PPU_Scr_H_Bit = pState->PPU_Scr_H_Bit;
    PPU_Scr_H_Bit_Next = pState->PPU_Scr_H_Bit_Next;
    PPU_Latch_Flag = pState->PPU_Latch_Flag;
    PPU_UpDown_Clip = pState->PPU_UpDown_Clip;
    PPU_NameTableBank = pState->PPU_NameTableBank;
    byVramWriteEnable = pState->byVramWriteEnable;
    FrameIRQ_Enable = pState->FrameIRQ_Enable;
    ChrBufUpdate = pState->ChrBufUpdate;
    PPU_Addr = pState->PPU_Addr; PPU_Temp = pState->PPU_Temp;
    PPU_Increment = pState->PPU_Increment;
    PPU_Scanline = pState->PPU_Scanline;
    PPU_SP_Height = pState->PPU_SP_Height;
    FrameStep = pState->FrameStep; FrameSkip = pState->FrameSkip;
    FrameCnt = pState->FrameCnt;
    SpriteJustHit = pState->SpriteJustHit;
    APU_Mute = pState->APU_Mute;
    PAD1_Latch = pState->PAD1_Latch; PAD2_Latch = pState->PAD2_Latch;
    PAD_System = pState->PAD_System;
    PAD1_Bit = pState->PAD1_Bit; PAD2_Bit = pState->PAD2_Bit;

    ROMBANK0 = pRomBanks[0]; ROMBANK1 = pRomBanks[1];
    ROMBANK2 = pRomBanks[2]; ROMBANK3 = pRomBanks[3];
    SRAMBANK = pSramBank;
    for (i = 0; i < NES_STATE_PPU_BANKS; i++)
        PPUBANK[i] = pPpuBanks[i];
    PPU_BG_Base = pPpuBgBase;
    PPU_SP_Base = pPpuSpriteBase;

    if (!K6502_LoadState(pState->aCpuState, pState->nCpuStateBytes) ||
        !InfoNES_pAPULoadState(
            pState->aApuState, pState->nApuStateBytes))
        return FALSE;

    m_uFrameTick = pState->uFrameTick;
    m_uFrame = pState->uFrame;
    m_uLine = pState->uLine;
    return TRUE;
}

Int32 NesSystem::GetSRAMBytes()
{
    return (m_bRomReady && m_pNesRom && m_pNesRom->HasBatterySRAM())
        ? SRAM_SIZE
        : 0;
}

Uint8 *NesSystem::GetSRAMData()
{
    return GetSRAMBytes() > 0 ? SRAM : NULL;
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
    /* Nes_Snd_Emu/Blip_Buffer synthesizes the five base 2A03 channels
       directly at 32 kHz. AudMixBuffer already has the PS2-tested cubic
       32->48-kHz path, so the NES path no longer relies on an unsupported
       44.1-kHz passthrough or a second frame-local resampler. */
    return 32000;
}
