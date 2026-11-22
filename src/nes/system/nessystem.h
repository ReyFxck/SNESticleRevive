/* nessystem.h
 *
 * Emu::System wrapper for the selected PS2 NES backend.
 * MesenCE is the default; NES_BACKEND=infones retains the legacy core and
 * its fixed snapshots. Mesen snapshots have a distinct variable payload.
 *
 * Mirrors the structure of SnesSystem (snes.h) so the mainloop's polymorphic
 * dispatch through Emu::System* works identically for NES and SNES.
 *
 * Legacy InfoNES cartridge support (the Mesen bridge is separate):
 *   - SetRom() seeds InfoNES globals (NesHeader, ROM, VROM, optional CHR
 *     RAM) and runs InfoNES_Init() / InfoNES_Reset() for the cartridge.
 *   - ExecuteFrame() steps the InfoNES core for exactly one NES frame; its
 *     RGBA8 WorkFrame points directly into the uploaded render surface.
 *   - Input is wired through InfoNES_PadState and the cycle-timed base 2A03
 *     audio stream is routed to the same PS2 mix buffer used by the SNES core.
 *   - Battery-backed 8 KiB SRAM and versioned save states are wired.
 *     States include CPU, PPU, pAPU, CHR RAM and mapper-private data.
 *
 * FDS disk swapping remains separate from cartridge SRAM/state support.
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
#if NES_MESENCE
#include "../mesence/mesence_bridge.h"
#endif


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

#if NES_MESENCE
    Bool          SnapshotState();
    Bool          AllocateState(Uint32 bytes);
    Uint8        *GetSnapshotData();
    Uint32        GetSnapshotBytes();
    Bool          RestoreSnapshot();
    Uint32        GetFrameRate();
#else
    Uint32        GetFrameRate() { return 60; }
    /* NES-only typed hooks used by mainloop_state.cpp. */
    void          SaveState(NesStateT *pState);
    Bool          RestoreState(NesStateT *pState);
#endif
    Bool          IsRomReady() const          {return m_bRomReady;}

    /* FDS disk swapping (Phase 5). The current build returns NULL so
       mainloop_input.cpp's FDS swap branch is a no-op even after the
       #if 0 around it is flipped. */
    NesMMU       *GetMMU()                  {return NULL;}
    void          SetNesDisk(NesDisk *pDisk) {m_pNesDisk = pDisk;}

private:
#if NES_MESENCE
    struct MesenceCore *m_pCore;
#else
    void          DiagnosticPaint(class CRenderSurface *pTarget);
#endif

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
