/* nessystem.h
 *
 * Emu::System wrapper that owns the InfoNES NES core for the PS2 build.
 *
 * Mirrors the structure of SnesSystem (snes.h) so the mainloop's polymorphic
 * dispatch through Emu::System* works identically for NES and SNES.
 *
 * Phase 3 scope (current):
 *   - SetRom() seeds InfoNES globals (NesHeader, ROM, VROM, optional CHR
 *     RAM) and runs InfoNES_Init() / InfoNES_Reset() for the cartridge.
 *   - ExecuteFrame() steps the InfoNES core for exactly one NES frame and
 *     converts WorkFrame[256*240] (RGB555) into the RGBA8 render surface
 *     uploaded by mainloop_process.cpp.
 *   - Input is wired through InfoNES_PadState -> reads SysInputT, remaps
 *     SNES bits to NES bits.  No audio yet (Phase 4).
 *   - SaveState / RestoreState are still stubs (Phase 5).
 *
 * Phase 4 adds InfoNES_SoundOutput -> CMixBuffer, Phase 5 covers state +
 * SRAM + FDS disk swap.
 */

#ifndef _NESSYSTEM_H
#define _NESSYSTEM_H

/* types.h MUST come before emusys.h - emusys.h refers to Uint32/Int32
   etc. but does not include types.h itself (SnesSystem gets away with
   it because snes.h transitively pulls types.h first). The NES path
   includes emusys.h directly via this header, so we need to seed the
   typedefs explicitly. */
#include "types.h"

#include "emusys.h"

#include "nesrom.h"
#include "nesstate.h"


class NesMMU; /* Phase 5 - FDS disk-swap mux. Forward-declared so other
                 code can reference NesSystem::GetMMU() without dragging
                 the full type in until we need it. */


class NesSystem : public Emu::System
{
public:
    NesSystem();
    ~NesSystem();

    /* Emu::System hooks ---------------------------------------------- */
    virtual void  SetRom(Emu::Rom *pRom);
    virtual void  Reset();
    virtual void  SoftReset();

    virtual void  ExecuteFrame(Emu::SysInputT *pInput,
                               class CRenderSurface *pTarget,
                               class CMixBuffer *pMixBuf,
                               ModeE eMode);

    virtual Int32 GetStateSize();
    virtual void  SaveState(void *pState, Int32 nStateBytes);
    virtual void  RestoreState(void *pState, Int32 nStateBytes);

    virtual Int32  GetSRAMBytes();
    virtual Uint8 *GetSRAMData();

    virtual const char *GetString(StringE eString);
    virtual Uint32 GetSampleRate();

    /* NES-only state hooks used by mainloop_state.cpp once it's
       un-#if-0'd. They mirror SnesSystem::Save/RestoreState. */
    void          SaveState(NesStateT *pState);
    Bool          RestoreState(NesStateT *pState);

    /* FDS disk swapping (Phase 5). The current build returns NULL so
       mainloop_input.cpp's FDS swap branch is a no-op even after the
       #if 0 around it is flipped. */
    NesMMU       *GetMMU()                  {return NULL;}
    void          SetNesDisk(NesDisk *pDisk) {m_pNesDisk = pDisk;}

private:
    void          DiagnosticPaint(class CRenderSurface *pTarget);

    NesRom    *m_pNesRom;     /* current cartridge image, owned by mainloop */
    NesDisk   *m_pNesDisk;    /* current FDS disk     (Phase 5) */
    Uint8     *m_pCHRRam;     /* allocated only for ROMs that ship without
                                 CHR ROM (NesHeader.byVRomSize == 0).
                                 InfoNES treats VROM as 8 KB of CHR memory
                                 the cart can write to; we own the buffer. */
    Bool       m_bInitialized; /* InfoNES_Init() called once across the
                                  lifetime of this NesSystem. */
    Bool       m_bRomReady;    /* TRUE once SetRom has wired a cart through
                                  InfoNES_Reset() successfully. */
    Uint32     m_uFrameTick;  /* per-frame counter, used by the diagnostic
                                 paint when no ROM is loaded so the user can
                                 see the loop is alive */
};

#endif
