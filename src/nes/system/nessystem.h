/* nessystem.h
 *
 * Emu::System wrapper for the MesenCE NES backend on PS2.
 * Mirrors the structure of SnesSystem (snes.h) so the mainloop's polymorphic
 * dispatch through Emu::System* works identically for NES and SNES.
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
#include "../mesence/mesence_bridge.h"


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

    /* MesenCE-specific state hooks (variable payload). */
    Bool          SnapshotState();
    Bool          AllocateState(Uint32 bytes);
    Uint8        *GetSnapshotData();
    Uint32        GetSnapshotBytes();
    Bool          RestoreSnapshot();
    Uint32        GetFrameRate();

    Bool          IsRomReady() const          {return m_bRomReady;}

    /* FDS disk swapping (Phase 5). The current build returns NULL so
       mainloop_input.cpp's FDS swap branch is a no-op even after the
       #if 0 around it is flipped. */
    NesMMU       *GetMMU()                  {return NULL;}
    void          SetNesDisk(NesDisk *pDisk) {m_pNesDisk = pDisk;}

private:
    struct MesenceCore *m_pCore;

    NesRom    *m_pNesRom;     /* current cartridge image, owned by mainloop */
    NesDisk   *m_pNesDisk;    /* current FDS disk     (Phase 5) */
    Uint8     *m_pCHRRam;     /* allocated only for ROMs that ship without
                                 CHR ROM (NesHeader.byVRomSize == 0).
                                 MesenCE treats CHR RAM similarly. */
    Bool       m_bRomReady;    /* TRUE once SetRom has wired a cart through MesenCE */
    Uint32     m_uFrameTick;  /* per-frame counter, used by the diagnostic
                                  paint when no ROM is loaded so the user can
                                  see the loop is alive */
};

#endif